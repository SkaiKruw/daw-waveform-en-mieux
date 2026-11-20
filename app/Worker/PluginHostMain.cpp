// onde-plugin-host : processus isolé qui charge UN plugin (VST3 ou démo) pour Onde.
//   onde-plugin-host <mémoire-partagée> <spec> <pid-hôte>   spec = "vst3:<chemin>[#<nom>]" ou spec de démo
//   onde-plugin-host --scan <chemin.vst3>                    liste les plugins d'un fichier (scan isolé)
#include <JuceHeader.h>
#include "onde/WorkerChannel.h"
#include "onde/WorkerPlugins.h"
#include <atomic>
#include <thread>

using namespace onde;

namespace
{
    void copyText (char* dest, size_t size, const juce::String& s)
    {
        std::snprintf (dest, size, "%s", s.toRawUTF8());
    }

    int scanFile (const juce::String& path)
    {
        juce::VST3PluginFormat fmt;
        juce::OwnedArray<juce::PluginDescription> found;
        fmt.findAllTypesForFile (found, path);
        for (auto* d : found)
            std::printf ("PLUGIN\t%s\t%s\t%d\t%s\t%s\n", d->name.toRawUTF8(), d->manufacturerName.toRawUTF8(),
                         d->isInstrument ? 1 : 0, d->fileOrIdentifier.toRawUTF8(), d->createIdentifierString().toRawUTF8());
        std::fflush (stdout);
        return found.isEmpty() ? 1 : 0;
    }
}

// Transport de l'hôte vu par le plugin (tempo, position musicale, lecture) : délais synchronisés, arpèges…
class SharedPlayHead  : public juce::AudioPlayHead
{
public:
    explicit SharedPlayHead (const SharedBlock& b) : sampleRate (b.sampleRate) {}
    const Slot* current = nullptr;                 // bloc en cours de traitement
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (current == nullptr) return {};
        const auto& block = *current;
        PositionInfo p;
        p.setBpm (block.bpm > 1.0 ? block.bpm : 120.0);
        p.setTimeSignature (TimeSignature { (int) juce::jmax (1u, block.timeSigNumerator), (int) juce::jmax (1u, block.timeSigDenominator) });
        p.setTimeInSamples ((juce::int64) block.samplePosition);
        p.setTimeInSeconds ((double) block.samplePosition / juce::jmax (1.0, sampleRate));
        p.setPpqPosition (block.ppqPosition);
        p.setPpqPositionOfLastBarStart (block.ppqBarStart);
        p.setIsPlaying (block.isPlaying != 0);
        p.setIsLooping (block.isLooping != 0);
        if (block.isLooping != 0) p.setLoopPoints (LoopPoints { block.ppqLoopStart, block.ppqLoopEnd });
        return p;
    }
private:
    double sampleRate;
};

class EditorWindow  : public juce::DocumentWindow
{
public:
    EditorWindow (const juce::String& title, juce::AudioProcessorEditor* editor, std::atomic<uint32_t>& visibleFlag)
        : DocumentWindow (title, juce::Colour (0xff1c1e22), DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          visible (visibleFlag)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }
    void closeButtonPressed() override { setVisible (false); visible.store (0); }
private:
    std::atomic<uint32_t>& visible;
};

class PluginHostApp  : public juce::JUCEApplication,
                       private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "onde-plugin-host"; }
    const juce::String getApplicationVersion() override { return "0.3.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        WorkerChannel::installCrashHandlers();
        WorkerChannel::limitRealtimeRuntime();
        const auto args = getCommandLineParameterArray();

        if (args[0] == "--scan")
        {
            setApplicationReturnValue (scanFile (args[1].unquoted()));
            quit();
            return;
        }
        if (args.size() < 2 || ! channel.open (args[0].unquoted().toStdString(), args.size() > 2 ? args[2].toRawUTF8() : nullptr))
        {
            setApplicationReturnValue (4);
            quit();
            return;
        }

        auto& b = channel.block();
        const juce::String spec = args[1].unquoted();
        if (spec.startsWith ("vst3:"))
        {
            if (! loadVST3 (spec.fromFirstOccurrenceOf ("vst3:", false, false), b))
            {
                b.workerReady.store (1);           // l'hôte lit errorText puis voit le processus se terminer
                juce::Thread::sleep (300);
                setApplicationReturnValue (6);
                quit();
                return;
            }
        }
        else
        {
            demo = createWorkerPlugin (spec.toStdString());
            demo->prepare (b.sampleRate, b.numChannels);
            copyText (b.pluginName, sizeof (b.pluginName), spec);
            b.isInstrument = spec.contains ("synth") ? 1u : 0u;
        }

        audioThread = std::thread ([this] { audioLoop(); });
        b.workerReady.store (1);
        startTimer (30);
    }

    void shutdown() override
    {
        running = false;
        if (audioThread.joinable()) audioThread.join();
        stopTimer();
        window = nullptr;
        if (instance) instance->releaseResources();
        instance = nullptr;
    }

private:
    bool loadVST3 (const juce::String& specRest, SharedBlock& b)
    {
        const auto path = specRest.upToFirstOccurrenceOf ("#", false, false);
        const auto wantedName = specRest.fromFirstOccurrenceOf ("#", false, false);
        juce::VST3PluginFormat fmt;
        juce::OwnedArray<juce::PluginDescription> found;
        fmt.findAllTypesForFile (found, path);
        juce::PluginDescription* desc = found.isEmpty() ? nullptr : found[0];
        for (auto* d : found)
            if (wantedName.isNotEmpty() && d->name == wantedName) desc = d;
        if (desc == nullptr)
        {
            copyText (b.errorText, sizeof (b.errorText), "aucun plugin VST3 dans " + path);
            return false;
        }
        juce::String error;
        instance = fmt.createInstanceFromDescription (*desc, b.sampleRate, (int) kMaxFrames, error);
        if (instance == nullptr)
        {
            copyText (b.errorText, sizeof (b.errorText), error.isEmpty() ? juce::String ("échec du chargement") : error);
            return false;
        }

        // Stéréo en sortie (et en entrée pour un effet) si le plugin l'accepte.
        auto layout = instance->getBusesLayout();
        if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
        if (instance->checkBusesLayoutSupported (layout))
            instance->setBusesLayout (layout);

        playHead = std::make_unique<SharedPlayHead> (b);
        instance->setPlayHead (playHead.get());
        instance->prepareToPlay (b.sampleRate, (int) kMaxFrames);
        b.latencySamples.store ((uint32_t) juce::jmax (0, instance->getLatencySamples()));
        const int chans = juce::jmax (2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
        scratch.setSize (chans, (int) kMaxFrames);
        midi.ensureSize (kMaxMidiEvents * 4);

        copyText (b.pluginName, sizeof (b.pluginName), desc->name);
        b.isInstrument = desc->isInstrument ? 1u : 0u;
        // Pas de hasEditor() ici : pour un VST3, il crée la vue du plugin pour vérifier qu'elle existe, et certains
        // plugins (Neural DSP…) attendent alors indéfiniment un événement d'interface que la boucle de messages,
        // pas encore lancée, ne délivre jamais. La fenêtre est créée à la demande (showEditor).
        b.hasEditor = 1u;
        return true;
    }

    void audioLoop()
    {
        WorkerChannel::makeCurrentThreadAudio();
        auto& b = channel.block();
        uint64_t lastDone = b.requestSeq.load (std::memory_order_acquire);
        while (running)
        {
            const auto w = channel.waitForRequest (200);
            if (w == WorkerChannel::Wait::hostGone || w == WorkerChannel::Wait::shutdown)
            {
                juce::MessageManager::callAsync ([] { juce::JUCEApplicationBase::quit(); });
                return;
            }
            if (w != WorkerChannel::Wait::request)
                continue;

            // Toutes les requêtes en attente, dans l'ordre (au plus deux en traitement anticipé).
            while (running && lastDone < b.requestSeq.load (std::memory_order_acquire))
            {
                const uint64_t seq = lastDone + 1;
                auto& s = b.slot (seq);
                if (b.params[kCrashTestParam] > 0.5f)
                    crashForTesting();                // bouton de démo « Simuler un plantage »

                if (instance != nullptr)
                {
                    const int n = (int) juce::jlimit<uint32_t> (1, kMaxFrames, s.numFrames);
                    for (int c = 0; c < scratch.getNumChannels(); ++c)
                    {
                        if (c < (int) b.numChannels) scratch.copyFrom (c, 0, s.channel ((uint32_t) c), n);
                        else                         scratch.clear (c, 0, n);
                    }
                    midi.clear();
                    for (uint32_t i = 0; i < juce::jmin (s.numMidiEvents, kMaxMidiEvents); ++i)
                        midi.addEvent (s.midi[i].data, s.midi[i].size, (int) juce::jmin<uint32_t> (s.midi[i].offset, (uint32_t) n - 1));

                    if ((s.isOffline != 0) != instance->isNonRealtime())
                        instance->setNonRealtime (s.isOffline != 0);
                    playHead->current = &s;
                    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), scratch.getNumChannels(), n);
                    instance->processBlock (view, midi);
                    b.latencySamples.store ((uint32_t) juce::jmax (0, instance->getLatencySamples()), std::memory_order_relaxed);

                    for (uint32_t c = 0; c < b.numChannels; ++c)
                        std::memcpy (s.channel (c), scratch.getReadPointer (juce::jmin ((int) c, scratch.getNumChannels() - 1)), (size_t) n * sizeof (float));
                }
                else if (demo != nullptr)
                {
                    demo->process (b, s);
                }
                channel.respond (seq);
                lastDone = seq;
            }
        }
    }

    // Fil principal : fenêtres d'édition et préréglages.
    void timerCallback() override
    {
        auto& b = channel.block();
        switch (channel.pendingCommand())
        {
            case Command::none: break;
            case Command::showEditor:
            {
                juce::AudioProcessorEditor* ed = nullptr;
                if (instance != nullptr && window == nullptr) ed = instance->createEditorIfNeeded();
                if (window == nullptr && ed != nullptr)
                    window = std::make_unique<EditorWindow> (instance->getName() + juce::String::fromUTF8 (" — isolé par Onde"), ed, b.editorVisible);
                if (window != nullptr)
                {
                    window->setVisible (true);
                    window->toFront (true);
                    b.editorVisible.store (1);
                    channel.commandDone (true);
                }
                else { b.hasEditor = 0u; channel.commandDone (false); }   // pas d'interface : on le saura désormais
                break;
            }
            case Command::hideEditor:
                if (window) window->setVisible (false);
                b.editorVisible.store (0);
                channel.commandDone (true);
                break;
            case Command::saveState:
            {
                juce::MemoryBlock mb;
                if (instance != nullptr) instance->getStateInformation (mb);
                const bool ok = mb.getSize() <= kMaxStateBytes;
                if (ok) { std::memcpy (b.state, mb.getData(), mb.getSize()); b.stateSize = (uint32_t) mb.getSize(); }
                channel.commandDone (ok);
                break;
            }
            case Command::loadState:
                if (instance != nullptr && b.stateSize > 0)
                    instance->setStateInformation (b.state, (int) b.stateSize);
                channel.commandDone (true);
                break;
        }
    }

    WorkerChannel channel;
    std::unique_ptr<SharedPlayHead> playHead;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<WorkerPlugin> demo;
    std::unique_ptr<EditorWindow> window;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    std::thread audioThread;
    std::atomic<bool> running { true };
};

START_JUCE_APPLICATION (PluginHostApp)
