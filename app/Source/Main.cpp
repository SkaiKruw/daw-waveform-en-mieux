// Onde — prototype v0.3. Modes :
//   Onde [projet.onde]            interface normale
//   Onde --render-test <dossier>  rendu hors ligne avec plantage d'un plugin isolé (vérification)
//   Onde --ui-test <dossier>      scénario d'édition automatique + captures de chaque vue (vérification)
//   Onde --stems-test <dossier>   séparateur de sources : précision (SDR) et import (vérification)
#include <JuceHeader.h>
#include "MainComponent.h"
#include "Preferences.h"
#include "OndeSynth.h"
#include "OndeFx2.h"
#include "Chains.h"
#include "Drummer.h"
#include "DrummerPanel.h"
#include "NewTrackPanel.h"
#include "DemoContent.h"
#include "StudioLookAndFeel.h"
#include "Exporter.h"
#include "Profiler.h"
#include "OndeEffects.h"

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
        MainComponent::testMode = args[0].startsWith ("--");
        // En cas de plantage : pile d'appels écrite dans %APPDATA%\Onde\plantage.txt (diagnostic).
        juce::SystemStats::setApplicationCrashHandler ([] (void*)
        {
            const auto f = appDataDir().getChildFile ("plantage.txt");
            f.getParentDirectory().createDirectory();
            f.replaceWithText (juce::Time::getCurrentTime().toString (true, true) + "\n" + juce::SystemStats::getStackBacktrace());
        });
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        tooltips = std::make_unique<juce::TooltipWindow> (nullptr, 600);
        engine = std::make_unique<te::Engine> ("Onde", std::make_unique<OndeUIBehaviour>(), nullptr);
        engine->getPluginManager().createBuiltInType<SandboxPlugin>();
        ondefx::registerTypes (*engine);          // effets intégrés d'Onde (EQ, Comp, Limiteur)
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
        if (args[0] == "--render-logo")
        {
            // Rend le logo (SVG embarqué) en PNG pour l'icône de l'application et la page du téléphone.
            const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1].isEmpty() ? "logo" : args[1].unquoted());
            dir.createDirectory();
            auto logo = theme::makeLogo();
            int written = 0;
            for (int size : { 1024, 512, 256, 192, 180, 128, 64, 48, 32, 16 })
            {
                juce::Image img (juce::Image::ARGB, size, size, true);
                {
                    juce::Graphics g (img);
                    if (logo) logo->drawWithin (g, { 0.0f, 0.0f, (float) size, (float) size }, juce::RectanglePlacement::centred, 1.0f);
                }
                const auto f = dir.getChildFile ("logo-" + juce::String (size) + ".png");
                f.deleteFile();
                juce::FileOutputStream out (f);
                if (out.openedOk() && juce::PNGImageFormat().writeImageToStream (img, out)) ++written;
            }
            std::printf ("%d images du logo écrites dans %s\n", written, dir.getFullPathName().toRawUTF8());
            setApplicationReturnValue (written == 10 && logo != nullptr ? 0 : 1);
            quit();
            return;
        }
        if (args[0] == "--perf-test")
        {
            juce::MessageManager::callAsync ([this, dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1].isEmpty() ? "perf-test" : args[1].unquoted())]
            {
                setApplicationReturnValue (runPerfTest (dir));
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

        prefs::applyUiScale();                         // taille de l'interface choisie dans les paramètres
        settings = std::make_unique<juce::PropertiesFile> (appDataDir().getChildFile ("reglages.xml"), juce::PropertiesFile::Options());
        const bool crashedLastTime = settings->getBoolValue ("sessionOpen", false);
        settings->setValue ("sessionOpen", true);
        settings->saveIfNeeded();

        auto content = std::make_unique<MainComponent> (*engine, *catalog);
        auto* mc = content.get();
        window = std::make_unique<MainWindow> (std::move (content));
        mc->onTitleChanged = [this] (const juce::String& t) { if (window) window->setName (t + U (" — Onde")); };

        mc->onOpenDemo = [this, mc] { openDemo (*mc); };
        const juce::File arg (args[0].unquoted());
        const bool openedFile = args.size() > 0 && arg.existsAsFile();
        const bool hub = ! openedFile && HubPanel::showAtStartup();
        if (openedFile)  mc->openProjectFile (arg);
        else if (hub)    mc->newProject();          // l'écran d'accueil propose modèles et projets récents
        else             openDemo (*mc);

        // Première ouverture : analyse des VST3 en arrière-plan (dans des processus isolés).
        if (catalog->getVST3 (true).isEmpty() && catalog->getVST3 (false).isEmpty())
            catalog->startScan();

        // Récupération après un arrêt brutal d'Onde (avant l'écran d'accueil).
        if (crashedLastTime && MainComponent::recoveryFile().existsAsFile())
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, U ("Récupérer la session ?"),
                U ("Onde ne s'est pas fermé normalement la dernière fois. Une copie de secours de votre projet sans titre existe. Voulez-vous la rouvrir ?"),
                U ("Récupérer"), U ("Ignorer"), nullptr,
                juce::ModalCallbackFunction::create ([mc, hub] (int r)
                {
                    if (r == 1) mc->openProjectFile (MainComponent::recoveryFile());
                    else if (hub) mc->showHub();
                }));
        else if (hub)
            juce::MessageManager::callAsync ([mc] { mc->showHub(); });
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
        mc.markCleanWhenSettled();
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
                if (sp != nullptr)
                {
                    // Certains plugins (amplis à modèles, échantillonneurs) mettent longtemps à se charger :
                    // on attend que le processus isolé soit prêt (60 s au plus) avant de rendre.
                    sp->initialise ({ te::TimePosition(), 48000.0, 512 });
                    const auto loadStart = juce::Time::getMillisecondCounter();
                    const juce::uint32 maxWait = (juce::uint32) juce::jmax (60000, juce::SystemStats::getEnvironmentVariable ("ONDE_PLUGIN_WAIT_MS", "0").getIntValue());
                    while (juce::Time::getMillisecondCounter() - loadStart < maxWait
                           && (sp->getSandbox() == nullptr || sp->getSandbox()->getState() == onde::PluginSandbox::State::starting))
                        pump (100);
                    std::printf ("  info   : %s prêt en %u ms (état : %s)\n", e.name.toRawUTF8(), juce::Time::getMillisecondCounter() - loadStart,
                                 sp->getSandbox() ? onde::toString (sp->getSandbox()->getState()) : "aucun");
                    if (inst) pump (8000);    // échantillonneurs : sons chargés en arrière-plan
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
                // Paramètres du plugin exposés dans Onde : réglage envoyé au plugin, retour d'un préréglage chargé,
                // automation audible au rendu.
                if (running)
                {
                    auto* sb = sp->getSandbox();
                    int count = 0, first = -1, level = -1;
                    for (int i = 0; i < sp->getNumPluginParameters(); ++i)
                        if (auto* prm = sp->getPluginParameter (i))
                        {
                            ++count;
                            const auto nm = prm->getParameterName().toLowerCase();
                            const bool avoid = nm.contains ("bypass") || nm.contains ("preset") || nm.contains ("program") || nm.contains ("midi");
                            if (first < 0 && ! avoid) first = i;
                            if (level < 0 && ! inst && (nm == "mix" || nm.contains ("dry/wet") || nm.contains ("wet") || nm.contains ("output") || nm.contains ("gain")) && ! avoid) level = i;
                        }
                    bool sent = false, followed = false;
                    juce::String before, after;
                    if (first >= 0)
                    {
                        auto* prm = sp->getPluginParameter (first);
                        const float original = prm->getCurrentValue(), target = original < 0.5f ? 0.8f : 0.2f;
                        before = prm->getCurrentValueAsString();
                        prm->setParameter (target, juce::sendNotification);
                        for (int k = 0; k < 20 && std::abs (sb->getPluginParameter (first) - target) > 0.02f; ++k) pump (100);
                        pump (300);
                        after = prm->getCurrentValueAsString();
                        // Le plugin peut arrondir (paramètre à crans) : on accepte sa valeur la plus proche.
                        sent = std::abs (sb->getPluginParameter (first) - target) < 0.2f && std::abs (sb->getPluginParameter (first) - original) > 1.0e-4f;
                        // Retour : on recharge le préréglage d'origine dans le plugin, Onde doit suivre.
                        juce::MemoryBlock mb; mb.fromBase64Encoding (sp->savedState.get());
                        sb->loadState (std::vector<uint8_t> ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize()));
                        for (int k = 0; k < 30 && std::abs (prm->getCurrentValue() - original) > 1.0e-3f; ++k) pump (100);
                        followed = std::abs (prm->getCurrentValue() - original) <= 1.0e-3f;
                    }
                    if (first < 0)   // seulement des paramètres techniques (MIDI, bypass) : rien à régler
                        check (count > 0, e.name + U (" : ") + juce::String (count) + U (" paramètre(s) exposé(s), uniquement techniques (MIDI, contournement)"));
                    else
                    check (count > 0 && sent && followed,
                           e.name + U (" : ") + juce::String (count) + U (" paramètres exposés ; « ")
                           + (first >= 0 ? sp->getPluginParameter (first)->getParameterName() : juce::String ("?")) + U (" » réglé depuis Onde (")
                           + before + U (" → ") + after + U (") puis rendu au plugin par son préréglage ")
                           + (followed ? U ("(Onde suit)") : U ("(NON SUIVI)")));

                    if (level >= 0)
                    {
                        // Automation : paramètre à 0 pendant 2 s puis au maximum ; le niveau doit changer.
                        auto* prm = sp->getPluginParameter (level);
                        auto& curve = prm->getCurve();
                        curve.addPoint (te::TimePosition(), 0.0f, 1.0f, nullptr);
                        curve.addPoint (te::TimePosition::fromSeconds (2.0), 1.0f, 1.0f, nullptr);
                        const auto autoWav = dir.getChildFile ("plugin-" + juce::String (n) + "-automation.wav");
                        rp.destFile = autoWav;
                        autoWav.deleteFile();
                        te::Renderer::renderToFile ("Test automation", rp);
                        // Écart avec le rendu sans automation, moitié par moitié (une réverbération à 0 % ou 100 %
                        // garde à peu près le même niveau, mais le son change).
                        double ra = 0, rb = 0;
                        const auto plain = readAll (wav, ra), automated = readAll (autoWav, rb);
                        auto changeDb = [&] (int half)
                        {
                            const int len2 = (int) (2.0 * 48000), s0 = half * len2 + 4800;
                            double diff = 0, ref = 0;
                            for (int i = s0; i < std::min ({ s0 + len2 - 9600, plain.getNumSamples(), automated.getNumSamples() }); ++i)
                            {
                                const double a = plain.getSample (0, i), b = automated.getSample (0, i);
                                diff += (a - b) * (a - b); ref += a * a;
                            }
                            return 10.0 * std::log10 ((diff + 1.0e-12) / (ref + 1.0e-12));
                        };
                        const double c0 = changeDb (0), c1 = changeDb (1);
                        check (c0 > -30.0 && c1 > -30.0, e.name + U (" : automation de « ") + prm->getParameterName() + U (" » (0 puis 100 %) audible au rendu : écart ")
                                                         + juce::String (c0, 1) + U (" dB puis ") + juce::String (c1, 1) + U (" dB par rapport au réglage fixe"));
                        for (int k = curve.getNumPoints(); --k >= 0;) curve.removePoint (k, nullptr);
                    }
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
    // Mesure de fluidité : gros projet joué sur la carte son, une phase par vue ; temps CPU du fil de
    // l'interface, du processus et charge audio, plus les fonctions les plus coûteuses de l'interface.
    int runPerfTest (const juce::File& dir)
    {
        dir.createDirectory();
        std::printf ("\nMesure de fluidité\n\n");
        auto content = std::make_unique<MainComponent> (*engine, *catalog);
        auto* m = content.get();
        window = std::make_unique<MainWindow> (std::move (content));
        openDemo (*m);
        pump (1500);
        auto& ctx = m->getContext();
        auto& edit = *m->getEdit();
        // Projet de 64 mesures : chaque événement est recopié toutes les 8 mesures, puis chaque piste x4.
        const double bars8 = 8 * demo::barSeconds();
        for (auto* t : te::getAudioTracks (edit))
        {
            const auto clips = t->getClips();
            for (auto* c : clips)
                for (int k = 1; k < 8; ++k)
                {
                    const auto pos = c->getPosition();
                    const te::TimeRange r { pos.getStart() + te::TimeDuration::fromSeconds (k * bars8), pos.getEnd() + te::TimeDuration::fromSeconds (k * bars8) };
                    if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
                        t->insertWaveClip (w->getName(), AudioEditorView::sourceFileOf (*w), { r, pos.getOffset() }, false);
                    else if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                        if (auto nc = t->insertMIDIClip (mc->getName(), r, nullptr)) nc->getSequence().copyFrom (mc->getSequence(), nullptr);
                }
        }
        const auto originals = te::getAudioTracks (edit);
        for (auto* t : originals)
            if (! t->getClips().isEmpty())
                for (int k = 0; k < 3; ++k) { ctx.selectTrack (t); ctx.perform ("track.duplicate"); }
        edit.getUndoManager().clearUndoHistory();
        ctx.perform ("view.zoomFit");
        EditContext::prepareAudioProxies (edit, 60000);
        pump (1000);
        int clipCount = 0;
        for (auto* t : te::getAudioTracks (edit)) clipCount += t->getClips().size();
        std::printf ("  projet : %d pistes, %d événements, %.0f s\n\n", te::getAudioTracks (edit).size(), clipCount, edit.getLength().inSeconds());

        te::Clip* aMidi = nullptr; te::Clip* aWave = nullptr;
        for (auto* t : te::getAudioTracks (edit))
            for (auto* c : t->getClips())
            {
                if (aMidi == nullptr && dynamic_cast<te::MidiClip*> (c)) aMidi = c;
                if (aWave == nullptr && dynamic_cast<te::WaveAudioClip*> (c)) aWave = c;
            }

        auto& dm = engine->getDeviceManager();
        edit.getTransport().setPosition (te::TimePosition());
        m->getContext().findAction ("transport.play")->perform();
        pump (800);
        struct Phase { const char* name; std::function<void()> setup; };
        const Phase phases[] = {
            { "Fenêtre de projet seule",        [&] { m->showView (0); ctx.perform ("view.arrangement"); } },
            { "Projet + MixConsole",            [&] { m->showView (1); } },
            { "Projet + éditeur de notes",      [&] { ctx.selectClip (aMidi); m->showView (2); } },
            { "Projet + éditeur audio",         [&] { ctx.selectClip (aWave); m->showView (3); } },
        };
        const bool profile = juce::SystemStats::getEnvironmentVariable ("ONDE_PERF_PROFILE", {}).isNotEmpty();
        for (auto& ph : phases)
        {
            ph.setup();
            pump (600);
            SamplingProfiler prof;
            if (profile) prof.start();
            const double t0 = juce::Time::getMillisecondCounterHiRes() / 1000.0, ui0 = threadCpuSeconds(), p0 = processCpuSeconds();
            double audio = 0; int n = 0;
            for (int i = 0; i < 60; ++i) { pump (100); audio += dm.getCpuUsage(); ++n; }
            const double wall = juce::Time::getMillisecondCounterHiRes() / 1000.0 - t0;
            const auto report = profile ? prof.stop (60) : juce::String();
            std::printf ("  %-30s interface %5.1f %% d'un cœur · processus %5.1f %% · audio %4.1f %%\n", ph.name,
                         100.0 * (threadCpuSeconds() - ui0) / wall, 100.0 * (processCpuSeconds() - p0) / wall, 100.0 * audio / n);
            if (profile) std::printf ("%s\n", report.toRawUTF8());
            std::fflush (stdout);
            snapshot (*m, dir.getChildFile (juce::String ("perf-") + juce::String (&ph - phases) + ".png"));
        }
        m->getContext().findAction ("transport.play")->perform();
        pump (300);

        // Coût de chaque effet et de chaque moteur d'Onde Synth : 10 s de stéréo à 48 kHz, blocs de 512,
        // en % d'un cœur (temps de calcul / durée du son).
        std::printf ("\n  Coût des effets et instruments Onde (%% d'un cœur, 48 kHz) :\n");
        auto bench = [&] (const char* label, const char* type, std::function<void (te::Plugin&)> setup, bool synth)
        {
            auto p = edit.getPluginCache().createNewPlugin (type, {});
            if (p == nullptr) return;
            if (setup) setup (*p);
            p->initialise ({ te::TimePosition(), 48000.0, 512 });
            juce::AudioBuffer<float> buf (2, 512);
            te::MidiMessageArray midi;
            juce::Random rnd (1);
            const int blocks = 48000 * 10 / 512;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < blocks; ++b)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, synth ? 0.0f : 0.3f * std::sin (0.05f * (b * 512 + i)) + 0.05f * (rnd.nextFloat() - 0.5f));
                midi.clear();
                if (synth && b % 94 == 0)             // un accord de 8 notes par seconde, tenu
                    for (int note : { 36, 48, 55, 60, 64, 67, 71, 74 }) midi.addMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0.0, te::MPESourceID {});
                if (synth && b % 94 == 80)
                    for (int note : { 36, 48, 55, 60, 64, 67, 71, 74 }) midi.addMidiMessage (juce::MidiMessage::noteOff (1, note), 0.0, te::MPESourceID {});
                te::PluginRenderContext rc (&buf, 0, 512, &midi, 0.0, {}, true, false, false, false);
                p->applyToBuffer (rc);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("    %-34s %5.2f %%\n", label, ms / 100.0);   // 10 s = 10000 ms -> pourcentage
            std::fflush (stdout);
        };
        bench ("Onde EQ (7 bandes, 2 dynamiques)", ondefx::EqPlugin::xmlTypeName, [] (te::Plugin& p)
        {
            auto& eq = dynamic_cast<ondefx::EqPlugin&> (p);
            for (int b = 1; b < 6; ++b) eq.setStored (ondefx::EqPlugin::bandId (b, "gain"), 3.0f);
            eq.setStored (ondefx::EqPlugin::bandId (2, "dyn"), 1.0f); eq.setStored (ondefx::EqPlugin::bandId (4, "dyn"), 1.0f);
            eq.setStored (ondefx::EqPlugin::bandId (0, "on"), 1.0f); eq.setStored (ondefx::EqPlugin::bandId (0, "slope"), 2.0f);
        }, false);
        bench ("Onde Comp (RMS, anticipation)", ondefx::CompPlugin::xmlTypeName, [] (te::Plugin& p)
        { auto& c = dynamic_cast<ondefx::CompPlugin&> (p); c.setStored ("detect", 1); c.setStored ("lookahead", 1); c.setStored ("autorel", 1); }, false);
        bench ("Onde Limiteur (crête vraie, sonie)", ondefx::LimiterPlugin::xmlTypeName, nullptr, false);
        bench ("Onde Saturation (x2)", ondefx::SaturationPlugin::xmlTypeName, nullptr, false);
        bench (U ("Onde Écho").toRawUTF8(), ondefx::EchoPlugin::xmlTypeName, nullptr, false);
        bench ("Onde Espace", ondefx::SpacePlugin::xmlTypeName, nullptr, false);
        for (const char* preset : { "Piano de concert", "Piano électrique", "Nappe aérienne", "Basse synthé", "Batterie acoustique" })
        {
            const juce::String label = U ("Onde Synth : ") + U (preset) + U (" (8 notes)");
            bench (label.toRawUTF8(), ondefx::SynthPlugin::xmlTypeName, [preset] (te::Plugin& p)
            { dynamic_cast<ondefx::SynthPlugin&> (p).applyPreset (*ondefx::SynthPlugin::findPreset (U (preset))); }, true);
        }
        return 0;
    }

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

        // Clavier d'Onde (à l'écran / ordinateur) : il joue l'instrument de la piste sélectionnée, et ses notes
        // s'enregistrent comme celles d'un clavier MIDI.
        {
            auto& ctx = m->getContext();
            edit = m->getEdit();
            te::AudioTrack* inst = nullptr;
            for (auto* t : te::getAudioTracks (*edit)) if (inst == nullptr && ctx.getInstrument (*t) != nullptr) inst = t;
            check (inst != nullptr, U ("le projet de démonstration a une piste instrument : ") + (inst ? inst->getName() : juce::String ("aucune")));
            if (inst != nullptr)
            {
                ctx.selectTrack (inst);
                ctx.perform ("midi.keyboard");
                pump (600);
                KeyboardPanel* kb = nullptr;
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (auto* p = dynamic_cast<KeyboardPanel*> (dw->getContentComponent())) kb = p;
                check (kb != nullptr, U ("la fenêtre « Clavier » s'ouvre (Ctrl+Maj+K)"));
                for (auto in : edit->getAllInputDevices())
                    std::printf ("  info   : entrée « %s » type %d activée %d cible piste %d\n", in->getInputDevice().getName().toRawUTF8(),
                                 (int) in->getInputDevice().getDeviceType(), (int) in->getInputDevice().isEnabled(), (int) te::isOnTargetTrack (*in, *inst, 0));
                if (kb != nullptr)
                {
                    snapshot (*kb, dir.getChildFile ("live-clavier.png"));
                    // Jeu en direct, transport arrêté : le vumètre de la piste doit bouger.
                    te::LevelMeasurer::Client instClient;
                    auto* meter = inst->getLevelMeterPlugin();
                    if (meter != nullptr) meter->measurer.addClient (instClient);
                    edit->getTransport().ensureContextAllocated();
                    pump (300);
                    instClient.getAndClearAudioLevel (0); instClient.getAndClearAudioLevel (1);
                    kb->noteOn (60, 110);
                    float livePeak = -100.0f;
                    for (int i = 0; i < 8; ++i) { pump (100); livePeak = juce::jmax (livePeak, instClient.getAndClearAudioLevel (0).dB, instClient.getAndClearAudioLevel (1).dB); }
                    kb->noteOff (60);
                    pump (300);
                    if (meter != nullptr) meter->measurer.removeClient (instClient);
                    check (livePeak > -60.0f, U ("une note jouée au clavier d'Onde fait sonner « ") + inst->getName() + U (" » : crête ") + juce::String (livePeak, 1) + " dB");

                    // Enregistrement : Do, Mi, Sol joués pendant que le transport enregistre.
                    // Notes de la piste avant la prise (Tracktion peut fusionner la prise dans le conteneur existant).
                    auto countNotes = [inst] (int pitch)
                    {
                        int k = 0;
                        for (auto* c : inst->getClips())
                            if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                                for (auto* n : mc->getSequence().getNotes()) if (n->getNoteNumber() == pitch) ++k;
                        return k;
                    };
                    const int before60 = countNotes (60), before64 = countNotes (64), before67 = countNotes (67);
                    for (auto* t : te::getAudioTracks (*edit)) if (ctx.isTrackArmed (*t)) ctx.setTrackArmed (*t, false);
                    ctx.selectTrack (inst);
                    edit->getTransport().setPosition (te::TimePosition::fromSeconds (demo::barSeconds() * 2));
                    ctx.perform ("transport.record");
                    pump (500);
                    for (int note : { 60, 64, 67 }) { kb->noteOn (note, 100); pump (250); kb->noteOff (note); pump (150); }
                    pump (300);
                    const bool wasRecording = edit->getTransport().isRecording();
                    ctx.perform ("transport.record");
                    pump (1500);
                    const int n60 = countNotes (60) - before60, n64 = countNotes (64) - before64, n67 = countNotes (67) - before67;
                    check (wasRecording && n60 >= 1 && n64 >= 1 && n67 >= 1,
                           U ("les notes jouées au clavier d'Onde sont enregistrées : Do ") + juce::String (n60) + U (", Mi ") + juce::String (n64)
                           + U (", Sol ") + juce::String (n67) + U (" nouvelle(s) note(s)"));
                    if (auto* dw = kb->findParentComponentOfClass<juce::DialogWindow>()) delete dw;
                }
            }
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
        check (ctx.getInstrument (*inst) != nullptr, U ("la piste instrument a un instrument (Onde Synth)"));
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
        PluginEntry rev;
        for (auto& e : catalog->getBuiltIns (false)) if (e.builtInType == ondefx::SpacePlugin::xmlTypeName) rev = e;
        PluginCatalog::addToTrack (*inst, rev);
        bool hasReverb = false; for (auto p : inst->pluginList.getPlugins()) hasReverb |= dynamic_cast<ondefx::SpacePlugin*> (p) != nullptr;
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
                    if (auto* ae = mc->getAudioEditor())
                    {
                        // Aucun bouton de la barre ne doit sortir de l'éditeur (largeur de la fenêtre de test).
                        juce::StringArray outside;
                        for (auto* child : ae->getChildren())
                            if (child->isVisible() && child->getRight() > ae->getWidth()) outside.add (child->getTitle().isNotEmpty() ? child->getTitle() : child->getName());
                        if (auto* b = dynamic_cast<juce::Button*> (ae->getChildren().getLast())) juce::ignoreUnused (b);
                        check (outside.isEmpty(), U ("éditeur audio : tous les boutons tiennent dans la largeur (") + juce::String (ae->getWidth()) + " px) " + outside.joinIntoString (", "));
                    }
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

        // Télécommande (téléphone) : vraies requêtes HTTP sur 127.0.0.1.
        {
            auto& e2 = *mc->getEdit();
            auto& remote = mc->getRemote();
            const int port = 8799;
            check (remote.start (port, true), U ("télécommande : serveur démarré (port ") + juce::String (port) + ")");
            auto request = [&] (const juce::String& target, bool post, int& status) -> juce::String
            {
                // Requête HTTP/1.1 brute, comme fetch() dans le navigateur du téléphone.
                status = 0;
                juce::StreamingSocket s;
                if (! s.connect ("127.0.0.1", port, 3000)) return {};
                const auto req = (post ? "POST " : "GET ") + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                const auto raw = req.toStdString();
                s.write (raw.data(), (int) raw.size());
                juce::MemoryBlock resp;
                char buf[4096];
                for (;;)
                {
                    if (s.waitUntilReady (true, 3000) <= 0) break;
                    const int n = s.read (buf, (int) sizeof (buf), false);
                    if (n <= 0) break;
                    resp.append (buf, (size_t) n);
                }
                const auto text = juce::String::fromUTF8 ((const char*) resp.getData(), (int) resp.getSize());
                status = text.fromFirstOccurrenceOf (" ", false, false).getIntValue();
                return text.fromFirstOccurrenceOf ("\r\n\r\n", false, false);
            };
            int st = 0;
            const auto page = request ("/", false, st);
            check (st == 200 && page.contains (U ("Télécommande")) && page.contains ("api/state"), U ("le téléphone reçoit la page de télécommande (") + juce::String (page.length()) + U (" caractères)"));
            const auto png = request ("/logo-192.png", false, st);
            check (st == 200, U ("la page reçoit le logo (icône d'écran d'accueil, réponse ") + juce::String (st) + ")");
            request ("/api/state", false, st);
            check (st == 403, U ("sans code d'appairage, l'API refuse (") + juce::String (st) + ")");
            const auto k = "k=" + remote.getCode();
            pump (200);
            auto state = juce::JSON::parse (request ("/api/state?" + k, false, st));
            const int nTracks = te::getAudioTracks (e2).size();
            check (st == 200 && state["tracks"].size() == nTracks && state.hasProperty ("bars") && (bool) state["playing"] == false,
                   U ("état lu par le téléphone : ") + state["bars"].toString() + U (", ") + juce::String (state["bpm"].toString().getDoubleValue(), 1) + U (" BPM, ")
                   + juce::String (state["tracks"].size()) + U (" pistes"));
            auto actions = juce::JSON::parse (request ("/api/actions?" + k, false, st));
            bool hasPlay = false, hasQuit = false;
            for (int i = 0; i < actions.size(); ++i) { hasPlay |= actions[i]["id"] == juce::var ("transport.play"); hasQuit |= actions[i]["id"] == juce::var ("file.quit"); }
            check (hasPlay && ! hasQuit, U ("liste des commandes pour les boutons perso (") + juce::String (actions.size()) + U (", sans « Quitter »)"));
            check (remote.phoneConnected(), U ("la barre d'outils voit le téléphone connecté (voyant vert)"));
            const bool loopBefore = e2.getTransport().looping.get();
            request ("/api/action?id=transport.loop&" + k, true, st);
            pump (300);
            check (st == 200 && e2.getTransport().looping.get() != loopBefore, U ("un bouton du téléphone déclenche une commande d'Onde (Cycle)"));
            request ("/api/action?id=transport.loop&" + k, true, st); pump (200);
            auto* first = te::getAudioTracks (e2)[0];
            const bool muteBefore = first->isMuted (false);
            request ("/api/track?i=0&op=mute&" + k, true, st);
            pump (300);
            check (first->isMuted (false) != muteBefore, U ("le téléphone rend une piste muette (« ") + first->getName() + U (" »)"));
            request ("/api/track?i=0&op=mute&" + k, true, st); pump (300);
            state = juce::JSON::parse (request ("/api/state?" + k, false, st));
            check (state["tracks"][0]["mute"] == juce::var (muteBefore), U ("l'état renvoyé suit le changement"));
            request ("/api/action?id=file.quit&" + k, true, st);
            check (st == 404, U ("les commandes dangereuses (Quitter…) sont refusées (") + juce::String (st) + ")");
            int last = 0;
            for (int i = 0; i < 11; ++i) { request ("/api/state?k=000000", false, st); last = st; }
            request ("/api/state?" + k, false, st);
            check (last == 429 && st == 429, U ("10 codes faux bloquent l'API 30 s (") + juce::String (last) + ")");
            // QR code de l'adresse.
            const auto qr = QrCode::encode ("http://192.168.1.42:8765/?k=123456");
            bool finders = qr.size() > 0;
            for (auto [cx, cy] : { std::pair<int, int> { 0, 0 }, { qr.size() - 7, 0 }, { 0, qr.size() - 7 } })
                for (int i = 0; i < 7 && finders; ++i)
                    finders = qr.dark (cx + i, cy) && qr.dark (cx, cy + i) && qr.dark (cx + 6, cy + i) && qr.dark (cx + i, cy + 6) && qr.dark (cx + 3, cy + 3) && ! qr.dark (cx + 1, cy + 1);
            check (finders && qr.version() >= 2 && qr.version() <= 4 && QrCode::encode (juce::String::repeatedString ("x", 200)).size() == 0,
                   U ("QR code de l'adresse : version ") + juce::String (qr.version()) + U (", ") + juce::String (qr.size()) + U (" × ") + juce::String (qr.size()) + U (" modules"));
            {
                RemotePanel panel (remote);
                pump (200);
                snapshot (panel, dir.getChildFile ("10-telecommande.png"));
                AboutPanel about;
                snapshot (about, dir.getChildFile ("11-a-propos.png"));
                check (theme::makeLogo() != nullptr, U ("le logo (SVG embarqué) se charge"));
            }
            remote.stop();
        }

        // Écran d'accueil : modèles de projet et projets récents.
        {
            auto names = [&] { juce::StringArray n; for (auto* t : te::getAudioTracks (*mc->getEdit())) n.add (t->getName()); return n; };
            mc->newProjectFromTemplate (1);
            const auto rec = names();
            check (rec.joinIntoString (",").startsWith ("Voix,Guitare,Basse,Batterie") && rec.size() == 5 && ! mc->getEdit()->hasChangedSinceSaved(),
                   U ("modèle « Enregistrement » : ") + rec.joinIntoString (", "));

            // Modèles personnels (sans les événements, ou avec) et paramètres de l'application.
            {
                auto& e0 = *mc->getEdit();
                const auto names0 = names();
                te::getAudioTracks (e0).getFirst()->insertMIDIClip ("motif", { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, nullptr);
                MainComponent::templatesFolder().deleteRecursively();
                const bool savedEmpty = mc->saveAsTemplate (U ("Mon modèle"), false);
                const bool savedFull = mc->saveAsTemplate (U ("Mon modèle avec événements"), true);
                const auto files = MainComponent::userTemplates();
                auto byName = [&] (const char* n) { for (auto& f : files) if (f.getFileNameWithoutExtension() == U (n)) return f; return juce::File(); };
                auto countClips = [&] { int n = 0; for (auto* t : te::getAudioTracks (*mc->getEdit())) n += t->getClips().size(); return n; };
                const bool openedEmpty = files.size() == 2 && mc->newProjectFromUserTemplate (byName ("Mon modèle"));
                const auto names1 = names();
                const int clipsEmpty = countClips();
                const bool clean = ! mc->getEdit()->hasChangedSinceSaved();
                const bool openedFull = files.size() == 2 && mc->newProjectFromUserTemplate (byName ("Mon modèle avec événements"));
                const int clipsFull = countClips();
                check (savedEmpty && savedFull && openedEmpty && openedFull && names1 == names0 && clipsEmpty == 0 && clipsFull == 1 && clean,
                       U ("modèles personnels : ") + juce::String (files.size()) + U (" enregistrés, rouverts avec les pistes « ") + names1.joinIntoString (", ")
                       + U (" », ") + juce::String (clipsEmpty) + U (" événement sans, ") + juce::String (clipsFull) + U (" avec"));

                prefs::file().setValue ("bpm", 97.0);
                mc->newProject();
                const double bpm = mc->getEdit()->tempoSequence.getTempos()[0]->getBpm();
                prefs::file().removeValue ("bpm");
                SettingsPanel settings (*mc);
                pump (150);
                snapshot (settings, dir.getChildFile ("40-parametres-general.png"));
                settings.showTab (4); pump (150);
                snapshot (settings, dir.getChildFile ("41-parametres-modeles.png"));
                settings.showTab (3); pump (150);
                snapshot (settings, dir.getChildFile ("42-parametres-plugins.png"));
                check (std::abs (bpm - 97.0) < 0.01 && SettingsPanel::tabNames().size() == 5 && settings.currentTab() == 3,
                       U ("paramètres : tempo des nouveaux projets appliqué (") + juce::String (bpm, 0) + U (" BPM), 5 onglets"));
            }
            mc->newProjectFromTemplate (2);
            const auto prod = names();
            int instruments = 0;
            juce::StringArray sounds;
            for (auto* t : te::getAudioTracks (*mc->getEdit()))
                if (auto* inst = mc->getContext().getInstrument (*t))
                {
                    ++instruments;
                    if (auto* s = dynamic_cast<ondefx::SynthPlugin*> (inst)) sounds.add (s->getPresetName());
                }
            check (prod.joinIntoString (",").startsWith ("Batterie,Basse,Accords,Nappe,Lead,Voix") && instruments == 5
                   && sounds.joinIntoString (",") == U ("Batterie acoustique,Basse synthé,Piano électrique,Nappe chaude,Lead scie")
                   && ! mc->getEdit()->hasChangedSinceSaved(),
                   U ("modèle « Production » : ") + prod.joinIntoString (", ") + U (" (sons : ") + sounds.joinIntoString (", ") + ")");
            const auto saved = dir.getChildFile (U ("projet récent.onde"));
            check (mc->saveProjectTo (saved) && MainComponent::recentProjects()[0] == saved.getFullPathName(),
                   U ("un projet enregistré passe en tête des projets récents"));
            HubPanel hubPanel (*mc);
            check (hubPanel.numRecent() >= 1, U ("l'écran d'accueil liste les projets récents (") + juce::String (hubPanel.numRecent()) + ")");
            pump (200);
            snapshot (hubPanel, dir.getChildFile ("12-accueil.png"));
        }

        // Historique des modifications et « Rendre définitif » (projet « Production » ouvert ci-dessus).
        {
            auto& ed = *mc->getEdit();
            auto& c3 = mc->getContext();
            auto& um = ed.getUndoManager();
            um.clearUndoHistory();
            const int tracks0 = te::getAudioTracks (ed).size();
            um.beginNewTransaction (U ("Ajouter une piste"));  c3.addAudioTrack();
            um.beginNewTransaction (U ("Renommer"));           te::getAudioTracks (ed).getLast()->setName ("Essai");
            um.beginNewTransaction (U ("Ajouter une piste"));  c3.addAudioTrack();
            um.beginNewTransaction();
            HistoryPanel history (c3);
            const int n = history.numEntries();
            history.goTo (0);
            const int afterUndo = te::getAudioTracks (ed).size();
            history.goTo (n - 1);
            const int afterRedo = te::getAudioTracks (ed).size();
            check (n >= 4 && afterUndo == tracks0 && afterRedo == tracks0 + 2,
                   U ("historique : ") + juce::String (n) + U (" étapes ; retour au début (") + juce::String (afterUndo) + U (" pistes) puis à la fin (") + juce::String (afterRedo) + ")");
            pump (200);
            snapshot (history, dir.getChildFile ("13-historique.png"));

            // Rendre définitif : un événement réglé (gain, inversion, fondu) rendu dans un fichier.
            const auto source = dir.getChildFile ("export-format.wav");
            auto* target = te::getAudioTracks (ed).getLast();
            auto clipPtr = target->insertWaveClip ("boucle", source, { { te::TimePosition::fromSeconds (1.0), te::TimePosition::fromSeconds (5.0) }, te::TimeDuration::fromSeconds (2.0) }, false);
            auto* w = clipPtr.get();
            check (w != nullptr, U ("événement à rendre définitif inséré"));
            if (w != nullptr)
            {
                w->setGainDB (-6.0f);
                w->setIsReversed (true);
                w->setFadeIn (te::TimeDuration::fromSeconds (1.0));
                EditContext::prepareAudioProxies (ed, 60000);
                const auto before = dir.getChildFile ("definitif-avant.wav"), after = dir.getChildFile ("definitif-apres.wav");
                renderEdit (ed, before, { target });
                juce::String err;
                c3.selectClip (w);
                auto* nc = AudioEditorView::bounce (c3, *w, err);
                renderEdit (ed, after, { target });
                double r1 = 0, r2 = 0;
                const auto a = readAll (before, r1), b = readAll (after, r2);
                const double match = sdr (a, b);
                check (nc != nullptr && std::abs (nc->getGainDB()) < 0.01f && ! nc->getIsReversed() && nc->getFadeIn().inSeconds() < 0.001
                           && std::abs (nc->getPosition().getStart().inSeconds() - 1.0) < 1e-6 && match > 30.0,
                       U ("« Rendre définitif » : même son (") + juce::String (match, 1) + U (" dB), réglages appliqués au fichier ") + err);
                um.undo();
                bool restored = false;
                for (auto* c : target->getClips())
                    if (auto* ow = dynamic_cast<te::WaveAudioClip*> (c)) restored = restored || (ow->getIsReversed() && std::abs (ow->getGainDB() + 6.0f) < 0.01f);
                check (restored, U ("« Annuler » rend l'événement d'origine et ses réglages"));
            }
        }

        // Effets Onde : mesures sur des sinus (100 Hz, 1 kHz, 8 kHz).
        {
            auto& ed = *mc->getEdit();
            auto& c4 = mc->getContext();
            const double fs = 48000.0;
            const int len = (int) (fs * 4.0);
            auto writeTones = [&] (const juce::File& f, std::initializer_list<std::pair<double, float>> tones, double phase = 0.0, bool fades = false)
            {
                juce::AudioBuffer<float> b (2, len); b.clear();
                for (auto [hzv, amp] : tones)
                    for (int i = 0; i < len; ++i)
                    {
                        float v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * hzv * i / fs + phase);
                        if (fades)   // fondus de 20 ms : pas de transitoire aux extrémités
                        {
                            const double edge = std::min (i, len - 1 - i) / (0.02 * fs);
                            if (edge < 1.0) v *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * edge));
                        }
                        b.addSample (0, i, v); b.addSample (1, i, v);
                    }
                f.deleteFile();
                std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
                juce::WavAudioFormat wav;
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), fs, 2, 32, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer (b, 0, len); }
            };
            // Amplitude d'une sinusoïde (Goertzel) sur la seconde 2 à 3 du rendu.
            auto toneDb = [] (const juce::AudioBuffer<float>& b, double rate, double hzv, int ch = 0)
            {
                const int s0 = (int) (rate * 2.0), n = (int) rate;
                if (b.getNumSamples() < s0 + n || ch >= b.getNumChannels()) return -200.0;
                const double w = juce::MathConstants<double>::twoPi * hzv / rate, coeff = 2.0 * std::cos (w);
                double q1 = 0, q2 = 0;
                for (int i = 0; i < n; ++i) { const double q0 = coeff * q1 - q2 + b.getSample (ch, s0 + i); q2 = q1; q1 = q0; }
                const double mag = std::sqrt (q1 * q1 + q2 * q2 - q1 * q2 * coeff) * 2.0 / n;
                return 20.0 * std::log10 (std::max (1e-10, mag));
            };
            auto peakDb = [] (const juce::AudioBuffer<float>& b) { return juce::Decibels::gainToDecibels (b.getMagnitude (0, 0, b.getNumSamples()), -200.0f); };
            const auto tones = dir.getChildFile ("sinus-3.wav"), loud = dir.getChildFile ("sinus-fort.wav");
            writeTones (tones, { { 100.0, 0.2f }, { 1000.0, 0.2f }, { 8000.0, 0.2f } });
            writeTones (loud, { { 1000.0, 0.5f } });   // -6 dBFS

            auto* t = c4.addAudioTrack();
            t->setName ("Test effets");
            t->insertWaveClip ("sinus", tones, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
            auto renderTrack = [&] (const char* name) { double r = 0; const auto f = dir.getChildFile (juce::String (name) + ".wav"); renderEdit (ed, f, { t }); return readAll (f, r); };

            auto eqPtr = ed.getPluginCache().createNewPlugin (ondefx::EqPlugin::xmlTypeName, {});
            t->pluginList.insertPlugin (eqPtr, 0, nullptr);
            auto* eq = dynamic_cast<ondefx::EqPlugin*> (eqPtr.get());
            check (eq != nullptr, U ("« Onde EQ » inséré sur une piste"));
            if (eq != nullptr)
            {
                const auto flat = renderTrack ("eq-neutre");
                eq->param (ondefx::EqPlugin::bandId (3, "gain"))->setParameter (12.0f, juce::sendNotification);
                const auto boosted = renderTrack ("eq-cloche");
                const double d100 = toneDb (boosted, 48000, 100) - toneDb (flat, 48000, 100);
                const double d1k = toneDb (boosted, 48000, 1000) - toneDb (flat, 48000, 1000);
                const double d8k = toneDb (boosted, 48000, 8000) - toneDb (flat, 48000, 8000);
                check (std::abs (d1k - 12.0) < 0.5 && std::abs (d100) < 1.0 && std::abs (d8k) < 1.0,
                       U ("Onde EQ : cloche +12 dB à 1 kHz → ") + juce::String (d1k, 2) + U (" dB (100 Hz ") + juce::String (d100, 2) + U (", 8 kHz ") + juce::String (d8k, 2) + ")");
                eq->param (ondefx::EqPlugin::bandId (3, "gain"))->setParameter (0.0f, juce::sendNotification);
                eq->param (ondefx::EqPlugin::bandId (0, "on"))->setParameter (1.0f, juce::sendNotification);
                eq->param (ondefx::EqPlugin::bandId (0, "freq"))->setParameter (1000.0f, juce::sendNotification);
                const auto cut = renderTrack ("eq-coupe-bas");
                const double c100 = toneDb (cut, 48000, 100) - toneDb (flat, 48000, 100), c8k = toneDb (cut, 48000, 8000) - toneDb (flat, 48000, 8000);
                check (c100 < -60.0 && std::abs (c8k) < 0.5, U ("Onde EQ : coupe-bas 24 dB/oct à 1 kHz → 100 Hz ") + juce::String (c100, 1) + U (" dB, 8 kHz ") + juce::String (c8k, 2) + " dB");
                // Pentes : 12 dB/oct → -40 dB une décade plus bas ; 48 dB/oct → bien plus.
                eq->param (ondefx::EqPlugin::bandId (0, "slope"))->setParameter (0.0f, juce::sendNotification);
                const double s12 = toneDb (renderTrack ("eq-pente-12"), 48000, 100) - toneDb (flat, 48000, 100);
                eq->param (ondefx::EqPlugin::bandId (0, "slope"))->setParameter (2.0f, juce::sendNotification);
                const double s48 = toneDb (renderTrack ("eq-pente-48"), 48000, 100) - toneDb (flat, 48000, 100);
                check (std::abs (s12 + 40.0) < 1.5 && s48 < -100.0,
                       U ("Onde EQ : pentes du coupe-bas à 100 Hz → 12 dB/oct ") + juce::String (s12, 1) + U (" dB, 48 dB/oct ") + juce::String (s48, 1) + " dB");
                eq->param (ondefx::EqPlugin::bandId (0, "slope"))->setParameter (1.0f, juce::sendNotification);
                eq->param (ondefx::EqPlugin::bandId (0, "on"))->setParameter (0.0f, juce::sendNotification);

                // Canaux par bande : la cloche en « Gauche » ne touche pas la droite ; en « Side » elle ne touche
                // pas une source mono (L = R) ; en « Mid » elle agit pleinement.
                eq->param (ondefx::EqPlugin::bandId (3, "gain"))->setParameter (12.0f, juce::sendNotification);
                eq->param (ondefx::EqPlugin::bandId (3, "mode"))->setParameter (1.0f, juce::sendNotification);
                const auto left = renderTrack ("eq-gauche");
                const double gl = toneDb (left, 48000, 1000, 0) - toneDb (flat, 48000, 1000, 0), gr = toneDb (left, 48000, 1000, 1) - toneDb (flat, 48000, 1000, 1);
                eq->param (ondefx::EqPlugin::bandId (3, "mode"))->setParameter (4.0f, juce::sendNotification);
                const double gs = toneDb (renderTrack ("eq-side"), 48000, 1000) - toneDb (flat, 48000, 1000);
                eq->param (ondefx::EqPlugin::bandId (3, "mode"))->setParameter (3.0f, juce::sendNotification);
                const double gm = toneDb (renderTrack ("eq-mid"), 48000, 1000) - toneDb (flat, 48000, 1000);
                check (std::abs (gl - 12.0) < 0.5 && std::abs (gr) < 0.1 && std::abs (gs) < 0.1 && std::abs (gm - 12.0) < 0.5,
                       U ("Onde EQ : canaux par bande → gauche ") + juce::String (gl, 1) + U (" / droite ") + juce::String (gr, 2)
                       + U (", side ") + juce::String (gs, 2) + U (", mid ") + juce::String (gm, 1) + " dB");
                eq->param (ondefx::EqPlugin::bandId (3, "mode"))->setParameter (0.0f, juce::sendNotification);

                // Bande dynamique : 1 kHz à -14 dBFS, seuil -30, plage -6 → -6 dB à 1 kHz, le reste intact ;
                // seuil 0 (jamais atteint) → aucun effet.
                eq->param (ondefx::EqPlugin::bandId (3, "gain"))->setParameter (0.0f, juce::sendNotification);
                eq->param (ondefx::EqPlugin::bandId (3, "dyn"))->setParameter (1.0f, juce::sendNotification);
                eq->param (ondefx::EqPlugin::bandId (3, "thr"))->setParameter (-30.0f, juce::sendNotification);
                eq->param (ondefx::EqPlugin::bandId (3, "range"))->setParameter (-6.0f, juce::sendNotification);
                const auto dyn = renderTrack ("eq-dynamique");
                const double y1k = toneDb (dyn, 48000, 1000) - toneDb (flat, 48000, 1000), y8k = toneDb (dyn, 48000, 8000) - toneDb (flat, 48000, 8000);
                eq->param (ondefx::EqPlugin::bandId (3, "thr"))->setParameter (0.0f, juce::sendNotification);
                const double idle = toneDb (renderTrack ("eq-dynamique-repos"), 48000, 1000) - toneDb (flat, 48000, 1000);
                check (std::abs (y1k + 6.0) < 0.5 && std::abs (y8k) < 0.5 && std::abs (idle) < 0.1,
                       U ("Onde EQ : bande dynamique → 1 kHz ") + juce::String (y1k, 2) + U (" dB (attendu -6), 8 kHz ") + juce::String (y8k, 2)
                       + U (" dB, sous le seuil ") + juce::String (idle, 2) + " dB");
                eq->param (ondefx::EqPlugin::bandId (3, "dyn"))->setParameter (0.0f, juce::sendNotification);
                if (auto e = ondefx::createEditor (*eq)) { pump (300); snapshot (*e, dir.getChildFile ("14-onde-eq.png")); }
                // Les réglages sont enregistrés avec le projet.
                eq->param (ondefx::EqPlugin::bandId (2, "gain"))->setParameter (-5.5f, juce::sendNotification);
                const auto saved = dir.getChildFile ("effets.onde");
                bool kept = false;
                if (mc->saveProjectTo (saved))
                    if (auto reopened = MainComponent::loadProject (*engine, saved))
                    {
                        for (auto* rt : te::getAudioTracks (*reopened))
                            for (auto* rp : rt->pluginList)
                                if (auto* re = dynamic_cast<ondefx::EqPlugin*> (rp)) kept = std::abs (re->value (ondefx::EqPlugin::bandId (2, "gain")) + 5.5f) < 0.01f;
                        EditContext::destroyEdit (reopened);
                    }
                check (kept, U ("les réglages d'Onde EQ sont enregistrés avec le projet"));
                eqPtr->deleteFromParent();
            }

            // Compresseur : -6 dBFS, seuil -24, taux 4, genou 0 → -24 + 18/4 = -19,5 dBFS en régime établi.
            for (auto* c : t->getClips()) c->removeFromParent();
            t->insertWaveClip ("fort", loud, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
            auto compPtr = ed.getPluginCache().createNewPlugin (ondefx::CompPlugin::xmlTypeName, {});
            t->pluginList.insertPlugin (compPtr, 0, nullptr);
            if (auto* comp = dynamic_cast<ondefx::CompPlugin*> (compPtr.get()))
            {
                comp->param ("threshold")->setParameter (-24.0f, juce::sendNotification);
                comp->param ("ratio")->setParameter (4.0f, juce::sendNotification);
                comp->param ("knee")->setParameter (0.0f, juce::sendNotification);
                const auto out = renderTrack ("comp");
                const double level = toneDb (out, 48000, 1000);
                check (std::abs (level + 19.5) < 1.0, U ("Onde Comp : -6 dBFS, seuil -24, taux 4:1 → ") + juce::String (level, 2) + U (" dBFS (attendu -19,5)"));
                // Détection RMS : un sinus à -6 dBFS crête vaut -9 dB RMS → -6 - (15 × 3/4) = -17,25 dBFS.
                comp->param ("detect")->setParameter (1.0f, juce::sendNotification);
                const double rmsLevel = toneDb (renderTrack ("comp-rms"), 48000, 1000);
                comp->param ("detect")->setParameter (0.0f, juce::sendNotification);
                check (std::abs (rmsLevel + 17.25) < 0.75, U ("Onde Comp : détection RMS → ") + juce::String (rmsLevel, 2) + U (" dBFS (attendu -17,25)"));
                // Anticipation : latence de 5 ms déclarée (compensée par le moteur), même niveau.
                comp->param ("lookahead")->setParameter (1.0f, juce::sendNotification);
                const double laLevel = toneDb (renderTrack ("comp-anticipation"), 48000, 1000);
                check (std::abs (comp->getLatencySeconds() * 1000.0 - 5.0) < 0.01 && std::abs (laLevel + 19.5) < 1.0,
                       U ("Onde Comp : anticipation → latence ") + juce::String (comp->getLatencySeconds() * 1000.0, 1) + U (" ms, niveau ") + juce::String (laLevel, 2) + " dBFS");
                comp->param ("lookahead")->setParameter (0.0f, juce::sendNotification);
                // Styles et relâchement auto : le niveau établi reste celui du réglage (pas de dérive).
                comp->param ("style")->setParameter (1.0f, juce::sendNotification);
                comp->param ("autorel")->setParameter (1.0f, juce::sendNotification);
                const double optoLevel = toneDb (renderTrack ("comp-opto"), 48000, 1000);
                comp->param ("style")->setParameter (0.0f, juce::sendNotification);
                comp->param ("autorel")->setParameter (0.0f, juce::sendNotification);
                check (std::abs (optoLevel + 19.5) < 1.0, U ("Onde Comp : style opto + relâchement auto → ") + juce::String (optoLevel, 2) + " dBFS");
                if (auto e = ondefx::createEditor (*comp)) { pump (200); snapshot (*e, dir.getChildFile ("15-onde-comp.png")); }
                compPtr->deleteFromParent();
            }
            // Limiteur : +12 dB d'entrée sur -6 dBFS (soit +6), plafond -1 → aucune crête au-dessus de -1 dBFS.
            auto limPtr = ed.getPluginCache().createNewPlugin (ondefx::LimiterPlugin::xmlTypeName, {});
            t->pluginList.insertPlugin (limPtr, 0, nullptr);
            if (auto* lim = dynamic_cast<ondefx::LimiterPlugin*> (limPtr.get()))
            {
                lim->param ("input")->setParameter (12.0f, juce::sendNotification);
                lim->param ("ceiling")->setParameter (-1.0f, juce::sendNotification);
                const auto out = renderTrack ("limiteur");
                const float pk = peakDb (out);
                check (pk <= -0.99f && pk > -1.5f && std::abs (lim->getLatencySeconds() * 1000.0 - 1.5) < 0.1,
                       U ("Onde Limiteur : entrée +6 dBFS, plafond -1 → crête ") + juce::String (pk, 2) + U (" dBFS, latence ") + juce::String (lim->getLatencySeconds() * 1000.0, 2) + " ms");

                // Sonie (EBU R128) : 1 kHz à -20 dBFS sur les deux canaux = -20,0 LUFS.
                const auto quiet = dir.getChildFile ("sinus-20.wav");
                writeTones (quiet, { { 1000.0, 0.1f } }, 0.0, true);
                for (auto* c : t->getClips()) c->removeFromParent();
                t->insertWaveClip ("calme", quiet, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
                lim->param ("input")->setParameter (0.0f, juce::sendNotification);
                // Le rendu hors ligne travaille sur une copie de l'effet : les mesures se lisent sur une
                // instance à part, alimentée directement avec le fichier rendu (blocs de 512).
                auto meterPtr = ed.getPluginCache().createNewPlugin (ondefx::LimiterPlugin::xmlTypeName, {});
                auto* meter = dynamic_cast<ondefx::LimiterPlugin*> (meterPtr.get());
                auto measure = [&] (juce::AudioBuffer<float> b, bool truePeak, int upTo = 1 << 30)
                {
                    meter->param ("ceiling")->setParameter (-1.0f, juce::sendNotification);
                    meter->param ("truepeak")->setParameter (truePeak ? 1.0f : 0.0f, juce::sendNotification);
                    meter->initialise ({ te::TimePosition(), 48000.0, 512 });
                    for (int s = 0; s < std::min (upTo, b.getNumSamples()); s += 512)
                    {
                        te::PluginRenderContext rc (&b, s, std::min (512, b.getNumSamples() - s), nullptr, 0.0, {}, true, false, true, false);
                        meter->applyToBuffer (rc);
                    }
                    return b;
                };
                const auto qb = renderTrack ("sonie");
                measure (qb, true, (int) (48000 * 3.5));
                const float li = meter->integratedLufs.load(), lm = meter->momentaryLufs.load(), ls = meter->shortTermLufs.load();
                check (std::abs (li + 20.0f) < 0.3f && std::abs (lm + 20.0f) < 0.3f && std::abs (ls + 20.0f) < 0.5f,
                       U ("Onde Limiteur : sonie d'un 1 kHz à -20 dBFS → intégrée ") + juce::String (li, 2) + U (", momentanée ")
                       + juce::String (lm, 2) + U (", court terme ") + juce::String (ls, 2) + " LUFS");

                // Analyse du mix (sortie du projet) : sonie, corrélation ; Mono et -20 dB n'agissent qu'à l'écoute.
                {
                    auto* am = ondefx::MasterMeterPlugin::findOrCreate (ed);
                    bool single = am != nullptr && ondefx::MasterMeterPlugin::findOrCreate (ed) == am;
                    auto probePtr = ed.getPluginCache().createNewPlugin (ondefx::MasterMeterPlugin::xmlTypeName, {});
                    auto* probe = dynamic_cast<ondefx::MasterMeterPlugin*> (probePtr.get());
                    probe->initialise ({ te::TimePosition(), 48000.0, 512 });
                    auto live = qb, offline = qb;
                    probe->monoListen = true; probe->dimListen = true;
                    for (int s = 0; s < (int) (48000 * 3.5); s += 512)
                    {
                        te::PluginRenderContext rc (&offline, s, 512, nullptr, 0.0, {}, true, false, true, false);
                        probe->applyToBuffer (rc);
                    }
                    const float mi = probe->integrated.load(), corr = probe->correlation.load(), tpm = probe->truePeakMax.load();
                    probe->resetMeasurements();
                    for (int s = 0; s < live.getNumSamples(); s += 512)
                    {
                        te::PluginRenderContext rc (&live, s, std::min (512, live.getNumSamples() - s), nullptr, 0.0, {}, true, false, false, false);
                        probe->applyToBuffer (rc);
                    }
                    const float offPk = peakDb (offline) - peakDb (qb), livePk = peakDb (live) - peakDb (qb);
                    check (single && std::abs (mi + 20.0f) < 0.3f && corr > 0.99f && std::abs (tpm + 20.0f) < 0.5f && std::abs (offPk) < 0.01f && std::abs (livePk + 20.0f) < 0.2f,
                           U ("Analyse du mix : 1 kHz à -20 dBFS → ") + juce::String (mi, 2) + U (" LUFS, crête vraie ") + juce::String (tpm, 1)
                           + U (" dBTP, corrélation ") + juce::String (corr, 2) + U (" ; Mono + -20 dB : export ") + juce::String (offPk, 2)
                           + U (" dB, écoute ") + juce::String (livePk, 1) + " dB" + (single ? "" : U (" (analyse en double !)")));
                    am->monoListen = true; am->dimListen = true;
                    const auto withListen = renderTrack ("analyse-ecoute");
                    am->monoListen = false; am->dimListen = false;
                    t->getVolumePlugin()->polarity = true;
                    const auto inverted = renderTrack ("polarite");
                    t->getVolumePlugin()->polarity = false;
                    double dot = 0, e1 = 0, e2 = 0;
                    for (int i = 0; i < std::min (inverted.getNumSamples(), qb.getNumSamples()); ++i)
                    { const double x = qb.getSample (0, i), y = inverted.getSample (0, i); dot += x * y; e1 += x * x; e2 += y * y; }
                    const double inv = dot / std::sqrt (juce::jmax (1.0e-12, e1 * e2));
                    check (std::abs (peakDb (withListen) - peakDb (qb)) < 0.05f && inv < -0.99,
                           U ("export avec Mono et -20 dB actifs : crête ") + juce::String (peakDb (withListen) - peakDb (qb), 2)
                           + U (" dB d'écart ; polarité Ø : corrélation avec l'original ") + juce::String (inv, 3));
                }

                // Crête vraie : sinus à fs/4 déphasé de 45° → échantillons à -3 dBFS, crête réelle à 0 dBTP.
                const auto isp = dir.getChildFile ("sinus-inter-echantillons.wav");
                writeTones (isp, { { 12000.0, 1.0f } }, juce::MathConstants<double>::pi / 4.0, true);
                for (auto* c : t->getClips()) c->removeFromParent();
                t->insertWaveClip ("isp", isp, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
                // Mesure de la crête vraie d'un rendu : l'instance de mesure, sans crête vraie, ne limite pas
                // un signal sous -1 dBFS et lit la crête vraie de ce qui passe.
                lim->param ("truepeak")->setParameter (0.0f, juce::sendNotification);
                const auto offBuf = renderTrack ("limiteur-sans-tp");
                const float spOff = peakDb (offBuf);
                measure (offBuf, false);
                const float tpOff = meter->truePeakMaxDb.load();
                lim->param ("truepeak")->setParameter (1.0f, juce::sendNotification);
                const auto onBuf = renderTrack ("limiteur-tp");
                const float spOn = peakDb (onBuf);
                measure (onBuf, false);
                const float tpOn = meter->truePeakMaxDb.load();
                check (std::abs (spOff + 3.01f) < 0.1f && std::abs (tpOff) < 0.2f && tpOn <= -0.85f && std::abs (spOn + 4.0f) < 0.3f,
                       U ("Onde Limiteur : crête vraie → sans : échantillons ") + juce::String (spOff, 2) + U (" dBFS, mesurée ") + juce::String (tpOff, 2)
                       + U (" dBTP ; avec : ") + juce::String (tpOn, 2) + U (" dBTP (plafond -1), échantillons ") + juce::String (spOn, 2) + " dBFS");
                if (auto e = ondefx::createEditor (*lim)) { pump (200); snapshot (*e, dir.getChildFile ("16-onde-limiteur.png")); }
                limPtr->deleteFromParent();
            }

            // Modulateurs : un LFO carré à 2 Hz sur le volume fait alterner le niveau (trémolo) au rendu.
            for (auto* c : t->getClips()) c->removeFromParent();
            t->insertWaveClip ("modulation", tones, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
            // Niveau (dB) de chaque tranche de 20 ms entre 1 et 3 s : écart entre la plus forte et la plus faible.
            auto swingDb = [] (const juce::AudioBuffer<float>& b)
            {
                float lo = 1.0e9f, hi = -1.0e9f;
                for (int s = 48000; s + 960 <= std::min (b.getNumSamples(), 3 * 48000); s += 960)
                {
                    const float db = juce::Decibels::gainToDecibels (b.getRMSLevel (0, s, 960), -200.0f);
                    lo = std::min (lo, db); hi = std::max (hi, db);
                }
                return hi - lo;
            };
            const double still = swingDb (renderTrack ("modulation-sans"));
            auto lfo = ModulatorPanel::addModulator (*t, ModulatorPanel::Kind::lfo);
            auto* lfoMod = dynamic_cast<te::LFOModifier*> (lfo.get());
            check (lfoMod != nullptr && ModulatorPanel::describe (*lfo) == "LFO 1", U ("modulateur LFO ajouté à la piste (« ") + (lfo ? ModulatorPanel::describe (*lfo) : juce::String()) + U (" »)"));
            if (lfoMod != nullptr)
            {
                lfoMod->waveParam->setParameter ((float) te::LFOModifier::waveSquare, juce::sendNotification);
                lfoMod->rateParam->setParameter (2.0f, juce::sendNotification);
                auto* vol = t->getVolumePlugin()->volParam.get();
                auto assignment = ModulatorPanel::assign (*lfo, *vol, -0.3f);
                const double moving = swingDb (renderTrack ("modulation-lfo"));
                check (assignment != nullptr && still < 0.5 && moving > 6.0,
                       U ("LFO carré 2 Hz sur le volume → le niveau varie de ") + juce::String (moving, 1) + U (" dB (sans modulateur : ") + juce::String (still, 2) + " dB)");

                // Fenêtre des modulateurs : une carte par modulateur, avec son affectation.
                ctx.selectTrack (t);
                ctx.perform ("track.modulators");
                pump (400);
                ModulatorPanel* panel = nullptr;
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (auto* p = dynamic_cast<ModulatorPanel*> (dw->getContentComponent())) panel = p;
                auto step = ModulatorPanel::addModulator (*t, ModulatorPanel::Kind::step);
                pump (500);
                check (panel != nullptr && panel->numCards() == 2 && step != nullptr && ModulatorPanel::describe (*step) == U ("Pas à pas 1"),
                       U ("fenêtre des modulateurs : ") + juce::String (panel != nullptr ? panel->numCards() : -1) + U (" cartes (LFO, pas à pas)"));
                if (panel != nullptr) snapshot (*panel, dir.getChildFile ("17-modulateurs.png"));

                // Enregistrés avec le projet : modulateurs, réglages et affectation.
                const auto saved = dir.getChildFile ("modulateurs.onde");
                bool kept = false;
                if (mc->saveProjectTo (saved))
                    if (auto reopened = MainComponent::loadProject (*engine, saved))
                    {
                        for (auto* rt : te::getAudioTracks (*reopened))
                            if (rt->getName() == t->getName())
                                if (auto* list = rt->getModifierList())
                                {
                                    auto mods = list->getModifiers();
                                    auto* rl = mods.size() == 2 ? dynamic_cast<te::LFOModifier*> (mods[0].get()) : nullptr;
                                    auto assigned = rt->getVolumePlugin()->volParam->getAssignments();
                                    kept = rl != nullptr && std::abs (rl->rateParam->getCurrentValue() - 2.0f) < 0.01f
                                           && assigned.size() == 1 && std::abs (assigned[0]->value.get() + 0.3f) < 0.001f;
                                }
                        EditContext::destroyEdit (reopened);
                    }
                check (kept, U ("modulateurs, réglages et affectation enregistrés avec le projet"));

                // Annuler : l'ajout du pas à pas, puis l'affectation du LFO.
                auto& um = ed.getUndoManager();
                const auto lastAction = um.getUndoDescription();
                const bool historyNamed = um.getUndoDescriptions().contains (U ("Ajouter un modulateur (Pas à pas)"));
                int undos = 0;
                while (t->getModifierList()->getModifiers().size() > 1 && um.canUndo() && undos < 10) { um.undo(); ++undos; }
                pump (300);
                const bool lfoKept = t->getModifierList()->getModifiers().size() == 1 && vol->getAssignments().size() == 1;
                while (vol->getAssignments().size() > 0 && um.canUndo() && undos < 20) { um.undo(); ++undos; }
                pump (300);
                check (lfoKept && historyNamed && vol->getAssignments().isEmpty() && panel != nullptr && panel->numCards() == 1,
                       U ("annuler retire le pas à pas (le LFO et son affectation restent), puis l'affectation du LFO — dernière action : « ")
                       + lastAction + U (" », ") + juce::String (undos) + U (" annulation(s)"));
                if (panel != nullptr) if (auto* dw = panel->findParentComponentOfClass<juce::DialogWindow>()) delete dw;
            }
            for (auto* list = t->getModifierList(); list != nullptr && list->getModifiers().size() > 0;)
                list->getModifiers()[0]->remove();

            // Automation d'un paramètre d'effet (pas seulement volume et panoramique) : gain de la cloche
            // à 1 kHz à 0 dB puis +12 dB à partir de 2 s.
            {
                auto eqAuto = ed.getPluginCache().createNewPlugin (ondefx::EqPlugin::xmlTypeName, {});
                t->pluginList.insertPlugin (eqAuto, 0, nullptr);
                const auto flatAgain = renderTrack ("automation-eq-neutre");
                if (auto* eq3 = dynamic_cast<ondefx::EqPlugin*> (eqAuto.get()))
                {
                    auto& curve = eq3->param (ondefx::EqPlugin::bandId (3, "gain"))->getCurve();
                    curve.addPoint (te::TimePosition(), 0.0f, 1.0f, nullptr);
                    curve.addPoint (te::TimePosition::fromSeconds (2.0), 12.0f, 1.0f, nullptr);
                    const auto autoBuf = renderTrack ("automation-eq");
                    // toneDb mesure de 2 à 3 s ; avant 2 s, on mesure à part le premier quart de seconde.
                    const double late = toneDb (autoBuf, 48000, 1000) - toneDb (flatAgain, 48000, 1000);
                    double early = 0;
                    {
                        float a = 0, b = 0;
                        for (int i = 4800; i < 48000; ++i) { a = std::max (a, std::abs (autoBuf.getSample (0, i))); b = std::max (b, std::abs (flatAgain.getSample (0, i))); }
                        early = juce::Decibels::gainToDecibels (a) - juce::Decibels::gainToDecibels (b);
                    }
                    check (std::abs (late - 12.0) < 0.6 && std::abs (early) < 0.5,
                           U ("automation d'un paramètre d'effet (cloche 1 kHz d'Onde EQ) : ") + juce::String (early, 2) + U (" dB avant 2 s, ")
                           + juce::String (late, 2) + U (" dB après (attendu 0 puis +12)"));
                }
                eqAuto->deleteFromParent();
            }

            // Onde Synth et bibliothèque de sons : chaque son joue, sans saturer ; justesse ; batterie ; annuler ;
            // enregistrement avec le projet ; fenêtres.
            {
                auto* st = c4.addInstrumentTrack (nullptr);
                ondefx::SynthPlugin* synth = nullptr;
                if (st != nullptr) for (auto* p : st->pluginList) if (auto* s = dynamic_cast<ondefx::SynthPlugin*> (p)) synth = s;
                check (synth != nullptr && st->getName() == "Piano de concert",
                       U ("une nouvelle piste instrument a Onde Synth, son « Piano de concert » (piste « ") + (st ? st->getName() : juce::String()) + U (" »)"));
                if (synth != nullptr)
                {
                    auto midiClip = st->insertMIDIClip ("test", { te::TimePosition(), te::TimePosition::fromSeconds (3.0) }, nullptr);
                    auto setNotes = [&] (std::initializer_list<int> pitches, double beats)
                    {
                        auto& seq = midiClip->getSequence();
                        seq.clear (nullptr);
                        for (int p : pitches) seq.addNote (p, te::BeatPosition(), te::BeatDuration::fromBeats (beats), 100, 0, nullptr);
                    };
                    auto renderSynth = [&] (const juce::String& name)
                    {
                        double r = 0; const auto f = dir.getChildFile ("synth-" + juce::File::createLegalFileName (name) + ".wav");
                        renderEdit (ed, f, { st }); return readAll (f, r);
                    };
                    // Tous les sons de la bibliothèque.
                    int ok = 0, total = 0;
                    juce::String bad;
                    for (auto& preset : ondefx::SynthPlugin::presets())
                    {
                        ++total;
                        const bool drums = juce::String::fromUTF8 (preset.category) == "Batteries";
                        LibraryPanel::applyToTrack (c4, st, preset);
                        auto& seq = midiClip->getSequence();
                        seq.clear (nullptr);
                        if (drums) for (int k = 0; k < 4; ++k) for (int p : { 36, 38, 42 }) seq.addNote (p, te::BeatPosition::fromBeats (k), te::BeatDuration::fromBeats (0.25), 110, 0, nullptr);
                        else for (int p : { 48, 60, 64, 67 }) seq.addNote (p, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);
                        const auto b = renderSynth (juce::String::fromUTF8 (preset.name));
                        const float peak = b.getMagnitude (0, b.getNumSamples());
                        const float rms = b.getRMSLevel (0, 0, juce::jmin (b.getNumSamples(), 48000));
                        bool finite = true;
                        for (int i = 0; i < b.getNumSamples() && finite; ++i) finite = std::isfinite (b.getSample (0, i));
                        if (finite && peak < 1.0f && juce::Decibels::gainToDecibels (rms) > -40.0f) ++ok;
                        else bad << juce::String::fromUTF8 (preset.name) << " (crête " << juce::String (juce::Decibels::gainToDecibels (peak), 1)
                                  << ", RMS " << juce::String (juce::Decibels::gainToDecibels (rms), 1) << ") ";
                    }
                    check (ok == total, U ("bibliothèque : ") + juce::String (ok) + "/" + juce::String (total) + U (" sons jouent, sans saturer ni valeur invalide ") + bad);

                    // Justesse : La 440 Hz sur l'orgue (sinus) et sur le piano (partiels presque harmoniques).
                    auto pitchOf = [] (const juce::AudioBuffer<float>& b)
                    {
                        // Passages par zéro montants, interpolés, sur 0,2 à 1,2 s.
                        double first = -1, last = -1; int count = 0;
                        for (int i = 9600 + 1; i < juce::jmin (b.getNumSamples(), 57600); ++i)
                        {
                            const float a = b.getSample (0, i - 1), c = b.getSample (0, i);
                            if (a < 0 && c >= 0) { const double t = i - 1 + a / (a - c); if (first < 0) first = t; last = t; ++count; }
                        }
                        return count > 1 ? (count - 1) * 48000.0 / (last - first) : 0.0;
                    };
                    LibraryPanel::applyToTrack (c4, st, *ondefx::SynthPlugin::findPreset ("Orgue doux"));
                    synth->param ("sub")->setParameter (0.0f, juce::sendNotification);
                    synth->param ("mix2")->setParameter (0.0f, juce::sendNotification);
                    synth->param ("vibrato")->setParameter (0.0f, juce::sendNotification);
                    setNotes ({ 69 }, 4.0);
                    const double organHz = pitchOf (renderSynth ("la-orgue"));
                    LibraryPanel::applyToTrack (c4, st, *ondefx::SynthPlugin::findPreset ("Piano doux"));
                    setNotes ({ 69 }, 4.0);
                    const auto pianoBuf = renderSynth ("la-piano");
                    // Le piano a des partiels : on mesure le fondamental par Goertzel autour de 440 Hz.
                    double bestHz = 0, bestMag = 0;
                    for (double hzv = 430.0; hzv <= 450.0; hzv += 0.25)
                    {
                        const double w = juce::MathConstants<double>::twoPi * hzv / 48000.0, coeff = 2.0 * std::cos (w);
                        double q1 = 0, q2 = 0;
                        for (int i = 4800; i < 4800 + 48000 && i < pianoBuf.getNumSamples(); ++i) { const double q0 = coeff * q1 - q2 + pianoBuf.getSample (0, i); q2 = q1; q1 = q0; }
                        const double mag = q1 * q1 + q2 * q2 - q1 * q2 * coeff;
                        if (mag > bestMag) { bestMag = mag; bestHz = hzv; }
                    }
                    check (std::abs (organHz - 440.0) < 0.5 && std::abs (bestHz - 440.0) < 1.5,
                           U ("justesse : La 440 Hz → orgue ") + juce::String (organHz, 2) + U (" Hz, piano ") + juce::String (bestHz, 2) + " Hz");



                    // Essai du son depuis la fenêtre : une note jouée au clavier de la fenêtre fait sonner l'instrument.
                    {
                        auto probe = ed.getPluginCache().createNewPlugin (ondefx::SynthPlugin::xmlTypeName, {});
                        auto* ps = dynamic_cast<ondefx::SynthPlugin*> (probe.get());
                        ps->applyPreset (*ondefx::SynthPlugin::findPreset (U ("Guitare folk")));
                        ps->initialise ({ te::TimePosition(), 48000.0, 512 });
                        ps->auditionNote (64, 0.8f);
                        juce::AudioBuffer<float> ab (2, 512 * 20);
                        for (int s = 0; s < ab.getNumSamples(); s += 512)
                        {
                            te::PluginRenderContext rc (&ab, s, 512, nullptr, 0.0, {}, true, false, false, false);
                            ps->applyToBuffer (rc);
                        }
                        const float pk = ab.getMagnitude (0, 0, ab.getNumSamples());
                        check (pk > 0.01f && pk < 1.0f, U ("essai du son depuis la fenêtre de l'instrument : crête ") + juce::String (juce::Decibels::gainToDecibels (pk), 1) + " dBFS");
                    }

                    // Cordes pincées (modèle physique) : justesse du grave de la 8 cordes à l'aigu, la corde résonne puis s'éteint.
                    {
                        auto fundamental = [] (const juce::AudioBuffer<float>& b, double target)
                        {
                            double best = 0, bestMag = 0;
                            for (double hzv = target * 0.97; hzv <= target * 1.03; hzv += target * 0.0003)
                            {
                                const double w = juce::MathConstants<double>::twoPi * hzv / 48000.0, coeff = 2.0 * std::cos (w);
                                double q1 = 0, q2 = 0;
                                for (int i = 4800; i < 4800 + 48000 && i < b.getNumSamples(); ++i) { const double q0 = coeff * q1 - q2 + b.getSample (0, i); q2 = q1; q1 = q0; }
                                const double mag = q1 * q1 + q2 * q2 - q1 * q2 * coeff;
                                if (mag > bestMag) { bestMag = mag; best = hzv; }
                            }
                            return best;
                        };
                        juce::String report; bool inTune = true, rings = true;
                        for (auto [presetName, note] : { std::pair<const char*, int> { "Guitare classique (nylon)", 45 }, { "Guitare 8 cordes (djent)", 30 },
                                                         { "Guitare folk", 64 }, { "Basse électrique (doigts)", 28 }, { "Harpe", 81 } })
                        {
                            LibraryPanel::applyToTrack (c4, st, *ondefx::SynthPlugin::findPreset (U (presetName)));
                            setNotes ({ note }, 6.0);
                            const auto gb = renderSynth (U ("corde-") + juce::String (note));
                            const double target = 440.0 * std::pow (2.0, (note - 69) / 12.0), hz = fundamental (gb, target);
                            const double cents = 1200.0 * std::log2 (juce::jmax (1.0, hz) / target);
                            const float early = gb.getRMSLevel (0, 4800, 9600), late = gb.getRMSLevel (0, 96000, 9600);
                            inTune = inTune && std::abs (cents) < 6.0;
                            rings = rings && early > 0.003f && late < early;
                            report << U (presetName) << " " << juce::String (hz, 2) << " Hz (" << (cents >= 0 ? "+" : "") << juce::String (cents, 1) << " ct) ; ";
                        }
                        check (inTune && rings, U ("cordes pincées : ") + report + (rings ? U ("chaque corde résonne puis s'éteint") : U ("décroissance anormale")));
                    }

                    // Batterie : grosse caisse grave, charleston aiguë (centre de gravité du spectre).
                    LibraryPanel::applyToTrack (c4, st, *ondefx::SynthPlugin::findPreset ("Batterie acoustique"));
                    auto brightness = [] (const juce::AudioBuffer<float>& b)
                    {
                        // Rapport d'énergie de la dérivée (aigus) à celle du signal : grand = son brillant.
                        double e = 0, d = 0;
                        for (int i = 1; i < juce::jmin (b.getNumSamples(), 24000); ++i) { const double x = b.getSample (0, i), dx = x - b.getSample (0, i - 1); e += x * x; d += dx * dx; }
                        return e > 0 ? std::sqrt (d / e) * 48000.0 / juce::MathConstants<double>::twoPi : 0.0;
                    };
                    setNotes ({ 36 }, 0.5);
                    const double kickHz = brightness (renderSynth ("grosse-caisse"));
                    setNotes ({ 42 }, 0.5);
                    const double hatHz = brightness (renderSynth ("charleston"));
                    check (kickHz > 20 && kickHz < 400 && hatHz > 4000,
                           U ("batterie : grosse caisse (note 36) centrée vers ") + juce::String (juce::roundToInt (kickHz)) + U (" Hz, charleston (note 42) vers ")
                           + juce::String (juce::roundToInt (hatHz)) + " Hz");

                    // Annuler un changement de son, puis enregistrer avec le projet.
                    LibraryPanel::applyToTrack (c4, st, *ondefx::SynthPlugin::findPreset ("Nappe chaude"));
                    ed.getUndoManager().undo();
                    pump (200);
                    const bool undone = synth->getPresetName() == "Batterie acoustique" && juce::roundToInt (synth->value ("engine")) == 3;
                    const auto undoInfo = synth->getPresetName() + " / moteur " + juce::String (synth->value ("engine")) + " (état " + synth->state["engine"].toString() + ")" + " / historique " + ed.getUndoManager().getUndoDescription();
                    LibraryPanel::applyToTrack (c4, st, *ondefx::SynthPlugin::findPreset ("Basse acide"));
                    const auto saved = dir.getChildFile ("synth.onde");
                    bool kept = false;
                    if (mc->saveProjectTo (saved))
                        if (auto reopened = MainComponent::loadProject (*engine, saved))
                        {
                            for (auto* rt : te::getAudioTracks (*reopened))
                                for (auto* rp : rt->pluginList)
                                    if (auto* rs = dynamic_cast<ondefx::SynthPlugin*> (rp))
                                        kept = kept || (rs->getPresetName() == "Basse acide" && std::abs (rs->value ("reso") - 0.8f) < 0.001f);
                            EditContext::destroyEdit (reopened);
                        }
                    check (undone && kept, U ("changement de son annulable (") + juce::String (undone ? "oui" : "non") + " : " + undoInfo + U ("), son et réglages enregistrés avec le projet (")
                                           + juce::String (kept ? "oui" : "non") + ")");

                    // Un bouton tourné puis « annuler » : le son revient (la valeur entendue, pas seulement l'affichage).
                    {
                        auto& um = ed.getUndoManager();
                        pump (500);
                        um.beginNewTransaction ("Réglage");
                        synth->param ("reso")->setParameter (0.1f, juce::sendNotification);
                        pump (500);
                        const float moved = synth->value ("reso");
                        um.undo();
                        pump (300);
                        const float back = synth->value ("reso");
                        um.redo();
                        pump (300);
                        const float again = synth->value ("reso");
                        check (std::abs (moved - 0.1f) < 1.0e-4f && std::abs (back - 0.8f) < 1.0e-4f && std::abs (again - 0.1f) < 1.0e-4f,
                               U ("réglage annulé puis rétabli, le son suit : ") + juce::String (moved, 2) + U (" → ") + juce::String (back, 2) + U (" → ") + juce::String (again, 2));
                    }

                    // Fenêtres : Onde Synth et bibliothèque.
                    if (auto e = ondefx::createEditor (*synth)) { pump (200); snapshot (*e, dir.getChildFile ("18-onde-synth.png")); }
                    for (auto [presetName, file] : { std::pair<const char*, const char*> { "Guitare folk 12 cordes", "18b-onde-synth-cordes.png" }, { "Batterie acoustique", "18c-onde-synth-batterie.png" }, { "Piano de concert", "18d-onde-synth-piano.png" } })
                    {
                        LibraryPanel::applyToTrack (c4, st, *ondefx::SynthPlugin::findPreset (U (presetName)));
                        if (auto e = ondefx::createEditor (*synth)) { pump (200); snapshot (*e, dir.getChildFile (file)); }
                    }
                    c4.selectTrack (st);
                    c4.perform ("midi.library");
                    pump (400);
                    LibraryPanel* lib = nullptr;
                    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                        if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                            if (auto* p = dynamic_cast<LibraryPanel*> (dw->getContentComponent())) lib = p;
                    check (lib != nullptr && lib->numPresetsShown() > 0, U ("la bibliothèque de sons s'ouvre (Ctrl+Maj+B) : ")
                                                                       + juce::String (lib ? lib->numPresetsShown() : 0) + U (" sons dans la première famille"));
                    if (lib != nullptr)
                    {
                        lib->selectCategory (2);
                        lib->choosePreset (0, false);
                        pump (300);
                        snapshot (*lib, dir.getChildFile ("19-bibliotheque.png"));
                        if (auto* dw = lib->findParentComponentOfClass<juce::DialogWindow>()) delete dw;
                    }
                }
                if (st != nullptr) { ed.deleteTrack (st); c4.changed(); }
            }

            // Effets Onde (saturation, écho, espace) et chaînes prêtes avec commandes rapides.
            {
                for (auto* c : t->getClips()) c->removeFromParent();
                for (auto* p : t->pluginList.getPlugins())
                    if (dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr) p->deleteFromParent();
                t->insertWaveClip ("fort", loud, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
                auto addFx = [&] (const char* type) { auto p = ed.getPluginCache().createNewPlugin (type, {}); t->pluginList.insertPlugin (p, 0, nullptr); return p; };

                // Saturation : harmonique 3 (caractère doux, impair) et 2 (lampe, pair) sur un 1 kHz à -6 dBFS.
                auto satPtr = addFx (ondefx::SaturationPlugin::xmlTypeName);
                auto* sat = dynamic_cast<ondefx::SaturationPlugin*> (satPtr.get());
                sat->param ("drive")->setParameter (0.0f, juce::sendNotification);
                sat->param ("character")->setParameter (0.0f, juce::sendNotification);
                const auto clean = renderTrack ("saturation-0");
                sat->param ("drive")->setParameter (24.0f, juce::sendNotification);
                const auto hot = renderTrack ("saturation-douce");
                sat->param ("character")->setParameter (1.0f, juce::sendNotification);
                const auto tube = renderTrack ("saturation-lampe");
                const double h3clean = toneDb (clean, 48000, 3000) - toneDb (clean, 48000, 1000);
                const double h3 = toneDb (hot, 48000, 3000) - toneDb (hot, 48000, 1000);
                const double h2tube = toneDb (tube, 48000, 2000) - toneDb (tube, 48000, 1000), h2soft = toneDb (hot, 48000, 2000) - toneDb (hot, 48000, 1000);
                const float peakHot = peakDb (hot);
                check (h3 > h3clean + 15.0 && h2tube > -35.0 && h2soft < -60.0 && peakHot < 0.0f,
                       U ("Onde Saturation : harmonique 3 ") + juce::String (h3clean, 1) + U (" dB à 0 dB, ") + juce::String (h3, 1)
                       + U (" dB à +24 dB ; harmonique 2 : douce ") + juce::String (juce::jmax (-120.0, h2soft), 0) + U (" dB, lampe ") + juce::String (h2tube, 1) + U (" dB ; crête ") + juce::String (peakHot, 1) + " dBFS");
                if (auto e = ondefx::createEditor (*sat)) { pump (200); snapshot (*e, dir.getChildFile ("20-onde-saturation.png")); }
                satPtr->deleteFromParent();

                // Onde Modulation : chaque effet modifie le son sans le faire exploser (flanger à forte réinjection compris).
                {
                    auto modPtr = addFx (ondefx::ModulationPlugin::xmlTypeName);
                    auto* mod = dynamic_cast<ondefx::ModulationPlugin*> (modPtr.get());
                    mod->param ("mix")->setParameter (0.0f, juce::sendNotification);
                    const auto dry = renderTrack ("modulation-sec");
                    const float dryPk = peakDb (dry);
                    juce::String report; bool allOk = true;
                    for (int m = 0; m < 5; ++m)
                    {
                        mod->param ("mode")->setParameter ((float) m, juce::sendNotification);
                        mod->param ("mix")->setParameter (m == 4 ? 100.0f : 50.0f, juce::sendNotification);
                        mod->param ("feedback")->setParameter (m == 2 ? 90.0f : m == 3 ? 60.0f : 0.0f, juce::sendNotification);
                        mod->param ("depth")->setParameter (70.0f, juce::sendNotification);
                        const auto wet = renderTrack ((juce::String ("modulation-") + juce::String (m)).toRawUTF8());
                        double diff = 0; bool finite = true;
                        for (int i = 0; i < juce::jmin (wet.getNumSamples(), dry.getNumSamples()); ++i)
                        { const float a = wet.getSample (0, i); finite = finite && std::isfinite (a); diff = std::max (diff, (double) std::abs (a - dry.getSample (0, i))); }
                        const float pk = peakDb (wet) - dryPk;
                        const bool ok = finite && diff > 0.01 && pk < 7.0f && pk > -15.0f;
                        allOk = allOk && ok;
                        report << juce::String::fromUTF8 (ondefx::ModulationPlugin::modeNames[m]) << " " << (pk >= 0 ? "+" : "") << juce::String (pk, 1) << " dB" << (ok ? "" : " (!)") << " ; ";
                    }
                    check (allOk, U ("Onde Modulation : ") + report + U ("son modifié, crête maîtrisée"));
                    if (auto e = ondefx::createEditor (*mod)) { pump (200); snapshot (*e, dir.getChildFile ("20b-onde-modulation.png")); }
                    modPtr->deleteFromParent();
                }

                // Écho et espace sur un clic isolé à 0,5 s.
                const auto click = dir.getChildFile ("clic.wav");
                {
                    juce::AudioBuffer<float> b (2, len); b.clear();
                    b.setSample (0, 24000, 0.8f); b.setSample (1, 24000, 0.8f);
                    click.deleteFile();
                    std::unique_ptr<juce::OutputStream> os (click.createOutputStream());
                    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (os.get(), fs, 2, 32, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer (b, 0, len); }
                }
                for (auto* c : t->getClips()) c->removeFromParent();
                t->insertWaveClip ("clic", click, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
                auto echoPtr = addFx (ondefx::EchoPlugin::xmlTypeName);
                auto* echo = dynamic_cast<ondefx::OndeEffect*> (echoPtr.get());
                echo->param ("division")->setParameter (1.0f, juce::sendNotification);      // noire
                echo->param ("feedback")->setParameter (0.0f, juce::sendNotification);
                echo->param ("mix")->setParameter (50.0f, juce::sendNotification);
                const auto echoed = renderTrack ("echo");
                const double bpm = ed.tempoSequence.getTempos()[0]->getBpm();
                int peakAt = -1; float peakV = 0;
                for (int i = 24000 + 2400; i < echoed.getNumSamples(); ++i) if (std::abs (echoed.getSample (0, i)) > peakV) { peakV = std::abs (echoed.getSample (0, i)); peakAt = i; }
                const double delayMs = (peakAt - 24000) / 48.0, expectMs = 60000.0 / bpm;
                check (std::abs (delayMs - expectMs) < 1.0 && peakV > 0.3f,
                       U ("Onde Écho : noire à ") + juce::String (bpm, 0) + U (" BPM → répétition à ") + juce::String (delayMs, 1) + U (" ms (attendu ") + juce::String (expectMs, 1) + " ms)");
                if (auto e = ondefx::createEditor (*echoPtr)) { echo->setStored ("pingpong", 1.0f); echo->setStored ("feedback", 50.0f); pump (300); snapshot (*e, dir.getChildFile ("27-onde-echo.png")); }
                echoPtr->deleteFromParent();

                auto spacePtr = addFx (ondefx::SpacePlugin::xmlTypeName);
                auto* space = dynamic_cast<ondefx::OndeEffect*> (spacePtr.get());
                space->param ("decay")->setParameter (2.0f, juce::sendNotification);
                space->param ("mix")->setParameter (100.0f, juce::sendNotification);
                space->param ("predelay")->setParameter (0.0f, juce::sendNotification);
                const auto tail = renderTrack ("espace");
                auto energyDb = [&] (double from, double to)
                {
                    double e = 0; const int a = 24000 + (int) (from * 48000), b = 24000 + (int) (to * 48000);
                    for (int i = a; i < juce::jmin (b, tail.getNumSamples()); ++i) e += tail.getSample (0, i) * tail.getSample (0, i) + tail.getSample (1, i) * tail.getSample (1, i);
                    return 10.0 * std::log10 (e / juce::jmax (1, b - a) + 1.0e-20);
                };
                const double slope = (energyDb (1.1, 1.3) - energyDb (0.2, 0.4)) / 0.9;          // dB par seconde
                const double rt60 = -60.0 / slope;
                double corr = 0, el = 0, er = 0;
                for (int i = 24000 + 4800; i < 24000 + 48000; ++i) { const double l = tail.getSample (0, i), r = tail.getSample (1, i); corr += l * r; el += l * l; er += r * r; }
                corr /= std::sqrt (el * er + 1.0e-20);
                check (rt60 > 1.4 && rt60 < 2.8 && std::abs (corr) < 0.5,
                       U ("Onde Espace : durée réglée 2 s → mesurée ") + juce::String (rt60, 2) + U (" s ; corrélation gauche/droite ") + juce::String (corr, 2) + U (" (stéréo large)"));
                if (auto e = ondefx::createEditor (*spacePtr)) { space->setStored ("mix", 25.0f); pump (300); snapshot (*e, dir.getChildFile ("28-onde-espace.png")); }
                spacePtr->deleteFromParent();

                // Chaînes prêtes : pose, remplacement, annulation ; une commande rapide change le son ; enregistrement.
                for (auto* c : t->getClips()) c->removeFromParent();
                t->insertWaveClip ("fort", loud, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
                auto chainCount = [&] { int n2 = 0; for (auto* p : t->pluginList) if (p->state.hasProperty ("ondeChain")) ++n2; return n2; };
                auto& um = ed.getUndoManager();
                um.beginNewTransaction (U ("Chaîne « Voix pop »"));
                chains::apply (*t, chains::find (U ("Voix pop")));
                const int popFx = chainCount(), popMacros = chains::quickControls (*t).size();
                um.beginNewTransaction (U ("Chaîne « Podcast »"));
                chains::apply (*t, chains::find ("Podcast"));
                const int podFx = chainCount();
                um.undo();
                pump (200);
                const bool backToPop = chains::currentChain (*t) == U ("Voix pop") && chainCount() == popFx;
                check (popFx == 4 && popMacros == 5 && podFx == 3 && backToPop,
                       U ("chaînes : « Voix pop » pose ") + juce::String (popFx) + U (" effets et ") + juce::String (popMacros) + U (" commandes rapides, « Podcast » la remplace (")
                       + juce::String (podFx) + U (" effets), annuler revient à « Voix pop » (") + juce::String (backToPop ? "oui" : "non") + ")");

                um.beginNewTransaction (U ("Chaîne « Guitare saturée »"));
                chains::apply (*t, chains::find (U ("Guitare saturée")));
                te::MacroParameter* drive = nullptr;
                for (auto m : chains::quickControls (*t)) if (m->getParameterName() == "Saturation") drive = m;
                double h3lo = 0, h3hi = 0;
                if (drive != nullptr)
                {
                    drive->setParameter (0.0f, juce::sendNotification);
                    const auto lo = renderTrack ("commande-saturation-0");
                    drive->setParameter (1.0f, juce::sendNotification);
                    const auto hi = renderTrack ("commande-saturation-100");
                    h3lo = toneDb (lo, 48000, 3000) - toneDb (lo, 48000, 1000);
                    h3hi = toneDb (hi, 48000, 3000) - toneDb (hi, 48000, 1000);
                    drive->setParameter (0.8f, juce::sendNotification);
                }
                bool chainKept = false;
                const auto savedChain = dir.getChildFile ("chaine.onde");
                if (mc->saveProjectTo (savedChain))
                    if (auto reopened = MainComponent::loadProject (*engine, savedChain))
                    {
                        for (auto* rt : te::getAudioTracks (*reopened))
                            if (chains::currentChain (*rt) == U ("Guitare saturée"))
                                for (auto m : chains::quickControls (*rt))
                                    if (m->getParameterName() == "Saturation" && std::abs (m->getCurrentValue() - 0.8f) < 0.01f) chainKept = true;
                        EditContext::destroyEdit (reopened);
                    }
                check (drive != nullptr && h3hi > h3lo + 3.0 && chainKept,
                       U ("commande rapide « Saturation » : harmonique 3 à ") + juce::String (h3lo, 1) + U (" dB (0 %) puis ") + juce::String (h3hi, 1)
                       + U (" dB (100 %) ; chaîne et réglage retrouvés après réouverture (") + juce::String (chainKept ? "oui" : "non") + ")");
                c4.selectTrack (t);
                c4.changed();
                pump (400);
                snapshot (*mc, dir.getChildFile ("21-commandes-rapides.png"));
                um.beginNewTransaction ("Retirer");
                chains::apply (*t, nullptr);
            }

            // Palette (Ctrl+K) : sons, chaînes et types de piste se trouvent par leur nom.
            check (c4.findAction (U ("sound.Piano de concert")) != nullptr && c4.findAction (U ("chain.Voix pop")) != nullptr && c4.findAction ("track.new.2") != nullptr,
                   U ("palette : « Son : Piano de concert », « Chaîne : Voix pop » et « Nouvelle piste : voix » s'y trouvent"));
            // Batteur : signatures des styles, complexité, intensité, remplissages, variations stables.
            {
                auto onBeat = [] (const std::vector<drummer::Hit>& hits, int note, int bars)
                {
                    int n2 = 0;
                    for (int b = 0; b < bars; ++b) for (int beat = 0; beat < 4; ++beat)
                        for (auto& h : hits) if (h.note == note && std::abs (h.beat - (b * 4 + beat)) < 0.03) { ++n2; break; }
                    return n2;
                };
                drummer::Settings s;
                s.fills = 0;
                s.style = 3;                                                     // électro : grosse caisse à chaque temps
                const auto electro = drummer::generate (s, 4, 4);
                s.style = 0;                                                     // pop : caisse claire sur 2 et 4
                const auto pop = drummer::generate (s, 4, 4);
                int snare24 = 0;
                for (int b = 0; b < 4; ++b) for (double beat : { 1.0, 3.0 })
                    for (auto& h : pop) if (h.note == 38 && std::abs (h.beat - (b * 4 + beat)) < 0.03) { ++snare24; break; }
                s.complexity = 0.0f; const auto simple = drummer::generate (s, 8, 4);
                s.complexity = 1.0f; const auto busy = drummer::generate (s, 8, 4);
                s.complexity = 0.5f; s.intensity = 0.2f; const auto soft = drummer::generate (s, 8, 4);
                s.intensity = 0.9f; const auto loudHits = drummer::generate (s, 8, 4);
                auto meanVel = [] (const std::vector<drummer::Hit>& h) { double v = 0; for (auto& x : h) v += x.velocity; return h.empty() ? 0.0 : v / (double) h.size(); };
                s.fills = 1; const auto withFill = drummer::generate (s, 4, 4);
                int toms = 0; for (auto& h : withFill) if (h.beat >= 12.0 && h.note >= 41 && h.note <= 50 && h.note != 42 && h.note != 46 && h.note != 49) ++toms;
                const auto again = drummer::generate (s, 4, 4);
                bool same = again.size() == withFill.size();
                for (size_t i = 0; same && i < again.size(); ++i) same = again[i].note == withFill[i].note && again[i].beat == withFill[i].beat && again[i].velocity == withFill[i].velocity;
                s.variation = 2; const auto other = drummer::generate (s, 4, 4);
                bool differs = other.size() != withFill.size();
                for (size_t i = 0; ! differs && i < other.size(); ++i) differs = other[i].beat != withFill[i].beat || other[i].velocity != withFill[i].velocity;
                check (onBeat (electro, 36, 4) == 16 && snare24 == 8 && busy.size() > simple.size() + 20 && meanVel (loudHits) > meanVel (soft) + 25.0
                       && toms >= 3 && same && differs,
                       U ("batteur : électro ") + juce::String (onBeat (electro, 36, 4)) + U ("/16 temps en grosse caisse, pop ") + juce::String (snare24)
                       + U ("/8 caisses claires sur 2 et 4, ") + juce::String ((int) simple.size()) + U (" notes en simple contre ") + juce::String ((int) busy.size())
                       + U (" en complexe, vélocité ") + juce::String (meanVel (soft), 0) + U (" (doux) / ") + juce::String (meanVel (loudHits), 0)
                       + U (" (fort), ") + juce::String (toms) + U (" coups de toms dans le remplissage, même variation identique, autre variation différente"));

                // Piste « Batteur », fenêtre (non modale), style changé puis annulé, son au rendu.
                c4.perform ("track.addDrummer");
                pump (500);
                te::AudioTrack* drumTrack = nullptr;
                for (auto* at : te::getAudioTracks (ed)) if (at->getName() == "Batteur") drumTrack = at;
                te::MidiClip* region = nullptr;
                if (drumTrack != nullptr) for (auto* c : drumTrack->getClips()) if (drummer::isDrummerRegion (c)) region = dynamic_cast<te::MidiClip*> (c);
                DrummerPanel* panel = nullptr;
                juce::DialogWindow* panelWindow = nullptr;
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (auto* p = dynamic_cast<DrummerPanel*> (dw->getContentComponent())) { panel = p; panelWindow = dw; }
                bool clapAfter = false, popBack = false, sounds = false;
                if (region != nullptr && panel != nullptr)
                {
                    snapshot (*panel, dir.getChildFile ("22-batteur.png"));
                    panel->setStyle (3);
                    for (auto* n : region->getSequence().getNotes()) clapAfter = clapAfter || n->getNoteNumber() == 39;
                    ed.getUndoManager().undo();
                    pump (200);
                    bool anyClap = false;
                    for (auto* n : region->getSequence().getNotes()) anyClap = anyClap || n->getNoteNumber() == 39;
                    popBack = drummer::read (*region).style == 0 && ! anyClap;
                    double r = 0; const auto f = dir.getChildFile ("batteur.wav");
                    renderEdit (ed, f, { drumTrack });
                    const auto b = readAll (f, r);
                    sounds = b.getNumSamples() > 0 && juce::Decibels::gainToDecibels (b.getRMSLevel (0, 0, b.getNumSamples())) > -40.0f;
                }
                check (region != nullptr && panel != nullptr && panelWindow != nullptr && ! panelWindow->isCurrentlyModal() && clapAfter && popBack && sounds,
                       U ("piste « Batteur » : région créée, fenêtre flottante (non modale), style Électro → claps, annuler → Pop, son au rendu (")
                       + juce::String (sounds ? "oui" : "non") + ")");
                // Mode de la région changeable à tout moment : notes modifiables (une retouche reste, les réglages
                // ne refont pas le jeu), puis retour en automatique (le jeu est refait).
                bool manualKept = false, autoBack = false;
                if (region != nullptr && panel != nullptr)
                {
                    auto& um = ed.getUndoManager();
                    um.beginNewTransaction ("Notes modifiables");
                    drummer::setAutomatic (*region, false);
                    auto& seq = region->getSequence();
                    if (auto* firstNote = seq.getNotes().getFirst()) seq.removeNote (*firstNote, &um);      // une retouche
                    const int edited = seq.getNumNotes();
                    pump (300);
                    panel->setStyle (2);                                                                     // sans effet en notes modifiables
                    manualKept = ! drummer::isAutomatic (region) && seq.getNumNotes() == edited && drummer::isDrummerRegion (region);
                    um.beginNewTransaction ("Automatique");
                    drummer::setAutomatic (*region, true);
                    autoBack = drummer::isAutomatic (region) && seq.getNumNotes() != edited;
                }
                check (manualKept && autoBack, U ("batteur : passage en notes modifiables (la retouche reste, les réglages ne refont rien) puis retour en automatique (jeu refait)"));
                if (panelWindow != nullptr) delete panelWindow;

                // Nouvelle piste : voix (chaîne « Voix pop »), instrument (Onde Synth, bibliothèque ouverte, non modale).
                auto* voiceTrack = NewTrackPanel::create (c4, NewTrackPanel::voice);
                auto* instTrack = NewTrackPanel::create (c4, NewTrackPanel::instrument);
                pump (400);
                juce::DialogWindow* libWindow = nullptr;
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (dynamic_cast<LibraryPanel*> (dw->getContentComponent()) != nullptr) libWindow = dw;
                ondefx::SynthPlugin* instSynth = nullptr;
                if (instTrack != nullptr) for (auto* p : instTrack->pluginList) if (auto* sp = dynamic_cast<ondefx::SynthPlugin*> (p)) instSynth = sp;
                check (voiceTrack != nullptr && chains::currentChain (*voiceTrack) == U ("Voix pop") && instSynth != nullptr
                       && libWindow != nullptr && ! libWindow->isCurrentlyModal() && chains::quickControls (*instTrack).size() >= 3,
                       U ("nouvelle piste : « Voix » avec la chaîne « ") + (voiceTrack ? chains::currentChain (*voiceTrack) : juce::String())
                       + U (" », instrument Onde Synth avec ") + juce::String (instTrack ? chains::quickControls (*instTrack).size() : 0)
                       + U (" commandes rapides, bibliothèque ouverte en fenêtre flottante"));
                {
                    NewTrackPanel cardsPanel (c4);
                    snapshot (cardsPanel, dir.getChildFile ("23-nouvelle-piste.png"));
                }
                if (libWindow != nullptr) delete libWindow;
                for (auto* tr : { drumTrack, voiceTrack, instTrack }) if (tr != nullptr) ed.deleteTrack (tr);
                c4.changed();
            }

            // Couloirs de contrôleurs MIDI dans l'éditeur de notes : expression et pitch bend dessinés, entendus au rendu.
            {
                auto* ccTrack = LibraryPanel::applyToTrack (c4, nullptr, *ondefx::SynthPlugin::findPreset ("Orgue doux"));
                ondefx::SynthPlugin* organ = nullptr;
                for (auto* p : ccTrack->pluginList) if (auto* sp = dynamic_cast<ondefx::SynthPlugin*> (p)) organ = sp;
                for (auto* id : { "vibrato", "sub", "mix2" }) organ->setStored (id, 0.0f);
                auto clip = ccTrack->insertMIDIClip ("cc", { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, nullptr);
                clip->getSequence().addNote (69, te::BeatPosition(), te::BeatDuration::fromBeats (8.0), 100, 0, nullptr);
                c4.selectTrack (ccTrack);
                c4.selectClip (clip.get());
                mc->showView (2);
                pump (400);
                std::function<PianoRollView* (juce::Component&)> findRoll = [&] (juce::Component& comp) -> PianoRollView*
                {
                    if (auto* r = dynamic_cast<PianoRollView*> (&comp)) return r;
                    for (auto* ch : comp.getChildren()) if (auto* r = findRoll (*ch)) return r;
                    return nullptr;
                };
                auto* roll = findRoll (*mc);
                auto countType = [&] (int type) { int k = 0; for (auto* ev : clip->getSequence().getControllerEvents()) if (ev->getType() == type) ++k; return k; };
                auto renderCc = [&] (const char* name) { double r = 0; const auto f = dir.getChildFile (juce::String (name) + ".wav"); renderEdit (ed, f, { ccTrack }); return readAll (f, r); };
                double quietDb = 0, loudDb = 0, bentHz = 0;
                int expressionEvents = 0, afterUndo = -1, kept = -1;
                if (roll != nullptr)
                {
                    auto& um = ed.getUndoManager();
                    um.beginNewTransaction ("Expression");
                    roll->setLane (11);
                    roll->drawControllerRamp (0.0, 3.9375, 0.0f, 0.0f);     // expression à 0 pendant 2 s…
                    roll->drawControllerRamp (4.0, 7.9375, 1.0f, 1.0f);     // …puis au maximum
                    expressionEvents = countType (11);
                    snapshot (*roll, dir.getChildFile ("24-couloir-expression.png"));
                    const auto expr = renderCc ("cc-expression");
                    quietDb = juce::Decibels::gainToDecibels (expr.getRMSLevel (0, 9600, 48000), -120.0f);
                    loudDb = juce::Decibels::gainToDecibels (expr.getRMSLevel (0, 2 * 48000 + 9600, 48000), -120.0f);
                    // Pitch bend au maximum (+2 demi-tons) : La 440 → Si 493,9 Hz.
                    um.beginNewTransaction ("Expression neutre");
                    roll->drawControllerRamp (0.0, 7.9375, 1.0f, 1.0f);
                    um.beginNewTransaction ("Pitch bend");
                    roll->setLane (te::MidiControllerEvent::pitchWheelType);
                    roll->drawControllerRamp (0.0, 7.9375, 1.0f, 1.0f);
                    const auto bent = renderCc ("cc-pitch-bend");
                    double first = -1, last = -1; int zc = 0;
                    for (int i = 9601; i < 57600; ++i)
                    {
                        const float a = bent.getSample (0, i - 1), c = bent.getSample (0, i);
                        if (a < 0 && c >= 0) { const double tz = i - 1 + a / (a - c); if (first < 0) first = tz; last = tz; ++zc; }
                    }
                    bentHz = zc > 1 ? (zc - 1) * 48000.0 / (last - first) : 0.0;
                    snapshot (*roll, dir.getChildFile ("25-couloir-pitch-bend.png"));
                    um.undo();
                    pump (200);
                    afterUndo = countType (te::MidiControllerEvent::pitchWheelType);
                    // Enregistré avec le projet.
                    const auto savedCc = dir.getChildFile ("controleurs.onde");
                    if (mc->saveProjectTo (savedCc))
                        if (auto reopened = MainComponent::loadProject (*engine, savedCc))
                        {
                            for (auto* rt : te::getAudioTracks (*reopened))
                                for (auto* c : rt->getClips())
                                    if (auto* rmc = dynamic_cast<te::MidiClip*> (c); rmc != nullptr && rmc->getName() == "cc")
                                    { kept = 0; for (auto* ev : rmc->getSequence().getControllerEvents()) if (ev->getType() == 11) ++kept; }
                            EditContext::destroyEdit (reopened);
                        }
                    roll->setLane (-1);
                }
                check (roll != nullptr && expressionEvents >= 2 && quietDb < -60.0 && loudDb > -30.0 && std::abs (bentHz - 493.88) < 1.5 && afterUndo == 0 && kept == countType (11),
                       U ("couloirs MIDI : expression dessinée (") + juce::String (expressionEvents) + U (" points) → ") + juce::String (quietDb, 0) + U (" dB puis ")
                       + juce::String (loudDb, 1) + U (" dB ; pitch bend au maximum → ") + juce::String (bentHz, 2) + U (" Hz (attendu 493,88) ; annuler retire le pitch bend ; ")
                       + juce::String (kept) + U (" points d'expression retrouvés après réouverture"));
                mc->showView (0);
                ed.deleteTrack (ccTrack);
                c4.changed();
            }

            // VCA : une piste dans un VCA à -12 dB sort 12 dB plus bas ; geler une piste garde son son.
            {
                auto* vcaSrc = c4.addAudioTrack();
                vcaSrc->setName ("Source VCA");
                vcaSrc->insertWaveClip ("fort", loud, { { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, te::TimeDuration() }, false);
                auto renderOne = [&] (te::Track* tr, const char* name) { double r = 0; const auto f = dir.getChildFile (juce::String (name) + ".wav"); renderEdit (ed, f, { tr }); return readAll (f, r); };
                const double before = toneDb (renderOne (vcaSrc, "vca-avant"), 48000, 1000);
                c4.selectTrack (vcaSrc);
                c4.perform ("track.addVCA");
                pump (300);
                auto* vcaFolder = vcaSrc->getParentFolderTrack();
                double after = 0;
                if (vcaFolder != nullptr && vcaFolder->getVCAPlugin() != nullptr)
                {
                    vcaFolder->getVCAPlugin()->setVolumeDb (-12.0f);
                    pump (200);
                    after = toneDb (renderOne (vcaSrc, "vca-moins-12"), 48000, 1000);
                }
                check (vcaFolder != nullptr && vcaFolder->getVCAPlugin() != nullptr && std::abs ((after - before) + 12.0) < 0.3 && std::abs (vcaSrc->getVolumePlugin()->getVolumeDb()) < 0.01,
                       U ("VCA : fader du dossier à -12 dB → la piste sort ") + juce::String (after - before, 2) + U (" dB, son propre fader reste à 0 dB"));
                c4.changed();
                pump (300);
                snapshot (*mc, dir.getChildFile ("26-vca.png"));

                // Geler : une piste Onde Synth figée en audio, même son ; dégeler la rend modifiable.
                auto* frozenTrack = LibraryPanel::applyToTrack (c4, nullptr, *ondefx::SynthPlugin::findPreset (U ("Piano électrique")));
                auto fclip = frozenTrack->insertMIDIClip ("gel", { te::TimePosition(), te::TimePosition::fromSeconds (3.0) }, nullptr);
                for (int p : { 57, 60, 64, 67 }) fclip->getSequence().addNote (p, te::BeatPosition(), te::BeatDuration::fromBeats (3.0), 100, 0, nullptr);
                const auto live = renderOne (frozenTrack, "gel-avant");
                c4.selectTrack (frozenTrack);
                c4.perform ("track.freeze");
                pump (500);
                const bool frozen = frozenTrack->isFrozen (te::Track::individualFreeze);
                const auto frozenBuf = renderOne (frozenTrack, "gel-apres");
                const double liveDb = juce::Decibels::gainToDecibels (live.getRMSLevel (0, 0, 96000), -120.0f);
                const double frozenDb = juce::Decibels::gainToDecibels (frozenBuf.getRMSLevel (0, 0, 96000), -120.0f);
                c4.perform ("track.freeze");
                pump (300);
                const bool thawed = ! frozenTrack->isFrozen (te::Track::individualFreeze);
                check (frozen && std::abs (liveDb - frozenDb) < 1.0 && liveDb > -40.0 && thawed,
                       U ("geler : piste gelée (") + juce::String (frozen ? "oui" : "non") + U ("), même son : ") + juce::String (liveDb, 1) + U (" dB avant, ")
                       + juce::String (frozenDb, 1) + U (" dB gelée ; dégelée (") + juce::String (thawed ? "oui" : "non") + ")");
                ed.deleteTrack (frozenTrack);
                if (vcaFolder != nullptr) ed.deleteTrack (vcaFolder);
                c4.changed();
            }

            // Paroles : texte avec titres de partie, calage au tapotement, prompteur, annuler, enregistrement.
            {
                c4.perform ("song.lyrics");
                pump (400);
                LyricsPanel* lp = nullptr;
                juce::DialogWindow* lw = nullptr;
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (auto* p = dynamic_cast<LyricsPanel*> (dw->getContentComponent())) { lp = p; lw = dw; }
                ed.getUndoManager().beginNewTransaction ("Paroles");
                lyrics::setText (ed, U ("[Couplet]\nLa nuit descend sur la ville\nOn marche sans se retourner\n\n[Refrain]\nEt la musique nous emporte"));
                pump (200);
                auto& tr = ed.getTransport();
                bool tapsOk = false, lineOk = false, undoOk = false, kept = false;
                if (lp != nullptr)
                {
                    lp->startTapping();
                    for (double b : { 0.0, 4.0, 8.0 }) { tr.setPosition (ed.tempoSequence.toTime (te::BeatPosition::fromBeats (b))); lp->tap(); }
                    const auto lines = lyrics::getLines (ed);
                    int sung = 0; double beats[3] { -1, -1, -1 };
                    for (auto& l : lines) if (! l.isHeading && sung < 3) beats[sung++] = l.beat;
                    tapsOk = lines.size() == 5 && sung == 3 && std::abs (beats[0]) < 1e-6 && std::abs (beats[1] - 4.0) < 1e-6 && std::abs (beats[2] - 8.0) < 1e-6;
                    lineOk = lyrics::lineAt (ed, 5.0) == 1 && lyrics::lineAt (ed, 9.0) == 2 && lyrics::lineAt (ed, -1.0) == -1;
                    tr.setPosition (ed.tempoSequence.toTime (te::BeatPosition::fromBeats (5.0)));
                    pump (300);
                    snapshot (*lp, dir.getChildFile ("29-paroles.png"));
                    ed.getUndoManager().undo();                          // dernier calage retiré
                    pump (100);
                    undoOk = lyrics::lineAt (ed, 9.0) == 1;
                    ed.getUndoManager().redo();
                    const auto savedLyrics = dir.getChildFile ("paroles.onde");
                    if (mc->saveProjectTo (savedLyrics))
                        if (auto reopened = MainComponent::loadProject (*engine, savedLyrics))
                        {
                            kept = lyrics::getText (*reopened).contains (U ("La nuit descend")) && lyrics::lineAt (*reopened, 9.0) == 2;
                            EditContext::destroyEdit (reopened);
                        }
                    mc->showView (0);
                    pump (300);
                    snapshot (*mc, dir.getChildFile ("30-paroles-projet.png"));
                }
                check (lp != nullptr && tapsOk && lineOk && undoOk && kept,
                       U ("paroles : 3 phrases calées en tapant Entrée (temps 1, 5, 9), prompteur sur la bonne phrase, annuler retire le dernier calage, retrouvées après réouverture"));
                if (lw != nullptr) delete lw;
                tr.setPosition (te::TimePosition());
            }

            // Tablature : écrire des cases, changer de corde, deux chiffres, doigté auto, texte, basse, son au rendu.
            {
                auto* gt = LibraryPanel::applyToTrack (c4, nullptr, *ondefx::SynthPlugin::findPreset ("Pluck"));
                gt->setName ("Guitare tab");
                auto tclip = gt->insertMIDIClip ("riff", { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, nullptr);
                c4.selectTrack (gt);
                c4.selectClip (tclip.get());
                mc->showView (4);
                pump (300);
                std::function<TabView* (juce::Component&)> findTab = [&] (juce::Component& comp) -> TabView*
                {
                    if (auto* r = dynamic_cast<TabView*> (&comp)) return r;
                    for (auto* ch : comp.getChildren()) if (auto* r = findTab (*ch)) return r;
                    return nullptr;
                };
                auto* tab = findTab (*mc);
                bool placed = false, restrung = false, twoDigits = false, ascii = false, bass = false, sounds = false;
                if (tab != nullptr)
                {
                    tab->setTuning (0);
                    auto* n1 = tab->writeFret (5, 0.0, 3);         // Mi grave, case 3 → Sol (43)
                    auto* n2 = tab->writeFret (4, 0.5, 2);         // La, case 2 → Si (47)
                    auto* n3 = tab->writeFret (0, 1.0, 0);         // Mi aigu à vide (64)
                    placed = n1 && n2 && n3 && n1->getNoteNumber() == 43 && n2->getNoteNumber() == 47 && n3->getNoteNumber() == 64
                             && tab->placeOf (*n1) == std::make_pair (5, 3) && tab->placeOf (*n3) == std::make_pair (0, 0);
                    // Même note sur la corde de Si : case 5.
                    tab->moveCaret (0, 1.0);
                    tab->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
                    restrung = n3->getNoteNumber() == 64 && tab->placeOf (*n3) == std::make_pair (1, 5);
                    // Deux chiffres tapés vite : case 12 sur la corde de Sol (55 + 12 = 67).
                    tab->moveCaret (2, 1.5);
                    tab->keyPressed (juce::KeyPress ('1', {}, '1'));
                    tab->keyPressed (juce::KeyPress ('2', {}, '2'));
                    int at15 = -1;
                    for (auto* n : tclip->getSequence().getNotes()) if (std::abs (n->getStartBeat().inBeats() - 1.5) < 1e-6) at15 = n->getNoteNumber();
                    twoDigits = at15 == 67;
                    snapshot (*tab, dir.getChildFile ("31-tablature.png"));
                    const auto text = tab->asciiTab();
                    ascii = juce::StringArray::fromLines (text).size() >= 7 && text.contains ("12") && text.contains ("|");
                    std::printf ("%s\n", text.toRawUTF8());
                    double r = 0; const auto f = dir.getChildFile ("tablature.wav");
                    renderEdit (ed, f, { gt });
                    const auto b = readAll (f, r);
                    sounds = juce::Decibels::gainToDecibels (b.getRMSLevel (0, 0, juce::jmin (b.getNumSamples(), 96000))) > -45.0f;
                    tab->autoFingering();
                    tab->setTuning (6);                          // basse 4 cordes : Sol grave (43) = corde de Sol à vide
                    bass = tab->placeOf (*n1) == std::make_pair (0, 0);
                    tab->setTuning (0);
                }
                check (tab != nullptr && placed && restrung && twoDigits && ascii && bass && sounds,
                       U ("tablature : cases écrites (Mi grave 3 = Sol), même note passée sur la corde de Si (case 5), « 12 » tapé = case 12, texte copiable, accordage basse, son au rendu"));
                mc->showView (0);
                ed.deleteTrack (gt);
                c4.changed();
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
