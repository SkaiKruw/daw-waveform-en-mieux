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

        {
            float pk = 0;
            for (int c = 0; c < chans; ++c) pk = std::max (pk, buf.getMagnitude (c, fc.bufferStartSample, fc.bufferNumSamples));
            inputDb = juce::Decibels::gainToDecibels (pk, -100.0f);
        }
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
        float outPk = 0;
        for (int c = 0; c < chans; ++c) outPk = std::max (outPk, buf.getMagnitude (c, fc.bufferStartSample, fc.bufferNumSamples));
        outputDb = juce::Decibels::gainToDecibels (outPk, -100.0f);
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
        using Values = std::vector<std::pair<const char*, float>>;
        const juce::Colour panelBg (0xff111316), gridLine (0xff23272d);

        //==========================================================================
        // Base des fenêtres d'effets Onde : en-tête, zone de visualisation, boutons, préréglages en bas.
        class FxWindow  : public juce::Component, protected juce::Timer
        {
        public:
            FxWindow (OndeEffect& p, juce::String sub, juce::Colour c) : fx (p), subtitle (std::move (sub)), colour (c) { startTimerHz (30); }

            void paint (juce::Graphics& g) override
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272e), 0, 0, juce::Colour (0xff15171b), 0, (float) getHeight(), false));
                g.fillAll();
                auto r = getLocalBounds().removeFromTop (44).reduced (12, 6);
                theme::drawLogo (g, r.removeFromLeft (r.getHeight()).toFloat().reduced (4.0f));
                r.removeFromLeft (8);
                g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
                g.drawText (fx.getName(), r.removeFromLeft (170), juce::Justification::centredLeft);
                g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
                g.drawText (subtitle, r, juce::Justification::centredLeft, true);
                auto d = display.toFloat();
                g.setColour (panelBg); g.fillRoundedRectangle (d, 8.0f);
                g.saveState();
                g.reduceClipRegion (display);
                paintDisplay (g, d.reduced (10.0f));
                g.restoreState();
                g.setColour (colour.withAlpha (0.25f)); g.drawRoundedRectangle (d, 8.0f, 1.0f);
                g.setColour (theme::dim); g.setFont (theme::ui (11.0f, true));
                g.drawText (U8 ("PRÉRÉGLAGES"), 14, getHeight() - 40, 100, 28, juce::Justification::centredLeft);
            }

        protected:
            virtual void paintDisplay (juce::Graphics&, juce::Rectangle<float>) = 0;
            static juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }

            ondeui::Knob* knob (const char* id, const char* label)
            {
                auto* k = new ondeui::Knob (fx.param (id), U8 (label), colour);
                controls.add (k); addAndMakeVisible (k); return k;
            }
            ondeui::ParamToggle* toggle (const char* id, const char* label)
            {
                auto* t = new ondeui::ParamToggle (fx.param (id), U8 (label), colour);
                controls.add (t); addAndMakeVisible (t); return t;
            }
            void preset (const char* name, Values values) { preset (U8 (name), std::move (values)); }
            void preset (const juce::String& name, Values values)
            {
                auto* b = presetButtons.add (new juce::TextButton (name));
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262a31));
                b->onClick = [this, values, n = name]
                {
                    fx.edit.getUndoManager().beginNewTransaction (U8 ("Préréglage « ") + n + U8 (" »"));
                    for (auto& [id, v] : values) fx.setStored (id, v);
                    repaint();
                };
                addAndMakeVisible (b);
            }
            // Mise en page commune : visualisation à gauche, réglages à droite (grille), préréglages en bas.
            void layout (int topRowH, juce::Component* topRow)
            {
                auto r = getLocalBounds().reduced (12).withTrimmedTop (36);
                auto bottom = r.removeFromBottom (32);
                bottom.removeFromLeft (100);
                const int pw = presetButtons.isEmpty() ? 0 : juce::jmin (130, bottom.getWidth() / presetButtons.size() - 6);
                for (auto* b : presetButtons) { b->setBounds (bottom.removeFromLeft (pw)); bottom.removeFromLeft (6); }
                r.removeFromBottom (10);
                if (topRow != nullptr) { topRow->setBounds (r.removeFromTop (topRowH)); r.removeFromTop (8); }
                auto right = r.removeFromRight (330);
                r.removeFromRight (12);
                display = r;
                layoutControls (right);
            }
            virtual void layoutControls (juce::Rectangle<int> area)
            {
                const int cols = 3, cw = area.getWidth() / cols;
                int i = 0;
                for (auto* c : controls)
                {
                    if (! c->isVisible()) continue;
                    juce::Rectangle<int> cell (area.getX() + (i % cols) * cw, area.getY() + (i / cols) * 96, cw - 6, 92);
                    if (dynamic_cast<ondeui::ParamToggle*> (c) != nullptr) cell = cell.withSizeKeepingCentre (cw - 8, 28);
                    c->setBounds (cell);
                    ++i;
                }
            }
            void timerCallback() override { repaint (display); }

            OndeEffect& fx;
            te::Plugin::Ptr keepAlive { &fx };
            juce::String subtitle;
            juce::Colour colour;
            juce::Rectangle<int> display;
            juce::OwnedArray<juce::Component> controls;
            juce::OwnedArray<juce::TextButton> presetButtons;
        };

        //==========================================================================
        // Saturation : caractère en cartes (courbe miniature), courbe de transfert, harmoniques produites, vumètres.
        class SaturationEditor  : public FxWindow
        {
        public:
            explicit SaturationEditor (SaturationPlugin& p) : FxWindow (p, U8 ("Saturation suréchantillonnée · le niveau perçu reste stable quand on pousse"), juce::Colour (0xffe5a04e)), sat (p)
            {
                cards.cardClicked = [this] (int i)
                {
                    sat.edit.getUndoManager().beginNewTransaction (U8 ("Caractère"));
                    sat.setStored ("character", (float) i);
                };
                cards.owner = &sat;
                addAndMakeVisible (cards);
                knob ("drive", "Saturation"); knob ("tone", "Tonalité"); knob ("mix", "Mélange"); knob ("output", "Sortie");
                preset ("Chaleur légère", { { "character", 1 }, { "drive", 6 }, { "tone", 0 }, { "mix", 60 }, { "output", 0 } });
                preset ("Bande chaude", { { "character", 2 }, { "drive", 12 }, { "tone", -0.2f }, { "mix", 100 }, { "output", 0 } });
                preset ("Lampe poussée", { { "character", 1 }, { "drive", 24 }, { "tone", 0.1f }, { "mix", 100 }, { "output", -2 } });
                preset ("Fuzz", { { "character", 3 }, { "drive", 30 }, { "tone", -0.3f }, { "mix", 100 }, { "output", -4 } });
                preset (U8 ("Parallèle"), { { "character", 0 }, { "drive", 24 }, { "tone", 0.2f }, { "mix", 35 }, { "output", 0 } });
                setSize (860, 440);
            }
            void resized() override { layout (62, &cards); }
        private:
            // Cartes de caractère : un clic choisit, la courbe miniature montre la forme.
            struct Cards : public juce::Component
            {
                std::function<void (int)> cardClicked;
                SaturationPlugin* owner = nullptr;
                void paint (juce::Graphics& g) override
                {
                    const int sel = owner ? juce::roundToInt (owner->value ("character")) : 0;
                    const float w = getWidth() / 4.0f;
                    for (int i = 0; i < 4; ++i)
                    {
                        auto r = juce::Rectangle<float> (i * w, 0, w, (float) getHeight()).reduced (4.0f, 2.0f);
                        g.setColour (i == sel ? juce::Colour (0xff3a2c1a) : juce::Colour (0xff1c1f24)); g.fillRoundedRectangle (r, 7.0f);
                        g.setColour (i == sel ? juce::Colour (0xffe5a04e) : juce::Colour (0xff30353d)); g.drawRoundedRectangle (r, 7.0f, i == sel ? 2.0f : 1.0f);
                        auto icon = r.removeFromLeft (r.getHeight()).reduced (10.0f);
                        juce::Path p;
                        for (int k = 0; k <= 30; ++k)
                        {
                            const float x = -1.0f + 2.0f * k / 30.0f, y = SaturationPlugin::shape (i, x * 3.0f) / juce::jmax (0.3f, std::abs (SaturationPlugin::shape (i, 3.0f)));
                            const auto pt = juce::Point<float> (icon.getX() + (x + 1.0f) * 0.5f * icon.getWidth(), icon.getCentreY() - y * icon.getHeight() * 0.45f);
                            if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                        }
                        g.setColour (i == sel ? juce::Colour (0xffe5a04e) : theme::muted); g.strokePath (p, juce::PathStrokeType (1.8f));
                        g.setColour (i == sel ? theme::text : theme::muted); g.setFont (theme::ui (13.5f, true));
                        static const char* desc[] = { "neutre, impaire", "harmoniques paires", "aigus adoucis", "écrêtage franc" };
                        g.drawText (juce::String::fromUTF8 (SaturationPlugin::characterNames[i]), r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::bottomLeft);
                        g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
                        g.drawText (juce::String::fromUTF8 (desc[i]), r, juce::Justification::topLeft);
                    }
                }
                void mouseUp (const juce::MouseEvent& e) override { if (cardClicked) cardClicked (juce::jlimit (0, 3, (int) (e.position.x / (getWidth() / 4.0f)))); repaint(); }
            } cards;

            void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
            {
                const int ch = juce::roundToInt (sat.value ("character"));
                const float drive = std::pow (10.0f, sat.value ("drive") / 20.0f);
                // Courbe de transfert (entrée → sortie) à la saturation réglée.
                auto curveArea = r.removeFromLeft (r.getWidth() * 0.5f).reduced (4.0f);
                g.setColour (gridLine);
                g.drawHorizontalLine ((int) curveArea.getCentreY(), curveArea.getX(), curveArea.getRight());
                g.drawVerticalLine ((int) curveArea.getCentreX(), curveArea.getY(), curveArea.getBottom());
                g.drawLine (curveArea.getX(), curveArea.getBottom(), curveArea.getRight(), curveArea.getY(), 1.0f);
                juce::Path p;
                const float norm = 1.0f / juce::jmax (0.2f, std::abs (SaturationPlugin::shape (ch, drive)));
                for (int k = 0; k <= 80; ++k)
                {
                    const float x = -1.0f + 2.0f * k / 80.0f;
                    const float y = juce::jlimit (-1.1f, 1.1f, SaturationPlugin::shape (ch, x * drive) * norm);
                    const auto pt = juce::Point<float> (curveArea.getX() + (x + 1.0f) * 0.5f * curveArea.getWidth(), curveArea.getCentreY() - y * curveArea.getHeight() * 0.5f);
                    if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                g.setColour (colour); g.strokePath (p, juce::PathStrokeType (2.2f));
                g.setColour (theme::dim); g.setFont (theme::ui (10.5f));
                g.drawText (U8 ("entrée → sortie"), curveArea.removeFromBottom (14), juce::Justification::centredRight);
                // Harmoniques produites par un sinus pleine échelle (H1 = fondamental).
                auto bars = r.reduced (8.0f, 4.0f);
                g.setColour (theme::muted); g.setFont (theme::ui (11.0f, true));
                g.drawText ("Harmoniques", bars.removeFromTop (16), juce::Justification::centredLeft);
                auto meters = bars.removeFromRight (40);
                double h[9] {};
                constexpr int N = 256;
                for (int k = 1; k <= 8; ++k)
                {
                    double re = 0, im = 0;
                    for (int i = 0; i < N; ++i)
                    {
                        const double ph = juce::MathConstants<double>::twoPi * i / N;
                        const double y = SaturationPlugin::shape (ch, (float) (0.7 * drive * std::sin (ph)));
                        re += y * std::cos (k * ph); im += y * std::sin (k * ph);
                    }
                    h[k] = std::sqrt (re * re + im * im) * 2.0 / N;
                }
                const float bw = bars.getWidth() / 8.0f;
                for (int k = 1; k <= 8; ++k)
                {
                    const double db = juce::jmax (-80.0, 20.0 * std::log10 (h[k] / juce::jmax (1.0e-9, h[1]) + 1.0e-12));
                    const float hh = (float) ((db + 80.0) / 80.0) * (bars.getHeight() - 16.0f);
                    auto bar = juce::Rectangle<float> (bars.getX() + (k - 1) * bw + 3.0f, bars.getBottom() - 14.0f - hh, bw - 6.0f, hh);
                    g.setColour (k == 1 ? theme::muted : (k % 2 == 0 ? juce::Colour (0xffe8697a) : colour)); g.fillRoundedRectangle (bar, 2.0f);
                    g.setColour (theme::dim); g.setFont (theme::ui (10.0f));
                    g.drawText ("H" + juce::String (k), (int) (bars.getX() + (k - 1) * bw), (int) bars.getBottom() - 13, (int) bw, 12, juce::Justification::centred);
                }
                // Vumètres entrée / sortie.
                auto meter = [&] (juce::Rectangle<float> m, float dbv, const char* label)
                {
                    g.setColour (juce::Colour (0xff1c1f24)); g.fillRoundedRectangle (m.withTrimmedBottom (14.0f), 3.0f);
                    const float lvl = juce::jlimit (0.0f, 1.0f, (dbv + 48.0f) / 48.0f);
                    auto fill = m.withTrimmedBottom (14.0f); fill = fill.removeFromBottom (fill.getHeight() * lvl);
                    g.setColour (dbv > -1.0f ? juce::Colour (0xffe8473f) : theme::play); g.fillRoundedRectangle (fill, 3.0f);
                    g.setColour (theme::dim); g.setFont (theme::ui (10.0f));
                    g.drawText (U8 (label), m.removeFromBottom (12.0f), juce::Justification::centred);
                };
                meter (meters.removeFromLeft (18.0f), shownIn, "E");
                meters.removeFromLeft (4.0f);
                meter (meters.removeFromLeft (18.0f), shownOut, "S");
            }
            void timerCallback() override
            {
                shownIn = juce::jmax (sat.inputDb.load(), shownIn - 1.2f);
                shownOut = juce::jmax (sat.outputDb.load(), shownOut - 1.2f);
                cards.repaint();
                FxWindow::timerCallback();
            }
            SaturationPlugin& sat;
            float shownIn = -100.0f, shownOut = -100.0f;
        };

        //==========================================================================
        // Écho : valeur de note en boutons, frise des répétitions (gauche/droite en ping-pong), durée affichée.
        class EchoEditor  : public FxWindow
        {
        public:
            explicit EchoEditor (EchoPlugin& p) : FxWindow (p, U8 ("Calé sur le tempo · chaque répétition plus sombre que la précédente"), juce::Colour (0xff2de2c6)), echo (p)
            {
                for (int i = 0; i < 8; ++i)
                {
                    auto* b = noteButtons.add (new juce::TextButton (juce::String::fromUTF8 (EchoPlugin::divisionNames[i])));
                    b->setColour (juce::TextButton::buttonOnColourId, colour);
                    b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
                    b->onClick = [this, i]
                    {
                        echo.edit.getUndoManager().beginNewTransaction (U8 ("Valeur de note"));
                        echo.setStored ("division", (float) i);
                        echo.setStored ("sync", 1.0f);
                    };
                    notes.addAndMakeVisible (b);
                }
                addAndMakeVisible (notes);
                toggle ("sync", "Suit le tempo"); toggle ("pingpong", "Ping-pong");
                timeKnob = knob ("time", "Durée libre");
                knob ("feedback", "Répétitions"); knob ("mix", "Mélange"); knob ("lowcut", "Coupe-bas"); knob ("highcut", "Coupe-haut");
                preset ("Slapback", { { "sync", 0 }, { "time", 110 }, { "feedback", 5 }, { "mix", 25 }, { "pingpong", 0 }, { "lowcut", 150 }, { "highcut", 6000 } });
                preset (U8 ("Croche pointée"), { { "sync", 1 }, { "division", 5 }, { "feedback", 35 }, { "mix", 22 }, { "pingpong", 0 }, { "lowcut", 150 }, { "highcut", 6000 } });
                preset ("Ping-pong", { { "sync", 1 }, { "division", 4 }, { "feedback", 45 }, { "mix", 25 }, { "pingpong", 1 }, { "lowcut", 200 }, { "highcut", 7000 } });
                preset ("Dub", { { "sync", 1 }, { "division", 5 }, { "feedback", 70 }, { "mix", 30 }, { "pingpong", 0 }, { "lowcut", 300 }, { "highcut", 2500 } });
                preset ("Ambiance", { { "sync", 1 }, { "division", 1 }, { "feedback", 55 }, { "mix", 15 }, { "pingpong", 1 }, { "lowcut", 250 }, { "highcut", 4000 } });
                setSize (860, 460);
            }
            void resized() override
            {
                layout (30, &notes);
                const int w = notes.getWidth() / 8;
                for (int i = 0; i < noteButtons.size(); ++i) noteButtons[i]->setBounds (i * w, 0, w - 4, notes.getHeight());
            }
        private:
            void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
            {
                const double bpm = juce::jmax (20.0, echo.edit.tempoSequence.getTempos()[0]->getBpm());
                const double delay = echo.currentDelaySeconds (bpm);
                const float fb = echo.value ("feedback") / 100.0f, mix = echo.value ("mix") / 100.0f;
                const bool ping = echo.value ("pingpong") > 0.5f;
                const float dark = juce::jlimit (0.0f, 1.0f, 1.0f - (echo.value ("highcut") - 1000.0f) / 19000.0f);
                const double span = juce::jmax (1.0, juce::jmin (6.0, delay * 10.0));
                auto info = r.removeFromBottom (18.0f);
                auto lanes = r;
                // Temps (noires) en filigrane.
                const double beat = 60.0 / bpm;
                g.setFont (theme::ui (10.0f));
                for (int k = 0; k * beat <= span; ++k)
                {
                    const float x = lanes.getX() + (float) (k * beat / span) * lanes.getWidth();
                    g.setColour (k % 4 == 0 ? juce::Colour (0xff2c3138) : gridLine);
                    g.drawVerticalLine ((int) x, lanes.getY(), lanes.getBottom());
                }
                const float midY = lanes.getCentreY();
                if (ping)
                {
                    g.setColour (gridLine); g.drawHorizontalLine ((int) midY, lanes.getX(), lanes.getRight());
                    g.setColour (theme::dim); g.drawText ("G", (int) lanes.getX() + 2, (int) lanes.getY() + 2, 14, 12, juce::Justification::left);
                    g.drawText ("D", (int) lanes.getX() + 2, (int) lanes.getBottom() - 14, 14, 12, juce::Justification::left);
                }
                // Le son direct, puis chaque répétition (hauteur = niveau, de plus en plus sombre).
                auto tap = [&] (double t, float level, bool lower, juce::Colour c)
                {
                    const float x = lanes.getX() + (float) (t / span) * lanes.getWidth();
                    const float h = juce::jlimit (0.0f, 1.0f, level) * (ping ? lanes.getHeight() * 0.48f : lanes.getHeight() * 0.95f);
                    auto bar = ping ? (lower ? juce::Rectangle<float> (x - 3.0f, midY + 2.0f, 6.0f, h) : juce::Rectangle<float> (x - 3.0f, midY - 2.0f - h, 6.0f, h))
                                    : juce::Rectangle<float> (x - 3.0f, lanes.getBottom() - h, 6.0f, h);
                    g.setColour (c); g.fillRoundedRectangle (bar, 2.0f);
                };
                tap (0.0, 1.0f - 0.5f * mix, false, theme::text.withAlpha (0.8f));
                if (ping) tap (0.0, 1.0f - 0.5f * mix, true, theme::text.withAlpha (0.8f));
                float level = mix;
                for (int k = 1; k * delay <= span && level > 0.01f; ++k)
                {
                    const auto c = colour.withMultipliedBrightness (1.0f - dark * 0.6f * (float) (k - 1) / 6.0f).withAlpha (0.95f);
                    tap (k * delay, level, ping && (k % 2 == 0), c);
                    level *= fb;
                }
                g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
                const int div = juce::roundToInt (echo.value ("division"));
                const auto what = echo.value ("sync") > 0.5f ? juce::String::fromUTF8 (EchoPlugin::divisionNames[juce::jlimit (0, 7, div)]) + " = " : U8 ("durée libre : ");
                g.drawText (what + juce::String (juce::roundToInt (delay * 1000.0)) + U8 (" ms à ") + juce::String (bpm, 0) + " BPM", info, juce::Justification::centredLeft);
            }
            void timerCallback() override
            {
                const bool sync = echo.value ("sync") > 0.5f;
                const int div = juce::roundToInt (echo.value ("division"));
                for (int i = 0; i < noteButtons.size(); ++i) noteButtons[i]->setToggleState (sync && i == div, juce::dontSendNotification);
                if (timeKnob != nullptr && timeKnob->isEnabled() == sync) { timeKnob->setEnabled (! sync); timeKnob->setAlpha (sync ? 0.35f : 1.0f); }
                FxWindow::timerCallback();
            }
            EchoPlugin& echo;
            juce::Component notes;
            juce::OwnedArray<juce::TextButton> noteButtons;
            ondeui::Knob* timeKnob = nullptr;
        };

        //==========================================================================
        // Espace : courbe de décroissance (pré-délai, premières réflexions, queue), aigus qui s'éteignent plus vite.
        class SpaceEditor  : public FxWindow
        {
        public:
            explicit SpaceEditor (SpacePlugin& p) : FxWindow (p, U8 ("Réverbération dense et douce · de la pièce à la cathédrale"), juce::Colour (0xffb98cff)), space (p)
            {
                knob ("size", "Taille"); knob ("decay", "Durée"); knob ("damping", "Amortissement");
                knob ("predelay", "Pré-délai"); knob ("width", "Largeur"); knob ("mix", "Mélange");
                preset (U8 ("Pièce"), { { "size", 0.25f }, { "decay", 0.5f }, { "damping", 0.6f }, { "predelay", 5 }, { "width", 0.8f }, { "mix", 18 } });
                preset ("Studio", { { "size", 0.4f }, { "decay", 1.0f }, { "damping", 0.5f }, { "predelay", 10 }, { "width", 1 }, { "mix", 20 } });
                preset ("Salle", { { "size", 0.7f }, { "decay", 2.4f }, { "damping", 0.45f }, { "predelay", 25 }, { "width", 1 }, { "mix", 25 } });
                preset (U8 ("Cathédrale"), { { "size", 1.0f }, { "decay", 7 }, { "damping", 0.35f }, { "predelay", 40 }, { "width", 1 }, { "mix", 30 } });
                preset ("Plaque", { { "size", 0.5f }, { "decay", 1.8f }, { "damping", 0.2f }, { "predelay", 0 }, { "width", 1 }, { "mix", 22 } });
                setSize (860, 430);
            }
            void resized() override { layout (0, nullptr); }
        private:
            void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
            {
                const float rt = space.value ("decay"), pre = space.value ("predelay") / 1000.0f, damp = space.value ("damping");
                const float size = space.value ("size"), mix = space.value ("mix") / 100.0f;
                const float span = juce::jmax (1.0f, pre + rt * 1.2f);
                auto info = r.removeFromBottom (18.0f);
                auto plot = r.withTrimmedLeft (30.0f);
                // Graduations : dB (0 à -60) et secondes.
                g.setFont (theme::ui (10.0f));
                for (int db = 0; db >= -60; db -= 20)
                {
                    const float y = plot.getY() + (-db / 60.0f) * plot.getHeight();
                    g.setColour (gridLine); g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
                    g.setColour (theme::dim); g.drawText (juce::String (db), (int) r.getX(), (int) y - 6, 26, 12, juce::Justification::centredRight);
                }
                const float stepS = span > 6 ? 2.0f : span > 2 ? 0.5f : 0.25f;
                for (float t = 0; t <= span; t += stepS)
                {
                    const float x = plot.getX() + t / span * plot.getWidth();
                    g.setColour (gridLine); g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                    g.setColour (theme::dim); g.drawText (juce::String (t, t < 1 ? 2 : 1) + " s", (int) x + 2, (int) plot.getBottom() - 12, 40, 12, juce::Justification::left);
                }
                auto toPt = [&] (float t, float db) { return juce::Point<float> (plot.getX() + t / span * plot.getWidth(), plot.getY() + juce::jlimit (0.0f, 1.0f, -db / 60.0f) * plot.getHeight()); };
                // Queue : décroissance de 60 dB en « durée » ; les aigus (amortis) s'éteignent plus vite.
                auto tail = [&] (float rt60, juce::Colour c, float thickness)
                {
                    juce::Path p; p.startNewSubPath (toPt (pre, -60.0f));
                    p.lineTo (toPt (pre, 0.0f));
                    for (int k = 1; k <= 60; ++k) { const float t = pre + rt60 * k / 60.0f * 1.05f; p.lineTo (toPt (t, -60.0f * (t - pre) / rt60)); }
                    juce::Path fill (p); fill.lineTo (toPt (pre + rt60 * 1.05f, -60.0f)); fill.closeSubPath();
                    g.setColour (c.withAlpha (0.15f)); g.fillPath (fill);
                    g.setColour (c); g.strokePath (p, juce::PathStrokeType (thickness));
                };
                tail (rt * (1.0f - 0.7f * damp), juce::Colour (0xff3fa4ff), 1.4f);
                tail (rt, colour, 2.2f);
                // Premières réflexions (plus espacées dans une grande pièce).
                juce::Random rnd (7);
                for (int k = 0; k < 7; ++k)
                {
                    const float t = pre + (0.004f + 0.006f * k) * (0.5f + 1.5f * size);
                    const float db = -3.0f - k * 2.5f - rnd.nextFloat() * 3.0f;
                    g.setColour (theme::text.withAlpha (0.7f)); g.drawLine ({ toPt (t, -60.0f), toPt (t, db) }, 1.5f);
                }
                // Légende des deux courbes.
                {
                    auto lg = juce::Rectangle<float> (plot.getRight() - 190.0f, plot.getY() + 4.0f, 186.0f, 16.0f);
                    g.setFont (theme::ui (10.5f));
                    g.setColour (colour); g.fillRect (lg.getX(), lg.getCentreY() - 1.0f, 14.0f, 2.5f);
                    g.setColour (theme::muted); g.drawText (U8 ("son global"), lg.withTrimmedLeft (18.0f).withWidth (70.0f), juce::Justification::centredLeft);
                    g.setColour (juce::Colour (0xff3fa4ff)); g.fillRect (lg.getX() + 90.0f, lg.getCentreY() - 1.0f, 14.0f, 2.5f);
                    g.setColour (theme::muted); g.drawText (U8 ("aigus (amortis)"), lg.withTrimmedLeft (108.0f), juce::Justification::centredLeft);
                }
                // Repère de la durée réglée.
                const auto mark = toPt (pre + rt, -60.0f);
                g.setColour (colour); g.drawVerticalLine ((int) mark.x, plot.getY(), plot.getBottom());
                g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
                g.drawText (U8 ("Durée ") + juce::String (rt, 2) + U8 (" s (−60 dB) · pré-délai ") + juce::String (juce::roundToInt (pre * 1000.0f)) + U8 (" ms · aigus amortis en ")
                            + juce::String (rt * (1.0f - 0.7f * damp), 2) + U8 (" s · mélange ") + juce::String (juce::roundToInt (mix * 100.0f)) + " %", info, juce::Justification::centredLeft, true);
            }
            SpacePlugin& space;
        };
    }

    std::unique_ptr<juce::Component> createFx2Editor (te::Plugin& p)
    {
        if (auto* s = dynamic_cast<SaturationPlugin*> (&p)) return std::make_unique<SaturationEditor> (*s);
        if (auto* e = dynamic_cast<EchoPlugin*> (&p))       return std::make_unique<EchoEditor> (*e);
        if (auto* r = dynamic_cast<SpacePlugin*> (&p))      return std::make_unique<SpaceEditor> (*r);
        return {};
    }
}
