#include "OndeEffects.h"

namespace ondefx
{
    //==============================================================================
    // Formules de filtres « Audio EQ Cookbook » (R. Bristow-Johnson).
    Biquad design (FilterType type, double fs, double freq, double q, double gainDb)
    {
        Biquad f;
        freq = juce::jlimit (5.0, fs * 0.49, freq);
        q = juce::jmax (0.05, q);
        const double w0 = juce::MathConstants<double>::twoPi * freq / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type)
        {
            case FilterType::peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                break;
            case FilterType::lowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;       a1 = -2 * ((A - 1) + (A + 1) * cw);    a2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case FilterType::highShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;       a1 = 2 * ((A - 1) - (A + 1) * cw);      a2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            case FilterType::highPass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha;    a1 = -2 * cw;   a2 = 1 - alpha;
                break;
            case FilterType::lowPass:
                b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2;
                a0 = 1 + alpha;    a1 = -2 * cw;   a2 = 1 - alpha;
                break;
        }
        f.b0 = (float) (b0 / a0); f.b1 = (float) (b1 / a0); f.b2 = (float) (b2 / a0);
        f.a1 = (float) (a1 / a0); f.a2 = (float) (a2 / a0);
        return f;
    }

    double magnitudeDb (const Biquad& f, double fs, double freq)
    {
        const double w = juce::MathConstants<double>::twoPi * freq / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
        const auto num = (double) f.b0 + (double) f.b1 * z1 + (double) f.b2 * z2;
        const auto den = 1.0 + (double) f.a1 * z1 + (double) f.a2 * z2;
        return 20.0 * std::log10 (std::max (1.0e-9, std::abs (num / den)));
    }

    //==============================================================================
    OndeEffect::~OndeEffect()
    {
        notifyListenersOfDeletion();
        for (auto* p : params) p->param->detachFromCurrentValue();
    }

    te::AutomatableParameter* OndeEffect::declare (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range,
                                                   float def, std::function<juce::String (float)> format)
    {
        auto* p = params.add (new Param());
        p->value.referTo (state, juce::Identifier (id), getUndoManager(), def);
        p->param = addParam (id, name, range, std::move (format), [] (const juce::String& s) { return s.getFloatValue(); });
        p->param->attachToCurrentValue (p->value);
        ids.add (id);
        return p->param.get();
    }

    te::AutomatableParameter* OndeEffect::param (const juce::String& id) const
    {
        const int i = ids.indexOf (id);
        return i >= 0 ? params[i]->param.get() : nullptr;
    }

    namespace
    {
        juce::String hz (float v)   { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
        juce::String db (float v)   { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
        juce::String ms (float v)   { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
        juce::NormalisableRange<float> freqRange (float lo, float hi)
        {
            juce::NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (std::sqrt (lo * hi));   // réglage logarithmique
            return r;
        }
        float dbToGain (float d) { return std::pow (10.0f, d / 20.0f); }
        float gainToDb (float g) { return 20.0f * std::log10 (std::max (1.0e-6f, g)); }
    }

    //==============================================================================
    const char* EqPlugin::xmlTypeName = "ondeEQ";

    FilterType EqPlugin::typeOf (int band)
    {
        switch (band)
        {
            case 0:  return FilterType::highPass;
            case 1:  return FilterType::lowShelf;
            case 5:  return FilterType::highShelf;
            case 6:  return FilterType::lowPass;
            default: return FilterType::peak;
        }
    }

    EqPlugin::EqPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        const float freqs[numBands] = { 30.0f, 100.0f, 300.0f, 1000.0f, 3500.0f, 10000.0f, 18000.0f };
        const char* names[numBands] = { "Coupe-bas", "Grave", "Bas-médium", "Médium", "Haut-médium", "Aigu", "Coupe-haut" };
        for (int b = 0; b < numBands; ++b)
        {
            const auto n = juce::String::fromUTF8 (names[b]);
            const bool cut = b == 0 || b == numBands - 1;
            declare (bandId (b, "on"), n + " actif", { 0.0f, 1.0f, 1.0f }, cut ? 0.0f : 1.0f, [] (float v) { return v > 0.5f ? "oui" : "non"; });
            declare (bandId (b, "freq"), n + juce::String::fromUTF8 (" : fréquence"), freqRange (20.0f, 20000.0f), freqs[b], hz);
            if (! cut)
            {
                declare (bandId (b, "gain"), n + " : gain", { -24.0f, 24.0f }, 0.0f, db);
                declare (bandId (b, "q"), n + " : Q", juce::NormalisableRange<float> (0.1f, 10.0f, 0.0f, 0.4f), b == 1 || b == 5 ? 0.71f : 1.0f,
                         [] (float v) { return juce::String (v, 2); });
            }
        }
        declare ("out", "Sortie", { -24.0f, 24.0f }, 0.0f, db);
    }

    void EqPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        updateFilters (true);
        reset();
    }

    void EqPlugin::reset()
    {
        for (auto& band : filters) for (auto& f : band) f.reset();
    }

    void EqPlugin::updateFilters (bool force)
    {
        for (int b = 0; b < numBands; ++b)
        {
            const bool cut = b == 0 || b == numBands - 1;
            const float on = value (bandId (b, "on")), f = value (bandId (b, "freq"));
            const float g = cut ? 0.0f : value (bandId (b, "gain")), q = cut ? 0.71f : value (bandId (b, "q"));
            if (! force && shown[b][0] == on && shown[b][1] == f && shown[b][2] == g && shown[b][3] == q) continue;
            shown[b][0] = on; shown[b][1] = f; shown[b][2] = g; shown[b][3] = q;
            if (cut)
            {
                // 24 dB/octave : deux étages de Butterworth (Q 0,541 et 1,307).
                for (int s = 0; s < 2; ++s)
                {
                    auto nf = design (typeOf (b), rate, f, s == 0 ? 0.5412 : 1.3066, 0.0);
                    std::copy (std::begin (filters[b][s].z1), std::end (filters[b][s].z1), std::begin (nf.z1));
                    std::copy (std::begin (filters[b][s].z2), std::end (filters[b][s].z2), std::begin (nf.z2));
                    filters[b][s] = nf;
                }
            }
            else
            {
                auto nf = design (typeOf (b), rate, f, q, g);
                std::copy (std::begin (filters[b][0].z1), std::end (filters[b][0].z1), std::begin (nf.z1));
                std::copy (std::begin (filters[b][0].z2), std::end (filters[b][0].z2), std::begin (nf.z2));
                filters[b][0] = nf;
            }
        }
    }

    void EqPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr) return;
        updateFilters (false);
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        const float outGain = dbToGain (value ("out"));
        for (int b = 0; b < numBands; ++b)
        {
            if (shown[b][0] < 0.5f) continue;
            const bool cut = b == 0 || b == numBands - 1;
            if (! cut && std::abs (shown[b][2]) < 0.01f) continue;     // cloche ou étagère à 0 dB : transparente
            for (int c = 0; c < chans; ++c)
            {
                float* d = buf.getWritePointer (c, start);
                for (int s = 0; s < (cut ? 2 : 1); ++s)
                {
                    auto& f = filters[b][s];
                    for (int i = 0; i < n; ++i) d[i] = f.process (d[i], c);
                }
            }
        }
        if (std::abs (outGain - 1.0f) > 1.0e-4f) for (int c = 0; c < chans; ++c) buf.applyGain (c, start, n, outGain);

        // Analyseur : somme mono après l'égaliseur (perdue si la fenêtre ne lit pas assez vite).
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto mono = [&] (int i) { float v = buf.getSample (0, start + i); if (chans > 1) v = 0.5f * (v + buf.getSample (1, start + i)); return v; };
        for (int i = 0; i < n1; ++i) fifoData[(size_t) (s1 + i)] = mono (i);
        for (int i = 0; i < n2; ++i) fifoData[(size_t) (s2 + i)] = mono (n1 + i);
        fifo.finishedWrite (n1 + n2);
    }

    double EqPlugin::responseDb (double freq) const
    {
        double total = value ("out");
        for (int b = 0; b < numBands; ++b)
        {
            if (value (bandId (b, "on")) < 0.5f) continue;
            const bool cut = b == 0 || b == numBands - 1;
            if (cut)
                for (int s = 0; s < 2; ++s) total += magnitudeDb (design (typeOf (b), rate, value (bandId (b, "freq")), s == 0 ? 0.5412 : 1.3066, 0.0), rate, freq);
            else
                total += magnitudeDb (design (typeOf (b), rate, value (bandId (b, "freq")), value (bandId (b, "q")), value (bandId (b, "gain"))), rate, freq);
        }
        return total;
    }

    int EqPlugin::readAnalyser (float* dest, int max)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (max, s1, n1, s2, n2);
        std::copy (fifoData.begin() + s1, fifoData.begin() + s1 + n1, dest);
        std::copy (fifoData.begin() + s2, fifoData.begin() + s2 + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    //==============================================================================
    const char* CompPlugin::xmlTypeName = "ondeComp";

    CompPlugin::CompPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        declare ("threshold", "Seuil", { -60.0f, 0.0f }, -18.0f, db);
        declare ("ratio", "Taux", juce::NormalisableRange<float> (1.0f, 20.0f, 0.0f, 0.4f), 4.0f, [] (float v) { return juce::String (v, 1) + ":1"; });
        declare ("attack", "Attaque", juce::NormalisableRange<float> (0.1f, 100.0f, 0.0f, 0.35f), 10.0f, ms);
        declare ("release", juce::String::fromUTF8 ("Relâchement"), juce::NormalisableRange<float> (10.0f, 1000.0f, 0.0f, 0.4f), 120.0f, ms);
        declare ("knee", "Genou", { 0.0f, 24.0f }, 6.0f, db);
        declare ("makeup", "Gain de sortie", { -12.0f, 24.0f }, 0.0f, db);
        declare ("mix", juce::String::fromUTF8 ("Mélange"), { 0.0f, 100.0f }, 100.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; });
        declare ("scHpf", juce::String::fromUTF8 ("Filtre de détection"), freqRange (20.0f, 500.0f), 20.0f,
                 [] (float v) { return v <= 20.5f ? juce::String ("off") : hz (v); });
    }

    void CompPlugin::initialise (const te::PluginInitialisationInfo& info) { rate = info.sampleRate; scFreq = -1; reset(); }

    float CompPlugin::transferDb (float x) const
    {
        const float t = value ("threshold"), r = value ("ratio"), k = value ("knee");
        float y = x;
        if (2.0f * (x - t) < -k) y = x;
        else if (2.0f * std::abs (x - t) <= k && k > 0.0f) y = x + (1.0f / r - 1.0f) * (x - t + k / 2.0f) * (x - t + k / 2.0f) / (2.0f * k);
        else y = t + (x - t) / r;
        return y + value ("makeup");
    }

    void CompPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        const float t = value ("threshold"), r = value ("ratio"), k = value ("knee");
        const float makeup = dbToGain (value ("makeup")), wet = value ("mix") / 100.0f;
        const float att = std::exp (-1.0f / (0.001f * value ("attack") * (float) rate));
        const float rel = std::exp (-1.0f / (0.001f * value ("release") * (float) rate));
        const float f = value ("scHpf");
        if (f != scFreq) { scFreq = f; auto nf = design (FilterType::highPass, rate, f, 0.71, 0.0); nf.z1[0] = sc.z1[0]; nf.z1[1] = sc.z1[1]; nf.z2[0] = sc.z2[0]; nf.z2[1] = sc.z2[1]; sc = nf; }
        const bool useSc = f > 20.5f;
        const float peakDecay = std::exp (-1.0f / (0.010f * (float) rate));
        float peakIn = 0, peakOut = 0, maxGr = 0;
        for (int i = 0; i < n; ++i)
        {
            // Détection : crête stéréo liée (filtrée si le filtre de détection est actif).
            float det = 0;
            for (int c = 0; c < chans; ++c)
            {
                float x = buf.getSample (c, start + i);
                peakIn = std::max (peakIn, std::abs (x));
                if (useSc) x = sc.process (x, c);
                det = std::max (det, std::abs (x));
            }
            // Suiveur de crête (montée immédiate, descente ~10 ms) : le niveau d'un sinus est sa crête, pas sa
            // valeur instantanée.
            env = det > env ? det : env * peakDecay + det * (1.0f - peakDecay);
            const float inDb = gainToDb (env);
            float target = 0;   // réduction voulue (dB, ≥ 0)
            if (2.0f * (inDb - t) < -k) target = 0;
            else if (k > 0 && 2.0f * std::abs (inDb - t) <= k) target = -(1.0f / r - 1.0f) * (inDb - t + k / 2.0f) * (inDb - t + k / 2.0f) / (2.0f * k);
            else target = (inDb - t) * (1.0f - 1.0f / r);
            grDb = target > grDb ? att * grDb + (1 - att) * target : rel * grDb + (1 - rel) * target;
            maxGr = std::max (maxGr, grDb);
            const float g = dbToGain (-grDb) * makeup;
            for (int c = 0; c < chans; ++c)
            {
                const float x = buf.getSample (c, start + i);
                const float y = wet * x * g + (1.0f - wet) * x;
                buf.setSample (c, start + i, y);
                peakOut = std::max (peakOut, std::abs (y));
            }
        }
        gainReductionDb = maxGr; inputDb = gainToDb (peakIn); outputDb = gainToDb (peakOut);
    }

    //==============================================================================
    const char* LimiterPlugin::xmlTypeName = "ondeLimiter";

    LimiterPlugin::LimiterPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        declare ("input", juce::String::fromUTF8 ("Gain d'entrée"), { 0.0f, 24.0f }, 0.0f, db);
        declare ("ceiling", "Plafond", { -12.0f, 0.0f }, -0.3f, [] (float v) { return juce::String (v, 1) + " dBFS"; });
        declare ("release", juce::String::fromUTF8 ("Relâchement"), juce::NormalisableRange<float> (1.0f, 500.0f, 0.0f, 0.4f), 60.0f, ms);
    }

    void LimiterPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        lookahead = juce::jmax (1, juce::roundToInt (lookaheadMs * 0.001 * rate));
        for (auto& d : delay) d.assign ((size_t) lookahead, 0.0f);
        peaks.assign ((size_t) lookahead, 0.0f);
        reset();
    }

    void LimiterPlugin::reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (peaks.begin(), peaks.end(), 0.0f);
        pos = 0; gain = 1.0f;
    }

    void LimiterPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || peaks.empty()) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        const float in = dbToGain (value ("input")), ceil = dbToGain (value ("ceiling"));
        const float relCoef = std::exp (-1.0f / (0.001f * value ("release") * (float) rate));
        const float attackStep = 1.0f / (float) lookahead;
        float minGain = 1.0f, peakOut = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // Crête entrante (après le gain d'entrée), mémorisée sur la fenêtre d'anticipation.
            float x[2] = { 0, 0 }, pk = 0;
            for (int c = 0; c < chans; ++c) { x[c] = buf.getSample (c, start + i) * in; pk = std::max (pk, std::abs (x[c])); }
            peaks[(size_t) pos] = pk;
            float windowPeak = 0;
            for (float p : peaks) windowPeak = std::max (windowPeak, p);
            const float target = windowPeak > ceil ? ceil / windowPeak : 1.0f;
            // Descente progressive sur la durée d'anticipation (le gain est déjà bas quand la crête sort),
            // remontée exponentielle selon le relâchement.
            if (target < gain) gain = std::max (target, gain - attackStep);
            else gain = target + (gain - target) * relCoef;
            minGain = std::min (minGain, gain);
            for (int c = 0; c < chans; ++c)
            {
                auto& d = delay[(size_t) c];
                float y = d[(size_t) pos] * gain;
                d[(size_t) pos] = x[c];
                y = juce::jlimit (-ceil, ceil, y);    // garantie finale : jamais au-dessus du plafond
                buf.setSample (c, start + i, y);
                peakOut = std::max (peakOut, std::abs (y));
            }
            pos = (pos + 1) % lookahead;
        }
        gainReductionDb = -gainToDb (minGain);
        outputDb = gainToDb (peakOut);
    }

    void registerTypes (te::Engine& engine)
    {
        auto& pm = engine.getPluginManager();
        pm.createBuiltInType<EqPlugin>();
        pm.createBuiltInType<CompPlugin>();
        pm.createBuiltInType<LimiterPlugin>();
    }
}
