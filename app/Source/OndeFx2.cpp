#include "OndeFx2.h"
#include "ParamControls.h"
#include "Theme.h"

namespace ondefx
{
    namespace
    {
        juce::String dbText (float v)  { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
        juce::String pctText (float v) { return juce::String (juce::roundToInt (v)) + " %"; }
        juce::String hzText (float v)  { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
        juce::String msText (float v)  { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms"; }
        juce::String onOffText (float v) { return v > 0.5f ? "oui" : "non"; }
        juce::NormalisableRange<float> logRange (float lo, float hi) { juce::NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi)); return r; }
        std::function<juce::String (float)> choiceText (const char* const* names, int n)
        {
            return [names, n] (float v) { return juce::String::fromUTF8 (names[juce::jlimit (0, n - 1, juce::roundToInt (v))]); };
        }
        float dbToGain (float d) { return std::pow (10.0f, d / 20.0f); }
        float onePoleCoef (float hz, double fs) { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) fs); }
        constexpr int maxBlock = 8192;
    }

    //==============================================================================
    const char* SaturationPlugin::xmlTypeName = "ondeSaturation";

    SaturationPlugin::SaturationPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        declare ("drive", "Saturation", { 0.0f, 36.0f }, 12.0f, dbText);
        declare ("character", juce::String::fromUTF8 ("Caractère"), { 0.0f, 3.0f, 1.0f }, 1.0f, choiceText (characterNames, 4));
        declare ("tone", juce::String::fromUTF8 ("Tonalité"), { -1.0f, 1.0f }, 0.0f, [] (float v) { return v < -0.02f ? "sombre " + juce::String (juce::roundToInt (-v * 100)) : v > 0.02f ? "clair " + juce::String (juce::roundToInt (v * 100)) : juce::String ("neutre"); });
        declare ("mix", juce::String::fromUTF8 ("Mélange"), { 0.0f, 100.0f }, 100.0f, pctText);
        declare ("output", "Sortie", { -24.0f, 12.0f }, 0.0f, dbText);
    }

    float SaturationPlugin::shape (int character, float x) noexcept
    {
        switch (character)
        {
            case 0:  return std::tanh (x);
            case 1:  return std::tanh (x + 0.25f) - std::tanh (0.25f);                 // lampe : asymétrique (harmoniques paires)
            case 2:  return x / (1.0f + std::abs (x));                                 // bande : compression douce
            default: { const float a = std::abs (x * 1.5f); return x * 1.5f / std::pow (1.0f + std::pow (a, 8.0f), 0.125f); }   // fuzz : écrêtage presque dur
        }
    }

    void SaturationPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        preparedBlock = juce::jlimit (64, maxBlock, info.blockSizeSamples * 2);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampling->initProcessing ((size_t) preparedBlock);
        const int lat = juce::roundToInt (oversampling->getLatencyInSamples());
        for (auto& d : dryLine) d.assign ((size_t) lat + 1, 0.0f);
        for (auto& s : dryScratch) s.assign ((size_t) preparedBlock, 0.0f);
        dryPos = 0;
        reset();
    }

    void SaturationPlugin::reset()
    {
        if (oversampling) oversampling->reset();
        for (int c = 0; c < 2; ++c) dc[c] = dcIn[c] = tiltLP[c] = tapeLP[c] = 0;
    }

    double SaturationPlugin::getLatencySeconds()
    {
        return oversampling ? oversampling->getLatencyInSamples() / rate : 0.0;
    }

    void SaturationPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || oversampling == nullptr) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels());
        if (chans == 0) return;
        const int character = juce::jlimit (0, 3, juce::roundToInt (value ("character")));
        const float drive = dbToGain (value ("drive")), tone = value ("tone"), mix = value ("mix") / 100.0f;
        // Compensation : le niveau perçu reste à peu près le même quand on pousse la saturation.
        const float makeup = dbToGain (value ("output")) / std::sqrt (drive);
        const float tiltCoef = onePoleCoef (900.0f, rate), tapeCoef = onePoleCoef (9000.0f, rate * 2.0);
        // Le signal direct est retardé comme le signal suréchantillonné (mélange parallèle sans filtrage en peigne).
        const int latency = juce::roundToInt (oversampling->getLatencyInSamples());
        if (dryLine[0].empty()) return;

        for (int done = 0; done < fc.bufferNumSamples;)
        {
            const int n = juce::jmin (preparedBlock, fc.bufferNumSamples - done);
            const int start = fc.bufferStartSample + done;
            auto* dry = dryScratch;
            for (int c = 0; c < chans; ++c)
            {
                const float* src = buf.getReadPointer (c, start);
                for (int i = 0; i < n; ++i)
                {
                    auto& line = dryLine[c];
                    const int p = (dryPos + i) % (int) line.size();
                    dry[c][i] = line[(size_t) p];
                    line[(size_t) p] = src[i];
                }
            }
            dryPos = (dryPos + n) % (int) dryLine[0].size();

            juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) chans, (size_t) start, (size_t) n);
            auto up = oversampling->processSamplesUp (block);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                float* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    float x = d[i] * drive;
                    if (character == 2) { tapeLP[c] += tapeCoef * (x - tapeLP[c]); x = tapeLP[c]; }   // bande : aigus adoucis
                    d[i] = shape (character, x);
                }
            }
            oversampling->processSamplesDown (block);
            for (int c = 0; c < chans; ++c)
            {
                float* d = buf.getWritePointer (c, start);
                for (int i = 0; i < n; ++i)
                {
                    float y = d[i];
                    // Coupe-continu (les courbes asymétriques créent un décalage).
                    const float hpY = y - dcIn[c] + 0.995f * dc[c];
                    dcIn[c] = y; dc[c] = hpY; y = hpY;
                    // Tonalité : bascule grave / aigu autour de 900 Hz.
                    tiltLP[c] += tiltCoef * (y - tiltLP[c]);
                    const float high = y - tiltLP[c];
                    y = tiltLP[c] * (1.0f - 0.6f * juce::jmax (0.0f, tone)) + high * (1.0f + 1.2f * juce::jmax (0.0f, tone) - 0.8f * juce::jmax (0.0f, -tone));
                    d[i] = dry[c][i] * (1.0f - mix) + y * makeup * mix;
                }
            }
            done += n;
        }
    }

    //==============================================================================
    const char* EchoPlugin::xmlTypeName = "ondeEcho";

    EchoPlugin::EchoPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        declare ("sync", "Suit le tempo", { 0.0f, 1.0f, 1.0f }, 1.0f, onOffText);
        declare ("division", "Valeur de note", { 0.0f, 7.0f, 1.0f }, 4.0f, choiceText (divisionNames, 8));
        declare ("time", juce::String::fromUTF8 ("Durée (libre)"), logRange (10.0f, 2000.0f), 350.0f, msText);
        declare ("feedback", juce::String::fromUTF8 ("Répétitions"), { 0.0f, 95.0f }, 35.0f, pctText);
        declare ("lowcut", "Coupe-bas", logRange (20.0f, 2000.0f), 150.0f, hzText);
        declare ("highcut", "Coupe-haut", logRange (1000.0f, 20000.0f), 6000.0f, hzText);
        declare ("pingpong", "Ping-pong", { 0.0f, 1.0f, 1.0f }, 0.0f, onOffText);
        declare ("mix", juce::String::fromUTF8 ("Mélange"), { 0.0f, 100.0f }, 25.0f, pctText);
    }

    void EchoPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        for (auto& l : line) l.assign ((size_t) (rate * 2.6) + 4, 0.0f);
        reset();
    }

    void EchoPlugin::reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0; smoothedDelay = -1;
        for (int c = 0; c < 2; ++c) hp[c] = lp[c] = 0;
    }

    double EchoPlugin::currentDelaySeconds (double bpm) const
    {
        if (value ("sync") > 0.5f)
            return divisionBeats[juce::jlimit (0, 7, juce::roundToInt (value ("division")))] * 60.0 / juce::jmax (20.0, bpm);
        return value ("time") / 1000.0;
    }

    void EchoPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || line[0].empty()) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels());
        if (chans == 0) return;
        // Tempo : séquence interne (fil audio : pas d'accès qui recalculerait la séquence).
        const double bpm = edit.tempoSequence.getInternalSequence().getBpmAt (fc.editTime.getStart());
        const float target = (float) juce::jlimit (1.0, rate * 2.5, currentDelaySeconds (bpm) * rate);
        if (smoothedDelay < 0) smoothedDelay = target;
        const float glide = 1.0f - std::exp (-1.0f / (0.05f * (float) rate));
        const float fb = value ("feedback") / 100.0f, mix = value ("mix") / 100.0f;
        const float hpC = onePoleCoef (value ("lowcut"), rate), lpC = onePoleCoef (value ("highcut"), rate);
        const bool ping = value ("pingpong") > 0.5f;
        const int len = (int) line[0].size();
        for (int i = 0; i < fc.bufferNumSamples; ++i)
        {
            smoothedDelay += glide * (target - smoothedDelay);
            float readPos = (float) writePos - smoothedDelay;
            while (readPos < 0) readPos += (float) len;
            const int i0 = (int) readPos, i1 = (i0 + 1) % len;
            const float frac = readPos - (float) i0;
            float wet[2], in[2];
            for (int c = 0; c < 2; ++c)
            {
                wet[c] = line[c][(size_t) i0] + frac * (line[c][(size_t) i1] - line[c][(size_t) i0]);
                in[c] = buf.getSample (juce::jmin (c, chans - 1), fc.bufferStartSample + i);
            }
            // Réinjection filtrée : chaque répétition un peu plus sombre et moins grave.
            float back[2];
            for (int c = 0; c < 2; ++c)
            {
                lp[c] += lpC * (wet[c] - lp[c]);
                hp[c] += hpC * (lp[c] - hp[c]);
                back[c] = (lp[c] - hp[c]) * fb;
            }
            if (ping)
            {
                line[0][(size_t) writePos] = 0.5f * (in[0] + in[1]) + back[1];   // l'entrée part à gauche, puis rebondit
                line[1][(size_t) writePos] = back[0];
            }
            else
                for (int c = 0; c < 2; ++c) line[c][(size_t) writePos] = in[c] + back[c];
            writePos = (writePos + 1) % len;
            for (int c = 0; c < chans; ++c)
                buf.setSample (c, fc.bufferStartSample + i, in[c] * (1.0f - mix) + wet[c] * mix);
        }
    }

    //==============================================================================
    const char* SpacePlugin::xmlTypeName = "ondeSpace";

    namespace
    {
        constexpr float lineMs[SpacePlugin::numLines] = { 31.7f, 37.1f, 41.1f, 43.7f, 47.3f, 53.9f, 59.3f, 67.1f };
        constexpr float diffuserMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
    }

    SpacePlugin::SpacePlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        declare ("size", "Taille", { 0.0f, 1.0f }, 0.5f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        declare ("decay", juce::String::fromUTF8 ("Durée"), logRange (0.2f, 12.0f), 2.0f, [] (float v) { return juce::String (v, 2) + " s"; });
        declare ("damping", "Amortissement", { 0.0f, 1.0f }, 0.5f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        declare ("predelay", juce::String::fromUTF8 ("Pré-délai"), { 0.0f, 200.0f }, 20.0f, msText);
        declare ("width", juce::String::fromUTF8 ("Largeur"), { 0.0f, 1.0f }, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        declare ("mix", juce::String::fromUTF8 ("Mélange"), { 0.0f, 100.0f }, 25.0f, pctText);
    }

    void SpacePlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        for (int i = 0; i < numLines; ++i) lines[i].assign ((size_t) (lineMs[i] * 0.001 * 2.0 * rate) + 64, 0.0f);
        pre.assign ((size_t) (0.21 * rate) + 4, 0.0f);
        for (int i = 0; i < 4; ++i) { diffusers[i].buf.assign ((size_t) (diffuserMs[i] * 0.001 * rate) + 1, 0.0f); diffusers[i].g = 0.62f; }
        reset();
    }

    void SpacePlugin::reset()
    {
        for (int i = 0; i < numLines; ++i) { std::fill (lines[i].begin(), lines[i].end(), 0.0f); pos[i] = 0; damp[i] = 0; }
        std::fill (pre.begin(), pre.end(), 0.0f); prePos = 0;
        for (auto& d : diffusers) { std::fill (d.buf.begin(), d.buf.end(), 0.0f); d.pos = 0; }
        lfoPhase = 0; lowCut[0] = lowCut[1] = 0;
    }

    void SpacePlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || pre.empty()) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels());
        if (chans == 0) return;
        const float size = 0.5f + 1.5f * value ("size"), rt60 = value ("decay"), mix = value ("mix") / 100.0f, width = value ("width");
        const float dampA = 1.0f - 0.85f * value ("damping");
        const int preDelay = juce::jlimit (0, (int) pre.size() - 2, juce::roundToInt (value ("predelay") * 0.001f * (float) rate));
        float len[numLines], gain[numLines];
        for (int i = 0; i < numLines; ++i)
        {
            len[i] = juce::jlimit (8.0f, (float) lines[i].size() - 20.0f, lineMs[i] * 0.001f * size * (float) rate);
            gain[i] = std::pow (10.0f, -3.0f * len[i] / ((float) rate * rt60));   // -60 dB après rt60 secondes
        }
        const float lowCutC = onePoleCoef (90.0f, rate);
        const double lfoInc = 0.7 / rate;
        float modNow[4] {};
        for (int s = 0; s < fc.bufferNumSamples; ++s)
        {
            const float inL = buf.getSample (0, fc.bufferStartSample + s), inR = buf.getSample (chans - 1, fc.bufferStartSample + s);
            // Pré-délai puis diffusion (passe-tout) : l'attaque se fond dans la queue.
            pre[(size_t) prePos] = 0.5f * (inL + inR);
            int rp = prePos - preDelay; if (rp < 0) rp += (int) pre.size();
            float x = pre[(size_t) rp];
            prePos = (prePos + 1) % (int) pre.size();
            for (auto& d : diffusers)
            {
                const float delayed = d.buf[(size_t) d.pos];
                const float v = x + d.g * delayed;
                d.buf[(size_t) d.pos] = v;
                x = delayed - d.g * v;
                d.pos = (d.pos + 1) % (int) d.buf.size();
            }
            if ((s & 15) == 0)          // modulation lente (0,7 Hz) : recalculée tous les 16 échantillons
                for (int m = 0; m < 4; ++m) modNow[m] = 5.0f * (float) std::sin (juce::MathConstants<double>::twoPi * (lfoPhase + m * 0.25));
            // Lecture des 8 lignes (les 4 premières légèrement modulées), amortissement des aigus.
            float o[numLines];
            for (int i = 0; i < numLines; ++i)
            {
                const float mod = i < 4 ? modNow[i] : 0.0f;
                float r = (float) pos[i] - (len[i] + mod);
                const int L = (int) lines[i].size();
                while (r < 0) r += (float) L;
                const int a = (int) r % L, b = (a + 1) % L;
                const float f = r - std::floor (r);
                const float v = lines[i][(size_t) a] + f * (lines[i][(size_t) b] - lines[i][(size_t) a]);
                damp[i] += dampA * (v - damp[i]);
                o[i] = damp[i];
            }
            // Matrice de Hadamard 8x8 (transformée rapide), normalisée : énergie conservée, mélange total.
            float h[numLines];
            for (int i = 0; i < numLines; ++i) h[i] = o[i];
            for (int step = 1; step < numLines; step *= 2)
                for (int i = 0; i < numLines; i += 2 * step)
                    for (int j = i; j < i + step; ++j) { const float u = h[j], w = h[j + step]; h[j] = u + w; h[j + step] = u - w; }
            const float norm = 1.0f / std::sqrt ((float) numLines);
            for (int i = 0; i < numLines; ++i)
            {
                lines[i][(size_t) pos[i]] = x * (i % 2 == 0 ? 0.5f : -0.5f) + h[i] * norm * gain[i];
                pos[i] = (pos[i] + 1) % (int) lines[i].size();
            }
            lfoPhase += lfoInc; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            // Sorties décorrélées gauche / droite, coupe-bas, largeur.
            float wl = (o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]) * 0.3f;
            float wr = (o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7]) * 0.3f;
            lowCut[0] += lowCutC * (wl - lowCut[0]); wl -= lowCut[0];
            lowCut[1] += lowCutC * (wr - lowCut[1]); wr -= lowCut[1];
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            wl = mid + side; wr = mid - side;
            buf.setSample (0, fc.bufferStartSample + s, inL * (1.0f - mix) + wl * mix);
            if (chans > 1) buf.setSample (1, fc.bufferStartSample + s, inR * (1.0f - mix) + wr * mix);
        }
    }

    //==============================================================================
    namespace
    {
        // Fenêtre commune : titre, une phrase, puis un bouton (ou une liste) par réglage.
        class SimpleFxEditor  : public juce::Component
        {
        public:
            SimpleFxEditor (OndeEffect& p, juce::String subtitleText, juce::Colour c) : plugin (p), subtitle (std::move (subtitleText)), colour (c)
            {
                for (auto* prm : p.getAutomatableParameters())
                {
                    const auto range = prm->valueRange;
                    const bool choice = range.interval >= 1.0f && range.end - range.start <= 7.0f;
                    if (choice) controls.add (new ondeui::ParamChoice (prm, prm->getParameterName()));
                    else        controls.add (new ondeui::Knob (prm, prm->getParameterName(), colour));
                    addAndMakeVisible (controls.getLast());
                }
                setSize (juce::jmax (420, 24 + juce::jmin (controls.size(), 6) * 104), 52 + ((controls.size() + 5) / 6) * 112 + 12);
            }
            void paint (juce::Graphics& g) override
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272e), 0, 0, juce::Colour (0xff17191d), 0, (float) getHeight(), false));
                g.fillAll();
                auto r = getLocalBounds().removeFromTop (44).reduced (12, 6);
                theme::drawLogo (g, r.removeFromLeft (r.getHeight()).toFloat().reduced (4.0f));
                r.removeFromLeft (8);
                g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
                g.drawText (plugin.getName(), r.removeFromLeft (150), juce::Justification::centredLeft);
                g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
                g.drawText (subtitle, r, juce::Justification::centredLeft, true);
            }
            void resized() override
            {
                auto area = getLocalBounds().withTrimmedTop (52).reduced (12, 0);
                for (int i = 0; i < controls.size(); ++i)
                {
                    juce::Rectangle<int> cell (area.getX() + (i % 6) * 104, area.getY() + (i / 6) * 112, 100, 104);
                    if (dynamic_cast<ondeui::ParamChoice*> (controls[i]) != nullptr) cell = cell.withSizeKeepingCentre (100, 44);
                    controls[i]->setBounds (cell);
                }
            }
        private:
            OndeEffect& plugin;
            te::Plugin::Ptr keepAlive { &plugin };
            juce::String subtitle;
            juce::Colour colour;
            juce::OwnedArray<juce::Component> controls;
        };
    }

    std::unique_ptr<juce::Component> createFx2Editor (te::Plugin& p)
    {
        const auto U8 = [] (const char* s) { return juce::String::fromUTF8 (s); };
        if (auto* s = dynamic_cast<SaturationPlugin*> (&p)) return std::make_unique<SimpleFxEditor> (*s, U8 ("Douce, lampe, bande ou fuzz · suréchantillonnée · mélange parallèle"), juce::Colour (0xffe5a04e));
        if (auto* e = dynamic_cast<EchoPlugin*> (&p))       return std::make_unique<SimpleFxEditor> (*e, U8 ("Calé sur le tempo · répétitions filtrées · ping-pong"), juce::Colour (0xff2de2c6));
        if (auto* r = dynamic_cast<SpacePlugin*> (&p))      return std::make_unique<SimpleFxEditor> (*r, U8 ("Réverbération dense et douce · taille, durée, amortissement"), juce::Colour (0xffb98cff));
        return {};
    }
}
