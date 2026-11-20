// Onde — prototype v0.3. Modes :
//   Onde [projet.onde]            interface normale
//   Onde --render-test <dossier>  rendu hors ligne avec plantage d'un plugin isolé (vérification)
//   Onde --ui-test <dossier>      scénario d'édition automatique + captures de chaque vue (vérification)
//   Onde --stems-test <dossier>   séparateur de sources : précision (SDR) et import (vérification)
#include <JuceHeader.h>
#include "MainComponent.h"
#include "DemoContent.h"
#include "StudioLookAndFeel.h"
#include "Exporter.h"

namespace
{
    // Exécute les tâches longues (rendus, exports) jusqu'au bout.
    struct OndeUIBehaviour  : public te::UIBehaviour
    {
        void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
        {
            while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain) {}
        }
    };

    juce::File appDataDir() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde"); }

    juce::Array<double> rmsPerBar (const juce::File& f, int bars, double barSec)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        juce::Array<double> out;
        if (auto r = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (f)))
        {
            const int perBar = (int) (barSec * r->sampleRate);
            juce::AudioBuffer<float> b (2, perBar);
            for (int bar = 0; bar < bars; ++bar)
            {
                b.clear();
                r->read (&b, 0, perBar, (juce::int64) bar * perBar, true, true);
                out.add ((b.getRMSLevel (0, 0, perBar) + b.getRMSLevel (1, 0, perBar)) * 0.5);
            }
        }
        return out;
    }

    int failures = 0;
    bool tracing = false;
    void trace (const char* m) { if (tracing) { std::fprintf (stderr, "[trace %u] %s\n", juce::Time::getMillisecondCounter(), m); std::fflush (stderr); } }
    void check (bool ok, const juce::String& m) { std::printf ("  %s : %s\n", ok ? "ok    " : "ÉCHEC ", m.toRawUTF8()); if (! ok) ++failures; std::fflush (stdout); }

    bool renderEdit (te::Edit& edit, const juce::File& wav, juce::Array<te::Track*> tracks)
    {
        if (edit.getTransport().isPlaying()) edit.getTransport().stop (false, true);
        EditContext::prepareAudioProxies (edit, 60000);
        te::Renderer::Parameters p (edit);
        p.destFile = wav;
        p.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        p.bitDepth = 24; p.sampleRateForAudio = 48000.0; p.blockSizeForAudio = 512;
        p.time = { te::TimePosition(), te::TimePosition::fromSeconds (demo::songSeconds()) };
        p.usePlugins = true;
        p.tracksToDo = te::toBitSet (tracks);
        wav.deleteFile();
        return te::Renderer::renderToFile ("Rendu de test", p).existsAsFile();
    }

    int runRenderTest (te::Engine& engine, const juce::File& dir)
    {
        dir.createDirectory();
        const double crashAt = 4 * demo::barSeconds();
        const juce::String crashSpec = "synth-pad,crash-at:" + juce::String (crashAt, 4);
        struct R { bool ok; onde::PluginSandbox::Stats st; onde::PluginSandbox::State state; };
        auto render = [&] (const juce::String& spec, const juce::File& wav, bool padOnly)
        {
            auto p = demo::create (engine, dir.getChildFile (wav.getFileNameWithoutExtension() + ".onde"), spec);
            juce::Array<te::Track*> tracks;
            for (auto* t : te::getAudioTracks (*p.edit)) if (! padOnly || t->getName() == "Pad") tracks.add (t);
            R r { renderEdit (*p.edit, wav, tracks), {}, {} };
            if (p.pad && p.pad->getSandbox()) { r.st = p.pad->getSandbox()->getStats(); r.state = p.pad->getSandbox()->getState(); }
            return r;
        };
        const auto mixA = dir.getChildFile ("mix-sans-plantage.wav"), mixB = dir.getChildFile ("mix-avec-plantage.wav");
        const auto padA = dir.getChildFile ("pad-seul-sans-plantage.wav"), padB = dir.getChildFile ("pad-seul-avec-plantage.wav");
        const auto rA = render ("synth-pad", mixA, false), rB = render (crashSpec, mixB, false);
        const auto rC = render ("synth-pad", padA, true),  rD = render (crashSpec, padB, true);
        const auto a = rmsPerBar (mixA, demo::bars, demo::barSeconds()), b = rmsPerBar (mixB, demo::bars, demo::barSeconds());
        const auto c = rmsPerBar (padA, demo::bars, demo::barSeconds()), d = rmsPerBar (padB, demo::bars, demo::barSeconds());

        std::printf ("\nRendu hors ligne, 8 mesures à 92 BPM, 48 kHz — le synthé isolé plante au début de la mesure 5\n\n");
        std::printf ("Mesure | Mix sans plantage | Mix avec plantage | Pad seul sans | Pad seul avec\n");
        for (int i = 0; i < demo::bars && i < a.size() && i < b.size() && i < c.size() && i < d.size(); ++i)
            std::printf ("   %d   |      %.4f       |      %.4f       |    %.4f     |    %.4f\n", i + 1, a[i], b[i], c[i], d[i]);
        std::printf ("\n");
        check (rA.ok && rB.ok && rC.ok && rD.ok, U ("les 4 rendus vont jusqu'au bout : l'hôte n'a jamais planté"));
       #if JUCE_WINDOWS
        const bool av = rB.st.exitCode == 0xC0000005u;
       #else
        const bool av = rB.st.lastSignal == SIGSEGV;
       #endif
        std::printf ("  info   : état du synthé après le rendu B : %s, signal %d, code %u\n", onde::toString (rB.state), rB.st.lastSignal, rB.st.exitCode);
        check (rB.state == onde::PluginSandbox::State::crashed && av, U ("le processus du synthé a bien planté (accès mémoire invalide)"));
        bool before = d.size() == demo::bars; for (int i = 0; i < 4 && before; ++i) before = c[i] > 0.01 && std::abs (c[i] - d[i]) < 1e-4;
        check (before, U ("mesures 1 à 4 : le synthé joue à l'identique avant le plantage"));
        // Mesure 5 : au plus le dernier bloc calculé avant le plantage (traitement anticipé) ; ensuite, silence total.
        bool after = d.size() == demo::bars && d[4] < 0.002; for (int i = 5; i < 8 && after; ++i) after = c[i] > 0.01 && d[i] < 1e-5;
        check (after, U ("mesures 5 à 8 : le synthé se tait dès le plantage (mesure 5 : ") + juce::String (d.size() > 4 ? d[4] : -1.0, 4) + ")");
        bool others = b.size() == demo::bars; for (int i = 4; i < 8 && others; ++i) others = b[i] > 0.05;
        check (others, U ("mesures 5 à 8 : batterie et basse continuent dans le mix"));
        std::printf ("\n%s\n", failures == 0 ? "RENDU OK" : "DES VÉRIFICATIONS ÉCHOUENT");
        return failures == 0 ? 0 : 1;
    }

    // --- Séparateur de sources : précision mesurée (SDR) sur le projet de démo ---------------------
    juce::AudioBuffer<float> readAll (const juce::File& f, double& rate)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        juce::AudioBuffer<float> b;
        if (auto r = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (f)))
        {
            rate = r->sampleRate;
            b.setSize (2, (int) r->lengthInSamples);
            r->read (&b, 0, b.getNumSamples(), 0, true, true);
        }
        return b;
    }

    // Rapport signal / distorsion (dB), comme le SDR de MUSDB : 10 log10 (Σ ref² / Σ (ref - est)²).
    double sdr (const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& est)
    {
        double num = 0, den = 0;
        const int n = juce::jmin (ref.getNumSamples(), est.getNumSamples());
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const double r = ref.getSample (c, i), e = est.getSample (c, i);
                num += r * r; den += (r - e) * (r - e);
            }
        return 10.0 * std::log10 ((num + 1e-9) / (den + 1e-9));
    }

    double energyRatioDb (const juce::AudioBuffer<float>& part, const juce::AudioBuffer<float>& whole)
    {
        double a = 0, b = 0;
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < part.getNumSamples(); ++i)  a += (double) part.getSample (c, i) * part.getSample (c, i);
            for (int i = 0; i < whole.getNumSamples(); ++i) b += (double) whole.getSample (c, i) * whole.getSample (c, i);
        }
        return 10.0 * std::log10 ((a + 1e-12) / (b + 1e-12));
    }

    int runStemsTest (te::Engine& engine, EditContext& ctx, const juce::File& dir)
    {
        dir.createDirectory();
        std::printf ("\nSéparateur de sources (Demucs v4 fine-tuned)\n\n");
        juce::String why;
        check (stems::isAvailable (why), U ("moteur et modèles présents ") + why);
        if (failures > 0) return 1;

        auto p = demo::create (engine, dir.getChildFile ("demo.onde"), "synth-pad");
        auto& edit = *p.edit;
        MainComponent::installPathResolver (edit);
        auto render = [&] (const juce::File& f, juce::Array<te::Track*> tracks, double rate)
        {
            EditContext::prepareAudioProxies (edit, 60000);
            te::Renderer::Parameters rp (edit);
            rp.destFile = f; rp.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
            rp.bitDepth = 32; rp.sampleRateForAudio = rate; rp.blockSizeForAudio = 512; rp.ditheringEnabled = false;
            rp.time = { te::TimePosition(), te::TimePosition::fromSeconds (demo::songSeconds()) };
            rp.usePlugins = true; rp.tracksToDo = te::toBitSet (tracks);
            f.deleteFile();
            return te::Renderer::renderToFile ("Rendu de test", rp).existsAsFile();
        };
        auto trackNamed = [&] (const juce::String& n) -> te::Track* { for (auto* t : te::getAudioTracks (edit)) if (t->getName() == n) return t; return nullptr; };
        juce::Array<te::Track*> all { trackNamed ("Batterie"), trackNamed ("Basse"), trackNamed ("Pad") };
        const auto mix48 = dir.getChildFile ("mix-48k.wav");
        {
            // La batterie de démo est un fichier à côté du projet : il doit être trouvé quel que soit le dossier courant.
            auto* dc = dynamic_cast<te::WaveAudioClip*> (dynamic_cast<te::AudioTrack*> (trackNamed ("Batterie"))->getClips()[0]);
            check (dc != nullptr && dc->getAudioFile().isValid(), U ("démo : l'événement de batterie trouve son fichier (") + (dc ? dc->getAudioFile().getFile().getFileName() : juce::String()) + ")");
        }
        // Références : chaque piste seule, rendue à 48 kHz. Le mix est leur somme exacte (certains synthés ont une
        // phase aléatoire d'un rendu à l'autre), puis tout est converti à 44,1 kHz par le même rééchantillonneur.
        bool ok = true;
        const char* refNames[] = { "Batterie", "Basse", "Pad" };
        juce::AudioBuffer<float> mixSum;
        for (auto* n : refNames)
        {
            juce::String e;
            const auto r48 = dir.getChildFile (juce::String ("ref48-") + n + ".wav");
            const bool r = render (r48, { trackNamed (n) }, 48000.0)
                           && stems::resampleTo44k (r48, 0.0, 1.0e6, false, dir.getChildFile (juce::String ("ref-") + n + ".wav"), e);
            if (! r) std::printf ("  info   : référence %s non rendue %s\n", n, e.toRawUTF8());
            ok = r && ok;
            double rr = 0;
            const auto b = readAll (r48, rr);
            if (mixSum.getNumSamples() == 0) { mixSum.setSize (2, b.getNumSamples()); mixSum.clear(); }
            for (int c = 0; c < 2; ++c) mixSum.addFrom (c, 0, b, c, 0, juce::jmin (b.getNumSamples(), mixSum.getNumSamples()));
        }
        {
            mix48.deleteFile();
            std::unique_ptr<juce::OutputStream> os (mix48.createOutputStream());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (os ? wav.createWriterFor (os.get(), 48000.0, 2, 32, {}, 0) : nullptr);
            if (w != nullptr) { os.release(); ok = w->writeFromAudioSampleBuffer (mixSum, 0, mixSum.getNumSamples()) && ok; }
            else ok = false;
        }
        // Rendu direct à 44,1 kHz d'une piste audio (fichier source à 48 kHz) : doit contenir du son.
        const bool direct44 = render (dir.getChildFile ("batterie-44k.wav"), { trackNamed ("Batterie") }, 44100.0);
        check (direct44, U ("rendu à 44,1 kHz d'un événement audio enregistré à 48 kHz"));
        check (ok, U ("mix (48 kHz) et références par piste (44,1 kHz) rendus"));

        // Le mix devient un événement audio d'un nouveau projet : chemin complet de l'interface.
        auto e2 = te::Edit::createSingleTrackEdit (engine);
        e2->editFileRetriever = [f = dir.getChildFile ("projet-separation.onde")] { return f; };
        MainComponent::installPathResolver (*e2);
        ctx.edit = e2.get();
        auto* track = te::getAudioTracks (*e2)[0];
        const double startAt = 2.0;
        auto clip = track->insertWaveClip ("Mix", mix48, { { te::TimePosition::fromSeconds (startAt), te::TimePosition::fromSeconds (startAt + demo::songSeconds()) }, te::TimeDuration() }, false);
        const auto work = stems::workFolderFor (*e2, "Mix");
        juce::String err;
        ok = clip != nullptr && stems::writeClipInput (*clip, work.getChildFile ("source-44k.wav"), err);
        check (ok, U ("événement exporté à 44,1 kHz (rééchantillonnage sinc depuis 48 kHz) ") + err);

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        int reports = 0;
        float lastP = 0;
        // ONDE_STEMS_REUSE : réutilise une séparation précédente (mise au point des mesures sans 10 min de calcul).
        const auto previous = dir.getChildFile (U ("Séparations")).getChildFile ("Mix");
        if (ok && juce::SystemStats::getEnvironmentVariable ("ONDE_STEMS_REUSE", {}).isNotEmpty() && previous.getChildFile ("vocals.wav").existsAsFile() && previous != work)
        {
            for (auto& s : stems::list()) previous.getChildFile (juce::String (s.id) + ".wav").copyFileTo (work.getChildFile (juce::String (s.id) + ".wav"));
            reports = 3; lastP = 1.0f;
            std::printf ("  info   : séparation précédente réutilisée\n");
        }
        else
            ok = ok && stems::separateFile (work.getChildFile ("source-44k.wav"), work, [&] (float pr) { ++reports; lastP = pr; return true; }, err, 0,
                                            juce::SystemStats::getEnvironmentVariable ("ONDE_STEMS_MODE", "precis"));
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        check (ok && reports > 2 && lastP >= 0.999f, U ("séparation terminée en ") + juce::String (secs, 1) + U (" s pour ") + juce::String (demo::songSeconds(), 1)
                                     + U (" s d'audio (") + juce::String (reports) + U (" rapports de progression) ") + err);
        if (! ok) return 1;

        double rate = 0;
        const auto mix = readAll (work.getChildFile ("source-44k.wav"), rate);
        const auto drums = readAll (work.getChildFile ("drums.wav"), rate), bass = readAll (work.getChildFile ("bass.wav"), rate);
        const auto other = readAll (work.getChildFile ("other.wav"), rate), vocals = readAll (work.getChildFile ("vocals.wav"), rate);
        const auto refD = readAll (dir.getChildFile ("ref-Batterie.wav"), rate), refB = readAll (dir.getChildFile ("ref-Basse.wav"), rate);
        const auto refO = readAll (dir.getChildFile ("ref-Pad.wav"), rate);
        check (drums.getNumSamples() == mix.getNumSamples(), U ("les sources ont exactement la longueur de l'événement (") + juce::String (drums.getNumSamples()) + U (" échantillons)"));
        // Somme des sources = mix (Demucs conserve l'énergie) : résidu faible.
        juce::AudioBuffer<float> sum (2, mix.getNumSamples()); sum.clear();
        for (auto* b : { &drums, &bass, &other, &vocals }) for (int c = 0; c < 2; ++c) sum.addFrom (c, 0, *b, c, 0, juce::jmin (sum.getNumSamples(), b->getNumSamples()));
        const double recon = sdr (mix, sum);
        const double sd = sdr (refD, drums), sb = sdr (refB, bass), so = sdr (refO, other), voc = energyRatioDb (vocals, mix);
        std::printf ("  info   : SDR batterie %.1f dB, basse %.1f dB, autres (pad) %.1f dB, voix %.1f dB sous le mix, reconstruction %.1f dB\n", sd, sb, so, voc, recon);
        {
            const juce::AudioBuffer<float>* refs[] = { &refD, &refB, &refO }, *ests[] = { &drums, &bass, &other, &vocals };
            std::printf ("  info   : énergie (dB / mix) réf. batterie %.1f, basse %.1f, pad %.1f | estimées batterie %.1f, basse %.1f, autres %.1f, voix %.1f\n",
                         energyRatioDb (refD, mix), energyRatioDb (refB, mix), energyRatioDb (refO, mix),
                         energyRatioDb (drums, mix), energyRatioDb (bass, mix), energyRatioDb (other, mix), energyRatioDb (vocals, mix));
            for (int r = 0; r < 3; ++r)
                std::printf ("  info   : réf. %-8s SDR contre batterie %.1f, basse %.1f, autres %.1f, voix %.1f, mix %.1f\n", refNames[r],
                             sdr (*refs[r], *ests[0]), sdr (*refs[r], *ests[1]), sdr (*refs[r], *ests[2]), sdr (*refs[r], *ests[3]), sdr (*refs[r], mix));
        }
        check (sd > 8.0, U ("batterie isolée avec précision : SDR ") + juce::String (sd, 1) + " dB");
        check (sb > 8.0, U ("basse isolée avec précision : SDR ") + juce::String (sb, 1) + " dB");
        check (so > 5.0, U ("autres instruments (pad) isolés : SDR ") + juce::String (so, 1) + " dB");
        check (voc < -20.0, U ("pas de voix inventée sur un morceau instrumental : ") + juce::String (voc, 1) + " dB");
        check (recon > 20.0, U ("la somme des 4 sources redonne le mix : ") + juce::String (recon, 1) + " dB");

        // Import dans le projet : dossier, 4 pistes alignées, original muet, annulable.
        const int before = te::getAudioTracks (*e2).size();
        auto* folder = stems::importStems (ctx, *clip, work, err);
        const auto kids = folder != nullptr ? folder->getAllAudioSubTracks (false) : juce::Array<te::AudioTrack*>();
        bool aligned = kids.size() == 4;
        for (auto* k : kids)
            aligned = aligned && k->getClips().size() == 1
                      && std::abs (k->getClips()[0]->getPosition().getStart().inSeconds() - startAt) < 1e-6
                      && std::abs (k->getClips()[0]->getPosition().getLength().inSeconds() - demo::songSeconds()) < 1e-6;
        check (folder != nullptr && aligned && clip->isMuted() && te::getAudioTracks (*e2).size() == before + 4,
               U ("import : dossier « ") + (folder ? folder->getName() : juce::String()) + U (" » avec Voix, Batterie, Basse, Autres alignées, original muet"));
        const auto rendered = dir.getChildFile ("rendu-sources.wav");
        {
            te::Renderer::Parameters rp (*e2);
            rp.destFile = rendered; rp.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
            rp.bitDepth = 32; rp.sampleRateForAudio = 44100.0; rp.blockSizeForAudio = 512; rp.ditheringEnabled = false;
            rp.time = { te::TimePosition::fromSeconds (startAt), te::TimePosition::fromSeconds (startAt + demo::songSeconds()) };
            rp.usePlugins = true; rp.tracksToDo = te::toBitSet (te::getAllTracks (*e2));
            EditContext::prepareAudioProxies (*e2, 60000);
            te::Renderer::renderToFile ("Rendu", rp);
        }
        const auto played = readAll (rendered, rate);
        const double playSdr = sdr (mix, played);
        check (playSdr > 20.0, U ("le projet joue les 4 sources = le mix d'origine : ") + juce::String (playSdr, 1) + " dB");
        e2->getUndoManager().undo();
        check (te::getAudioTracks (*e2).size() == before && ! clip->isMuted(), U ("Annuler retire les sources et rend l'original audible"));
        ctx.edit = nullptr;
        std::printf ("\n%s\n", failures == 0 ? "SÉPARATEUR OK" : "DES VÉRIFICATIONS ÉCHOUENT");
        return failures == 0 ? 0 : 1;
    }

    void snapshot (juce::Component& c, const juce::File& f)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (img, out);
    }

    void pump (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

//==============================================================================
class OndeApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Onde"; }
    const juce::String getApplicationVersion() override    { return "0.4.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.removeEmptyStrings();
        tracing = args[0].startsWith ("--");
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        tooltips = std::make_unique<juce::TooltipWindow> (nullptr, 600);
        engine = std::make_unique<te::Engine> ("Onde", std::make_unique<OndeUIBehaviour>(), nullptr);
        engine->getPluginManager().createBuiltInType<SandboxPlugin>();
        catalog = std::make_unique<PluginCatalog>();

        if (args[0] == "--render-test")
        {
            const int code = runRenderTest (*engine, juce::File::getCurrentWorkingDirectory().getChildFile (args[1].isEmpty() ? "rendu-test" : args[1].unquoted()));
            setApplicationReturnValue (code);
            quit();
            return;
        }
        if (args[0] == "--stems-test")
        {
            juce::MessageManager::callAsync ([this, dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1].isEmpty() ? "stems-test" : args[1].unquoted())]
            {
                EditContext c (*engine, *catalog);
                setApplicationReturnValue (runStemsTest (*engine, c, dir));
                quit();
            });
            return;
        }
        if (args[0] == "--plugin-test")
        {
            juce::MessageManager::callAsync ([this, dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1].isEmpty() ? "plugin-test" : args[1].unquoted())]
            {
                setApplicationReturnValue (runPluginTest (dir));
                quit();
            });
            return;
        }
        if (args[0] == "--live-test")
        {
            juce::MessageManager::callAsync ([this, dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1].isEmpty() ? "live-test" : args[1].unquoted())]
            {
                setApplicationReturnValue (runLiveTest (dir));
                quit();
            });
            return;
        }
        if (args[0] == "--ui-test")
        {
            juce::MessageManager::callAsync ([this, dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1].isEmpty() ? "ui-test" : args[1].unquoted())]
            {
                setApplicationReturnValue (runUiTest (dir));
                quit();
            });
            return;
        }

        settings = std::make_unique<juce::PropertiesFile> (appDataDir().getChildFile ("reglages.xml"), juce::PropertiesFile::Options());
        const bool crashedLastTime = settings->getBoolValue ("sessionOpen", false);
        settings->setValue ("sessionOpen", true);
        settings->saveIfNeeded();

        auto content = std::make_unique<MainComponent> (*engine, *catalog);
        auto* mc = content.get();
        window = std::make_unique<MainWindow> (std::move (content));
        mc->onTitleChanged = [this] (const juce::String& t) { if (window) window->setName (t + U (" — Onde")); };

        const juce::File arg (args[0].unquoted());
        if (args.size() > 0 && arg.existsAsFile())
            mc->openProjectFile (arg);
        else
            openDemo (*mc);

        // Première ouverture : analyse des VST3 en arrière-plan (dans des processus isolés).
        if (catalog->getVST3 (true).isEmpty() && catalog->getVST3 (false).isEmpty())
            catalog->startScan();

        // Récupération après un arrêt brutal d'Onde.
        if (crashedLastTime && MainComponent::recoveryFile().existsAsFile())
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, U ("Récupérer la session ?"),
                U ("Onde ne s'est pas fermé normalement la dernière fois. Une copie de secours de votre projet sans titre existe. Voulez-vous la rouvrir ?"),
                U ("Récupérer"), U ("Ignorer"), nullptr,
                juce::ModalCallbackFunction::create ([mc] (int r)
                {
                    if (r == 1) mc->openProjectFile (MainComponent::recoveryFile());
                }));
    }

    void shutdown() override
    {
        if (settings) { settings->setValue ("sessionOpen", false); settings->saveIfNeeded(); }
        trace ("arrêt : fenêtre");
        window = nullptr;
        mc = nullptr;
        tooltips = nullptr;
        trace ("arrêt : catalogue");
        catalog = nullptr;
        trace ("arrêt : moteur");
        engine = nullptr;
        trace ("arrêt : terminé");
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        auto* mc = window ? dynamic_cast<MainComponent*> (window->getContentComponent()) : nullptr;
        if (mc != nullptr && mc->getEdit() != nullptr && mc->getEdit()->hasChangedSinceSaved())
        {
            juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, U ("Enregistrer les modifications ?"),
                U ("Le projet a des modifications non enregistrées."), U ("Enregistrer"), U ("Ne pas enregistrer"), U ("Annuler"), nullptr,
                juce::ModalCallbackFunction::create ([mc] (int r)
                {
                    if (r == 1) mc->saveProject ([] (bool ok) { if (ok) juce::JUCEApplication::quit(); });
                    else if (r == 2) { MainComponent::recoveryFile().deleteFile(); juce::JUCEApplication::quit(); }
                }));
            return;
        }
        quit();
    }

private:
    void openDemo (MainComponent& mc)
    {
        auto vst = catalog->getVST3 (true);
        const PluginEntry* synth = nullptr;
        for (auto& e : vst) if (e.name == "Onde Test Synth") synth = &e;
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("onde-demo").getChildFile ("Nuits du Nord.onde");
        auto project = demo::create (*engine, file, "synth-pad", synth);
        mc.setEdit (std::move (project.edit), file, true);
        if (auto* e = mc.getEdit()) { e->getUndoManager().clearUndoHistory(); e->resetChangedStatus(); }
    }

    // Chaque VST3 installé : chargement isolé, son, fenêtre, préréglage, latence.
    int runPluginTest (const juce::File& dir)
    {
        dir.createDirectory();
        std::printf ("\nTest de tous les plugins VST3 installés\n");
        catalog->startScan();
        for (int i = 0; i < 1200 && catalog->isScanning(); ++i) juce::Thread::sleep (100);
        for (auto& f : catalog->getFailures()) std::printf ("  info   : analyse refusée : %s\n", f.toRawUTF8());

        // Signal de test : accord de sinus pendant 4 s.
        const auto tone = dir.getChildFile ("signal-test.wav");
        {
            tone.deleteFile();
            juce::AudioBuffer<float> buf (2, 48000 * 4);
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = 0.15f * (std::sin (i * 2.0f * juce::MathConstants<float>::pi * 220.0f / 48000.0f)
                                         + std::sin (i * 2.0f * juce::MathConstants<float>::pi * 330.0f / 48000.0f));
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            if (auto out = std::unique_ptr<juce::FileOutputStream> (tone.createOutputStream()))
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (out.get(), 48000.0, 2, 24, {}, 0)))
                {
                    out.release();
                    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                }
        }

        int n = 0;
        for (bool inst : { true, false })
            for (auto& e : catalog->getVST3 (inst))
            {
                if (e.manufacturer == "Onde") continue;        // plugins de test d'Onde : déjà couverts ailleurs
                if (auto* only = std::getenv ("ONDE_PLUGIN")) if (! e.name.containsIgnoreCase (only)) continue;
                const auto t0 = juce::Time::getMillisecondCounter();
                auto file = dir.getChildFile ("plugin-" + juce::String (++n) + ".onde");
                auto edit = te::createEmptyEdit (*engine, file);
                edit->editFileRetriever = [file] { return file; };
                MainComponent::installPathResolver (*edit);
                auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr);
                const double len = 4.0;
                if (inst)
                {
                    auto mc = track->insertMIDIClip ("test", { te::TimePosition(), te::TimePosition::fromSeconds (len) }, nullptr);
                    for (int k = 0; k < 6; ++k)
                        for (int note : { 36, 38, 42, 48, 52, 55, 60 })
                            mc->getSequence().addNote (note, te::BeatPosition::fromBeats (k * 1.0), te::BeatDuration::fromBeats (0.8), 110, 0, nullptr);
                }
                else track->insertWaveClip ("signal", tone, { { te::TimePosition(), te::TimePosition::fromSeconds (len) }, te::TimeDuration() }, false);

                trace ((juce::String ("plugin : ajout ") + e.name).toRawUTF8());
                auto p = PluginCatalog::addToTrack (*track, e);
                auto* sp = dynamic_cast<SandboxPlugin*> (p.get());
                const auto wav = dir.getChildFile ("plugin-" + juce::String (n) + ".wav");
                te::Renderer::Parameters rp (*edit);
                rp.destFile = wav; rp.audioFormat = engine->getAudioFileFormatManager().getWavFormat();
                rp.bitDepth = 24; rp.sampleRateForAudio = 48000.0; rp.blockSizeForAudio = 512;
                rp.time = { te::TimePosition(), te::TimePosition::fromSeconds (len) };
                rp.usePlugins = true; rp.tracksToDo = te::toBitSet (te::getAllTracks (*edit));
                wav.deleteFile();
                if (inst && sp != nullptr)
                {
                    // Les échantillonneurs chargent leurs sons en arrière-plan : on leur laisse le temps.
                    sp->initialise ({ te::TimePosition(), 48000.0, 512 });
                    pump (8000);
                }
                trace ("plugin : rendu");
                const bool rendered = te::Renderer::renderToFile ("Test plugin", rp).existsAsFile();
                trace ("plugin : rendu fini");
                double rms = 0.0;
                for (auto v : rmsPerBar (wav, 2, 2.0)) rms = juce::jmax (rms, v);

                bool running = sp != nullptr && sp->getSandbox() != nullptr && sp->getSandbox()->getState() == onde::PluginSandbox::State::running;
                // Lecture en temps réel sur la carte son : le plugin doit répondre dans les temps.
                juce::uint64 rtOk = 0, rtMissed = 0;
                if (running && engine->getDeviceManager().deviceManager.getCurrentAudioDevice() != nullptr)
                {
                    auto& tr = edit->getTransport();
                    tr.ensureContextAllocated();
                    pump (500);
                    const auto before = sp->getSandbox() ? sp->getSandbox()->getStats() : onde::PluginSandbox::Stats();
                    tr.setPosition (te::TimePosition());
                    tr.play (false);
                    pump (500);
                    const auto early = sp->getSandbox() ? sp->getSandbox()->getStats().blocksMissed - before.blocksMissed : 0;
                    pump (2500);
                    tr.stop (false, false);
                    std::printf ("  info   : %-24s retards pendant la première demi-seconde : %llu\n", e.name.toRawUTF8(), (unsigned long long) early);
                    pump (300);
                    if (auto* sb = sp->getSandbox())
                    {
                        const auto after = sb->getStats();
                        rtOk = after.blocksOk - before.blocksOk;
                        rtMissed = after.blocksMissed - before.blocksMissed;
                    }
                    running = sp->getSandbox() != nullptr && sp->getSandbox()->getState() == onde::PluginSandbox::State::running;
                    std::printf ("  info   : %-24s temps réel : %llu blocs à l'heure, %llu en retard\n", e.name.toRawUTF8(), rtOk, rtMissed);
                }
                bool window = false;
                if (running && sp->hasEditor())
                {
                    sp->showEditor();
                    pump (1500);
                    window = sp->isEditorVisible();
                    sp->hideEditor();
                }
                int stateBytes = 0;
                if (running)
                {
                    sp->flushPluginStateToValueTree(); juce::MemoryBlock mb; mb.fromBase64Encoding (sp->savedState.get()); stateBytes = (int) mb.getSize();
                    dir.getChildFile ("plugin-" + juce::String (n) + ".state").replaceWithData (mb.getData(), mb.getSize());
                }
                const auto stats = sp && sp->getSandbox() ? sp->getSandbox()->getStats() : onde::PluginSandbox::Stats();
                std::printf ("  info   : %-24s %s · rendu %d · niveau %.4f · fenêtre %s · préréglage %d octets · latence %.1f ms · blocs manqués %llu · %u ms\n",
                             e.name.toRawUTF8(), inst ? "instrument" : "effet", (int) rendered, rms,
                             sp && sp->hasEditor() ? (window ? "ouverte" : "ÉCHEC") : "aucune", stateBytes,
                             sp ? sp->getLatencySeconds() * 1000.0 : 0.0, (unsigned long long) stats.blocksMissed, juce::Time::getMillisecondCounter() - t0);
                // Un instrument muet par défaut (échantillonneur sans kit chargé) n'est pas une panne d'Onde : on le signale.
                const bool silentSampler = inst && rms <= 1.0e-4;
                if (silentSampler && running)
                    std::printf ("  info   : %s reste muet sans son chargé : choisissez un kit/preset dans sa fenêtre puis vérifiez à l'oreille\n", e.name.toRawUTF8());
                check (running && rendered && (rms > 1.0e-4 || silentSampler) && (! sp->hasEditor() || window) && stateBytes > 0,
                       e.name + U (" (") + e.manufacturer + U (") : se charge isolé, ") + (silentSampler ? U ("muet par défaut") : U ("produit du son"))
                       + U (", ouvre sa fenêtre, sauvegarde son réglage")
                       + (running ? juce::String() : U (" — ") + (sp ? sp->describeFailure() : juce::String ("pas de plugin"))));
                check (rtOk > 50 && rtMissed * 100 <= rtOk, e.name + U (" : lecture en temps réel sans décrochage (") + juce::String ((juce::int64) rtMissed)
                       + U (" bloc(s) en retard sur ") + juce::String ((juce::int64) (rtOk + rtMissed)) + ")");
                std::fflush (stdout);
                trace ("plugin : fermeture");
                track = nullptr;                               // la piste ne doit pas survivre à son projet
                p = nullptr;
                EditContext::destroyEdit (edit);
                trace ("plugin : fermé");
            }
        std::printf ("\n%s\n", failures == 0 ? "PLUGINS OK" : "DES PLUGINS ÉCHOUENT");
        return failures == 0 ? 0 : 1;
    }

    // Lecture réelle sur la carte son : la tête avance, du son sort, la vue suit.
    int runLiveTest (const juce::File& dir)
    {
        dir.createDirectory();
        std::printf ("\nTest de lecture en temps réel\n");
        auto content = std::make_unique<MainComponent> (*engine, *catalog);
        auto* m = content.get();
        window = std::make_unique<MainWindow> (std::move (content));
        openDemo (*m);
        pump (1500);
        auto& dm = engine->getDeviceManager();
        check (dm.deviceManager.getCurrentAudioDevice() != nullptr, U ("une carte son est ouverte : ")
               + (dm.deviceManager.getCurrentAudioDevice() ? dm.deviceManager.getCurrentAudioDevice()->getName() : juce::String ("aucune")));
        auto* edit = m->getEdit();
        te::LevelMeasurer::Client client, groupClient;
        // La batterie passe dans un groupe : son vumètre (tranche de groupe de la MixConsole) doit bouger en lecture.
        te::LevelMeterPlugin* groupMeter = nullptr;
        for (auto* t : te::getAudioTracks (*edit))
            if (t->getName() == "Batterie")
            {
                m->getContext().selectTrack (t);
                if (auto* g = m->getContext().addFolderTrack (true, true)) groupMeter = EditContext::groupMeter (*g, false);
            }
        if (groupMeter != nullptr) groupMeter->measurer.addClient (groupClient);
        float groupPeak = -100.0f;
        {
            auto& tr = edit->getTransport();
            auto s = juce::String::formatted ("avant lecture : contexte=%d chargement=%d cycle=%d boucle=%.2f-%.2f pos=%.2f lecture=%d",
                                              (int) (edit->getCurrentPlaybackContext() != nullptr), (int) edit->isLoading(), (int) tr.looping.get(),
                                              tr.getLoopRange().getStart().inSeconds(), tr.getLoopRange().getEnd().inSeconds(), tr.getPosition().inSeconds(), (int) tr.isPlaying());
            onde::trace (s.toRawUTF8());
        }
        m->getContext().findAction ("transport.play")->perform();
        {
            auto& tr = edit->getTransport();
            auto s = juce::String::formatted ("APRES PLAY : contexte=%d lecture=%d pos=%.3f", (int) (edit->getCurrentPlaybackContext() != nullptr), (int) tr.isPlaying(), tr.getPosition().inSeconds());
            onde::trace (s.toRawUTF8());
        }
        pump (300);
        if (auto* pc = edit->getCurrentPlaybackContext()) pc->masterLevels.addClient (client);
        struct Spy : te::TransportControl::Listener
        {
            void playbackContextChanged() override { onde::trace ("transport: contexte de lecture changé"); }
            void autoSaveNow() override {}
            void setAllLevelMetersActive (bool) override {}
            void setVideoPosition (te::TimePosition, bool) override {}
            void startVideo() override {}
            void stopVideo() override {}
        } spy;
        edit->getTransport().addListener (&spy);
        float peak = -100.0f;
        for (int i = 0; i < 30; ++i)
        {
            pump (100);
            peak = juce::jmax (peak, client.getAndClearAudioLevel (0).dB, client.getAndClearAudioLevel (1).dB);
            groupPeak = juce::jmax (groupPeak, groupClient.getAndClearAudioLevel (0).dB, groupClient.getAndClearAudioLevel (1).dB);
            if (onde::traceEnabled())
            {
                auto s = juce::String::formatted ("t=%d ms lecture=%d pos=%.2f", (i + 1) * 100, (int) edit->getTransport().isPlaying(), edit->getTransport().getPosition().inSeconds());
                onde::trace (s.toRawUTF8());
            }
        }
        edit->getTransport().removeListener (&spy);
        const double pos = edit->getTransport().getPosition().inSeconds();
        check (edit->getTransport().isPlaying(), U ("la lecture est en cours"));
        check (pos > 2.0 && pos < 6.0, U ("la tête de lecture avance en temps réel : ") + juce::String (pos, 2) + " s");
        check (peak > -40.0f, U ("du son sort au master : crête ") + juce::String (peak, 1) + " dB");
        check (groupMeter != nullptr && groupPeak > -40.0f, U ("le vumètre de la tranche de groupe bouge en lecture : crête ") + juce::String (groupPeak, 1) + " dB");
        if (groupMeter != nullptr) groupMeter->measurer.removeClient (groupClient);
        snapshot (*m, dir.getChildFile ("live-lecture.png"));
        for (int v : { 1, 2 }) { m->showView (v); pump (500); snapshot (*m, dir.getChildFile ("live-vue-" + juce::String (v) + ".png")); }
        m->showView (0);
        m->getContext().findAction ("transport.play")->perform();
        pump (300);
        check (! edit->getTransport().isPlaying(), U ("Espace arrête la lecture"));
        if (auto* pc = edit->getCurrentPlaybackContext()) pc->masterLevels.removeClient (client);

        // Enregistrement réel depuis l'entrée de la carte son sur la piste « Voix lead ».
        {
            auto& ctx = m->getContext();
            te::AudioTrack* voice = nullptr;
            for (auto* t : te::getAudioTracks (*edit)) if (t->getName() == "Voix lead") voice = t;
            const int before = voice ? voice->getClips().size() : -1;
            std::printf ("  info   : entrées audio disponibles : %d\n", dm.getNumWaveInDevices());
            if (voice != nullptr && dm.getNumWaveInDevices() > 0)
            {
                ctx.selectTrack (voice);
                edit->getTransport().setPosition (te::TimePosition::fromSeconds (demo::barSeconds()));
                ctx.perform ("transport.record");
                pump (2500);
                const bool wasRecording = edit->getTransport().isRecording();
                ctx.perform ("transport.record");
                pump (1500);
                check (wasRecording, U ("R lance l'enregistrement sur la piste armée"));
                const int after = voice->getClips().size();
                juce::String detail;
                if (after > before)
                    if (auto* w = dynamic_cast<te::WaveAudioClip*> (voice->getClips().getLast()))
                    {
                        detail = juce::String (w->getPosition().getLength().inSeconds(), 2) + " s, " + w->getAudioFile().getFile().getFileName();
                        std::printf ("  info   : prise : %s | source « %s » -> %s (existe %d) | temp %s\n", w->getAudioFile().getFile().getFullPathName().toRawUTF8(),
                                     w->getSourceFileReference().source.get().toRawUTF8(), w->getSourceFileReference().getFile().getFullPathName().toRawUTF8(),
                                     (int) w->getSourceFileReference().getFile().existsAsFile(), juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName().toRawUTF8());
                    }
                check (after == before + 1, U ("la prise enregistrée apparaît comme événement audio (") + detail + ")");
                snapshot (*m, dir.getChildFile ("live-enregistrement.png"));

                // « Enregistrer sous » : la prise (dans le dossier temporaire) est copiée à côté du projet et retrouvée.
                const auto saved = dir.getChildFile ("projet-enregistre.onde");
                dir.getChildFile ("projet-enregistre - Audio").deleteRecursively();
                check (m->saveProjectTo (saved), U ("le projet avec la prise s'enregistre"));
                const auto copied = dir.getChildFile ("projet-enregistre - Audio").findChildFiles (juce::File::findFiles, false, "*.wav");
                check (! copied.isEmpty(), U ("la prise est copiée dans « projet-enregistre - Audio » (") + juce::String (copied.size()) + U (" fichier(s))"));
                m->openProjectFile (saved);
                bool found = false;
                for (auto* t : te::getAudioTracks (*m->getEdit()))
                    if (t->getName() == "Voix lead")
                        for (auto* c : t->getClips())
                            if (auto* w = dynamic_cast<te::WaveAudioClip*> (c)) found |= w->getSourceFileReference().getFile().existsAsFile();
                check (found, U ("après réouverture, la prise est retrouvée"));
                edit = m->getEdit();
            }
            else std::printf ("  info   : pas d'entrée audio : test d'enregistrement ignoré\n");
        }
        std::printf ("\n%s\n", failures == 0 ? "LECTURE OK" : "DES VÉRIFICATIONS ÉCHOUENT");
        std::fflush (stdout);
        edit->resetChangedStatus();
        window = nullptr;
        return failures == 0 ? 0 : 1;
    }

    // Scénario d'édition automatique : vérifie les fonctions sans souris ni carte son.
    int runUiTest (const juce::File& dir)
    {
        dir.createDirectory();
        std::printf ("\nScénario d'interface automatique\n");
        catalog->startScan();
        for (int i = 0; i < 600 && catalog->isScanning(); ++i) juce::Thread::sleep (100);
        const auto synths = catalog->getVST3 (true), effects = catalog->getVST3 (false);
        check (! synths.isEmpty() && ! effects.isEmpty(), U ("l'analyse isolée trouve les VST3 de test : ") + catalog->getStatus());

        mc = std::make_unique<MainComponent> (*engine, *catalog);
        mc->setSize (1440, 860);
        auto& ctx = mc->getContext();
        const PluginEntry* synth = nullptr;
        for (auto& e : synths) if (e.name == "Onde Test Synth") synth = &e;
        auto project = demo::create (*engine, dir.getChildFile ("demo.onde"), "synth-pad", synth);
        mc->setEdit (std::move (project.edit), dir.getChildFile ("demo.onde"), true);
        auto& edit = *mc->getEdit();
        auto tracks = te::getAudioTracks (edit);
        check (tracks.size() == (synth ? 5 : 4), U ("le projet de démo a ses pistes (") + juce::String (tracks.size()) + ")");

        // Pistes.
        ctx.findAction ("track.addInst")->perform();
        ctx.findAction ("track.addAudio")->perform();
        check (te::getAudioTracks (edit).size() == tracks.size() + 2, U ("« Ajouter une piste » fonctionne (instrument + audio)"));
        auto* inst = te::getAudioTracks (edit)[tracks.size()];
        check (ctx.getInstrument (*inst) != nullptr, U ("la piste instrument a un synthé 4OSC"));
        ctx.selectTrack (te::getAudioTracks (edit).getLast());
        ctx.findAction ("track.delete")->perform();
        check (te::getAudioTracks (edit).size() == tracks.size() + 1, U ("« Supprimer la piste » fonctionne"));
        ctx.findAction ("edit.undo")->perform();
        check (te::getAudioTracks (edit).size() == tracks.size() + 2, U ("« Annuler » restaure la piste supprimée"));

        // Clips.
        auto* drums = tracks[0];
        auto* drumClip = drums->getClips().getFirst();
        ctx.selectClip (drumClip);
        edit.getTransport().setPosition (te::TimePosition::fromSeconds (2 * demo::barSeconds()));
        ctx.findAction ("clip.split")->perform();
        check (drums->getClips().size() == 2, U ("« Couper » divise le clip de batterie en deux"));
        ctx.findAction ("edit.undo")->perform();
        check (drums->getClips().size() == 1, U ("« Annuler » recolle le clip"));
        ctx.selectClip (drums->getClips().getFirst());
        ctx.findAction ("clip.duplicate")->perform();
        check (drums->getClips().size() == 2 && std::abs (drums->getClips()[1]->getPosition().getStart().inSeconds() - demo::songSeconds()) < 0.01,
               U ("« Dupliquer » place une copie juste après"));
        ctx.findAction ("clip.delete")->perform();
        check (drums->getClips().size() == 1, U ("« Supprimer le clip » fonctionne"));

        // MIDI + quantification.
        auto mc1 = inst->insertMIDIClip ("Test", { te::TimePosition(), te::TimePosition::fromSeconds (demo::barSeconds()) }, nullptr);
        mc1->getSequence().addNote (60, te::BeatPosition::fromBeats (0.1), te::BeatDuration::fromBeats (0.4), 100, 0, nullptr);
        mc1->getSequence().addNote (64, te::BeatPosition::fromBeats (1.9), te::BeatDuration::fromBeats (0.6), 100, 0, nullptr);
        ctx.selectClip (mc1.get());
        ctx.findAction ("clip.quantise")->perform();
        const auto& notes = mc1->getSequence().getNotes();
        check (notes.size() == 2 && std::abs (notes[0]->getStartBeat().inBeats()) < 1e-6 && std::abs (notes[1]->getStartBeat().inBeats() - 2.0) < 1e-6,
               U ("« Quantifier » aligne les notes sur la grille"));

        mc1 = nullptr;   // ne pas garder de référence vers un clip du projet qui sera fermé

        // Effets intégrés + VST3 isolé ajoutés à une piste.
        ctx.selectTrack (inst);
        auto rev = catalog->getBuiltIns (false)[2];
        PluginCatalog::addToTrack (*inst, rev);
        bool hasReverb = false; for (auto p : inst->pluginList.getPlugins()) hasReverb |= dynamic_cast<te::ReverbPlugin*> (p) != nullptr;
        check (hasReverb, U ("un effet intégré (réverbération) s'ajoute avant le volume"));
        const PluginEntry* crashFx = nullptr;
        for (auto& e : effects) if (e.name == "Onde Test Crash") crashFx = &e;
        if (crashFx) PluginCatalog::addToTrack (*tracks[1], *crashFx);
        check (crashFx != nullptr && ctx.getSandboxPlugins (*tracks[1]).size() == 1, U ("un effet VST3 isolé s'ajoute à la piste Basse"));

        // Enregistrer, rouvrir.
        const auto saved = dir.getChildFile ("projet-test.onde");
        check (te::EditFileOperations (edit).writeToFile (saved, false), U ("le projet s'enregistre"));
        mc->openProjectFile (saved);
        auto& edit2 = *mc->getEdit();
        bool vstBack = false;
        for (auto* t : te::getAudioTracks (edit2)) for (auto* sp : ctx.getSandboxPlugins (*t)) vstBack |= sp->spec.get().startsWith ("vst3:");
        check (te::getAudioTracks (edit2).size() == tracks.size() + 2 && vstBack, U ("le projet se rouvre avec ses pistes et ses VST3 isolés"));

        // Export WAV (inclut les VST3 isolés).
        const auto wav = dir.getChildFile ("export.wav");
        check (mc->exportWavTo (wav), U ("« Exporter en WAV » produit un fichier"));
        auto bars = rmsPerBar (wav, 8, demo::barSeconds());
        check (bars.size() == 8 && bars[0] > 0.02, U ("l'export contient du son"));
        if (synth)
        {
            juce::Array<te::Track*> only;
            for (auto* t : te::getAudioTracks (edit2)) if (t->getName() == U ("Synthé VST3")) only.add (t);
            const auto vf = dir.getChildFile ("synthe-vst3-seul.wav");
            renderEdit (edit2, vf, only);
            auto v = rmsPerBar (vf, 8, demo::barSeconds());
            check (v.size() == 8 && v[1] < 1e-4 && v[5] > 0.01, U ("la piste « Synthé VST3 » joue sa mélodie via le VST3 isolé (silence avant la mesure 5, son après)"));
        }

        // Captures de chaque vue.
        ctx.selectTrack (te::getAudioTracks (edit2)[0]);
        mc->showView (0); pump (400); snapshot (*mc, dir.getChildFile ("1-arrangement.png"));
        mc->showView (1); pump (400); snapshot (*mc, dir.getChildFile ("2-mixeur.png"));
        for (auto* t : te::getAudioTracks (edit2))
            for (auto* c : t->getClips()) if (auto* m = dynamic_cast<te::MidiClip*> (c); m && t->getName() == "Basse") ctx.selectClip (m);
        mc->showView (2); pump (400); snapshot (*mc, dir.getChildFile ("3-piano-roll.png"));
        mc->showView (0); mc->openPalette (U ("rev")); pump (400); snapshot (*mc, dir.getChildFile ("4-palette.png"));
        mc->closePalette();

        // Plantage simulé visible dans l'arrangement (sur la piste Basse, qui porte l'effet VST3 isolé).
        te::AudioTrack* bassTrack = nullptr;
        for (auto* t : te::getAudioTracks (edit2)) if (t->getName() == "Basse") bassTrack = t;
        check (bassTrack != nullptr && ! ctx.getSandboxPlugins (*bassTrack).isEmpty(), U ("la piste Basse a retrouvé son effet VST3 isolé"));
        if (auto sps = bassTrack ? ctx.getSandboxPlugins (*bassTrack) : juce::Array<SandboxPlugin*>(); ! sps.isEmpty())
        {
            auto* sp = sps.getFirst();
            if (sp->getSandbox() == nullptr) sp->initialise ({ te::TimePosition(), 48000.0, 512 });
            sp->simulateCrash();
            std::vector<float> l (512), r (512); float* io[] = { l.data(), r.data() };
            for (int i = 0; i < 20 && ! sp->isFailed(); ++i) { sp->getSandbox()->process (io, 2, 512, 0); juce::Thread::sleep (10); }
            check (sp->isFailed(), U ("un plantage simulé du VST3 isolé est détecté : ") + sp->describeFailure());
            ctx.changed();
            mc->showView (0); pump (600); snapshot (*mc, dir.getChildFile ("5-plantage.png"));
            sp->restartSandbox();
            check (! sp->isFailed(), U ("« Relancer » remet le VST3 en marche"));
        }

        // Fonctions de la fenêtre de projet (outils, transport, automation).
        {
            auto& e2 = *mc->getEdit();
            auto* drums2 = te::getAudioTracks (e2)[0];
            int probe = 0;
            auto drumsLevel = [&] (const char* tag)
            {
                const auto f = dir.getChildFile ("sonde-" + juce::String (++probe) + ".wav");
                const bool ok = renderEdit (e2, f, { drums2 });
                auto lv = rmsPerBar (f, 8, demo::barSeconds());
                std::printf ("  info   : batterie seule (%s) : rendu %d, mesure 2 = %.4f, mesure 7 = %.4f\n", tag, (int) ok,
                             lv.size() == 8 ? lv[1] : -1.0, lv.size() == 8 ? lv[6] : -1.0);
                std::fflush (stdout);
                return lv;
            };
            {
                auto lv = drumsLevel ("après réouverture");
                check (lv.size() == 8 && lv[1] > 0.05, U ("après réouverture, l'audio de la batterie est retrouvé et audible"));
            }
            ctx.selectClip (drums2->getClips().getFirst());
            e2.getTransport().setPosition (te::TimePosition::fromSeconds (2 * demo::barSeconds()));
            ctx.perform ("clip.split");
            const int afterSplit = drums2->getClips().size();
            ctx.selectClip (drums2->getClips().getFirst());
            ctx.perform ("clip.glue");
            check (afterSplit == 2 && drums2->getClips().size() == 1, U ("« Coller » réunit deux morceaux contigus du même fichier"));
            ctx.selectClip (drums2->getClips().getFirst());
            ctx.perform ("clip.mute");
            check (drums2->getClips().getFirst()->isMuted(), U ("« Rendre muet » coupe l'événement"));
            ctx.perform ("clip.mute");
            ctx.perform ("transport.locators");
            check (std::abs (e2.getTransport().getLoopRange().getEnd().inSeconds() - drums2->getClips().getFirst()->getPosition().getEnd().inSeconds()) < 0.01,
                   U ("« P » place les délimiteurs sur l'événement"));
            ctx.perform ("tool.2");
            check (ctx.tool == Tool::split, U ("la touche 3 choisit l'outil Ciseaux"));
            ctx.perform ("tool.0");
            e2.getTransport().setPosition (te::TimePosition::fromSeconds (2.5 * demo::barSeconds()));
            ctx.perform ("transport.forward");
            check (std::abs (e2.getTransport().getPosition().inSeconds() - 3 * demo::barSeconds()) < 0.01, U ("« + » avance à la mesure suivante"));
            ctx.perform ("transport.rewind");
            check (std::abs (e2.getTransport().getPosition().inSeconds() - 2 * demo::barSeconds()) < 0.01, U ("« - » recule à la mesure précédente"));

            // Automation : le volume de la batterie tombe à zéro à la mesure 5, et le rendu doit le respecter.
            drumsLevel ("avant automation");
            auto* vol = drums2->getVolumePlugin()->volParam.get();
            auto& curve = vol->getCurve();
            const double cut = 4 * demo::barSeconds();
            curve.addPoint (te::TimePosition::fromSeconds (cut - 0.02), vol->getCurrentValue(), 0.0f, nullptr);
            curve.addPoint (te::TimePosition::fromSeconds (cut), 0.0f, 0.0f, nullptr);
            const auto af = dir.getChildFile ("batterie-automation.wav");
            trace ("rendu avec automation");
            const bool rendered = renderEdit (e2, af, { drums2 });
            trace (rendered ? "rendu OK" : "rendu ÉCHOUÉ");
            auto v = rmsPerBar (af, 8, demo::barSeconds());
            check (v.size() == 8 && v[1] > 0.01 && v[6] < 0.001, U ("l'automation de volume est appliquée au rendu (mesure 2 : ")
                   + juce::String (v.size() == 8 ? v[1] : 0.0, 3) + U (", mesure 7 : ") + juce::String (v.size() == 8 ? v[6] : 0.0, 4) + ")");
            // Copier / coller.
            curve.removePoint (1, nullptr); curve.removePoint (0, nullptr);
            ctx.selectClip (drums2->getClips().getFirst());
            ctx.perform ("edit.copy");
            e2.getTransport().setPosition (te::TimePosition::fromSeconds (demo::songSeconds()));
            ctx.selectTrack (drums2);
            ctx.perform ("edit.paste");
            check (drums2->getClips().size() == 2 && std::abs (drums2->getClips()[1]->getPosition().getStart().inSeconds() - demo::songSeconds()) < 0.01,
                   U ("Ctrl+C / Ctrl+V colle une copie de l'événement au curseur"));
            ctx.perform ("edit.undo");

            // Sélection multiple : Ctrl+A puis Maj+M rend tout muet, Ctrl+D duplique le bloc.
            {
                const int total = [&] { int n = 0; for (auto* t : te::getAudioTracks (e2)) n += t->getClips().size(); return n; }();
                ctx.perform ("edit.selectAll");
                const int sel = ctx.getSelectedClips().size();
                ctx.perform ("clip.mute");
                bool allMuted = true;
                for (auto* c : ctx.getSelectedClips()) allMuted &= c->isMuted();
                ctx.perform ("edit.undo");
                check (sel == total && total >= 3 && allMuted, U ("Ctrl+A sélectionne les ") + juce::String (total) + U (" événements, Maj+M les rend tous muets"));
                te::AudioTrack* bass = nullptr;
                for (auto* t : te::getAudioTracks (e2)) if (t->getName() == "Basse") bass = t;
                ctx.setClipSelection ({ drums2->getClips().getFirst(), bass->getClips().getFirst() });
                ctx.perform ("clip.duplicate");
                check (drums2->getClips().size() == 2 && bass->getClips().size() == 2 && ctx.getSelectedClips().size() == 2,
                       U ("Ctrl+D duplique un groupe d'événements sur plusieurs pistes (batterie ") + juce::String (drums2->getClips().size())
                       + U (", ") + bass->getName() + " " + juce::String (bass->getClips().size()) + U (", sélection ")
                       + juce::String (ctx.getSelectedClips().size()) + ")");
                ctx.perform ("edit.undo");
            }

            // Repères.
            for (int b : { 2, 6 })
            {
                e2.getTransport().setPosition (te::TimePosition::fromSeconds (b * demo::barSeconds()));
                ctx.perform ("transport.addMarker");
            }
            e2.getTransport().setPosition (te::TimePosition());
            ctx.perform ("transport.nextMarker");
            const double m1 = e2.getTransport().getPosition().inSeconds();
            ctx.perform ("transport.nextMarker");
            const double m2 = e2.getTransport().getPosition().inSeconds();
            ctx.perform ("transport.prevMarker");
            check (e2.getMarkerManager().getMarkers().size() == 2 && std::abs (m1 - 2 * demo::barSeconds()) < 0.01 && std::abs (m2 - 6 * demo::barSeconds()) < 0.01
                       && std::abs (e2.getTransport().getPosition().inSeconds() - m1) < 0.01,
                   U ("Inser. ajoute des repères, Maj+N / Maj+B naviguent entre eux"));

            // Piste FX + envoi : la réverbération ajoute du signal au mixage.
            auto mixLevel = [&] (const char* name)
            {
                const auto f = dir.getChildFile (juce::String (name) + ".wav");
                renderEdit (e2, f, te::getAllTracks (e2));
                auto lv = rmsPerBar (f, 8, demo::barSeconds());
                double sum = 0; for (auto x : lv) sum += x;
                return sum;
            };
            const double dry = mixLevel ("mix-sans-envoi");
            ctx.perform ("track.addFx");
            auto fxTracks = ctx.getFxTracks();
            check (fxTracks.size() == 1 && EditContext::fxBusOf (*fxTracks[0]) == 0, U ("« Ajouter une piste FX » crée un bus d'effet avec retour"));
            if (auto* send = ctx.ensureSend (*drums2, 0)) send->setGainDb (0.0f);
            const double wet = mixLevel ("mix-avec-envoi");
            check (wet > dry * 1.03, U ("l'envoi vers la piste FX s'entend dans le mixage (") + juce::String (dry, 3) + U (" → ") + juce::String (wet, 3) + ")");
            ctx.selectTrack (drums2);

            trace ("affichage de l'automation");
            ctx.toggleAutomation (*drums2);
            trace ("showView");
            mc->showView (0);
            trace ("pump");
            pump (400);
            trace ("capture");
            snapshot (*mc, dir.getChildFile ("6-automation.png"));
            trace ("capture faite");

            // Éditeur audio et traitements des événements audio (sur la batterie).
            {
                auto* w = dynamic_cast<te::WaveAudioClip*> (drums2->getClips().getFirst());
                auto& um = e2.getUndoManager();
                auto level = [&] (const char* name)
                {
                    const auto f = dir.getChildFile (juce::String (name) + ".wav");
                    renderEdit (e2, f, { drums2 });
                    return rmsPerBar (f, 8, demo::barSeconds());
                };
                const auto ref = level ("audio-ref");
                um.beginNewTransaction ("Gain");
                w->setGainDB (-6.0f);
                const auto lowered = level ("audio-gain");
                const double ratio = ref.size() == 8 && lowered.size() == 8 && ref[1] > 0 ? lowered[1] / ref[1] : 0.0;
                check (ratio > 0.45 && ratio < 0.56, U ("le gain de l'événement (-6 dB) est appliqué au rendu (rapport ") + juce::String (ratio, 3) + ")");
                w->setGainDB (0.0f);

                um.beginNewTransaction (U ("Fondu"));
                w->setFadeIn (te::TimeDuration::fromSeconds (2 * demo::barSeconds()));
                w->setFadeOut (te::TimeDuration::fromSeconds (demo::barSeconds()));
                const auto faded = level ("audio-fondus");
                check (faded.size() == 8 && faded[0] < ref[0] * 0.6 && faded[7] < ref[7] * 0.7 && std::abs (faded[3] - ref[3]) < 0.01,
                       U ("les fondus d'entrée et de sortie sont appliqués au rendu (mesure 1 : ") + juce::String (faded.size() == 8 ? faded[0] : 0.0, 3)
                       + U (", mesure 4 : ") + juce::String (faded.size() == 8 ? faded[3] : 0.0, 3) + ")");
                w->setFadeIn ({}); w->setFadeOut ({});

                um.beginNewTransaction ("Normaliser");
                const float peak = AudioEditorView::peakOf (*w);
                const bool normed = AudioEditorView::normalise (*w);
                check (normed && peak > 0.0f && std::abs (w->getGainDB() - (-0.1f - juce::Decibels::gainToDecibels (peak))) < 0.01f,
                       U ("« Normaliser » règle le gain pour une crête à -0,1 dB (crête ") + juce::String (juce::Decibels::gainToDecibels (peak), 1)
                       + U (" dB, gain ") + juce::String (w->getGainDB(), 1) + " dB)");
                w->setGainDB (0.0f);

                um.beginNewTransaction ("Inverser");
                w->setIsReversed (true);
                const auto rev = level ("audio-inverse");
                double revSum = 0; for (auto x : rev) revSum += x;
                check (revSum > 0.3, U ("l'événement inversé est rendu (niveau ") + juce::String (revSum, 3) + ")");
                w->setIsReversed (false);

                um.beginNewTransaction ("Transposition");
                w->setPitchChange (12.0f);
                const auto up = level ("audio-transpose");
                double upSum = 0; for (auto x : up) upSum += x;
                check (upSum > 0.3, U ("l'événement transposé (+12) est rendu (niveau ") + juce::String (upSum, 3) + ")");
                w->setPitchChange (0.0f);

                // Plage dans l'éditeur audio : supprimer la mesure 2, puis rogner à la mesure 3.
                ctx.selectClip (w);
                mc->showView (3); pump (400);
                auto* ae = mc->getAudioEditor();
                const double bar = demo::barSeconds();
                ae->setSelection ({ bar, 2 * bar });
                const bool deleted = ae->deleteSelection();
                bool gapOk = false;
                if (drums2->getClips().size() == 2)
                    gapOk = std::abs (drums2->getClips()[0]->getPosition().getEnd().inSeconds() - bar) < 0.01
                         && std::abs (drums2->getClips()[1]->getPosition().getStart().inSeconds() - 2 * bar) < 0.01;
                check (deleted && gapOk, U ("« Supprimer la sélection » retire la mesure 2 de l'événement (") + juce::String (drums2->getClips().size()) + U (" morceaux)"));
                ctx.perform ("edit.undo");
                ctx.selectClip (drums2->getClips().getFirst());
                ae->setSelection ({ 2 * bar, 3 * bar });
                const bool trimmed = ae->trimToSelection();
                auto* tc = drums2->getClips().getFirst();
                check (trimmed && drums2->getClips().size() == 1 && std::abs (tc->getPosition().getStart().inSeconds() - 2 * bar) < 0.01
                           && std::abs (tc->getPosition().getLength().inSeconds() - bar) < 0.01,
                       U ("« Rogner à la sélection » ne garde que la mesure 3"));
                ctx.perform ("edit.undo");
                check (drums2->getClips().size() == 1 && std::abs (drums2->getClips().getFirst()->getPosition().getLength().inSeconds() - 8 * bar) < 0.01,
                       U ("« Annuler » rend l'événement entier"));
                ctx.selectClip (drums2->getClips().getFirst());
                if (auto* w2 = dynamic_cast<te::WaveAudioClip*> (drums2->getClips().getFirst()))
                {
                    w2->setFadeIn (te::TimeDuration::fromSeconds (bar)); w2->setFadeOut (te::TimeDuration::fromSeconds (bar * 0.5)); w2->setGainDB (-3.0f);
                    pump (600); snapshot (*mc, dir.getChildFile ("10-editeur-audio.png"));
                    w2->setFadeIn ({}); w2->setFadeOut ({}); w2->setGainDB (0.0f);
                }
                mc->showView (0);
            }

            // Export dans tous les formats (lecture du fichier produit, durée, niveau).
            {
                auto checkFile = [&] (const juce::File& f, double minSeconds)
                {
                    juce::AudioFormatManager fm; fm.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
                    if (r == nullptr) return juce::String ("illisible");
                    const double secs = (double) r->lengthInSamples / r->sampleRate;
                    juce::AudioBuffer<float> b ((int) r->numChannels, (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 4)));
                    r->read (&b, 0, b.getNumSamples(), (juce::int64) (r->sampleRate * 2), true, true);
                    const float level = b.getRMSLevel (0, 0, b.getNumSamples());
                    if (secs < minSeconds || level < 0.01f) return juce::String ("vide ou trop court (") + juce::String (secs, 1) + " s)";
                    return juce::String();
                };
                struct Case { ExportSettings::Format f; const char* name; };
                for (auto c : { Case { ExportSettings::Format::wav, "WAV 24 bits" }, Case { ExportSettings::Format::flac, "FLAC 24 bits" },
                                Case { ExportSettings::Format::mp3, "MP3 320 kbit/s" }, Case { ExportSettings::Format::ogg, "OGG Vorbis" } })
                {
                    ExportSettings s; s.format = c.f; s.sampleRate = 48000.0;
                    juce::StringArray written; juce::String err;
                    const bool ok = exporter::exportMix (e2, dir.getChildFile ("export-format"), s, written, err);
                    const auto problem = ok ? checkFile (juce::File (written[0]), 15.0) : err;
                    check (ok && problem.isEmpty(), U ("export ") + U (c.name) + (problem.isEmpty() ? U (" : fichier lisible, ")
                           + juce::String (juce::File (written[0]).getSize() / 1024) + " Ko" : U (" : ") + problem));
                }
                ExportSettings lufs; lufs.format = ExportSettings::Format::wav; lufs.normalise = ExportSettings::Normalise::peak; lufs.normaliseLevel = -1.0f;
                juce::StringArray w1; juce::String e1;
                exporter::exportMix (e2, dir.getChildFile ("export-normalise"), lufs, w1, e1);
                float peak = 0.0f;
                {
                    juce::AudioFormatManager fm; fm.registerBasicFormats();
                    if (auto r = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (juce::File (w1[0]))))
                    {
                        juce::Range<float> lv[2];
                        r->readMaxLevels (0, r->lengthInSamples, lv, 2);
                        peak = juce::jmax (std::abs (lv[0].getStart()), lv[0].getEnd(), std::abs (lv[1].getStart()), lv[1].getEnd());
                    }
                }
                check (std::abs (juce::Decibels::gainToDecibels (peak) + 1.0f) < 0.2f, U ("normalisation à l'export : crête à ") + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dB");
                ExportSettings stems; stems.separateTracks = true; stems.format = ExportSettings::Format::flac;
                juce::StringArray ws; juce::String es;
                const bool sok = exporter::exportMix (e2, dir.getChildFile ("stems").getChildFile ("projet"), stems, ws, es);
                check (sok && ws.size() >= 3, U ("export piste par piste : ") + juce::String (ws.size()) + U (" fichiers FLAC ") + es);
            }

            // Dossiers et groupes.
            {
                auto& um = e2.getUndoManager();
                auto mixRms = [&] (const char* name)
                {
                    const auto f = dir.getChildFile (juce::String (name) + ".wav");
                    renderEdit (e2, f, te::getAllTracks (e2));
                    double sum = 0; for (auto x : rmsPerBar (f, 8, demo::barSeconds())) sum += x;
                    return sum;
                };
                const int before = te::getAudioTracks (e2).size();
                ctx.selectTrack (drums2);
                um.beginNewTransaction ("Dossier");
                auto* folder = ctx.addFolderTrack (false, true);
                check (folder != nullptr && drums2->getParentFolderTrack() == folder && EditContext::depthOf (*drums2) == 1
                           && te::getAudioTracks (e2).size() == before,
                       U ("« Nouveau dossier contenant cette piste » range la batterie dans un dossier"));
                const double full = mixRms ("mix-dossier");
                folder->setMute (true);
                const double mutedFolder = mixRms ("mix-dossier-muet");
                folder->setMute (false);
                check (mutedFolder < full * 0.9, U ("rendre le dossier muet coupe ses pistes (") + juce::String (full, 3) + U (" → ") + juce::String (mutedFolder, 3) + ")");
                ctx.toggleCollapsed (*folder);
                mc->showView (1); pump (300);
                {
                    // Le repli est enregistré dans le projet.
                    const auto f = dir.getChildFile ("repli-dossier.onde");
                    const bool saved = mc->saveProjectTo (f);
                    auto reopened = MainComponent::loadProject (*engine, f);
                    bool collapsedAfter = false;
                    if (reopened != nullptr)
                        for (auto* ft : te::getTracksOfType<te::FolderTrack> (*reopened, true))
                            if (ft->itemID == folder->itemID) collapsedAfter = EditContext::isCollapsed (*ft);
                    check (saved && collapsedAfter, U ("le repli d'un dossier est enregistré avec le projet"));
                    EditContext::destroyEdit (reopened);
                }
                ctx.toggleCollapsed (*folder);

                // La batterie (piste la plus forte) passe du dossier à un groupe.
                um.beginNewTransaction (U ("Sortir"));
                ctx.moveTrackOutOfFolder (*drums2);
                check (drums2->getParentFolderTrack() == nullptr, U ("« Sortir du dossier » remet la piste au premier niveau"));
                ctx.selectTrack (drums2);
                um.beginNewTransaction ("Groupe");
                auto* grp = ctx.addFolderTrack (true, true);
                check (grp != nullptr && grp->isSubmixFolder() && grp->getVolumePlugin() != nullptr && drums2->getParentFolderTrack() == grp,
                       U ("« Nouveau groupe » crée une piste de groupe (sous-mixage) avec son volume"));
                const double withGroup = mixRms ("mix-groupe");
                check (EditContext::groupMeter (*grp, false) != nullptr, U ("la tranche de groupe a son vumètre (mesuré en lecture par --live-test)"));
                grp->getVolumePlugin()->setVolumeDb (-100.0f);
                const double groupDown = mixRms ("mix-groupe-coupe");
                grp->getVolumePlugin()->setVolumeDb (0.0f);
                check (groupDown < withGroup * 0.97, U ("le volume du groupe agit sur les pistes qu'il contient (") + juce::String (withGroup, 3) + U (" → ") + juce::String (groupDown, 3) + ")");
                ctx.selectTrack (drums2);
                mc->showView (0); pump (400); snapshot (*mc, dir.getChildFile ("8-dossiers.png"));
                mc->showView (1); pump (400); snapshot (*mc, dir.getChildFile ("9-groupe-mixconsole.png"));
                mc->showView (0);
                int undos = 0;
                while (! te::getTracksOfType<te::FolderTrack> (e2, true).isEmpty() && undos < 10) { ctx.perform ("edit.undo"); ++undos; }
                check (te::getTracksOfType<te::FolderTrack> (e2, true).isEmpty() && te::getAudioTracks (e2).size() == before
                           && drums2->getParentFolderTrack() == nullptr,
                       U ("« Annuler » retire dossiers et groupe sans perdre de piste (") + juce::String (undos) + U (" annulations, ")
                       + juce::String (te::getAudioTracks (e2).size()) + "/" + juce::String (before) + U (" pistes)"));
            }

            // Piste de tempo : passage à 184 BPM à la mesure 5 (deux fois plus vite).
            {
                auto& ts = e2.tempoSequence;
                e2.getUndoManager().beginNewTransaction ("Tempo");
                ts.insertTempo (te::BeatPosition::fromBeats (16.0), 184.0, 1.0f);
                const double bar5 = ctx.toSeconds (16.0), bar6 = ctx.toSeconds (20.0);
                check (ctx.hasTempoChanges() && std::abs (bar5 - 4 * demo::barSeconds()) < 0.001 && std::abs ((bar6 - bar5) - 4 * 60.0 / 184.0) < 0.001,
                       U ("piste de tempo : la mesure 5 passe à 184 BPM (mesure de ") + juce::String (bar6 - bar5, 3) + " s)");
                check (ctx.formatBars (bar6).trim() == "6. 1. 1.   0" || ctx.formatBars (bar6).trim().startsWith ("6. 1. 1."),
                       U ("le compteur suit le tempo variable : ") + ctx.formatBars (bar6).trim());
                e2.getTransport().setPosition (te::TimePosition::fromSeconds (bar5 + 0.1));
                ctx.perform ("transport.forward");
                check (std::abs (e2.getTransport().getPosition().inSeconds() - bar6) < 0.001, U ("« + » saute à la mesure 6 au nouveau tempo"));
                check (std::abs (ctx.bpmAt (bar5 + 0.5) - 184.0) < 0.01 && std::abs (ctx.bpmAt (1.0) - demo::bpm) < 0.01, U ("le tempo au curseur est lu sur la piste de tempo"));
                ts.getTempo (1)->setCurve (0.0f);
                ts.insertTempo (te::BeatPosition::fromBeats (28.0), 92.0, 1.0f);
                const double mid = ctx.bpmAt (ctx.toSeconds (22.0));
                check (mid > 100.0 && mid < 180.0, U ("une rampe fait varier le tempo progressivement (") + juce::String (mid, 1) + " BPM)");
                ctx.tempoTrackShown = true; ctx.changed();
                mc->showView (0); pump (400); snapshot (*mc, dir.getChildFile ("7-tempo.png"));
                ctx.perform ("edit.undo");
                check (! ctx.hasTempoChanges(), U ("« Annuler » retire les changements de tempo"));
            }

            // Mode musical : au double du tempo, l'événement musical s'étire (moitié de durée), l'événement normal
            // garde sa durée mais suit sa mesure de départ, le conteneur MIDI suit aussi ses mesures.
            {
                juce::File loop;
                for (auto* t : te::getAudioTracks (e2))
                    for (auto* c : t->getClips())
                        if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
                            if (AudioEditorView::sourceFileOf (*w).existsAsFile()) loop = AudioEditorView::sourceFileOf (*w);
                const double beat = 60.0 / ctx.bpm(), bars4 = 16 * beat;
                auto tA = e2.insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getTopLevelTracks (e2).getLast()), nullptr);
                auto tB = e2.insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getTopLevelTracks (e2).getLast()), nullptr);
                tA->setName ("Musical"); tB->setName ("Normal");
                auto cA = tA->insertWaveClip ("boucle musicale", loop, { { te::TimePosition(), te::TimePosition::fromSeconds (bars4) }, te::TimeDuration() }, false);
                auto cB = tB->insertWaveClip ("boucle normale", loop, { { te::TimePosition::fromSeconds (8 * beat), te::TimePosition::fromSeconds (8 * beat + bars4) }, te::TimeDuration() }, false);
                auto cM = tB->insertMIDIClip ("conteneur", { te::TimePosition::fromSeconds (16 * beat + bars4), te::TimePosition::fromSeconds (20 * beat + bars4) }, nullptr);
                ctx.selectClip (cA.get());
                ctx.perform ("audio.musical");
                check (cA->getAutoTempo() && ! cB->getAutoTempo(), U ("Ctrl+Maj+M : mode musical sur l'événement sélectionné"));
                const double midiBeat = ctx.toBeats (cM->getPosition().getStart().inSeconds());
                e2.getUndoManager().beginNewTransaction ("Tempo");
                ctx.changeTempo ([&] { e2.tempoSequence.getTempos()[0]->setBpm (ctx.bpm() * 2.0); });
                const double lenA = cA->getPosition().getLength().inSeconds(), lenB = cB->getPosition().getLength().inSeconds();
                check (std::abs (lenA - bars4 / 2) < 0.01, U ("tempo doublé : l'événement musical s'étire avec lui (") + juce::String (lenA, 2) + " s au lieu de " + juce::String (bars4, 2) + " s)");
                check (std::abs (lenB - bars4) < 0.01 && std::abs (cB->getPosition().getStart().inSeconds() - 4 * beat) < 0.01,
                       U ("l'événement normal garde sa durée et reste au début de sa mesure (") + juce::String (cB->getPosition().getStart().inSeconds(), 2) + " s)");
                check (std::abs (ctx.toBeats (cM->getPosition().getStart().inSeconds()) - midiBeat) < 0.01,
                       U ("le conteneur MIDI reste sur sa mesure (temps ") + juce::String (ctx.toBeats (cM->getPosition().getStart().inSeconds()), 2) + ")");
                // Rendu de la piste musicale seule : du son jusqu'au bout de l'événement étiré.
                const auto wavM = dir.getChildFile ("mode-musical.wav");
                {
                    EditContext::prepareAudioProxies (e2, 60000);
                    te::Renderer::Parameters rp (e2);
                    rp.destFile = wavM; rp.audioFormat = engine->getAudioFileFormatManager().getWavFormat();
                    rp.bitDepth = 24; rp.sampleRateForAudio = 48000.0; rp.blockSizeForAudio = 512;
                    rp.time = { te::TimePosition(), te::TimePosition::fromSeconds (lenA) };
                    rp.usePlugins = true; rp.tracksToDo = te::toBitSet (juce::Array<te::Track*> { tA.get() });
                    wavM.deleteFile();
                    te::Renderer::renderToFile ("Rendu", rp);
                }
                double rate = 0;
                const auto rendered = readAll (wavM, rate);
                const int lastSec = (int) rate;
                const float tail = rendered.getNumSamples() > lastSec ? rendered.getRMSLevel (0, rendered.getNumSamples() - lastSec, lastSec) : 0.0f;
                check (tail > 0.01f, U ("rendu : l'événement musical joue jusqu'au bout de sa nouvelle durée (RMS de la dernière seconde ") + juce::String (tail, 3) + ")");
                // Preuve de l'étirement : toutes les frappes des 4 mesures sont là, deux fois plus serrées.
                auto onsets = [] (const juce::AudioBuffer<float>& b, double sr)
                {
                    const int hop = (int) (sr * 0.01);
                    int count = 0; float prev = 0.0f; double lastOnset = -1.0;
                    for (int i = 0; i + hop <= b.getNumSamples(); i += hop)
                    {
                        const float e = b.getRMSLevel (0, i, hop);
                        if (e > 0.05f && e > prev * 2.5f && i / sr - lastOnset > 0.08) { ++count; lastOnset = i / sr; }
                        prev = e;
                    }
                    return count;
                };
                const auto wavN = dir.getChildFile ("mode-normal.wav");
                {
                    te::Renderer::Parameters rp (e2);
                    rp.destFile = wavN; rp.audioFormat = engine->getAudioFileFormatManager().getWavFormat();
                    rp.bitDepth = 24; rp.sampleRateForAudio = 48000.0; rp.blockSizeForAudio = 512;
                    rp.time = { cB->getPosition().getStart(), cB->getPosition().getEnd() };
                    rp.usePlugins = true; rp.tracksToDo = te::toBitSet (juce::Array<te::Track*> { tB.get() });
                    wavN.deleteFile();
                    te::Renderer::renderToFile ("Rendu", rp);
                }
                const auto normal = readAll (wavN, rate);
                const int hitsMusical = onsets (rendered, rate), hitsNormal = onsets (normal, rate);
                check (hitsNormal > 4 && std::abs (hitsMusical - hitsNormal) <= juce::jmax (2, hitsNormal / 8),
                       U ("étirement réel : ") + juce::String (hitsMusical) + U (" frappes en ") + juce::String (lenA, 2) + U (" s contre ")
                       + juce::String (hitsNormal) + U (" en ") + juce::String (lenB, 2) + U (" s pour l'original"));
                ctx.perform ("edit.undo");
                check (std::abs (cA->getPosition().getLength().inSeconds() - bars4) < 0.01 && std::abs (cB->getPosition().getStart().inSeconds() - 8 * beat) < 0.01,
                       U ("« Annuler » remet le tempo et les événements à leur place"));
                tA->setMute (true); tB->setMute (true);
            }
        }

        std::printf ("\n%s\n", failures == 0 ? "INTERFACE OK — toutes les vérifications passent." : "DES VÉRIFICATIONS ÉCHOUENT");
        std::fflush (stdout);
        trace ("fin du scénario, fermeture de la fenêtre");
        mc = nullptr;
        trace ("fenêtre fermée");
        return failures == 0 ? 0 : 1;
    }

    class MainWindow  : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (std::unique_ptr<MainComponent> c)
            : DocumentWindow ("Onde", theme::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (c.release(), true);
            setResizable (true, true);
            setResizeLimits (1100, 640, 8000, 5000);
            centreWithSize (1600, 940);
            setVisible (true);
            if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                if (d->userArea.getWidth() <= 1920) setFullScreen (true);   // agrandie sur les écrans courants
            getContentComponent()->grabKeyboardFocus();
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        // Les raccourcis marchent même quand aucun élément n'a le clavier (fenêtre tout juste activée).
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (auto* c = getContentComponent()) return c->keyPressed (k);
            return false;
        }
        void activeWindowStatusChanged() override
        {
            DocumentWindow::activeWindowStatusChanged();
            if (isActiveWindow())
                if (auto* c = getContentComponent())
                    if (! c->hasKeyboardFocus (true)) c->grabKeyboardFocus();
        }
    };

    StudioLookAndFeel lookAndFeel;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<PluginCatalog> catalog;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<MainComponent> mc;
    std::unique_ptr<juce::PropertiesFile> settings;
};

START_JUCE_APPLICATION (OndeApplication)
