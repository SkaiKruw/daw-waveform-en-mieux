#include "SandboxPlugin.h"
#include "onde/Trace.h"

const char* SandboxPlugin::xmlTypeName = "ondeSandbox";

static const juce::Identifier specID ("spec"), nameID ("displayName"), stateID ("pluginState");

//==============================================================================
// Un paramètre du VST3 isolé : la valeur (0..1) part vers le processus du plugin à chaque changement,
// y compris depuis l'automation et les modulateurs (fil audio, sans verrou).
class SandboxPlugin::Parameter  : public te::AutomatableParameter
{
public:
    Parameter (SandboxPlugin& o, int i, const juce::String& id, const juce::String& name, const onde::PluginSandbox::ParamDesc& d)
        : te::AutomatableParameter (id, name, o, { 0.0f, 1.0f }), owner (&o), index (i),
          label (juce::String::fromUTF8 (d.label.c_str())), defaultValue (d.defaultValue)
    {
    }
    ~Parameter() override { notifyListenersOfDeletion(); }

    void parameterChanged (float v, bool byAutomation) override
    {
        if (auto* o = owner.load())
        {
            if (auto* s = o->getSandbox()) s->setPluginParameter (index, v);
            if (! byAutomation) o->edit.pluginChanged (*o);
        }
    }
    std::optional<float> getDefaultValue() const override   { return defaultValue; }
    juce::String getLabel() override                        { return label; }
    juce::String getCurrentValueAsString() override
    {
        // Texte du plugin (« 2,5 kHz ») tant qu'il correspond à la valeur actuelle, sinon un pourcentage.
        if (auto* o = owner.load())
            if (auto* s = o->getSandbox())
                if (std::abs (s->getPluginParameter (index) - getCurrentValue()) < 1.0e-4f)
                {
                    auto text = juce::String::fromUTF8 (s->getPluginParameterText (index).c_str());
                    if (text.isNotEmpty()) return label.isNotEmpty() && ! text.endsWith (label) ? text + " " + label : text;
                }
        return valueToString (getCurrentValue());
    }
    juce::String valueToString (float v) override           { return juce::String (v * 100.0f, 1) + " %"; }
    float stringToValue (const juce::String& s) override    { return juce::jlimit (0.0f, 1.0f, s.getFloatValue() / 100.0f); }

    std::atomic<SandboxPlugin*> owner;
    const int index;
private:
    juce::String label;
    float defaultValue;
};

SandboxPlugin::SandboxPlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
    spec.referTo (state, specID, nullptr, "synth-pad");
    displayName.referTo (state, nameID, nullptr, juce::String::fromUTF8 ("Synthé nappe"));
    savedState.referTo (state, stateID, nullptr, {});
    midiScratch.resize (onde::kMaxMidiEvents);
    paramTimer.setCallback ([this] { pollParameters(); });
    paramTimer.startTimerHz (20);
}

te::AutomatableParameter* SandboxPlugin::getPluginParameter (int index) const
{
    return juce::isPositiveAndBelow (index, (int) byIndex.size()) ? byIndex[(size_t) index] : nullptr;
}

void SandboxPlugin::buildParameters()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (paramsBuilt || sandbox == nullptr || sandbox->getState() != onde::PluginSandbox::State::running) return;
    paramsBuilt = true;
    const auto list = sandbox->getPluginParameters();
    byIndex.assign (list.size(), nullptr);
    std::map<juce::String, int> used;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& d = list[i];
        if ((d.flags & onde::paramAutomatable) == 0) continue;
        auto name = juce::String::fromUTF8 (d.name.c_str()).trim();
        if (name.isEmpty()) name = juce::String::fromUTF8 ("Paramètre ") + juce::String ((int) i + 1);
        if (const int n = ++used[name]; n > 1) name << " (" << n << ")";
        // Identifiant = index du plugin : stable d'une session à l'autre, garde l'automation enregistrée.
        auto* p = new Parameter (*this, (int) i, juce::String ((int) i), name, d);
        addAutomatableParameter (*p);
        byIndex[i] = p;
        p->setParameter (sandbox->getPluginParameter ((int) i), juce::dontSendNotification);
    }
    restoreChangedParametersFromState();
    // Valeurs restaurées par le projet : le plugin les reçoit.
    for (auto* p : byIndex)
        if (p != nullptr) sandbox->setPluginParameter (p->index, p->getCurrentValue());
}

void SandboxPlugin::pollParameters()
{
    if (sandbox == nullptr || sandbox->getState() != onde::PluginSandbox::State::running) return;
    if (! paramsBuilt) buildParameters();
    // Réglages faits dans la fenêtre du plugin (ou par un préréglage) : Onde les suit, les enregistre et
    // peut les automatiser.
    sandbox->collectPluginParameterChanges ([this] (int index, float v)
    {
        if (auto* p = getPluginParameter (index))
            if (std::abs (p->getCurrentValue() - v) > 1.0e-6f)
                p->setParameter (v, juce::sendNotification);
    });
}

SandboxPlugin::~SandboxPlugin()
{
    onde::trace ("~SandboxPlugin", getName().toRawUTF8());
    paramTimer.stopTimer();
    for (auto* p : byIndex) if (p != nullptr) p->owner = nullptr;
    notifyListenersOfDeletion();
    if (sandbox) sandbox->stop();
    onde::trace ("~SandboxPlugin: terminé");
}

juce::File SandboxPlugin::getWorkerFile()
{
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("onde-plugin-host.exe");
   #else
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("onde-plugin-host");
   #endif
}

bool SandboxPlugin::isSynth()
{
    if (sandbox != nullptr && sandbox->getState() == onde::PluginSandbox::State::running)
        return sandbox->getInfo().isInstrument;
    return spec.get().contains ("synth") || state.getProperty ("isInstrument", false);
}

void SandboxPlugin::ensureSandbox (double sampleRate, int blockSize)
{
    const auto wantedPipeline = (uint32_t) juce::jlimit (512, (int) onde::kMaxFrames, blockSize);
    if (sandbox != nullptr && preparedRate == sampleRate && sandbox->getPipelineFrames() >= wantedPipeline)
        return;

    if (sandbox) sandbox->stop();
    onde::PluginSandbox::Options opts { getWorkerFile().getFullPathName().toStdString(), spec.get().toStdString() };
    opts.hungAfterSeconds = 4.0;            // vrais plugins : jusqu'à 4 s de silence (chargement d'échantillons…) avant « bloqué »
    opts.deadlineFraction = 0.9;            // le graphe de Tracktion traite les autres pistes en parallèle
    opts.pipelineFrames = wantedPipeline;   // traitement anticipé : le plugin calcule pendant la période précédente
    sandbox = std::make_unique<onde::PluginSandbox> (opts);
    preparedRate = sampleRate;
    if (sandbox->start (sampleRate, 2) && sandbox->waitUntilReady (15000))
    {
        state.setProperty ("isInstrument", sandbox->getInfo().isInstrument, nullptr);
        if (savedState.get().isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (savedState.get()))
                sandbox->loadState (std::vector<uint8_t> ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize()));
        }
        // Sur le fil principal (ouverture, rendu hors ligne) : paramètres créés tout de suite, pour que
        // l'automation enregistrée s'applique dès le premier bloc.
        if (juce::MessageManager::existsAndIsCurrentThread()) buildParameters();
    }
}

void SandboxPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    ensureSandbox (info.sampleRate, info.blockSizeSamples);
}

void SandboxPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    te::engine::copyPropertiesToCachedValues (v, spec, displayName, savedState);
}

void SandboxPlugin::flushPluginStateToValueTree()
{
    te::Plugin::flushPluginStateToValueTree();
    if (sandbox != nullptr && sandbox->getState() == onde::PluginSandbox::State::running)
    {
        std::vector<uint8_t> data;
        if (sandbox->saveState (data, 2000) && ! data.empty())
            savedState = juce::MemoryBlock (data.data(), data.size()).toBase64Encoding();
    }
}

void SandboxPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || sandbox == nullptr)
        return;

    const int numCh = std::min (fc.destBuffer->getNumChannels(), 2);
    float* chans[2] = { nullptr, nullptr };
    for (int c = 0; c < numCh; ++c)
        chans[c] = fc.destBuffer->getWritePointer (c, fc.bufferStartSample);

    uint32_t numMidi = 0;
    if (fc.bufferForMidiMessages != nullptr)
        for (auto& m : *fc.bufferForMidiMessages)
        {
            if (numMidi >= onde::kMaxMidiEvents || m.getRawDataSize() > 3) continue;
            auto& e = midiScratch[numMidi++];
            e.offset = (uint32_t) juce::jlimit (0, fc.bufferNumSamples - 1, juce::roundToInt (m.getTimeStamp() * sampleRate));
            e.size = (uint8_t) m.getRawDataSize();
            std::memcpy (e.data, m.getRawData(), e.size);
        }

    // Transport transmis au plugin (tempo, position musicale, lecture, boucle).
    {
        onde::PluginSandbox::Transport tr;
        // Fil audio : uniquement la séquence interne (remplacée sous le verrou du callback audio, donc stable ici).
        // Les accesseurs de TempoSequence peuvent recalculer la séquence et prendre ce verrou, déjà tenu par le
        // fil de la carte son qui attend ce nœud : interblocage et lecture figée.
        const auto& ts = edit.tempoSequence.getInternalSequence();
        const auto t = fc.editTime.getStart();
        tr.bpm = ts.getBpmAt (t);
        const auto sig = ts.getTimeSignatureAt (t);
        if (sig.numerator > 0 && sig.denominator > 0) { tr.numerator = (uint32_t) sig.numerator; tr.denominator = (uint32_t) sig.denominator; }
        tr.ppqPosition = ts.toBeats (t).inBeats();
        tr.ppqBarStart = std::floor (tr.ppqPosition / juce::jmax (1u, tr.numerator)) * tr.numerator;
        tr.playing = fc.isPlaying;
        auto& transport = edit.getTransport();
        tr.looping = transport.looping.get();
        if (tr.looping)
        {
            const auto lr = transport.getLoopRange();
            tr.ppqLoopStart = ts.toBeats (lr.getStart()).inBeats();
            tr.ppqLoopEnd = ts.toBeats (lr.getEnd()).inBeats();
        }
        sandbox->setTransport (tr);
    }

    // Si le plugin isolé a planté, process() rend du silence : seule cette piste se tait.
    sandbox->process (chans, (uint32_t) numCh, (uint32_t) fc.bufferNumSamples,
                      (uint64_t) std::max<int64_t> (0, (int64_t) (fc.editTime.getStart().inSeconds() * sampleRate)),
                      midiScratch.data(), numMidi, fc.isRendering);
}

bool SandboxPlugin::isFailed() const
{
    if (sandbox == nullptr) return false;
    const auto s = sandbox->getState();
    return s == onde::PluginSandbox::State::crashed || s == onde::PluginSandbox::State::hung;
}

juce::String SandboxPlugin::describeFailure() const
{
    return sandbox ? juce::String::fromUTF8 (sandbox->describeFailure().c_str()) : juce::String();
}

void SandboxPlugin::restartSandbox()
{
    if (sandbox == nullptr) return;
    sandbox->restart();
    juce::MemoryBlock mb;
    if (savedState.get().isNotEmpty() && mb.fromBase64Encoding (savedState.get()))
        sandbox->loadState (std::vector<uint8_t> ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize()));
}

bool SandboxPlugin::hasEditor() const
{
    return sandbox != nullptr && sandbox->getInfo().hasEditor;
}

void SandboxPlugin::showEditor()
{
    if (sandbox) sandbox->sendCommand (onde::Command::showEditor, 5000);
}

void SandboxPlugin::hideEditor()
{
    if (sandbox) sandbox->sendCommand (onde::Command::hideEditor, 3000);
}

bool SandboxPlugin::isEditorVisible() const
{
    return sandbox != nullptr && sandbox->getInfo().editorVisible;
}

double SandboxPlugin::getLatencySeconds()
{
    if (sandbox == nullptr || preparedRate <= 0.0) return 0.0;
    return (sandbox->getPipelineFrames() + sandbox->getLatencySamples()) / preparedRate;
}

void SandboxPlugin::simulateCrash()
{
    if (sandbox) sandbox->setParameter (onde::kCrashTestParam, 1.0f);
}
