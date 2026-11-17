#include "Exporter.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #pragma comment (lib, "mfplat.lib")
 #pragma comment (lib, "mfreadwrite.lib")
 #pragma comment (lib, "mfuuid.lib")
#endif

juce::String ExportSettings::extension() const
{
    switch (format)
    {
        case Format::flac: return ".flac";
        case Format::mp3:  return ".mp3";
        case Format::ogg:  return ".ogg";
        case Format::wav:  break;
    }
    return ".wav";
}

juce::String ExportSettings::describe() const
{
    juce::String s;
    switch (format)
    {
        case Format::wav:  s << "WAV " << (bitDepth == 32 ? juce::String ("32 bits flottant") : juce::String (bitDepth) + " bits"); break;
        case Format::flac: s << "FLAC " << bitDepth << " bits"; break;
        case Format::mp3:  s << "MP3 " << mp3Kbps << " kbit/s"; break;
        case Format::ogg:  s << "OGG Vorbis q" << oggQuality; break;
    }
    s << ", " << juce::String (sampleRate / 1000.0, 1) << " kHz";
    if (normalise == Normalise::peak) s << U (", normalisé à ") << juce::String (normaliseLevel, 1) << U (" dB crête");
    if (normalise == Normalise::lufs) s << U (", normalisé à ") << juce::String (normaliseLevel, 0) << " LUFS";
    return s;
}

namespace exporter
{
#if JUCE_WINDOWS
    namespace
    {
        template <typename T> struct ComRelease { T* p = nullptr; ~ComRelease() { if (p) p->Release(); } T** operator&() { return &p; } T* operator->() { return p; } };
    }

    bool encodeMp3 (const juce::File& wav, const juce::File& mp3, int kbps, juce::String& error)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wav));
        if (reader == nullptr) { error = U ("WAV intermédiaire illisible"); return false; }
        const int sr = (int) reader->sampleRate;
        if (sr != 44100 && sr != 48000 && sr != 32000) { error = U ("le MP3 exige 32, 44,1 ou 48 kHz"); return false; }
        const UINT32 channels = juce::jlimit (1u, 2u, reader->numChannels);

        const bool comOk = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED)) ;
        if (FAILED (MFStartup (MF_VERSION))) { error = U ("Media Foundation indisponible"); if (comOk) CoUninitialize(); return false; }
        bool ok = false;
        {
            mp3.deleteFile();
            ComRelease<IMFSinkWriter> writer;
            ComRelease<IMFMediaType> outType, inType;
            DWORD stream = 0;
            HRESULT hr = MFCreateSinkWriterFromURL (mp3.getFullPathName().toWideCharPointer(), nullptr, nullptr, &writer);
            if (SUCCEEDED (hr)) hr = MFCreateMediaType (&outType);
            if (SUCCEEDED (hr)) hr = outType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED (hr)) hr = outType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_MP3);
            if (SUCCEEDED (hr)) hr = outType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) sr);
            if (SUCCEEDED (hr)) hr = outType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, channels);
            if (SUCCEEDED (hr)) hr = outType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (juce::jlimit (128, 320, kbps) * 1000 / 8));
            if (SUCCEEDED (hr)) hr = writer->AddStream (outType.p, &stream);
            if (SUCCEEDED (hr)) hr = MFCreateMediaType (&inType);
            if (SUCCEEDED (hr)) hr = inType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED (hr)) hr = inType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED (hr)) hr = inType->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED (hr)) hr = inType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) sr);
            if (SUCCEEDED (hr)) hr = inType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, channels);
            if (SUCCEEDED (hr)) hr = inType->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
            if (SUCCEEDED (hr)) hr = inType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) sr * channels * 2);
            if (SUCCEEDED (hr)) hr = writer->SetInputMediaType (stream, inType.p, nullptr);
            if (SUCCEEDED (hr)) hr = writer->BeginWriting();

            const int block = 4096;
            juce::AudioBuffer<float> buf ((int) channels, block);
            LONGLONG time = 0;
            for (juce::int64 pos = 0; SUCCEEDED (hr) && pos < reader->lengthInSamples; pos += block)
            {
                const int n = (int) juce::jmin<juce::int64> (block, reader->lengthInSamples - pos);
                reader->read (&buf, 0, n, pos, true, channels > 1);
                ComRelease<IMFMediaBuffer> mbuf;
                ComRelease<IMFSample> sample;
                const DWORD bytes = (DWORD) n * channels * 2;
                hr = MFCreateMemoryBuffer (bytes, &mbuf);
                BYTE* data = nullptr;
                if (SUCCEEDED (hr)) hr = mbuf->Lock (&data, nullptr, nullptr);
                if (SUCCEEDED (hr))
                {
                    auto* out = reinterpret_cast<int16_t*> (data);
                    for (int i = 0; i < n; ++i)
                        for (UINT32 c = 0; c < channels; ++c)
                            *out++ = (int16_t) juce::jlimit (-32768, 32767, juce::roundToInt (buf.getSample ((int) c, i) * 32767.0f));
                    mbuf->Unlock();
                    hr = mbuf->SetCurrentLength (bytes);
                }
                if (SUCCEEDED (hr)) hr = MFCreateSample (&sample);
                if (SUCCEEDED (hr)) hr = sample->AddBuffer (mbuf.p);
                const LONGLONG dur = (LONGLONG) n * 10000000LL / sr;
                if (SUCCEEDED (hr)) hr = sample->SetSampleTime (time);
                if (SUCCEEDED (hr)) hr = sample->SetSampleDuration (dur);
                if (SUCCEEDED (hr)) hr = writer->WriteSample (stream, sample.p);
                time += dur;
            }
            if (SUCCEEDED (hr)) hr = writer->Finalize();
            ok = SUCCEEDED (hr) && mp3.existsAsFile() && mp3.getSize() > 1000;
            if (! ok) error = U ("encodage MP3 refusé par Windows (code 0x") + juce::String::toHexString ((int) hr) + ")";
        }
        MFShutdown();
        if (comOk) CoUninitialize();
        return ok;
    }
#else
    bool encodeMp3 (const juce::File&, const juce::File&, int, juce::String& error) { error = U ("MP3 disponible sous Windows uniquement"); return false; }
#endif

    namespace
    {
        bool renderOne (te::Edit& edit, const juce::File& file, const ExportSettings& s, juce::Array<te::Track*> tracks, juce::String& error)
        {
            auto& afm = edit.engine.getAudioFileFormatManager();
            te::Renderer::Parameters p (edit);
            const bool mp3 = s.format == ExportSettings::Format::mp3;
            const auto target = mp3 ? file.getSiblingFile (file.getFileNameWithoutExtension() + "-onde-tmp.wav") : file;
            p.destFile = target;
            switch (s.format)
            {
                case ExportSettings::Format::flac: p.audioFormat = afm.getFlacFormat(); p.bitDepth = juce::jmin (24, s.bitDepth); break;
                case ExportSettings::Format::ogg:  p.audioFormat = afm.getOggFormat();  p.bitDepth = 16; p.quality = s.oggQuality; break;
                case ExportSettings::Format::mp3:  p.audioFormat = afm.getWavFormat();  p.bitDepth = 16; break;
                case ExportSettings::Format::wav:  p.audioFormat = afm.getWavFormat();  p.bitDepth = s.bitDepth; break;
            }
            if (p.audioFormat == nullptr) { error = U ("format non disponible dans cette version"); return false; }
            p.sampleRateForAudio = s.sampleRate;
            p.blockSizeForAudio = 512;
            p.ditheringEnabled = s.dither && p.bitDepth < 32;
            p.shouldNormalise = s.normalise == ExportSettings::Normalise::peak;
            p.shouldNormaliseByLUFS = s.normalise == ExportSettings::Normalise::lufs;
            p.normaliseToLevelDb = s.normaliseLevel;
            auto& tr = edit.getTransport();
            const auto range = s.cycleOnly && tr.getLoopRange().getLength().inSeconds() > 0.1
                                 ? tr.getLoopRange()
                                 : te::TimeRange (te::TimePosition(), te::TimePosition() + edit.getLength());
            p.time = range;
            p.endAllowance = te::TimeDuration::fromSeconds (juce::jmax (0.0, s.tailSeconds));
            p.usePlugins = true;
            p.tracksToDo = te::toBitSet (tracks);
            target.deleteFile();
            if (! te::Renderer::renderToFile (U ("Export"), p).existsAsFile()) { error = U ("le rendu a échoué"); return false; }
            if (mp3)
            {
                const bool ok = encodeMp3 (target, file, s.mp3Kbps, error);
                target.deleteFile();
                return ok;
            }
            return true;
        }
    }

    bool exportMix (te::Edit& edit, const juce::File& dest, const ExportSettings& s, juce::StringArray& written, juce::String& error)
    {
        EditContext::prepareAudioProxies (edit);
        const auto file = dest.withFileExtension (s.extension());
        file.getParentDirectory().createDirectory();
        if (! s.separateTracks)
        {
            if (! renderOne (edit, file, s, te::getAllTracks (edit), error)) return false;
            written.add (file.getFullPathName());
            return true;
        }
        // Une piste par fichier (pistes audio et instrument, groupes exclus : leurs pistes sont exportées une à une).
        int n = 0;
        for (auto* t : te::getAudioTracks (edit))
        {
            if (t->getClips().isEmpty()) continue;           // retours d'effets : pas de source propre
            juce::Array<te::Track*> one { t };
            const auto name = juce::File::createLegalFileName (juce::String (++n).paddedLeft ('0', 2) + " " + t->getName());
            const auto f = file.getSiblingFile (file.getFileNameWithoutExtension() + " - " + name).withFileExtension (s.extension());
            if (! renderOne (edit, f, s, one, error)) { error = t->getName() + " : " + error; return false; }
            written.add (f.getFullPathName());
        }
        return n > 0;
    }

    //==============================================================================
    class Dialog  : public juce::Component
    {
    public:
        Dialog (std::function<void (const ExportSettings&)> cb) : onExport (std::move (cb))
        {
            auto addBox = [this] (juce::ComboBox& b, juce::Label& l, const juce::String& text)
            {
                addAndMakeVisible (b); addAndMakeVisible (l);
                l.setText (text, juce::dontSendNotification); l.setFont (theme::ui (13.0f)); l.setColour (juce::Label::textColourId, theme::text);
                b.onChange = [this] { update(); };
            };
            addBox (format, formatL, "Format");
            format.addItem ("WAV", 1); format.addItem ("FLAC (sans perte)", 2); format.addItem ("MP3", 3); format.addItem ("OGG Vorbis", 4);
            addBox (depth, depthL, U ("Résolution"));
            addBox (rate, rateL, U ("Fréquence"));
            rate.addItem ("44,1 kHz", 1); rate.addItem ("48 kHz", 2); rate.addItem ("96 kHz", 3);
            addBox (kbps, kbpsL, U ("Débit MP3"));
            for (int k : { 128, 160, 192, 224, 256, 320 }) kbps.addItem (juce::String (k) + " kbit/s", k);
            addBox (norm, normL, "Normalisation");
            norm.addItem ("Aucune", 1); norm.addItem (U ("Crête à -0,1 dB"), 2); norm.addItem (U ("-14 LUFS (streaming)"), 3); norm.addItem (U ("-23 LUFS (diffusion)"), 4);
            addBox (range, rangeL, "Zone");
            range.addItem ("Tout le projet", 1); range.addItem (U ("Entre les délimiteurs (cycle)"), 2);
            addAndMakeVisible (dither); dither.setToggleState (true, juce::dontSendNotification);
            addAndMakeVisible (separate);
            addAndMakeVisible (tail); tail.setToggleState (true, juce::dontSendNotification);
            for (auto* t : { &dither, &separate, &tail }) t->setColour (juce::ToggleButton::textColourId, theme::text);
            addAndMakeVisible (go); styleButton (go, true);
            addAndMakeVisible (cancel); styleButton (cancel);
            go.onClick = [this]
            {
                ExportSettings s = current();
                if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (1);
                juce::MessageManager::callAsync ([cb = onExport, s] { cb (s); });
            };
            cancel.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0); };
            format.setSelectedId (1, juce::dontSendNotification);
            rate.setSelectedId (2, juce::dontSendNotification);
            kbps.setSelectedId (320, juce::dontSendNotification);
            norm.setSelectedId (1, juce::dontSendNotification);
            range.setSelectedId (1, juce::dontSendNotification);
            update();
            setSize (460, 360);
        }

        ExportSettings current() const
        {
            ExportSettings s;
            s.format = (ExportSettings::Format) (format.getSelectedId() - 1);
            s.bitDepth = depth.getSelectedId() > 0 ? depth.getSelectedId() : 24;
            s.sampleRate = rate.getSelectedId() == 1 ? 44100.0 : rate.getSelectedId() == 3 ? 96000.0 : 48000.0;
            if (s.format == ExportSettings::Format::mp3 && s.sampleRate > 48000.0) s.sampleRate = 48000.0;
            s.mp3Kbps = kbps.getSelectedId();
            s.normalise = norm.getSelectedId() == 1 ? ExportSettings::Normalise::none : norm.getSelectedId() == 2 ? ExportSettings::Normalise::peak : ExportSettings::Normalise::lufs;
            s.normaliseLevel = norm.getSelectedId() == 2 ? -0.1f : norm.getSelectedId() == 3 ? -14.0f : -23.0f;
            s.cycleOnly = range.getSelectedId() == 2;
            s.dither = dither.getToggleState();
            s.separateTracks = separate.getToggleState();
            s.tailSeconds = tail.getToggleState() ? 2.0 : 0.0;
            return s;
        }

        void update()
        {
            const int f = format.getSelectedId();
            const int previous = depth.getSelectedId();
            depth.clear (juce::dontSendNotification);
            if (f == 1) { depth.addItem ("16 bits", 16); depth.addItem ("24 bits", 24); depth.addItem ("32 bits flottant", 32); }
            if (f == 2) { depth.addItem ("16 bits", 16); depth.addItem ("24 bits", 24); }
            depth.setSelectedId (previous == 16 || previous == 24 || (previous == 32 && f == 1) ? previous : 24, juce::dontSendNotification);
            depth.setEnabled (f == 1 || f == 2);
            kbps.setEnabled (f == 3);
            if (f == 3 && rate.getSelectedId() == 3) rate.setSelectedId (2, juce::dontSendNotification);
            summary = current().describe();
            repaint();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            r.removeFromTop (6);
            for (auto row : { std::pair<juce::Label*, juce::ComboBox*> { &formatL, &format }, { &depthL, &depth }, { &rateL, &rate }, { &kbpsL, &kbps }, { &normL, &norm }, { &rangeL, &range } })
            {
                auto line = r.removeFromTop (28); r.removeFromTop (6);
                row.first->setBounds (line.removeFromLeft (120));
                row.second->setBounds (line);
            }
            dither.setBounds (r.removeFromTop (22));
            tail.setBounds (r.removeFromTop (22));
            separate.setBounds (r.removeFromTop (22));
            auto b = r.removeFromBottom (30);
            go.setBounds (b.removeFromRight (130)); b.removeFromRight (8);
            cancel.setBounds (b.removeFromRight (100));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::panel);
            g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
            g.drawText (summary, 16, getHeight() - 46 - 18, getWidth() - 32, 16, juce::Justification::centredLeft, true);
        }

    private:
        std::function<void (const ExportSettings&)> onExport;
        juce::ComboBox format, depth, rate, kbps, norm, range;
        juce::Label formatL, depthL, rateL, kbpsL, normL, rangeL;
        juce::ToggleButton dither { U ("Dithering (sorties 16 / 24 bits)") }, tail { U ("Ajouter 2 s de queue (réverbérations, délais)") },
                           separate { U ("Une piste par fichier (stems)") };
        juce::TextButton go { U ("Exporter…") }, cancel { "Annuler" };
        juce::String summary;
    };

    void showDialog (EditContext&, const juce::String&, std::function<void (const ExportSettings&)> onExport)
    {
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new Dialog (std::move (onExport)));
        o.dialogTitle = U ("Exporter le mixage audio");
        o.dialogBackgroundColour = theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }
}
