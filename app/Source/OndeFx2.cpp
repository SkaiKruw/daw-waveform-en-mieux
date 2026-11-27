#include "OndeFx2.h"
#include <complex>
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

    std::unique_ptr<juce::Component> createModulationEditor (ModulationPlugin&);
    std::unique_ptr<juce::Component> createAmpEditor (AmpPlugin&);

    std::unique_ptr<juce::Component> createFx2Editor (te::Plugin& p)
    {
        if (auto* m = dynamic_cast<ModulationPlugin*> (&p)) return createModulationEditor (*m);
        if (auto* a = dynamic_cast<AmpPlugin*> (&p)) return createAmpEditor (*a);
        if (auto* s = dynamic_cast<SaturationPlugin*> (&p)) return std::make_unique<SaturationEditor> (*s);
        if (auto* e = dynamic_cast<EchoPlugin*> (&p))       return std::make_unique<EchoEditor> (*e);
        if (auto* r = dynamic_cast<SpacePlugin*> (&p))      return std::make_unique<SpaceEditor> (*r);
        return {};
    }
}

namespace ondefx
{
    //==============================================================================
    const char* MasterMeterPlugin::xmlTypeName = "ondeMasterMeter";

    MasterMeterPlugin::MasterMeterPlugin (te::PluginCreationInfo info) : OndeEffect (info) {}

    MasterMeterPlugin* MasterMeterPlugin::findOrCreate (te::Edit& edit)
    {
        auto& list = edit.getMasterPluginList();
        for (auto* p : list) if (auto* m = dynamic_cast<MasterMeterPlugin*> (p)) return m;
        auto p = edit.getPluginCache().createNewPlugin (xmlTypeName, {});
        if (p == nullptr) return nullptr;
        list.insertPlugin (p, list.size(), nullptr);          // en dernier : ce qui sort vraiment
        return dynamic_cast<MasterMeterPlugin*> (p.get());
    }

    void MasterMeterPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        hopLen = juce::jmax (1, juce::roundToInt (0.1 * rate));
        const double pi = juce::MathConstants<double>::pi;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / rate), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (pi * f0 / rate), a0 = 1.0 + K / Q + K * K;
            high.b0 = 1.0; high.b1 = -2.0; high.b2 = 1.0; high.a1 = 2.0 * (K * K - 1.0) / a0; high.a2 = (1.0 - K / Q + K * K) / a0;
        }
        clear();
    }

    void MasterMeterPlugin::reset() { tp.reset(); }

    void MasterMeterPlugin::clear()
    {
        shelf.reset(); high.reset(); tp.reset();
        hopEnergy = 0; hopCount = 0; hopPos = 0; hopsFilled = 0;
        std::fill (std::begin (hops), std::end (hops), 0.0);
        std::fill (std::begin (binEnergy), std::end (binEnergy), 0.0);
        std::fill (std::begin (binCount), std::end (binCount), 0);
        sLL = sRR = sLR = 0; tpMax = 0;
        momentary = -100; shortTerm = -100; integrated = -100; truePeakMax = -100; correlation = 1.0f;
    }

    void MasterMeterPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr) return;
        if (resetRequested.exchange (false)) clear();
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        if (chans == 0) return;
        float* L = buf.getWritePointer (0, start);
        float* R = chans > 1 ? buf.getWritePointer (1, start) : L;
        auto lufs = [] (double e) { return -0.691 + 10.0 * std::log10 (std::max (e, 1.0e-12)); };
        auto meanLast = [this] (int count) { double s = 0; for (int j = 0; j < count; ++j) s += hops[(hopPos - 1 - j + 60) % 30]; return s / count; };
        const double corrCoef = std::exp (-1.0 / (0.3 * rate));
        float tpBlock = 0;
        for (int i = 0; i < n; ++i)
        {
            const double l = L[i], r = R[i];
            // Corrélation de phase (fenêtre glissante ~300 ms) : +1 = mono, 0 = très large, -1 = opposition de phase.
            sLL = sLL * corrCoef + l * l; sRR = sRR * corrCoef + r * r; sLR = sLR * corrCoef + l * r;
            tpBlock = std::max ({ tpBlock, tp.process (L[i], 0), tp.process (R[i], 1) });
            const double yl = high.process (shelf.process (l, 0), 0), yr = high.process (shelf.process (r, 1), 1);
            hopEnergy += yl * yl + yr * yr;
            if (++hopCount < hopLen) continue;
            hops[hopPos] = hopEnergy / hopLen; hopPos = (hopPos + 1) % 30; hopsFilled = std::min (30, hopsFilled + 1);
            hopEnergy = 0; hopCount = 0;
            if (hopsFilled < 4) continue;
            const double block = meanLast (4), blockL = lufs (block);
            momentary = (float) blockL;
            if (hopsFilled >= 30) shortTerm = (float) lufs (meanLast (30));
            if (blockL >= -70.0) { const int bin = std::min (799, (int) ((blockL + 70.0) * 10.0)); binEnergy[bin] += block; ++binCount[bin]; }
            double sum = 0; int count = 0;
            for (int b = 0; b < 800; ++b) { sum += binEnergy[b]; count += binCount[b]; }
            if (count == 0) continue;
            const int first = juce::jlimit (0, 800, (int) std::ceil ((lufs (sum / count) - 10.0 + 70.0) * 10.0));
            sum = 0; count = 0;
            for (int b = first; b < 800; ++b) { sum += binEnergy[b]; count += binCount[b]; }
            if (count > 0) integrated = (float) lufs (sum / count);
        }
        tpMax = std::max (tpMax, tpBlock);
        truePeakMax = juce::Decibels::gainToDecibels (tpMax, -100.0f);
        correlation = (float) (sLL > 1.0e-12 && sRR > 1.0e-12 ? sLR / std::sqrt (sLL * sRR) : 1.0);
        // Aides d'écoute : jamais pendant un export (le fichier reste celui du mix).
        if (! fc.isRendering)
        {
            const bool mono = monoListen.load() && chans > 1, dim = dimListen.load();
            const float g = dim ? 0.1f : 1.0f;
            if (mono || dim)
                for (int i = 0; i < n; ++i)
                {
                    const float m = mono ? 0.5f * (L[i] + R[i]) : 0.0f;
                    L[i] = (mono ? m : L[i]) * g;
                    if (chans > 1) R[i] = (mono ? m : R[i]) * g;
                }
        }
    }
}

namespace ondefx
{
    //==============================================================================
    const char* ModulationPlugin::xmlTypeName = "ondeModulation";

    ModulationPlugin::ModulationPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        juce::NormalisableRange<float> rateR (0.03f, 10.0f); rateR.setSkewForCentre (0.8f);
        declare ("mode", "Effet", { 0.0f, 4.0f, 1.0f }, 0.0f, choiceText (modeNames, 5));
        declare ("rate", "Vitesse", rateR, 0.8f, [] (float v) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; });
        declare ("depth", "Profondeur", { 0.0f, 100.0f }, 50.0f, pctText);
        declare ("delay", juce::String::fromUTF8 ("Retard"), { 2.0f, 30.0f }, 12.0f, [] (float v) { return juce::String (v, 1) + " ms"; });
        declare ("feedback", juce::String::fromUTF8 ("Réinjection"), { -95.0f, 95.0f }, 0.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; });
        declare ("width", "Largeur", { 0.0f, 100.0f }, 70.0f, pctText);
        declare ("mix", juce::String::fromUTF8 ("Mélange"), { 0.0f, 100.0f }, 50.0f, pctText);
    }

    void ModulationPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        int size = 1; while (size < (int) (rate * 0.08)) size <<= 1;      // 80 ms au moins
        for (auto& l : line) l.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void ModulationPlugin::reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0;
        for (auto& f : fbState) f = 0;
        for (auto& c : ap) for (auto& z : c) z = 0;
        for (auto& w : wetLP) w = 0;
    }

    float ModulationPlugin::delayMsAt (int mode, double ph) const
    {
        const float s = (float) std::sin (juce::MathConstants<double>::twoPi * ph);
        const float depth = value ("depth") / 100.0f, centre = value ("delay");
        switch (mode)
        {
            case 0:  return centre + centre * 0.45f * depth * s;
            case 1:  return centre * 0.9f + centre * 0.35f * depth * s;
            case 2:  { const float c = 0.3f + centre * 0.12f; return juce::jmax (0.08f, c * (1.0f + 0.95f * depth * s)); }
            case 3:  return 150.0f * std::pow (2.0f, (s + 1.0f) * 0.5f * (1.0f + depth * 4.0f));     // Hz du phaser
            default: return 5.0f + 4.0f * depth * s;
        }
    }

    void ModulationPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || line[0].empty()) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        if (chans == 0) return;
        const int mode = juce::jlimit (0, 4, juce::roundToInt (value ("mode")));
        const double inc = value ("rate") / rate;
        const float fb = value ("feedback") / 100.0f, width = value ("width") / 100.0f, mix = value ("mix") / 100.0f;
        const float wetLPc = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 7000.0f / (float) rate);
        auto readAt = [this] (int c, float delaySamples)
        {
            // Lecture interpolée (Hermite à 4 points) dans la ligne à retard.
            const float pos = (float) writePos - delaySamples;
            const int i = (int) std::floor (pos);
            const float t = pos - (float) i;
            const auto& l = line[c];
            const float y0 = l[(size_t) ((i - 1) & mask)], y1 = l[(size_t) (i & mask)], y2 = l[(size_t) ((i + 1) & mask)], y3 = l[(size_t) ((i + 2) & mask)];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        };
        const float msToS = (float) rate / 1000.0f;
        float apCoef[2] {};
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < chans; ++c)
            {
                float* d = buf.getWritePointer (c, start);
                const float x = d[i];
                const double ph = phase + (c == 1 ? 0.25 * width : 0.0);
                float wet = 0;
                switch (mode)
                {
                    case 0:
                        wet = 0.5f * (readAt (c, delayMsAt (0, ph) * msToS) + readAt (c, delayMsAt (0, ph + 0.5) * msToS));
                        line[c][(size_t) writePos] = x;
                        break;
                    case 1:
                    {
                        const float wobble = 0.15f * (float) std::sin (juce::MathConstants<double>::twoPi * (phase2 + c * 0.3));
                        for (int v = 0; v < 3; ++v) wet += readAt (c, (delayMsAt (1, ph + v / 3.0) + wobble) * msToS);
                        wet *= 0.4f;
                        wetLP[c] += wetLPc * (wet - wetLP[c]); wet = wetLP[c];
                        line[c][(size_t) writePos] = x;
                        break;
                    }
                    case 2:
                        wet = readAt (c, delayMsAt (2, ph) * msToS);
                        line[c][(size_t) writePos] = x + fb * wet;
                        break;
                    case 3:
                    {
                        if ((i & 7) == 0)
                        {
                            const float t = std::tan (juce::MathConstants<float>::pi * juce::jmin (delayMsAt (3, ph), (float) rate * 0.45f) / (float) rate);
                            apCoef[c] = (t - 1.0f) / (t + 1.0f);
                        }
                        float s = x + fb * fbState[c];
                        for (auto& z : ap[c]) { const float y = apCoef[c] * s + z; z = s - apCoef[c] * y; s = y; }
                        fbState[c] = s;
                        wet = s;
                        break;
                    }
                    default:
                        wet = readAt (c, delayMsAt (4, ph) * msToS);
                        line[c][(size_t) writePos] = x;
                        break;
                }
                d[i] = x * (1.0f - mix) + wet * mix;
            }
            writePos = (writePos + 1) & mask;
            phase += inc; if (phase >= 1.0) phase -= 1.0;
            phase2 += inc * 6.3; if (phase2 >= 1.0) phase2 -= 1.0;
        }
        lfoPhase = (float) phase;
    }

    namespace
    {
        // Fenêtre : choix de l'effet, courbe de l'oscillateur (gauche et droite) qui défile, réglages, préréglages.
        class ModulationEditor  : public FxWindow
        {
        public:
            explicit ModulationEditor (ModulationPlugin& p) : FxWindow (p, U8 ("Chorus, ensemble, flanger, phaser, vibrato · largeur stéréo"), juce::Colour (0xff5ad1e6)), mod (p)
            {
                for (int i = 0; i < 5; ++i)
                {
                    auto* b = modeButtons.add (new juce::TextButton (ModulationPlugin::modeNames[i]));
                    b->setColour (juce::TextButton::buttonOnColourId, colour);
                    b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
                    b->onClick = [this, i] { mod.edit.getUndoManager().beginNewTransaction (U8 ("Effet de modulation")); mod.setStored ("mode", (float) i); };
                    modes.addAndMakeVisible (b);
                }
                addAndMakeVisible (modes);
                knob ("rate", "Vitesse"); knob ("depth", "Profondeur"); knob ("delay", "Retard");
                knob ("feedback", "Réinjection"); knob ("width", "Largeur"); knob ("mix", "Mélange");
                preset ("Chorus doux", { { "mode", 0 }, { "rate", 0.6f }, { "depth", 35 }, { "delay", 14 }, { "feedback", 0 }, { "width", 80 }, { "mix", 45 } });
                preset (U8 ("Ensemble à cordes"), { { "mode", 1 }, { "rate", 0.5f }, { "depth", 60 }, { "delay", 10 }, { "feedback", 0 }, { "width", 100 }, { "mix", 60 } });
                preset ("Flanger jet", { { "mode", 2 }, { "rate", 0.15f }, { "depth", 80 }, { "delay", 12 }, { "feedback", 70 }, { "width", 50 }, { "mix", 50 } });
                preset ("Phaser lent", { { "mode", 3 }, { "rate", 0.25f }, { "depth", 55 }, { "delay", 12 }, { "feedback", 45 }, { "width", 60 }, { "mix", 50 } });
                preset ("Vibrato", { { "mode", 4 }, { "rate", 5.5f }, { "depth", 30 }, { "delay", 12 }, { "feedback", 0 }, { "width", 0 }, { "mix", 100 } });
                setSize (860, 460);
            }
            void resized() override
            {
                layout (30, &modes);
                const int w = modes.getWidth() / 5;
                for (int i = 0; i < modeButtons.size(); ++i) modeButtons[i]->setBounds (i * w, 0, w - 4, modes.getHeight());
            }
        private:
            void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
            {
                const int mode = juce::jlimit (0, 4, juce::roundToInt (mod.value ("mode")));
                const float rateHz = mod.value ("rate"), width = mod.value ("width") / 100.0f;
                auto info = r.removeFromBottom (18.0f);
                const float span = juce::jlimit (1.0f, 8.0f, 2.0f / juce::jmax (0.05f, rateHz));      // deux cycles affichés
                // Échelle verticale : retard (ms) ou fréquence (Hz, logarithmique) selon l'effet.
                float lo = 1e9f, hi = -1e9f;
                for (int k = 0; k < 64; ++k) { const float v = mod.delayMsAt (mode, k / 64.0); lo = std::min (lo, v); hi = std::max (hi, v); }
                if (mode == 3) { lo = std::log (lo); hi = std::log (hi); }
                if (hi - lo < 1.0e-3f) { lo -= 1.0f; hi += 1.0f; }
                const float pad = (hi - lo) * 0.15f; lo -= pad; hi += pad;
                auto yOf = [&] (float v) { if (mode == 3) v = std::log (v); return r.getBottom() - (v - lo) / (hi - lo) * r.getHeight(); };
                g.setColour (gridLine);
                for (int k = 1; k < 4; ++k) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * k / 4.0f), r.getX(), r.getRight());
                const double now = mod.lfoPhase.load();
                const int voices = mode == 0 ? 2 : mode == 1 ? 3 : 1;
                const juce::Colour right (0xffb98cff);
                for (int ch = 1; ch >= 0; --ch)
                    for (int v = 0; v < voices; ++v)
                    {
                        juce::Path p;
                        for (int i = 0; i <= 240; ++i)
                        {
                            const double ph = now - (1.0 - i / 240.0) * span * rateHz + (ch == 1 ? 0.25 * width : 0.0) + (double) v / voices;
                            const float x = r.getX() + r.getWidth() * i / 240.0f, y = yOf (mod.delayMsAt (mode, ph));
                            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                        }
                        const auto c = ch == 0 ? colour : right;
                        g.setColour (c.withAlpha (v == 0 ? 0.95f : 0.45f));
                        g.strokePath (p, juce::PathStrokeType (v == 0 ? 2.0f : 1.2f));
                        if (v == 0)
                        {
                            const float y = yOf (mod.delayMsAt (mode, now + (ch == 1 ? 0.25 * width : 0.0)));
                            g.fillEllipse (r.getRight() - 5.0f, y - 5.0f, 10.0f, 10.0f);
                        }
                    }
                g.setFont (theme::ui (11.0f, true));
                g.setColour (colour); g.drawText ("G", (int) r.getX() + 4, (int) r.getY() + 2, 20, 14, juce::Justification::left);
                g.setColour (right); g.drawText ("D", (int) r.getX() + 18, (int) r.getY() + 2, 20, 14, juce::Justification::left);
                g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
                const float a = mod.delayMsAt (mode, 0.75), b = mod.delayMsAt (mode, 0.25);
                const juce::String range = mode == 3 ? juce::String (juce::roundToInt (a)) + U8 (" à ") + juce::String (juce::roundToInt (b)) + U8 (" Hz (creux du phaser)")
                                                     : juce::String (a, 1) + U8 (" à ") + juce::String (b, 1) + U8 (" ms de retard");
                g.drawText (juce::String::fromUTF8 (ModulationPlugin::modeNames[mode]) + U8 (" · ") + range + U8 (" · un cycle toutes les ")
                            + juce::String (1.0f / rateHz, 2) + " s", info, juce::Justification::centredLeft);
            }
            void timerCallback() override
            {
                const int mode = juce::roundToInt (mod.value ("mode"));
                for (int i = 0; i < modeButtons.size(); ++i) modeButtons[i]->setToggleState (i == mode, juce::dontSendNotification);
                FxWindow::timerCallback();
            }
            ModulationPlugin& mod;
            juce::Component modes;
            juce::OwnedArray<juce::TextButton> modeButtons;
        };
    }

    std::unique_ptr<juce::Component> createModulationEditor (ModulationPlugin& m) { return std::make_unique<ModulationEditor> (m); }
}

namespace ondefx
{
    //==============================================================================
    const char* AmpPlugin::xmlTypeName = "ondeAmp";

    namespace
    {
        // Modèles : étages du préampli, gain (dB, bouton à 0 puis à 10), polarisation (asymétrie), passe-bas entre
        // étages, coupe-bas d'entrée (serrage), médium de l'égaliseur, attaque de l'étage de puissance.
        struct AmpModel { int stages; float minDb, maxDb, bias, interLP, preHP, midHz, power; };
        const AmpModel ampModels[5] = {
            { 1,  0.0f, 18.0f, 0.05f, 12000.0f,  70.0f, 650.0f, 0.6f },     // clair
            { 2,  6.0f, 32.0f, 0.15f,  9000.0f,  90.0f, 650.0f, 1.0f },     // crunch
            { 3, 12.0f, 48.0f, 0.20f,  7500.0f, 110.0f, 700.0f, 1.3f },     // lead
            { 4, 18.0f, 62.0f, 0.10f,  7000.0f, 160.0f, 550.0f, 1.2f },     // moderne
            { 2,  0.0f, 26.0f, 0.10f,  6000.0f,  30.0f, 500.0f, 0.8f } };   // basse
        struct Cab { float hp, hpQ, res, resDb, resQ, dip, dipDb, lp; };
        const Cab cabs[5] = {
            { 20, 0.7f,   0, 0, 1,    0,  0, 20000 },
            { 70, 0.9f, 110, 3, 1.2f, 0,  0,  6000 },
            { 75, 1.0f, 100, 4, 1.3f, 450, -1.5f, 5200 },
            { 85, 1.1f,  95, 5, 1.5f, 400, -2.5f, 4800 },
            { 40, 0.8f,  70, 3, 1.0f, 800, -3.0f, 3500 } };
    }

    void AmpPlugin::Biquad::set (int type, double f, double q, double gainDb, double fs)
    {
        f = juce::jlimit (10.0, fs * 0.45, f);
        const double w = juce::MathConstants<double>::twoPi * f / fs, cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (type)
        {
            case 0: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case 1: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case 2: B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case 3: { const double s = 2 * std::sqrt (A) * alpha;
                      B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                      A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s; break; }
            default: { const double s = 2 * std::sqrt (A) * alpha;
                      B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                      A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s; break; }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    float AmpPlugin::Biquad::magnitudeDb (double hz, double fs) const
    {
        const std::complex<double> z = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / fs);
        const auto num = (double) b0 + (double) b1 * z + (double) b2 * z * z, den = 1.0 + (double) a1 * z + (double) a2 * z * z;
        return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (num / den))));
    }

    AmpPlugin::AmpPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        auto tenth = [] (float v) { return juce::String (v, 1); };
        declare ("model", juce::String::fromUTF8 ("Modèle"), { 0.0f, 4.0f, 1.0f }, 1.0f, choiceText (modelNames, 5));
        declare ("gate", "Porte de bruit", { -90.0f, -30.0f }, -90.0f, [] (float v) { return v <= -89.5f ? juce::String::fromUTF8 ("coupée") : juce::String (juce::roundToInt (v)) + " dB"; });
        declare ("boost", "Boost", { 0.0f, 10.0f }, 0.0f, tenth);
        declare ("gain", "Gain", { 0.0f, 10.0f }, 5.0f, tenth);
        declare ("bass", "Graves", { 0.0f, 10.0f }, 5.0f, tenth);
        declare ("mid", juce::String::fromUTF8 ("Médiums"), { 0.0f, 10.0f }, 5.0f, tenth);
        declare ("treble", "Aigus", { 0.0f, 10.0f }, 5.0f, tenth);
        declare ("presence", juce::String::fromUTF8 ("Présence"), { 0.0f, 10.0f }, 5.0f, tenth);
        declare ("cab", "Baffle", { 0.0f, 4.0f, 1.0f }, 3.0f, choiceText (cabNames, 5));
        declare ("mic", "Micro (centre / bord)", { 0.0f, 1.0f }, 0.3f, [] (float v) { return v < 0.15f ? juce::String ("centre") : v > 0.85f ? juce::String ("bord") : juce::String (juce::roundToInt (v * 100)) + " %"; });
        declare ("level", "Volume", { -24.0f, 12.0f }, 0.0f, dbText);
        declare ("micType", "Type de micro", { 0.0f, 2.0f, 1.0f }, 0.0f, choiceText (micNames, 3));
        declare ("room", juce::String::fromUTF8 ("Micro d'ambiance"), { 0.0f, 100.0f }, 15.0f, pctText);
        declare ("sag", juce::String::fromUTF8 ("Affaissement (sag)"), { 0.0f, 10.0f }, 3.0f, tenth);
    }

    void AmpPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        preparedBlock = juce::jlimit (64, 8192, info.blockSizeSamples * 2);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) preparedBlock);
        int size = 1; while (size < (int) (rate * 0.08)) size <<= 1;        // 80 ms de réflexions
        for (auto& l : roomLine) l.assign ((size_t) size, 0.0f);
        roomMask = size - 1;
        filterKey.clear();
        reset();
    }

    void AmpPlugin::reset()
    {
        if (oversampling) oversampling->reset();
        for (auto* b : { &boostMid, &inputHP, &tight }) b->reset();
        for (auto& b : tone) b.reset();
        for (auto& b : cab) b.reset();
        for (int s = 0; s < 4; ++s) for (int c = 0; c < 2; ++c) stageHP[s][c] = stageHPx[s][c] = stageLP[s][c] = 0;
        gateEnv = 0; gateGain = 1; lowKeep[0] = lowKeep[1] = 0;
        for (auto& b : micEq) b.reset();
        for (auto& l : roomLine) std::fill (l.begin(), l.end(), 0.0f);
        roomPos = 0; sagEnv[0] = sagEnv[1] = roomLP[0] = roomLP[1] = 0;
    }

    double AmpPlugin::getLatencySeconds() { return oversampling ? oversampling->getLatencyInSamples() / rate : 0.0; }

    void AmpPlugin::updateFilters()
    {
        const int m = juce::jlimit (0, 4, juce::roundToInt (value ("model"))), c = juce::jlimit (0, 4, juce::roundToInt (value ("cab")));
        juce::String key; key << m << c;
        for (auto* id : { "bass", "mid", "treble", "presence", "mic", "boost", "micType" }) key << ";" << value (id);
        if (key == filterKey) return;
        filterKey = key;
        const auto& md = ampModels[m];
        const double fs = rate;
        inputHP.set (1, md.preHP, 0.7, 0, fs);
        boostMid.set (2, 720, 0.7, value ("boost") * 0.8, fs);
        tight.set (1, 60.0 + value ("boost") * 25.0, 0.7, 0, fs);              // le boost resserre le grave
        tone[0].set (3, m == 4 ? 80 : 120, 0.7, (value ("bass") - 5.0f) * 2.4f, fs);
        tone[1].set (2, md.midHz, 0.8, (value ("mid") - 5.0f) * 2.4f, fs);
        tone[2].set (4, 3000, 0.7, (value ("treble") - 5.0f) * 2.4f, fs);
        tone[3].set (4, 5500, 0.7, (value ("presence") - 5.0f) * 1.8f, fs);
        const auto& cb = cabs[c];
        const double lp = cb.lp * (1.0 - 0.4 * value ("mic"));
        cab[0].set (1, cb.hp, cb.hpQ, 0, fs);
        cab[1].set (2, juce::jmax (20.0f, cb.res), cb.resQ, cb.resDb, fs);
        cab[2].set (2, juce::jmax (20.0f, cb.dip), 1.0, cb.dipDb, fs);
        cab[3].set (0, lp, 0.75, 0, fs);
        cab[4].set (0, lp * 1.15, 0.9, 0, fs);
        // Couleur du micro : dynamique (bosse de présence), condensateur (plus d'air, plus de grave), ruban (doux, chaud).
        switch (juce::jlimit (0, 2, juce::roundToInt (value ("micType"))))
        {
            case 0:  micEq[0].set (3, 150, 0.7, -1.5, fs); micEq[1].set (2, 4500, 1.2, 3.5, fs); micEq[2].set (0, 11000, 0.7, 0, fs); break;
            case 1:  micEq[0].set (3, 120, 0.7, 1.5, fs);  micEq[1].set (2, 4000, 0.9, 1.0, fs); micEq[2].set (4, 7000, 0.7, 2.5, fs); break;
            default: micEq[0].set (3, 180, 0.7, 2.5, fs);  micEq[1].set (2, 2500, 0.9, -1.5, fs); micEq[2].set (4, 5000, 0.7, -5.0, fs); break;
        }
    }

    float AmpPlugin::responseDb (float hz) const
    {
        float db = 0;
        for (auto& b : tone) db += b.magnitudeDb (hz, rate);
        if (juce::roundToInt (value ("cab")) > 0) { for (auto& b : cab) db += b.magnitudeDb (hz, rate); for (auto& b : micEq) db += b.magnitudeDb (hz, rate); }
        return db;
    }

    void AmpPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || oversampling == nullptr) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels());
        if (chans == 0) return;
        updateFilters();
        const int m = juce::jlimit (0, 4, juce::roundToInt (value ("model")));
        const auto& md = ampModels[m];
        const bool withCab = juce::roundToInt (value ("cab")) > 0;
        const float gateDb = value ("gate"), boost = value ("boost");
        const float boostGain = dbToGain (boost * 1.8f);
        const float totalDb = md.minDb + (md.maxDb - md.minDb) * value ("gain") / 10.0f;
        const float perStage = dbToGain (totalDb / md.stages);
        const float osRate = (float) rate * 4.0f;
        const float lpC = onePoleCoef (md.interLP, osRate), hpC = std::exp (-juce::MathConstants<float>::twoPi * 30.0f / osRate);
        const float level = dbToGain (value ("level")) * (m == 0 ? 0.9f : 0.55f);
        const float gateThr = dbToGain (gateDb);
        const float atk = 1.0f - std::exp (-1.0f / (0.001f * (float) rate)), rel = 1.0f - std::exp (-1.0f / (0.06f * (float) rate));
        const float envRel = std::exp (-1.0f / (0.05f * (float) rate));
        const float keepC = onePoleCoef (220.0f, rate);
        const float sagAmt = value ("sag") * 0.18f, room = value ("room") / 100.0f;
        const float sagAtk = onePoleCoef (60.0f, rate), sagRel = onePoleCoef (5.0f, rate), roomC = onePoleCoef (4500.0f, rate);
        // Micro d'ambiance : premières réflexions de la pièce, différentes à gauche et à droite (largeur stéréo).
        static constexpr float tapMs[2][4] = { { 7.3f, 13.1f, 21.7f, 31.9f }, { 9.1f, 16.3f, 24.2f, 35.5f } };
        static constexpr float tapGain[4] = { 0.55f, 0.42f, 0.32f, 0.24f };
        int taps[2][4];
        for (int c = 0; c < 2; ++c) for (int t = 0; t < 4; ++t) taps[c][t] = (int) (tapMs[c][t] * 0.001f * (float) rate);
        {
            float pk = 0;
            for (int c = 0; c < chans; ++c) pk = std::max (pk, buf.getMagnitude (c, fc.bufferStartSample, fc.bufferNumSamples));
            inputDb = juce::Decibels::gainToDecibels (pk, -100.0f);
        }
        for (int done = 0; done < fc.bufferNumSamples;)
        {
            const int n = juce::jmin (preparedBlock, fc.bufferNumSamples - done);
            const int start = fc.bufferStartSample + done;
            // Entrée : porte de bruit, coupe-bas du modèle, boost ; graves propres gardés pour la basse.
            for (int i = 0; i < n; ++i)
            {
                float peak = 0;
                for (int c = 0; c < chans; ++c) peak = std::max (peak, std::abs (buf.getSample (c, start + i)));
                gateEnv = std::max (peak, gateEnv * envRel);
                const float target = gateDb <= -89.5f || gateEnv > gateThr ? 1.0f : 0.0f;
                gateGain += (target > gateGain ? atk : rel) * (target - gateGain);
                for (int c = 0; c < chans; ++c)
                {
                    float x = buf.getSample (c, start + i) * gateGain;
                    if (m == 4) lowKeep[c] += keepC * (x - lowKeep[c]);
                    x = inputHP.process (x, c);
                    if (boost > 0.01f) x = std::tanh (boostMid.process (tight.process (x, c), c) * boostGain);
                    buf.setSample (c, start + i, x);
                }
            }
            gateOpen = gateGain;
            // Préampli suréchantillonné : étages à lampe (tanh polarisée), liaison (coupe-bas), passe-bas entre étages.
            juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) chans, (size_t) start, (size_t) n);
            auto up = oversampling->processSamplesUp (block);
            const float biasOut = std::tanh (md.bias);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                float* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    float x = d[i];
                    for (int s = 0; s < md.stages; ++s)
                    {
                        x = std::tanh (x * perStage + md.bias) - biasOut;
                        const float hp = hpC * (stageHP[s][c] + x - stageHPx[s][c]);
                        stageHPx[s][c] = x; stageHP[s][c] = hp; x = hp;
                        stageLP[s][c] += lpC * (x - stageLP[s][c]); x = stageLP[s][c];
                    }
                    d[i] = x;
                }
            }
            oversampling->processSamplesDown (block);
            // Égaliseur, étage de puissance, baffle, volume.
            for (int c = 0; c < chans; ++c)
            {
                float* d = buf.getWritePointer (c, start);
                for (int i = 0; i < n; ++i)
                {
                    float y = d[i];
                    for (auto& b : tone) y = b.process (y, c);
                    // Étage de puissance : l'alimentation s'affaisse quand on joue fort (compression, gonflement de la note).
                    y /= 1.0f + sagAmt * sagEnv[c];
                    y = std::tanh (y * md.power) / md.power;
                    const float a = std::abs (y);
                    sagEnv[c] += (a > sagEnv[c] ? sagAtk : sagRel) * (a - sagEnv[c]);
                    if (m == 4) y += lowKeep[c] * 0.6f;                      // basse : fondamental propre conservé
                    if (withCab)
                    {
                        for (auto& b : cab) y = b.process (y, c);
                        for (auto& b : micEq) y = b.process (y, c);
                        if (room > 0.001f && ! roomLine[0].empty())
                        {
                            const int pos = roomPos + i;
                            auto& line = roomLine[c];
                            line[(size_t) (pos & roomMask)] = y;
                            float r = 0;
                            for (int t = 0; t < 4; ++t) r += line[(size_t) ((pos - taps[c][t]) & roomMask)] * tapGain[t];
                            roomLP[c] += roomC * (r - roomLP[c]);
                            y = y * (1.0f - 0.35f * room) + roomLP[c] * room;
                        }
                    }
                    d[i] = y * level;
                }
            }
            roomPos = (roomPos + n) & roomMask;
            done += n;
        }
    }

    namespace
    {
        // Fenêtre : modèles, façade de l'ampli (lampe qui s'allume avec le gain), réponse égaliseur + baffle.
        class AmpEditor  : public FxWindow
        {
        public:
            explicit AmpEditor (AmpPlugin& p) : FxWindow (p, U8 ("Ampli guitare et basse · porte, boost, préampli à lampes, sag, baffle, micro et pièce"), juce::Colour (0xffe5a04e)), amp (p)
            {
                for (int i = 0; i < 5; ++i)
                {
                    auto* b = modelButtons.add (new juce::TextButton (AmpPlugin::modelNames[i]));
                    b->setColour (juce::TextButton::buttonOnColourId, colour);
                    b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
                    b->onClick = [this, i] { amp.edit.getUndoManager().beginNewTransaction (U8 ("Modèle d'ampli")); amp.setStored ("model", (float) i); };
                    models.addAndMakeVisible (b);
                }
                addAndMakeVisible (models);
                knob ("gate", "Porte"); knob ("boost", "Boost"); knob ("gain", "Gain");
                knob ("bass", "Graves"); knob ("mid", "Médiums"); knob ("treble", "Aigus");
                knob ("presence", "Présence"); knob ("sag", "Sag"); knob ("level", "Volume");
                knob ("mic", "Micro centre/bord"); knob ("room", "Ambiance");
                auto* micChoice = new ondeui::ParamChoice (amp.param ("micType"), "Micro");
                controls.add (micChoice); addAndMakeVisible (micChoice);
                auto* cabChoice = new ondeui::ParamChoice (amp.param ("cab"), "Baffle");
                controls.add (cabChoice); addAndMakeVisible (cabChoice);
                preset ("Clair brillant", { { "model", 0 }, { "gate", -90 }, { "boost", 0 }, { "gain", 4 }, { "bass", 5 }, { "mid", 5 }, { "treble", 6.5f }, { "presence", 6 }, { "cab", 2 }, { "mic", 0.3f }, { "micType", 1 }, { "room", 28 }, { "sag", 4 } });
                preset ("Crunch rock", { { "model", 1 }, { "gate", -90 }, { "boost", 0 }, { "gain", 6 }, { "bass", 5 }, { "mid", 6.5f }, { "treble", 6 }, { "presence", 5 }, { "cab", 2 }, { "mic", 0.35f }, { "micType", 0 }, { "room", 20 }, { "sag", 5 } });
                preset (U8 ("Rock moderne épais"), { { "model", 2 }, { "gate", -70 }, { "boost", 3 }, { "gain", 6.5f }, { "bass", 6 }, { "mid", 6 }, { "treble", 5.5f }, { "presence", 5.5f }, { "cab", 3 }, { "mic", 0.4f }, { "micType", 2 }, { "room", 15 }, { "sag", 4 } });
                preset (U8 ("Métal serré"), { { "model", 3 }, { "gate", -58 }, { "boost", 7 }, { "gain", 6 }, { "bass", 5.5f }, { "mid", 4.5f }, { "treble", 6 }, { "presence", 6.5f }, { "cab", 3 }, { "mic", 0.25f }, { "micType", 0 }, { "room", 8 }, { "sag", 2 } });
                preset ("Djent", { { "model", 3 }, { "gate", -52 }, { "boost", 9 }, { "gain", 5 }, { "bass", 4.5f }, { "mid", 6 }, { "treble", 6.5f }, { "presence", 7 }, { "cab", 3 }, { "mic", 0.15f }, { "micType", 0 }, { "room", 5 }, { "sag", 1 } });
                preset ("Basse grasse", { { "model", 4 }, { "gate", -90 }, { "boost", 0 }, { "gain", 5 }, { "bass", 6 }, { "mid", 5.5f }, { "treble", 4.5f }, { "presence", 4 }, { "cab", 4 }, { "mic", 0.3f }, { "micType", 1 }, { "room", 10 }, { "sag", 3 } });
                setSize (940, 590);
            }
            void resized() override
            {
                layout (30, &models);
                const int w = models.getWidth() / 5;
                // Choix du micro et du baffle empilés dans la dernière case de la grille.
                std::vector<juce::Component*> choices;
                for (auto* c : controls) if (dynamic_cast<ondeui::ParamChoice*> (c) != nullptr) choices.push_back (c);
                if (! choices.empty())
                {
                    auto cell = choices.front()->getBounds();
                    for (size_t i = 0; i < choices.size(); ++i)
                        choices[i]->setBounds (cell.getX(), cell.getY() + (int) i * 46, cell.getWidth(), 42);
                }
                for (int i = 0; i < modelButtons.size(); ++i) modelButtons[i]->setBounds (i * w, 0, w - 4, models.getHeight());
            }
        private:
            void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
            {
                const int m = juce::jlimit (0, 4, juce::roundToInt (amp.value ("model")));
                const float gain = amp.value ("gain") / 10.0f;
                // Façade : tête d'ampli, grille, lampes qui rougeoient avec le gain, voyant de la porte de bruit.
                auto head = r.removeFromTop (r.getHeight() * 0.42f).reduced (4.0f, 2.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2622), 0, head.getY(), juce::Colour (0xff15130f), 0, head.getBottom(), false));
                g.fillRoundedRectangle (head, 8.0f);
                g.setColour (colour.withAlpha (0.5f)); g.drawRoundedRectangle (head, 8.0f, 1.2f);
                auto panel = head.reduced (12.0f, 10.0f);
                auto grille = panel.removeFromLeft (panel.getWidth() * 0.42f);
                g.setColour (juce::Colour (0xff3a332a));
                for (float y = grille.getY(); y < grille.getBottom(); y += 5.0f) g.drawHorizontalLine ((int) y, grille.getX(), grille.getRight());
                panel.removeFromLeft (14.0f);
                g.setColour (colour); g.setFont (theme::ui (22.0f, true));
                g.drawText (juce::String::fromUTF8 ("ONDE ") + juce::String::fromUTF8 (AmpPlugin::modelNames[m]).toUpperCase(), panel.removeFromTop (30.0f), juce::Justification::centredLeft);
                for (int k = 0; k < 4; ++k)
                {
                    auto tube = juce::Rectangle<float> (panel.getX() + k * 26.0f, panel.getY() + 6.0f, 16.0f, 30.0f);
                    g.setColour (juce::Colour (0xff3b3b3b)); g.fillRoundedRectangle (tube, 7.0f);
                    const float glow = juce::jlimit (0.0f, 1.0f, k < ampModels[m].stages ? 0.25f + 0.75f * gain : 0.1f);
                    g.setColour (juce::Colour (0xffff7a2a).withAlpha (glow)); g.fillEllipse (tube.reduced (4.0f, 8.0f));
                }
                const bool open = amp.gateOpen.load() > 0.5f || amp.value ("gate") <= -89.5f;
                g.setColour (open ? theme::ok : theme::rec); g.fillEllipse (panel.getRight() - 14.0f, panel.getY() + 10.0f, 9.0f, 9.0f);
                g.setColour (theme::muted); g.setFont (theme::ui (10.5f));
                g.drawText (open ? U8 ("porte ouverte") : U8 ("porte fermée"), (int) panel.getRight() - 110, (int) panel.getY() + 22, 104, 14, juce::Justification::centredRight);
                g.drawText (juce::String::fromUTF8 (AmpPlugin::cabNames[juce::jlimit (0, 4, juce::roundToInt (amp.value ("cab")))]), (int) panel.getX(), (int) panel.getBottom() - 14, 200, 14, juce::Justification::centredLeft);
                // Réponse : égaliseur de l'ampli + baffle.
                r.removeFromTop (8.0f);
                g.setColour (theme::dim); g.setFont (theme::ui (10.0f, true));
                g.drawText (U8 ("RÉPONSE DE L'ÉGALISEUR ET DU BAFFLE"), r.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft);
                auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / 40.0f) / std::log (500.0f); };
                auto yOf = [&] (float db) { return r.getCentreY() - juce::jlimit (-30.0f, 18.0f, db) * r.getHeight() / 48.0f; };
                g.setColour (gridLine);
                for (float f : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine ((int) xOf (f), r.getY(), r.getBottom());
                g.drawHorizontalLine ((int) yOf (0.0f), r.getX(), r.getRight());
                juce::Path p;
                for (int i = 0; i <= 160; ++i)
                {
                    const float f = 40.0f * std::pow (500.0f, i / 160.0f);
                    const float x = xOf (f), y = yOf (amp.responseDb (f));
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (colour); g.strokePath (p, juce::PathStrokeType (2.0f));
                g.setColour (theme::dim); g.setFont (theme::ui (9.5f));
                for (auto [f, t] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
                    g.drawText (t, (int) xOf (f) + 3, (int) r.getBottom() - 12, 30, 12, juce::Justification::centredLeft);
            }
            void timerCallback() override
            {
                const int m = juce::roundToInt (amp.value ("model"));
                for (int i = 0; i < modelButtons.size(); ++i) modelButtons[i]->setToggleState (i == m, juce::dontSendNotification);
                FxWindow::timerCallback();
            }
            AmpPlugin& amp;
            juce::Component models;
            juce::OwnedArray<juce::TextButton> modelButtons;
        };
    }

    std::unique_ptr<juce::Component> createAmpEditor (AmpPlugin& a) { return std::make_unique<AmpEditor> (a); }
}
