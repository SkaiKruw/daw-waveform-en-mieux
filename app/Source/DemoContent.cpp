#include "DemoContent.h"
#include "OndeSynth.h"
#include "Chains.h"

namespace
{
    te::AudioTrack* trackAt (te::Edit& edit, int index)
    {
        edit.ensureNumberOfAudioTracks (index + 1);
        return te::getAudioTracks (edit)[index];
    }

    // Boucle de batterie synthétisée (kick / caisse claire / charleston), 8 mesures.
    juce::File writeDrumLoop (const juce::File& dir)
    {
        const double sr = 48000.0;
        const int total = (int) (demo::songSeconds() * sr);
        juce::AudioBuffer<float> buf (2, total);
        buf.clear();
        juce::Random rnd (42);
        const double beat = 60.0 / demo::bpm;

        auto addKick = [&] (double t)
        {
            const int s0 = (int) (t * sr);
            double phase = 0;
            for (int i = 0; i < (int) (0.35 * sr) && s0 + i < total; ++i)
            {
                const double x = i / sr;
                phase += juce::MathConstants<double>::twoPi * (45.0 + 110.0 * std::exp (-x * 30.0)) / sr;
                const float v = (float) (std::sin (phase) * std::exp (-x * 9.0) * 0.9);
                buf.addSample (0, s0 + i, v); buf.addSample (1, s0 + i, v);
            }
        };
        auto addNoise = [&] (double t, double len, double decay, float gain, double tone)
        {
            const int s0 = (int) (t * sr);
            float last = 0;
            for (int i = 0; i < (int) (len * sr) && s0 + i < total; ++i)
            {
                const double x = i / sr;
                const float n = rnd.nextFloat() * 2.0f - 1.0f;
                const float hp = n - last * (float) tone; last = n;
                const float v = hp * (float) std::exp (-x * decay) * gain;
                buf.addSample (0, s0 + i, v); buf.addSample (1, s0 + i, v * 0.92f);
            }
        };

        for (int bar = 0; bar < demo::bars; ++bar)
            for (int step = 0; step < 16; ++step)
            {
                const double t = (bar * 4 + step / 4.0) * beat;
                if (step == 0 || step == 6 || step == 10) addKick (t);
                if (step == 4 || step == 12)              addNoise (t, 0.25, 18.0, 0.45f, 0.2);
                if (step % 2 == 0)                        addNoise (t, 0.06, 70.0, step % 4 == 2 ? 0.16f : 0.10f, 0.95);
            }

        auto file = dir.getChildFile ("batterie.wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
            if (auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (sr)
                                                                              .withNumChannels (2).withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (buf, 0, total);
        return file;
    }
}

#define U8(x) juce::String::fromUTF8 (x)

namespace demo
{
    DemoProject create (te::Engine& engine, const juce::File& editFile, const juce::String& padSpec, const PluginEntry* vst3Synth)
    {
        DemoProject p;
        editFile.getParentDirectory().createDirectory();
        p.edit = te::createEmptyEdit (engine, editFile);
        auto& edit = *p.edit;
        edit.editFileRetriever = [editFile] { return editFile; };
        // Chemins relatifs résolus à côté du projet (et non depuis le dossier courant) dès l'insertion des clips.
        edit.filePathResolver = [editFile] (const juce::String& path) -> juce::File
        {
            // Tracktion écrit « ../fichier.wav » : chemin relatif au fichier projet vu comme un dossier.
            return juce::File::isAbsolutePath (path) ? juce::File (path) : editFile.getChildFile (path);
        };
        edit.tempoSequence.getTempos()[0]->setBpm (bpm);
        const auto songEnd = te::TimePosition::fromSeconds (songSeconds());

        // 1. Batterie — clip audio.
        auto* drums = trackAt (edit, 0);
        drums->setName ("Batterie");
        drums->setColour (juce::Colour (0xffe58a6e));
        const auto drumFile = writeDrumLoop (editFile.getParentDirectory());
        engine.getAudioFileManager().forceFileUpdate (te::AudioFile (engine, drumFile));   // fichier réécrit : pas d'infos en cache
        if (! drumFile.existsAsFile()) DBG ("batterie de démo absente");
        drums->insertWaveClip ("Batterie A", drumFile,
                               { { te::TimePosition(), songEnd }, te::TimeDuration() }, false);

        // 2. Basse — clip MIDI joué par Onde Synth (son « Basse synthé »), avec ses commandes rapides.
        auto* bass = trackAt (edit, 1);
        bass->setName ("Basse");
        bass->setColour (juce::Colour (0xff6ea8fe));
        if (auto synth = edit.getPluginCache().createNewPlugin (ondefx::SynthPlugin::xmlTypeName, {}))
        {
            bass->pluginList.insertPlugin (synth, 0, nullptr);
            if (auto* s = dynamic_cast<ondefx::SynthPlugin*> (synth.get()))
                if (auto* preset = ondefx::SynthPlugin::findPreset (juce::String::fromUTF8 ("Basse synthé"))) s->applyPreset (*preset);
            chains::rebuildMacros (*bass);
        }
        if (auto clip = bass->insertMIDIClip ("Basse couplet", { te::TimePosition(), songEnd }, nullptr))
        {
            const int roots[] = { 38, 38, 34, 36, 38, 38, 33, 36 };   // Ré, Ré, Si♭, Do, Ré, Ré, La, Do
            for (int bar = 0; bar < bars; ++bar)
                for (int s = 0; s < 8; ++s)
                    if (s != 3 && s != 7)
                        clip->getSequence().addNote (roots[bar] + (s == 5 ? 12 : 0),
                                                     te::BeatPosition::fromBeats (bar * 4 + s * 0.5),
                                                     te::BeatDuration::fromBeats (0.4), 100, 0, nullptr);
        }

        // 3. Voix lead — vide, prête à enregistrer.
        p.voice = trackAt (edit, 2);
        p.voice->setName ("Voix lead");
        p.voice->setColour (juce::Colour (0xffa6d96a));

        // 4. Pad — synthé exécuté dans un processus isolé.
        auto* padTrack = trackAt (edit, 3);
        padTrack->setName ("Pad");
        padTrack->setColour (juce::Colour (0xffd493c4));
        if (auto plug = edit.getPluginCache().createNewPlugin (SandboxPlugin::xmlTypeName, {}))
        {
            if (auto* sp = dynamic_cast<SandboxPlugin*> (plug.get()))
            {
                sp->spec = padSpec;
                p.pad = sp;
            }
            padTrack->pluginList.insertPlugin (plug, 0, nullptr);
        }

        // 5. Optionnel : un vrai VST3, isolé, avec une mélodie.
        if (vst3Synth != nullptr)
        {
            auto* lead = trackAt (edit, 4);
            lead->setName (U8 ("Synthé VST3"));
            lead->setColour (juce::Colour (0xff5fc8b0));
            PluginCatalog::addToTrack (*lead, *vst3Synth);
            if (auto clip = lead->insertMIDIClip (U8 ("Mélodie"), { te::TimePosition::fromSeconds (4 * barSeconds()), songEnd }, nullptr))
            {
                const int line[] = { 62, 65, 69, 67, 65, 64, 62, 60 };
                for (int i = 0; i < 16; ++i)
                    clip->getSequence().addNote (line[i % 8] + 12, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.8), 96, 0, nullptr);
            }
        }

        edit.getTransport().setLoopRange ({ te::TimePosition(), songEnd });
        edit.getTransport().looping = true;
        return p;
    }

    void routeInputsToVoiceTrack (DemoProject& p)
    {
        auto& edit = *p.edit;
        auto& dm = edit.engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto wip = dm.getWaveInDevice (i))
            {
                wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                wip->setEnabled (true);
            }

        edit.getTransport().ensureContextAllocated();
        for (auto instance : edit.getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
            {
                [[maybe_unused]] auto r = instance->setTarget (p.voice->itemID, true, &edit.getUndoManager(), 0);
                break;
            }
    }
}
