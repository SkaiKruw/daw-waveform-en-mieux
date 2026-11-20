#include "OndeEffects.h"
#include "OndeSynth.h"
#include "OndeFx2.h"

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
            case FilterType::bandPass:            // gain de 0 dB au centre
                b0 = alpha;        b1 = 0;         b2 = -alpha;
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
    // Phase p : valeur reconstruite à l'instant (n - 6 + p/4) à partir des 12 derniers échantillons.
    // La phase 0 est l'échantillon lui-même ; les phases 1 à 3 sont les points entre deux échantillons.
    TruePeakDetector::TruePeakDetector()
    {
        const double pi = juce::MathConstants<double>::pi, half = 6.5;
        for (int p = 0; p < phases; ++p)
        {
            double sum = 0;
            for (int k = 0; k < taps; ++k)
            {
                const double u = k - 6.0 + p / 4.0;
                const double sinc = std::abs (u) < 1.0e-9 ? 1.0 : std::sin (pi * u) / (pi * u);
                const double w = std::abs (u) >= half ? 0.0 : 0.42 + 0.5 * std::cos (pi * u / half) + 0.08 * std::cos (2.0 * pi * u / half);
                coef[p][k] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (int k = 0; k < taps; ++k) coef[p][k] = (float) (coef[p][k] / sum);
        }
    }

    float TruePeakDetector::process (float x, int ch) noexcept
    {
        auto* h = hist[ch];
        for (int k = taps - 1; k > 0; --k) h[k] = h[k - 1];
        h[0] = x;
        float peak = std::abs (h[6]);
        for (int p = 1; p < phases; ++p)
        {
            float y = 0;
            for (int k = 0; k < taps; ++k) y += h[k] * coef[p][k];
            peak = std::max (peak, std::abs (y));
        }
        return peak;
    }

    void TruePeakDetector::reset() noexcept   { for (auto& h : hist) std::fill (std::begin (h), std::end (h), 0.0f); }

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
        juce::String onOff (float v) { return v > 0.5f ? "oui" : "non"; }
        juce::NormalisableRange<float> freqRange (float lo, float hi)
        {
            juce::NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (std::sqrt (lo * hi));   // réglage logarithmique
            return r;
        }
        juce::NormalisableRange<float> choice (int count)   { return { 0.0f, (float) (count - 1), 1.0f }; }
        std::function<juce::String (float)> names (const char* const* list, int count)
        {
            return [list, count] (float v) { return juce::String::fromUTF8 (list[juce::jlimit (0, count - 1, juce::roundToInt (v))]); };
        }
        float dbToGain (float d) { return std::pow (10.0f, d / 20.0f); }
        float gainToDb (float g) { return 20.0f * std::log10 (std::max (1.0e-6f, g)); }
        int index (float v)      { return juce::roundToInt (v); }
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

    const double* EqPlugin::cutQs (int slope)
    {
        // Butterworth d'ordre 2, 4 et 8 : Q de chaque étage du second ordre.
        static const double q12[] = { 0.70710678 };
        static const double q24[] = { 0.54119610, 1.30656296 };
        static const double q48[] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        return slope <= 0 ? q12 : slope == 1 ? q24 : q48;
    }

    EqPlugin::EqPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        const float freqs[numBands] = { 30.0f, 100.0f, 300.0f, 1000.0f, 3500.0f, 10000.0f, 18000.0f };
        const char* bandNames[numBands] = { "Coupe-bas", "Grave", "Bas-médium", "Médium", "Haut-médium", "Aigu", "Coupe-haut" };
        for (int b = 0; b < numBands; ++b)
        {
            const auto n = juce::String::fromUTF8 (bandNames[b]);
            const bool cut = isCut (b);
            declare (bandId (b, "on"), n + " actif", { 0.0f, 1.0f, 1.0f }, cut ? 0.0f : 1.0f, onOff);
            declare (bandId (b, "freq"), n + juce::String::fromUTF8 (" : fréquence"), freqRange (20.0f, 20000.0f), freqs[b], hz);
            if (cut)
            {
                declare (bandId (b, "slope"), n + " : pente", choice (3), 1.0f, names (slopeNames, 3));
            }
            else
            {
                declare (bandId (b, "gain"), n + " : gain", { -24.0f, 24.0f }, 0.0f, db);
                declare (bandId (b, "q"), n + " : Q", juce::NormalisableRange<float> (0.1f, 10.0f, 0.0f, 0.4f), b == 1 || b == 5 ? 0.71f : 1.0f,
                         [] (float v) { return juce::String (v, 2); });
                // Bande dynamique : au-dessus du seuil, le gain se déplace progressivement de « plage » dB.
                declare (bandId (b, "dyn"), n + " : dynamique", { 0.0f, 1.0f, 1.0f }, 0.0f, onOff);
                declare (bandId (b, "thr"), n + " : seuil", { -60.0f, 0.0f }, -30.0f, db);
                declare (bandId (b, "range"), n + " : plage", { -24.0f, 24.0f }, -6.0f, db);
            }
            declare (bandId (b, "mode"), n + " : canaux", choice (5), 0.0f, names (modeNames, 5));
        }
        declare ("out", "Sortie", { -24.0f, 24.0f }, 0.0f, db);
    }

    void EqPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        for (auto& s : shown) std::fill (std::begin (s), std::end (s), -1.0e9f);
        reset();
    }

    void EqPlugin::reset()
    {
        for (auto& band : filters) for (auto& f : band) f.reset();
        for (auto& f : detector) f.reset();
        for (int b = 0; b < numBands; ++b) { envDb[b] = -100.0f; dynAmount[b] = 0; dynamicDb[b] = 0; }
    }

    void EqPlugin::designBand (int b, float f, float q, float g, int stages)
    {
        // Les coefficients changent, l'état des filtres est conservé (aucun clic) ; un étage qui
        // s'ajoute (pente plus raide) part d'un état vide.
        const int before = shown[b][0] > 0.5f ? (isCut (b) ? stagesForSlope (index (shown[b][4])) : 1) : 0;
        if (isCut (b))
        {
            const double* qs = cutQs (index (shown[b][4] = (float) (stages == 1 ? 0 : stages == 2 ? 1 : 2)));
            for (int s = 0; s < stages; ++s)
            {
                filters[b][s].setCoefficients (design (typeOf (b), rate, f, qs[s], 0.0));
                if (s >= before) filters[b][s].reset();
            }
        }
        else
        {
            filters[b][0].setCoefficients (design (typeOf (b), rate, f, q, g));
            if (before == 0) filters[b][0].reset();
        }
        shown[b][0] = 1.0f; shown[b][1] = f; shown[b][2] = g; shown[b][3] = q;
    }

    void EqPlugin::runBand (int b, int stages, int mode, float* const* d, int chans, int start, int len) noexcept
    {
        auto filter = [&] (float* x, int ch, int count)
        {
            for (int s = 0; s < stages; ++s)
            {
                auto& f = filters[b][s];
                for (int i = 0; i < count; ++i) x[i] = f.process (x[i], ch);
            }
        };
        if (chans < 2)
        {
            if (mode == 2 || mode == 4) return;        // droite ou side : rien sur une source mono
            filter (d[0] + start, 0, len);
            return;
        }
        switch (mode)
        {
            case 1:  filter (d[0] + start, 0, len); break;
            case 2:  filter (d[1] + start, 1, len); break;
            case 3:
            case 4:
            {
                // Mid/Side : on code, on filtre la partie choisie, on décode (par paquets de 64).
                float part[64];
                for (int i0 = 0; i0 < len; i0 += 64)
                {
                    const int m = std::min (64, len - i0);
                    float* l = d[0] + start + i0;
                    float* r = d[1] + start + i0;
                    for (int i = 0; i < m; ++i) part[i] = mode == 3 ? 0.5f * (l[i] + r[i]) : 0.5f * (l[i] - r[i]);
                    float before[64];
                    std::copy (part, part + m, before);
                    filter (part, 0, m);
                    for (int i = 0; i < m; ++i)
                    {
                        const float delta = part[i] - before[i];
                        l[i] += delta;
                        r[i] += mode == 3 ? delta : -delta;
                    }
                }
                break;
            }
            default: filter (d[0] + start, 0, len); filter (d[1] + start, 1, len); break;
        }
    }

    void EqPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        if (chans == 0) return;
        float* d[2] = { buf.getWritePointer (0), chans > 1 ? buf.getWritePointer (1) : nullptr };
        constexpr int sub = 32;                                     // pas de recalcul des bandes dynamiques
        const float subSeconds = (float) (sub / rate);
        const float envAtt = std::exp (-subSeconds / 0.005f), envRel = std::exp (-subSeconds / 0.120f);

        for (int b = 0; b < numBands; ++b)
        {
            if (value (bandId (b, "on")) < 0.5f)
            {
                shown[b][0] = 0; dynAmount[b] = 0; dynamicDb[b] = 0; envDb[b] = -100.0f;
                continue;
            }
            const float f = value (bandId (b, "freq"));
            const int mode = index (value (bandId (b, "mode")));
            if (isCut (b))
            {
                const int slope = juce::jlimit (0, 2, index (value (bandId (b, "slope"))));
                const int stages = stagesForSlope (slope);
                if (shown[b][0] < 0.5f || shown[b][1] != f || index (shown[b][4]) != slope) designBand (b, f, 0.71f, 0.0f, stages);
                runBand (b, stages, mode, d, chans, start, n);
                continue;
            }
            const float g = value (bandId (b, "gain")), q = value (bandId (b, "q"));
            if (value (bandId (b, "dyn")) < 0.5f)
            {
                dynAmount[b] = 0; dynamicDb[b] = 0; envDb[b] = -100.0f;
                if (std::abs (g) < 0.01f) { shown[b][0] = 0; continue; }   // cloche ou étagère à 0 dB : transparente
                if (shown[b][0] < 0.5f || shown[b][1] != f || shown[b][2] != g || shown[b][3] != q) designBand (b, f, q, g, 1);
                runBand (b, 1, mode, d, chans, start, n);
                continue;
            }

            // Bande dynamique : détection par passe-bande à la fréquence et au Q de la bande, sur les canaux
            // de la bande (stéréo, gauche, droite, mid ou side).
            const float thr = value (bandId (b, "thr")), range = value (bandId (b, "range"));
            detector[b].setCoefficients (design (FilterType::bandPass, rate, f, typeOf (b) == FilterType::peak ? q : 0.71f, 0.0));
            for (int i0 = 0; i0 < n; i0 += sub)
            {
                const int m = std::min (sub, n - i0);
                float pk = 0;
                for (int i = 0; i < m; ++i)
                {
                    const int k = start + i0 + i;
                    const float l = d[0][k], r = chans > 1 ? d[1][k] : l;
                    switch (mode)
                    {
                        case 1:  pk = std::max (pk, std::abs (detector[b].process (l, 0))); break;
                        case 2:  pk = std::max (pk, std::abs (detector[b].process (r, 1))); break;
                        case 3:  pk = std::max (pk, std::abs (detector[b].process (0.5f * (l + r), 0))); break;
                        case 4:  pk = std::max (pk, std::abs (detector[b].process (0.5f * (l - r), 0))); break;
                        default: pk = std::max ({ pk, std::abs (detector[b].process (l, 0)), std::abs (detector[b].process (r, 1)) }); break;
                    }
                }
                const float lvl = gainToDb (pk);
                envDb[b] = lvl + (envDb[b] - lvl) * (lvl > envDb[b] ? envAtt : envRel);
                // Au-dessus du seuil, la plage s'applique progressivement sur 12 dB (pas d'effet d'interrupteur).
                dynAmount[b] = juce::jlimit (0.0f, 1.0f, (envDb[b] - thr) / 12.0f);
                const float eff = g + dynAmount[b] * range;
                if (shown[b][0] < 0.5f || shown[b][1] != f || std::abs (shown[b][2] - eff) > 0.02f || shown[b][3] != q)
                    designBand (b, f, q, eff, 1);
                runBand (b, 1, mode, d, chans, start + i0, m);
            }
            dynamicDb[b] = dynAmount[b] * range;
        }

        const float outGain = dbToGain (value ("out"));
        if (std::abs (outGain - 1.0f) > 1.0e-4f) for (int c = 0; c < chans; ++c) buf.applyGain (c, start, n, outGain);

        // Analyseur : somme mono après l'égaliseur (perdue si la fenêtre ne lit pas assez vite).
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto mono = [&] (int i) { float v = d[0][start + i]; if (chans > 1) v = 0.5f * (v + d[1][start + i]); return v; };
        for (int i = 0; i < n1; ++i) fifoData[(size_t) (s1 + i)] = mono (i);
        for (int i = 0; i < n2; ++i) fifoData[(size_t) (s2 + i)] = mono (n1 + i);
        fifo.finishedWrite (n1 + n2);
    }

    double EqPlugin::responseDb (double freq, bool withDynamics) const
    {
        double total = value ("out");
        for (int b = 0; b < numBands; ++b)
        {
            if (value (bandId (b, "on")) < 0.5f) continue;
            const float f = value (bandId (b, "freq"));
            if (isCut (b))
            {
                const int slope = juce::jlimit (0, 2, index (value (bandId (b, "slope"))));
                const double* qs = cutQs (slope);
                for (int s = 0; s < stagesForSlope (slope); ++s) total += magnitudeDb (design (typeOf (b), rate, f, qs[s], 0.0), rate, freq);
            }
            else
            {
                float g = value (bandId (b, "gain"));
                if (withDynamics && value (bandId (b, "dyn")) > 0.5f) g += dynamicDb[b].load();
                total += magnitudeDb (design (typeOf (b), rate, f, value (bandId (b, "q")), g), rate, freq);
            }
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
        static const char* detectNames[] = { "Crête", "RMS" };
        declare ("threshold", "Seuil", { -60.0f, 0.0f }, -18.0f, db);
        declare ("ratio", "Taux", juce::NormalisableRange<float> (1.0f, 20.0f, 0.0f, 0.4f), 4.0f, [] (float v) { return juce::String (v, 1) + ":1"; });
        declare ("attack", "Attaque", juce::NormalisableRange<float> (0.1f, 100.0f, 0.0f, 0.35f), 10.0f, ms);
        declare ("release", juce::String::fromUTF8 ("Relâchement"), juce::NormalisableRange<float> (10.0f, 1000.0f, 0.0f, 0.4f), 120.0f, ms);
        declare ("knee", "Genou", { 0.0f, 24.0f }, 6.0f, db);
        declare ("makeup", "Gain de sortie", { -12.0f, 24.0f }, 0.0f, db);
        declare ("mix", juce::String::fromUTF8 ("Mélange"), { 0.0f, 100.0f }, 100.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; });
        declare ("scHpf", juce::String::fromUTF8 ("Filtre de détection"), freqRange (20.0f, 500.0f), 20.0f,
                 [] (float v) { return v <= 20.5f ? juce::String ("off") : hz (v); });
        declare ("detect", juce::String::fromUTF8 ("Détection"), choice (2), 0.0f, names (detectNames, 2));
        declare ("autorel", juce::String::fromUTF8 ("Relâchement auto"), { 0.0f, 1.0f, 1.0f }, 0.0f, onOff);
        declare ("style", "Style", choice (3), 0.0f, names (styleNames, 3));
        declare ("link", juce::String::fromUTF8 ("Liaison stéréo"), { 0.0f, 100.0f }, 100.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; });
        declare ("lookahead", "Anticipation (5 ms)", { 0.0f, 1.0f, 1.0f }, 0.0f, onOff);
        declare ("listen", juce::String::fromUTF8 ("Écoute de la détection"), { 0.0f, 1.0f, 1.0f }, 0.0f, onOff);
    }

    void CompPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        scFreq = -1;
        delayLen = juce::jmax (1, juce::roundToInt (lookaheadMs * 0.001 * rate));
        for (auto& dl : delay) dl.assign ((size_t) delayLen, 0.0f);
        reset();
    }

    void CompPlugin::reset()
    {
        for (int c = 0; c < 2; ++c) { env[c] = rms[c] = grFast[c] = grSlow[c] = 0; }
        sc.reset();
        for (auto& dl : delay) std::fill (dl.begin(), dl.end(), 0.0f);
        delayPos = 0;
    }

    float CompPlugin::gainComputer (float x, float k) const
    {
        const float t = value ("threshold"), r = value ("ratio");
        if (2.0f * (x - t) < -k) return 0.0f;
        if (k > 0.0f && 2.0f * std::abs (x - t) <= k) return -(1.0f / r - 1.0f) * (x - t + k / 2.0f) * (x - t + k / 2.0f) / (2.0f * k);
        return (x - t) * (1.0f - 1.0f / r);
    }

    float CompPlugin::transferDb (float x) const
    {
        float k = value ("knee");
        if (index (value ("style")) == 2) k = std::max (k, 12.0f);
        return x - gainComputer (x, k) + value ("makeup");
    }

    void CompPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || delayLen == 0) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        if (chans == 0) return;
        const int style = juce::jlimit (0, 2, index (value ("style")));
        const bool rmsMode = index (value ("detect")) == 1, autoRel = value ("autorel") > 0.5f;
        const bool listen = value ("listen") > 0.5f, lookahead = value ("lookahead") > 0.5f;
        float k = value ("knee");
        if (style == 2) k = std::max (k, 12.0f);                    // vintage : genou large
        const float link = value ("link") / 100.0f;
        const float makeup = dbToGain (value ("makeup")), wet = value ("mix") / 100.0f;
        const float fs = (float) rate, relMs = value ("release");
        const float att = std::exp (-1.0f / (0.001f * value ("attack") * fs));
        const float relFixed = std::exp (-1.0f / (0.001f * relMs * fs));
        const float relFast = std::exp (-1.0f / (0.040f * fs)), relSlow = std::exp (-1.0f / (0.600f * fs));
        const float attSlow = std::exp (-1.0f / (0.150f * fs));
        const float peakDecay = std::exp (-1.0f / (0.010f * fs)), rmsCoef = std::exp (-1.0f / (0.010f * fs));
        const float f = value ("scHpf");
        if (f != scFreq) { scFreq = f; sc.setCoefficients (design (FilterType::highPass, rate, f, 0.71, 0.0)); }
        const bool useSc = f > 20.5f;
        constexpr float drive = 0.8f;                               // saturation douce du style vintage
        float peakIn = 0, peakOut = 0, maxGr = 0;
        for (int i = 0; i < n; ++i)
        {
            float x[2] {}, det[2] {}, lvl[2] {};
            for (int c = 0; c < chans; ++c)
            {
                x[c] = buf.getSample (c, start + i);
                peakIn = std::max (peakIn, std::abs (x[c]));
                det[c] = useSc ? sc.process (x[c], c) : x[c];
                if (rmsMode)
                {
                    rms[c] = rms[c] * rmsCoef + det[c] * det[c] * (1.0f - rmsCoef);
                    lvl[c] = std::sqrt (rms[c]);
                }
                else
                {
                    // Suiveur de crête (montée immédiate, descente ~10 ms) : le niveau d'un sinus est sa crête.
                    const float a = std::abs (det[c]);
                    env[c] = a > env[c] ? a : env[c] * peakDecay + a * (1.0f - peakDecay);
                    lvl[c] = env[c];
                }
            }
            if (chans == 1) lvl[1] = lvl[0];
            const float linked = std::max (lvl[0], lvl[1]);
            float gr[2] {};
            for (int c = 0; c < chans; ++c)
            {
                const float target = gainComputer (gainToDb (link * linked + (1.0f - link) * lvl[c]), k);
                if (autoRel)
                {
                    // Double enveloppe : un transitoire isolé se relâche vite (40 ms), une compression
                    // tenue se relâche lentement (600 ms), sans pompage.
                    grFast[c] = target > grFast[c] ? att * grFast[c] + (1 - att) * target : relFast * grFast[c] + (1 - relFast) * target;
                    grSlow[c] = target > grSlow[c] ? attSlow * grSlow[c] + (1 - attSlow) * target : relSlow * grSlow[c] + (1 - relSlow) * target;
                    gr[c] = std::max (grFast[c], grSlow[c]);
                }
                else
                {
                    float rel = relFixed;
                    if (style == 1)   // opto : plus la réduction est forte, plus le relâchement est lent
                        rel = std::exp (-1.0f / (0.001f * relMs * (1.0f + grFast[c] / 6.0f) * fs));
                    grFast[c] = target > grFast[c] ? att * grFast[c] + (1 - att) * target : rel * grFast[c] + (1 - rel) * target;
                    gr[c] = grFast[c];
                }
                maxGr = std::max (maxGr, gr[c]);
            }
            for (int c = 0; c < chans; ++c)
            {
                float dry = x[c];
                if (lookahead)
                {
                    auto& dl = delay[(size_t) c];
                    dry = dl[(size_t) delayPos];
                    dl[(size_t) delayPos] = x[c];
                }
                float y;
                if (listen) y = det[c];
                else
                {
                    float w = dry * dbToGain (-gr[c]) * makeup;
                    if (style == 2) w = std::tanh (w * drive) / drive;
                    y = wet * w + (1.0f - wet) * dry;
                }
                buf.setSample (c, start + i, y);
                peakOut = std::max (peakOut, std::abs (y));
            }
            if (lookahead) delayPos = (delayPos + 1) % delayLen;
        }
        gainReductionDb = maxGr; inputDb = gainToDb (peakIn); outputDb = gainToDb (peakOut);
    }

    //==============================================================================
    const char* LimiterPlugin::xmlTypeName = "ondeLimiter";

    LimiterPlugin::LimiterPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        static const char* targetNames[] = { "-14 LUFS (streaming)", "-16 LUFS (podcast)", "-23 LUFS (diffusion)" };
        declare ("input", juce::String::fromUTF8 ("Gain d'entrée"), { 0.0f, 24.0f }, 0.0f, db);
        declare ("ceiling", "Plafond", { -12.0f, 0.0f }, -1.0f, [] (float v) { return juce::String (v, 1) + " dB"; });
        declare ("release", juce::String::fromUTF8 ("Relâchement"), juce::NormalisableRange<float> (1.0f, 500.0f, 0.0f, 0.4f), 60.0f, ms);
        declare ("truepeak", juce::String::fromUTF8 ("Crête vraie (True Peak)"), { 0.0f, 1.0f, 1.0f }, 1.0f, onOff);
        declare ("target", "Cible de sonie", choice (3), 0.0f, names (targetNames, 3));
    }

    void LimiterPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        lookahead = juce::jmax (TruePeakDetector::delaySamples + 8, juce::roundToInt (lookaheadMs * 0.001 * rate));
        for (auto& d : delay) d.assign ((size_t) lookahead, 0.0f);
        peaks.assign ((size_t) lookahead, 0.0f);
        hopLen = juce::jmax (1, juce::roundToInt (0.1 * rate));

        // Filtre K (ITU-R BS.1770-4) recalculé pour la fréquence d'échantillonnage.
        const double pi = juce::MathConstants<double>::pi;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / rate), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            kShelf.b0 = (Vh + Vb * K / Q + K * K) / a0; kShelf.b1 = 2.0 * (K * K - Vh) / a0; kShelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            kShelf.a1 = 2.0 * (K * K - 1.0) / a0;       kShelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / rate), a0 = 1.0 + K / Q + K * K;
            kHigh.b0 = 1.0; kHigh.b1 = -2.0; kHigh.b2 = 1.0;
            kHigh.a1 = 2.0 * (K * K - 1.0) / a0; kHigh.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
        clearLoudness();
    }

    void LimiterPlugin::reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (peaks.begin(), peaks.end(), 0.0f);
        pos = 0; gain = 1.0f;
        tpIn.reset(); tpOut.reset();
    }

    void LimiterPlugin::clearLoudness()
    {
        kShelf.reset(); kHigh.reset();
        hopEnergy = 0; hopCount = 0; hopPos = 0; hopsFilled = 0;
        std::fill (std::begin (hops), std::end (hops), 0.0);
        std::fill (std::begin (binEnergy), std::end (binEnergy), 0.0);
        std::fill (std::begin (binCount), std::end (binCount), 0);
        tpMax = 0;
        momentaryLufs = -100; shortTermLufs = -100; integratedLufs = -100; truePeakMaxDb = -100;
    }

    void LimiterPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || peaks.empty()) return;
        if (resetRequested.exchange (false)) clearLoudness();
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        if (chans == 0) return;
        const float in = dbToGain (value ("input")), ceil = dbToGain (value ("ceiling"));
        const float relCoef = std::exp (-1.0f / (0.001f * value ("release") * (float) rate));
        const bool truePeak = value ("truepeak") > 0.5f;
        // La détection de crête vraie arrive avec quelques échantillons de retard : la descente du gain
        // doit être achevée avant que la crête ne sorte de la ligne à retard.
        const float attackStep = 1.0f / (float) (lookahead - (truePeak ? TruePeakDetector::delaySamples : 0));
        float minGain = 1.0f, peakOut = 0.0f, tpBlock = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float x[2] = { 0, 0 }, pk = 0;
            for (int c = 0; c < chans; ++c)
            {
                x[c] = buf.getSample (c, start + i) * in;
                pk = std::max (pk, truePeak ? tpIn.process (x[c], c) : std::abs (x[c]));
            }
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
                y = juce::jlimit (-ceil, ceil, y);    // garantie finale : jamais un échantillon au-dessus du plafond
                buf.setSample (c, start + i, y);
                peakOut = std::max (peakOut, std::abs (y));
                tpBlock = std::max (tpBlock, tpOut.process (y, c));
            }
            pos = (pos + 1) % lookahead;
        }
        gainReductionDb = -gainToDb (minGain);
        outputDb = gainToDb (peakOut);
        truePeakDb = gainToDb (tpBlock);
        tpMax = std::max (tpMax, tpBlock);
        truePeakMaxDb = gainToDb (tpMax);
        measureLoudness (buf.getReadPointer (0, start), buf.getReadPointer (chans > 1 ? 1 : 0, start), n);
    }

    void LimiterPlugin::measureLoudness (const float* l, const float* r, int n)
    {
        auto lufs = [] (double e) { return -0.691 + 10.0 * std::log10 (std::max (e, 1.0e-12)); };
        auto meanLast = [this] (int count)
        {
            double s = 0;
            for (int j = 0; j < count; ++j) s += hops[(hopPos - 1 - j + 60) % 30];
            return s / count;
        };
        for (int i = 0; i < n; ++i)
        {
            const double yl = kHigh.process (kShelf.process (l[i], 0), 0);
            const double yr = kHigh.process (kShelf.process (r[i], 1), 1);
            hopEnergy += yl * yl + yr * yr;
            if (++hopCount < hopLen) continue;

            // Tranche de 100 ms terminée : blocs de 400 ms (recouvrement de 75 %) et de 3 s.
            hops[hopPos] = hopEnergy / hopLen;
            hopPos = (hopPos + 1) % 30;
            hopsFilled = std::min (30, hopsFilled + 1);
            hopEnergy = 0; hopCount = 0;
            if (hopsFilled < 4) continue;

            const double block = meanLast (4);
            const double blockLufs = lufs (block);
            momentaryLufs = (float) blockLufs;
            if (hopsFilled >= 30) shortTermLufs = (float) lufs (meanLast (30));

            // Intégrée (EBU R128) : porte absolue à -70 LUFS, puis porte relative à -10 LU.
            if (blockLufs >= -70.0)
            {
                const int bin = std::min (numBins - 1, (int) ((blockLufs + 70.0) * 10.0));
                binEnergy[bin] += block;
                ++binCount[bin];
            }
            double sum = 0; int count = 0;
            for (int b = 0; b < numBins; ++b) { sum += binEnergy[b]; count += binCount[b]; }
            if (count == 0) continue;
            const double gate = lufs (sum / count) - 10.0;
            const int first = juce::jlimit (0, numBins, (int) std::ceil ((gate + 70.0) * 10.0));
            sum = 0; count = 0;
            for (int b = first; b < numBins; ++b) { sum += binEnergy[b]; count += binCount[b]; }
            if (count > 0) integratedLufs = (float) lufs (sum / count);
        }
    }

    void registerTypes (te::Engine& engine)
    {
        auto& pm = engine.getPluginManager();
        pm.createBuiltInType<EqPlugin>();
        pm.createBuiltInType<CompPlugin>();
        pm.createBuiltInType<LimiterPlugin>();
        pm.createBuiltInType<SynthPlugin>();
        pm.createBuiltInType<SaturationPlugin>();
        pm.createBuiltInType<EchoPlugin>();
        pm.createBuiltInType<SpacePlugin>();
    }
}
