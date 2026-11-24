#include "StemSeparator.h"
#include "AudioEditorView.h"

namespace stems
{
    const std::array<Stem, 4>& list()
    {
        static const std::array<Stem, 4> l { { { "vocals", "Voix", 0xffe8697a }, { "drums", "Batterie", 0xffe5a04e },
                                                { "bass", "Basse", 0xff6ea8fe }, { "other", "Autres", 0xff8fcf6a } } };
        return l;
    }

    juce::File executable()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile (
               #if JUCE_WINDOWS
                "onde-stems.exe"
               #else
                "onde-stems"
               #endif
               );
    }

    juce::File modelsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("modeles-demucs");
    }

    bool isAvailable (juce::String& why)
    {
        if (! executable().existsAsFile()) { why = U ("moteur de séparation absent (onde-stems)"); return false; }
        for (auto& s : list())
            if (! modelsFolder().getChildFile (juce::String ("ggml-model-htdemucs_ft_") + s.id + "-4s-f16.bin").existsAsFile())
            { why = U ("modèle Demucs manquant : ") + s.id; return false; }
        return true;
    }

    bool resampleTo44k (const juce::File& source, double startSeconds, double lengthSeconds, bool reversed,
                        const juce::File& wav44k, juce::String& error)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (source));
        if (r == nullptr) { error = U ("fichier source illisible"); return false; }
        const double srcRate = r->sampleRate;
        const auto start = juce::jlimit<juce::int64> (0, r->lengthInSamples, (juce::int64) std::llround (startSeconds * srcRate));
        const auto num = (int) juce::jmin<juce::int64> (r->lengthInSamples - start, (juce::int64) std::llround (lengthSeconds * srcRate));
        if (num < (int) (srcRate * 0.5)) { error = U ("événement trop court (0,5 s minimum)"); return false; }
        juce::AudioBuffer<float> in (2, num);
        r->read (&in, 0, num, start, true, true);                       // mono : dupliqué sur les deux canaux
        if (r->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, num);
        if (reversed) in.reverse (0, num);

        const double ratio = srcRate / 44100.0;
        const int outNum = (int) std::ceil (num / ratio);
        juce::AudioBuffer<float> out (2, outNum);
        if (std::abs (ratio - 1.0) < 1.0e-9)
            out.makeCopyOf (in);
        else
            for (int c = 0; c < 2; ++c)
            {
                juce::WindowedSincInterpolator interp;                    // sinc fenêtré : pas de repliement audible
                // marge de fin : l'interpolateur lit quelques échantillons au-delà
                std::vector<float> padded ((size_t) num + 256, 0.0f);
                std::copy (in.getReadPointer (c), in.getReadPointer (c) + num, padded.begin());
                interp.process (ratio, padded.data(), out.getWritePointer (c), outNum);
            }

        wav44k.getParentDirectory().createDirectory();
        wav44k.deleteFile();
        std::unique_ptr<juce::OutputStream> os (wav44k.createOutputStream());
        if (os == nullptr) { error = U ("écriture impossible : ") + wav44k.getFullPathName(); return false; }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 44100.0, 2, 32, {}, 0));
        if (w == nullptr) { error = U ("encodeur WAV indisponible"); return false; }
        os.release();
        return w->writeFromAudioSampleBuffer (out, 0, outNum);
    }

    bool writeClipInput (te::WaveAudioClip& c, const juce::File& wav44k, juce::String& error)
    {
        const auto pos = c.getPosition();
        return resampleTo44k (AudioEditorView::sourceFileOf (c), pos.getOffset().inSeconds(),
                              pos.getLength().inSeconds() * c.getSpeedRatio(), c.getIsReversed(), wav44k, error);
    }

    bool separateFile (const juce::File& wav44k, const juce::File& outDir, std::function<bool (float)> progress,
                       juce::String& error, int threads, const juce::String& mode)
    {
        juce::String why;
        if (! isAvailable (why)) { error = why; return false; }
        if (threads <= 0) threads = juce::jmax (1, juce::SystemStats::getNumCpus());
        for (auto& s : list()) outDir.getChildFile (juce::String (s.id) + ".wav").deleteFile();

        juce::ChildProcess p;
        if (! p.start (juce::StringArray { executable().getFullPathName(), modelsFolder().getFullPathName(),
                                           wav44k.getFullPathName(), outDir.getFullPathName(), juce::String (threads), mode },
                       juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        { error = U ("impossible de lancer le moteur de séparation"); return false; }

        juce::String pending, lastError;
        bool done = false;
        char buf[512];
        for (;;)
        {
            const int n = p.readProcessOutput (buf, sizeof (buf));
            if (n <= 0) { if (! p.isRunning()) break; juce::Thread::sleep (20); continue; }
            pending += juce::String::fromUTF8 (buf, n);
            while (pending.containsChar ('\n'))
            {
                const auto line = pending.upToFirstOccurrenceOf ("\n", false, false).trim();
                pending = pending.fromFirstOccurrenceOf ("\n", false, false);
                if (line.startsWith ("PROGRESS "))
                {
                    if (progress && ! progress (line.substring (9).getFloatValue()))
                    {
                        p.kill();
                        error = U ("séparation annulée");
                        return false;
                    }
                }
                else if (line.startsWith ("ERROR ")) lastError = line.substring (6);
                else if (line == "DONE") done = true;
            }
        }
        if (! done) { error = lastError.isNotEmpty() ? lastError : U ("le moteur de séparation s'est arrêté (code ") + juce::String ((int) p.getExitCode()) + ")"; return false; }
        for (auto& s : list())
            if (! outDir.getChildFile (juce::String (s.id) + ".wav").existsAsFile()) { error = U ("source manquante : ") + s.id; return false; }
        if (progress) progress (1.0f);
        return true;
    }

    juce::File workFolderFor (te::Edit& edit, const juce::String& clipName)
    {
        auto base = edit.editFileRetriever ? edit.editFileRetriever().getParentDirectory() : juce::File();
        if (base == juce::File() || ! base.isDirectory()) base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Onde");
        return base.getChildFile (U ("Séparations")).getNonexistentChildFile (juce::File::createLegalFileName (clipName), {}, true);
    }

    te::FolderTrack* importStems (EditContext& ctx, te::WaveAudioClip& clip, const juce::File& outDir, juce::String& error)
    {
        auto& edit = clip.edit;
        auto* srcTrack = dynamic_cast<te::AudioTrack*> (clip.getTrack());
        if (srcTrack == nullptr) { error = U ("piste introuvable"); return nullptr; }
        const auto pos = clip.getPosition();
        const auto clipName = clip.getName();
        const float gainDb = clip.getGainDB();
        const auto fadeIn = clip.getFadeIn(), fadeOut = clip.getFadeOut();
        const auto fadeInType = clip.getFadeInType(), fadeOutType = clip.getFadeOutType();
        const float pitch = clip.getPitchChange();

        edit.getUndoManager().beginNewTransaction (U ("Séparer les sources"));
        auto folder = edit.insertNewFolderTrack (te::TrackInsertPoint (*srcTrack, false), nullptr, false);
        if (folder == nullptr) { error = U ("création du dossier impossible"); return nullptr; }
        folder->setName (clipName + U (" (séparé)"));
        folder->setColour (juce::Colour (0xffb08a4a));
        te::Track* previous = nullptr;
        for (auto& s : list())
        {
            // Nom unique par séparation (« drums - Mix(2).wav ») : une séparation refaite sur le même événement ne
            // reprend jamais les fichiers (ou copies en cache) d'une séparation précédente de même nom et même taille.
            auto f = outDir.getChildFile (juce::String (s.id) + ".wav");
            const auto unique = outDir.getChildFile (juce::String (s.id) + " - " + outDir.getFileName() + ".wav");
            if (f.existsAsFile() && ! unique.existsAsFile() && f.moveFileTo (unique)) f = unique;
            else if (unique.existsAsFile()) f = unique;
            auto t = edit.insertNewAudioTrack (te::TrackInsertPoint (folder.get(), previous), nullptr);
            if (t == nullptr) continue;
            t->setName (U (s.label));
            t->setColour (juce::Colour (s.colour));
            previous = t.get();
            if (auto c = t->insertWaveClip (U (s.label) + " - " + clipName, f,
                                            { { pos.getStart(), pos.getEnd() }, te::TimeDuration() }, false))
            {
                c->setGainDB (gainDb);
                c->setFadeIn (fadeIn);   c->setFadeInType (fadeInType);
                c->setFadeOut (fadeOut); c->setFadeOutType (fadeOutType);
                if (std::abs (pitch) > 0.001f) c->setPitchChange (pitch);
            }
        }
        clip.setMuted (true);
        ctx.selectFolder (folder.get());
        ctx.changed();
        return folder.get();
    }

    //==============================================================================
    namespace
    {
        class ProgressPanel  : public juce::Component, private juce::Timer, private juce::Thread
        {
        public:
            ProgressPanel (EditContext& c, te::WaveAudioClip& clip)
                : juce::Thread ("Onde separation"), ctx (c), edit (clip.edit), clipID (clip.itemID), clipName (clip.getName())
            {
                title.setText (U ("Séparation de « ") + clipName + U (" » en voix, batterie, basse et autres"), juce::dontSendNotification);
                title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
                info.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
                // Durées estimées à partir de mesures sur un processeur 4 cœurs, ramenées au nombre de cœurs de ce PC.
                const double secs = clip.getPosition().getLength().inSeconds();
                const double coreScale = 4.0 / juce::jmax (1, juce::SystemStats::getNumPhysicalCpus());
                auto minutes = [&] (double factor) { return juce::String (juce::jmax (1, juce::roundToInt (secs * factor * coreScale / 60.0))); };
                quality.addItem (U ("Très précis : Demucs v4 fine-tuned, 4 modèles spécialisés (≈ ") + minutes (preciseFactor) + " min)", 1);
                quality.addItem (U ("Rapide : 1 modèle pour les 4 sources (≈ ") + minutes (fastFactor) + " min)", 2);
                quality.setSelectedId (lastQuality, juce::dontSendNotification);
                info.setText (U ("Le calcul tourne à part : vous pouvez continuer à travailler."), juce::dontSendNotification);
                addAndMakeVisible (title); addAndMakeVisible (info); addAndMakeVisible (quality); addAndMakeVisible (go); addAndMakeVisible (cancel);
                addChildComponent (bar);
                go.onClick = [this] { start(); };
                cancel.onClick = [this]
                {
                    if (finished || ! isThreadRunning()) { if (! startedWork || finished) closeWindow(); return; }
                    cancelled = true;
                    info.setText (U ("Annulation…"), juce::dontSendNotification);
                };
                setSize (560, 170);
            }

            void start()
            {
                lastQuality = quality.getSelectedId();
                mode = lastQuality == 2 ? "rapide" : "precis";
                quality.setVisible (false); go.setVisible (false); bar.setVisible (true);
                info.setText (U ("Préparation…"), juce::dontSendNotification);
                startedWork = true;
                started = juce::Time::getMillisecondCounterHiRes();
                workDir = workFolderFor (edit, clipName);
                auto* clip = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (edit, clipID));
                if (clip == nullptr) { error = U ("événement introuvable"); finished = true; failed = true; }
                else if (! writeClipInput (*clip, workDir.getChildFile ("source-44k.wav"), error)) { finished = true; failed = true; }
                else startThread();
                startTimerHz (5);
            }

            static constexpr double preciseFactor = 14.0, fastFactor = 7.0;   // temps de calcul / durée de l'audio
            static inline int lastQuality = 1;

            ~ProgressPanel() override { cancelled = true; stopThread (10000); }

            void resized() override
            {
                auto r = getLocalBounds().reduced (16);
                title.setBounds (r.removeFromTop (22)); r.removeFromTop (6);
                info.setBounds (r.removeFromTop (20)); r.removeFromTop (10);
                auto row = r.removeFromTop (26);
                bar.setBounds (row.withHeight (22));
                quality.setBounds (row);
                r.removeFromTop (14);
                auto buttons = r.removeFromTop (28);
                cancel.setBounds (buttons.removeFromRight (110));
                buttons.removeFromRight (8);
                go.setBounds (buttons.removeFromRight (110));
            }

        private:
            void run() override
            {
                const bool ok = separateFile (workDir.getChildFile ("source-44k.wav"), workDir,
                                              [this] (float p) { progress = (double) p; return ! cancelled && ! threadShouldExit(); }, error, 0, mode);
                failed = ! ok;
                finished = true;
            }

            void timerCallback() override
            {
                if (! finished)
                {
                    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
                    juce::String t = U ("Analyse en cours : ") + juce::String (juce::roundToInt (progress * 100.0)) + " %";
                    if (progress > 0.03)
                        t << U (" — reste environ ") << juce::String (juce::jmax (1, juce::roundToInt (elapsed * (1.0 - progress) / progress / 60.0))) << " min";
                    if (! cancelled) info.setText (t, juce::dontSendNotification);
                    return;
                }
                stopTimer();
                if (failed)
                {
                    if (ctx.setStatus) ctx.setStatus (U ("Séparation : ") + error);
                    if (cancelled) { closeWindow(); return; }
                    info.setText (U ("Échec : ") + error, juce::dontSendNotification);
                    cancel.setButtonText ("Fermer");
                    return;
                }
                auto* clip = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (edit, clipID));
                if (clip == nullptr) { info.setText (U ("L'événement a été supprimé entre-temps."), juce::dontSendNotification); cancel.setButtonText ("Fermer"); return; }
                juce::String err;
                if (importStems (ctx, *clip, workDir, err) != nullptr)
                {
                    if (ctx.setStatus) ctx.setStatus (U ("« ") + clipName + U (" » séparé en 4 pistes (voix, batterie, basse, autres)"));
                    closeWindow();
                }
                else { info.setText (U ("Import impossible : ") + err, juce::dontSendNotification); cancel.setButtonText ("Fermer"); }
            }

            void closeWindow()
            {
                if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0);
                juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<juce::Component> (findParentComponentOfClass<juce::DialogWindow>())]
                                                 { delete sp.getComponent(); });
            }

            EditContext& ctx;
            te::Edit& edit;
            te::EditItemID clipID;
            juce::String clipName, error;
            juce::File workDir;
            double progress = 0.0, started = 0.0;
            std::atomic<bool> cancelled { false }, finished { false }, failed { false };
            juce::Label title, info;
            juce::ProgressBar bar { progress };
            juce::TextButton cancel { "Annuler" }, go { U ("Séparer") };
            juce::ComboBox quality;
            juce::String mode { "precis" };
            bool startedWork = false;
        };
    }

    void separateSelectedClip (EditContext& ctx)
    {
        auto* clip = dynamic_cast<te::WaveAudioClip*> (ctx.getSelectedClip());
        if (clip == nullptr) { if (ctx.setStatus) ctx.setStatus (U ("Sélectionnez un événement audio à séparer")); return; }
        juce::String why;
        if (! isAvailable (why)) { if (ctx.setStatus) ctx.setStatus (U ("Séparation indisponible : ") + why); return; }
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new ProgressPanel (ctx, *clip));
        o.dialogTitle = U ("Séparer les sources");
        o.dialogBackgroundColour = juce::Colour (0xff23262b);
        o.escapeKeyTriggersCloseButton = false;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }
}
