#include "Pedalboard.h"
#include "OndeFx2.h"
#include "Theme.h"

namespace ondefx
{
    namespace pedals
    {
        const Info& info (int type)
        {
            static const Info list[numTypes] = {
                { "Vide", "", 0xff2a2d33, 0, {}, {} },
                { "Porte de bruit", "Dynamique", 0xff3a3f46, 2, { "Seuil", "Relâchement" }, { 0.35f, 0.3f } },
                { "Compresseur", "Dynamique", 0xff2f7fd6, 4, { "Sustain", "Attaque", "Niveau", "Mélange" }, { 0.5f, 0.3f, 0.5f, 1.0f } },
                { "Overdrive", "Saturation", 0xff3aa05a, 3, { "Gain", "Tonalité", "Niveau" }, { 0.45f, 0.5f, 0.5f } },
                { "Distorsion", "Saturation", 0xfff0a030, 3, { "Gain", "Tonalité", "Niveau" }, { 0.6f, 0.5f, 0.5f } },
                { "Fuzz", "Saturation", 0xffb8483a, 3, { "Fuzz", "Tonalité", "Niveau" }, { 0.7f, 0.5f, 0.5f } },
                { "Octaveur", "Hauteur", 0xff6a4fc0, 3, { "Octave basse", "Octave haute", "Direct" }, { 0.6f, 0.0f, 0.7f } },
                { "Wah automatique", "Filtre", 0xffe0c030, 3, { "Sensibilité", "Résonance", "Plage" }, { 0.5f, 0.5f, 0.6f } },
                { "Chorus", "Modulation", 0xff4fb8d0, 3, { "Vitesse", "Profondeur", "Mélange" }, { 0.35f, 0.5f, 0.5f } },
                { "Phaser", "Modulation", 0xffe07a30, 3, { "Vitesse", "Profondeur", "Réinjection" }, { 0.3f, 0.6f, 0.4f } },
                { "Flanger", "Modulation", 0xff8a8f98, 4, { "Vitesse", "Profondeur", "Réinjection", "Mélange" }, { 0.2f, 0.6f, 0.6f, 0.5f } },
                { "Trémolo", "Modulation", 0xff2e8f7a, 3, { "Vitesse", "Profondeur", "Forme" }, { 0.45f, 0.6f, 0.2f } },
                { "Délai analogique", "Temps", 0xff3d5fa8, 3, { "Temps", "Répétitions", "Mélange" }, { 0.4f, 0.35f, 0.35f } },
                { "Réverb à ressort", "Temps", 0xff9a6a3a, 3, { "Durée", "Tonalité", "Mélange" }, { 0.5f, 0.5f, 0.3f } },
                { "Égaliseur", "Filtre", 0xffd8d8d8, 4, { "Graves", "Médiums", "Aigus", "Niveau" }, { 0.5f, 0.5f, 0.5f, 0.5f } },
                { "Booster", "Niveau", 0xffd84a6a, 2, { "Niveau", "Aigus" }, { 0.5f, 0.5f } },
                { "Volume", "Niveau", 0xff444a52, 1, { "Volume" }, { 1.0f } },
            };
            return list[juce::jlimit (0, numTypes - 1, type)];
        }

        juce::String knobText (int type, int k, float v)
        {
            auto pct = [] (float x) { return juce::String (juce::roundToInt (x * 100)) + " %"; };
            auto db = [] (float x) { return (x > 0 ? "+" : "") + juce::String (x, 1) + " dB"; };
            switch (type)
            {
                case gate:       return k == 0 ? juce::String (juce::roundToInt (-80 + v * 60)) + " dB" : juce::String (juce::roundToInt (20 + v * 480)) + " ms";
                case comp:       return k == 1 ? juce::String (juce::roundToInt (1 + v * 49)) + " ms" : k == 2 ? db (-12 + v * 24) : pct (v);
                case overdrive: case distortion: case fuzz: return k == 2 ? db (-18 + v * 30) : pct (v);
                case autowah:    return pct (v);
                case chorus: case phaser: case flanger: case tremolo:
                    return k == 0 ? juce::String (0.05 + v * v * 9.95, 2) + " Hz" : (type == tremolo && k == 2) ? (v < 0.5f ? juce::String::fromUTF8 ("douce") : juce::String::fromUTF8 ("carrée")) : pct (v);
                case delay:      return k == 0 ? juce::String (juce::roundToInt (40 + v * 860)) + " ms" : pct (v);
                case spring:     return k == 0 ? juce::String (0.5 + v * 3.5, 1) + " s" : pct (v);
                case eq:         return db (-12 + v * 24);
                case boost:      return k == 0 ? db (v * 20) : db (-6 + v * 12);
                case volume:     return pct (v);
                default:         return pct (v);
            }
        }
    }

    //==============================================================================
    // État d'un emplacement : de quoi faire tourner n'importe quelle pédale (aucune allocation pendant le traitement).
    struct PedalboardPlugin::State
    {
        std::vector<float> line[2];
        int mask = 0, writePos = 0;
        double phase = 0;
        float env = 0, gain = 1, lp[2][4] {}, hp[2][2] {}, ap[2][8] {}, dc[2][2] {}, octLp[2] {}, flip[2] { 1, 1 }, prev[2] {}, fb[2] {};
        AmpPlugin::Biquad bq[4];
        int biquadType = -1;
        juce::String key;
        void clear()
        {
            for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
            writePos = 0; phase = 0; env = 0; gain = 1;
            std::memset (lp, 0, sizeof (lp)); std::memset (hp, 0, sizeof (hp)); std::memset (ap, 0, sizeof (ap)); std::memset (dc, 0, sizeof (dc));
            std::memset (octLp, 0, sizeof (octLp)); std::memset (prev, 0, sizeof (prev)); std::memset (fb, 0, sizeof (fb));
            flip[0] = flip[1] = 1;
            for (auto& b : bq) b.reset();
            key.clear();
        }
        inline float read (int c, float delaySamples) const
        {
            const float pos = (float) writePos - delaySamples;
            const int i = (int) std::floor (pos);
            const float t = pos - (float) i;
            const auto& l = line[c];
            return l[(size_t) (i & mask)] * (1.0f - t) + l[(size_t) ((i + 1) & mask)] * t;
        }
    };

    const char* PedalboardPlugin::xmlTypeName = "ondePedalboard";

    juce::String PedalboardPlugin::paramId (int slot, const char* what) { return "s" + juce::String (slot + 1) + what; }

    PedalboardPlugin::PedalboardPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        for (int s = 0; s < numSlots; ++s)
        {
            const auto n = juce::String (s + 1);
            declare (paramId (s, "type"), juce::String::fromUTF8 ("Pédale ") + n, { 0.0f, (float) (pedals::numTypes - 1), 1.0f }, 0.0f,
                     [] (float v) { return juce::String::fromUTF8 (pedals::info (juce::roundToInt (v)).name); });
            declare (paramId (s, "on"), juce::String::fromUTF8 ("Pédale ") + n + juce::String::fromUTF8 (" : allumée"), { 0.0f, 1.0f, 1.0f }, 1.0f,
                     [] (float v) { return v > 0.5f ? juce::String ("oui") : juce::String ("non"); });
            for (int k = 0; k < 4; ++k)
                declare (paramId (s, ("k" + juce::String (k + 1)).toRawUTF8()), juce::String::fromUTF8 ("Pédale ") + n + juce::String::fromUTF8 (" : bouton ") + juce::String (k + 1),
                         { 0.0f, 1.0f }, 0.5f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        }
        for (int s = 0; s < numSlots; ++s) states.push_back (std::make_unique<State>());
    }

    PedalboardPlugin::~PedalboardPlugin() = default;

    int PedalboardPlugin::slotType (int s) const       { return juce::jlimit (0, pedals::numTypes - 1, juce::roundToInt (value (paramId (s, "type")))); }
    bool PedalboardPlugin::slotOn (int s) const        { return value (paramId (s, "on")) > 0.5f; }
    float PedalboardPlugin::slotKnob (int s, int k) const { return value (paramId (s, ("k" + juce::String (k + 1)).toRawUTF8())); }

    void PedalboardPlugin::setPedal (int slot, int type)
    {
        setStored (paramId (slot, "type"), (float) type);
        setStored (paramId (slot, "on"), 1.0f);
        const auto& in = pedals::info (type);
        for (int k = 0; k < 4; ++k) setStored (paramId (slot, ("k" + juce::String (k + 1)).toRawUTF8()), k < in.numKnobs ? in.defaults[k] : 0.5f);
    }

    void PedalboardPlugin::moveSlot (int from, int to)
    {
        if (from == to || ! juce::isPositiveAndBelow (from, numSlots) || ! juce::isPositiveAndBelow (to, numSlots)) return;
        struct Saved { float type, on, k[4]; };
        std::array<Saved, numSlots> all;
        for (int s = 0; s < numSlots; ++s) { all[(size_t) s] = { (float) slotType (s), slotOn (s) ? 1.0f : 0.0f, {} }; for (int k = 0; k < 4; ++k) all[(size_t) s].k[k] = slotKnob (s, k); }
        const auto moving = all[(size_t) from];
        std::vector<Saved> order (all.begin(), all.end());
        order.erase (order.begin() + from);
        order.insert (order.begin() + to, moving);
        for (int s = 0; s < numSlots; ++s)
        {
            setStored (paramId (s, "type"), order[(size_t) s].type);
            setStored (paramId (s, "on"), order[(size_t) s].on);
            for (int k = 0; k < 4; ++k) setStored (paramId (s, ("k" + juce::String (k + 1)).toRawUTF8()), order[(size_t) s].k[k]);
        }
    }

    void PedalboardPlugin::applyPreset (const std::vector<std::pair<int, std::array<float, 4>>>& chain)
    {
        for (int s = 0; s < numSlots; ++s)
        {
            if (s < (int) chain.size())
            {
                setPedal (s, chain[(size_t) s].first);
                for (int k = 0; k < 4; ++k) setStored (paramId (s, ("k" + juce::String (k + 1)).toRawUTF8()), chain[(size_t) s].second[(size_t) k]);
            }
            else setPedal (s, pedals::empty);
        }
    }

    void PedalboardPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        int size = 1; while (size < (int) (rate * 2.2)) size <<= 1;      // 2 s de retard au plus
        for (auto& st : states)
        {
            for (auto& l : st->line) l.assign ((size_t) size, 0.0f);
            st->mask = size - 1;
            st->clear();
        }
        lastType.fill (-1);
    }

    void PedalboardPlugin::reset() { for (auto& st : states) if (! st->line[0].empty()) st->clear(); }

    namespace
    {
        inline float softClip (float x) { return x / (1.0f + std::abs (x)); }
        inline float hardClip (float x) { const float a = std::abs (x); return x / std::pow (1.0f + std::pow (a, 6.0f), 1.0f / 6.0f); }
        inline float onePole (float& s, float x, float a) { s += a * (x - s); return s; }
        float coef (double hz, double fs) { return 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * hz / fs); }
    }

    void PedalboardPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || states.empty() || states[0]->line[0].empty()) return;
        auto& buf = *fc.destBuffer;
        const int chans = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, start = fc.bufferStartSample;
        if (chans == 0) return;
        {
            float pk = 0; for (int c = 0; c < chans; ++c) pk = std::max (pk, buf.getMagnitude (c, start, n));
            inputDb = juce::Decibels::gainToDecibels (pk, -100.0f);
        }
        const double fs = rate;
        const float pi = juce::MathConstants<float>::pi;
        for (int s = 0; s < numSlots; ++s)
        {
            const int type = slotType (s);
            auto& st = *states[(size_t) s];
            if (type != lastType[(size_t) s]) { st.clear(); lastType[(size_t) s] = type; }
            if (type == pedals::empty || ! slotOn (s)) continue;
            float k[4]; for (int i = 0; i < 4; ++i) k[i] = slotKnob (s, i);
            // Filtres fixes de la pédale, recalculés quand un bouton change.
            juce::String key; key << type << ";" << k[0] << ";" << k[1] << ";" << k[2] << ";" << k[3];
            if (key != st.key)
            {
                st.key = key;
                if (type == pedals::overdrive) { st.bq[0].set (2, 720.0, 0.7, 6.0, fs); st.bq[1].set (0, 800.0 + k[1] * 4500.0, 0.7, 0, fs); }
                if (type == pedals::distortion) { st.bq[0].set (1, 120.0, 0.7, 0, fs); st.bq[1].set (0, 1500.0 + k[1] * 7000.0, 0.7, 0, fs); st.bq[2].set (2, 650.0, 0.8, -4.0 + k[1] * 2.0, fs); }
                if (type == pedals::fuzz) { st.bq[0].set (1, 60.0, 0.7, 0, fs); st.bq[1].set (0, 900.0 + k[1] * 5000.0, 0.6, 0, fs); }
                if (type == pedals::eq) { st.bq[0].set (3, 120.0, 0.7, -12.0 + k[0] * 24.0, fs); st.bq[1].set (2, 800.0, 0.8, -12.0 + k[1] * 24.0, fs); st.bq[2].set (4, 3200.0, 0.7, -12.0 + k[2] * 24.0, fs); }
                if (type == pedals::boost) st.bq[0].set (4, 2500.0, 0.7, -6.0 + k[1] * 12.0, fs);
                if (type == pedals::spring) st.bq[0].set (0, 1500.0 + k[1] * 5000.0, 0.7, 0, fs);
            }
            const float lfoHz = 0.05f + k[0] * k[0] * 9.95f;
            for (int i = 0; i < n; ++i)
            {
                // Grandeurs communes aux deux canaux (oscillateur, enveloppe).
                float peak = 0;
                for (int c = 0; c < chans; ++c) peak = std::max (peak, std::abs (buf.getSample (c, start + i)));
                const float atk = type == pedals::autowah ? coef (200.0, fs) : coef (100.0, fs), rel = coef (8.0, fs);
                st.env += (peak > st.env ? atk : rel) * (peak - st.env);
                const double ph = st.phase;
                st.phase += lfoHz / fs; if (st.phase >= 1.0) st.phase -= 1.0;
                for (int c = 0; c < chans; ++c)
                {
                    const float x = buf.getSample (c, start + i);
                    float y = x;
                    switch (type)
                    {
                        case pedals::gate:
                        {
                            const float thr = juce::Decibels::decibelsToGain (-80.0f + k[0] * 60.0f);
                            if (c == 0) { const float target = st.env > thr ? 1.0f : 0.0f; st.gain += (target > st.gain ? coef (300.0, fs) : coef (1.0 / (0.02 + k[1] * 0.48), fs)) * (target - st.gain); }
                            y = x * st.gain;
                            break;
                        }
                        case pedals::comp:
                        {
                            if (c == 0)
                            {
                                const float lvl = juce::Decibels::gainToDecibels (st.env + 1.0e-6f), thr = -10.0f - k[0] * 30.0f, ratio = 4.0f + k[0] * 6.0f;
                                const float target = lvl > thr ? juce::Decibels::decibelsToGain ((thr - lvl) * (1.0f - 1.0f / ratio)) : 1.0f;
                                st.gain += (target < st.gain ? coef (1000.0 / (1.0 + k[1] * 49.0), fs) : coef (4.0, fs)) * (target - st.gain);
                            }
                            const float wet = x * st.gain * juce::Decibels::decibelsToGain (-12.0f + k[2] * 24.0f + k[0] * 12.0f);
                            y = x * (1.0f - k[3]) + wet * k[3];
                            break;
                        }
                        case pedals::overdrive:
                        {
                            // Bosse dans les médiums, écrêtage doux, le grave reste en partie propre (garde l'attaque).
                            const float clean = onePole (st.lp[c][0], x, coef (180.0, fs));
                            const float mid = st.bq[0].process (x - clean, c);
                            float d = std::atan (mid * (1.0f + k[0] * 80.0f)) * 0.63f;
                            d = st.bq[1].process (d, c);
                            y = (d + clean * 0.35f) * juce::Decibels::decibelsToGain (-18.0f + k[2] * 30.0f);
                            break;
                        }
                        case pedals::distortion:
                        {
                            float d = st.bq[0].process (x, c) * (1.0f + k[0] * 250.0f);
                            d = hardClip (d) * 0.7f;
                            d = st.bq[2].process (st.bq[1].process (d, c), c);
                            y = d * juce::Decibels::decibelsToGain (-18.0f + k[2] * 30.0f);
                            break;
                        }
                        case pedals::fuzz:
                        {
                            float d = st.bq[0].process (x, c) * (1.0f + k[0] * 700.0f);
                            d = d > 0 ? std::tanh (d) : std::tanh (d * 0.6f) * 0.8f;       // asymétrique : grain et souffle
                            const float bias = onePole (st.dc[c][0], d, coef (8.0, fs));
                            d = st.bq[1].process (d - bias, c);
                            y = d * juce::Decibels::decibelsToGain (-18.0f + k[2] * 30.0f);
                            break;
                        }
                        case pedals::octave:
                        {
                            // Octave basse : bascule à chaque passage par zéro de la fondamentale ; haute : redressement.
                            const float f = onePole (st.octLp[c], x, coef (500.0, fs));
                            if (f > 0.0f && st.prev[c] <= 0.0f) st.flip[c] = -st.flip[c];
                            st.prev[c] = f;
                            const float down = onePole (st.lp[c][0], st.flip[c] * std::abs (f) * 2.0f, coef (900.0, fs));
                            float up = std::abs (x);
                            up -= onePole (st.dc[c][0], up, coef (20.0, fs));
                            y = x * k[2] + down * k[0] * 1.4f + up * k[1] * 1.6f;
                            break;
                        }
                        case pedals::autowah:
                        {
                            const float fc = juce::jlimit (150.0f, 6000.0f, 250.0f + st.env * k[0] * 30000.0f * (0.3f + k[2]));
                            const float g = std::tan (pi * fc / (float) fs), q = 0.6f + k[1] * 6.0f, kk = 1.0f / q;
                            const float a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
                            float& ic1 = st.lp[c][0]; float& ic2 = st.lp[c][1];
                            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
                            y = v1 * (1.2f + k[1]) + x * 0.15f;
                            break;
                        }
                        case pedals::chorus:
                        {
                            const double p = ph + (c == 1 ? 0.25 : 0.0);
                            const float d1 = (7.0f + 3.0f * k[1] * (float) std::sin (juce::MathConstants<double>::twoPi * p)) * (float) fs / 1000.0f;
                            const float d2 = (9.0f + 3.0f * k[1] * (float) std::sin (juce::MathConstants<double>::twoPi * (p + 0.5))) * (float) fs / 1000.0f;
                            st.line[c][(size_t) st.writePos] = x;
                            const float wet = 0.5f * (st.read (c, d1) + st.read (c, d2));
                            y = x * (1.0f - 0.5f * k[2]) + wet * k[2];
                            break;
                        }
                        case pedals::phaser:
                        {
                            const double p = ph + (c == 1 ? 0.15 : 0.0);
                            const float sweep = 0.5f + 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * p);
                            const float fc = 200.0f * std::pow (2.0f, sweep * 3.5f * (0.3f + k[1]));
                            const float t = std::tan (pi * fc / (float) fs), a = (t - 1.0f) / (t + 1.0f);
                            float s2 = x + st.fb[c] * k[2] * 0.9f;
                            for (int j = 0; j < 4; ++j) { const float yy = a * s2 + st.ap[c][j]; st.ap[c][j] = s2 - a * yy; s2 = yy; }
                            st.fb[c] = s2;
                            y = 0.5f * (x + s2);
                            break;
                        }
                        case pedals::flanger:
                        {
                            const double p = ph + (c == 1 ? 0.25 : 0.0);
                            const float d = (0.6f + 3.5f * k[1] * (0.5f + 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * p))) * (float) fs / 1000.0f;
                            const float wet = st.read (c, d);
                            st.line[c][(size_t) st.writePos] = x + wet * k[2] * 0.9f;
                            y = x * (1.0f - 0.5f * k[3]) + wet * k[3];
                            break;
                        }
                        case pedals::tremolo:
                        {
                            const float sine = 0.5f + 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * ph);
                            const float square = sine > 0.5f ? 1.0f : 0.0f;
                            const float shape = sine * (1.0f - k[2]) + square * k[2];
                            const float g = 1.0f - k[1] * shape;
                            y = x * onePole (st.lp[c][0], g, coef (300.0, fs));
                            break;
                        }
                        case pedals::delay:
                        {
                            // Répétitions de plus en plus sombres et un peu saturées, léger pleurage (circuit analogique).
                            const float wow = 1.0f + 0.002f * (float) std::sin (juce::MathConstants<double>::twoPi * 0.6 * (double) st.writePos / fs);
                            const float d = (40.0f + k[0] * 860.0f) * (float) fs / 1000.0f * wow;
                            float wet = st.read (c, d);
                            wet = onePole (st.lp[c][0], wet, coef (2800.0, fs));
                            st.line[c][(size_t) st.writePos] = std::tanh (x + wet * k[1] * 1.05f);
                            y = x + wet * k[2] * 1.2f;
                            break;
                        }
                        case pedals::spring:
                        {
                            // Ressort : boucle de retard avec des passe-tout (dispersion, « boing ») et amortissement.
                            const float loop = (33.0f + (c == 1 ? 4.0f : 0.0f)) * (float) fs / 1000.0f;
                            float s2 = st.read (c, loop);
                            for (int j = 0; j < 6; ++j) { const float a = 0.62f; const float yy = -a * s2 + st.ap[c][j]; st.ap[c][j] = s2 + a * yy; s2 = yy; }
                            const float decay = std::pow (0.001f, (33.0f / 1000.0f) / (0.5f + k[0] * 3.5f));
                            st.line[c][(size_t) st.writePos] = x + s2 * decay;
                            const float wet = st.bq[0].process (s2, c);
                            y = x * (1.0f - 0.4f * k[2]) + wet * k[2] * 1.5f;
                            break;
                        }
                        case pedals::eq:
                            y = st.bq[2].process (st.bq[1].process (st.bq[0].process (x, c), c), c) * juce::Decibels::decibelsToGain (-12.0f + k[3] * 24.0f);
                            break;
                        case pedals::boost:
                            y = st.bq[0].process (x, c) * juce::Decibels::decibelsToGain (k[0] * 20.0f);
                            break;
                        case pedals::volume:
                            y = x * k[0] * k[0];
                            break;
                        default: break;
                    }
                    buf.setSample (c, start + i, y);
                }
                st.writePos = (st.writePos + 1) & st.mask;
            }
        }
        {
            float pk = 0; for (int c = 0; c < chans; ++c) pk = std::max (pk, buf.getMagnitude (c, start, n));
            outputDb = juce::Decibels::gainToDecibels (pk, -100.0f);
        }
    }
}

//==============================================================================
// Fenêtre : le pédalier vu de dessus. Pédales dessinées (boîtier, boutons, diode, interrupteur au pied), câbles entre
// elles, glisser une pédale pour changer l'ordre, clic sur un emplacement vide pour en poser une.

namespace ondefx
{
    namespace
    {
        juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }
        // Majuscules, accents compris (toUpperCase ne les traite pas partout).
        juce::String upper (const juce::String& s)
        {
            auto r = s.toUpperCase();
            const char* pairs[][2] = { { "é", "É" }, { "è", "È" }, { "ê", "Ê" }, { "à", "À" }, { "â", "Â" }, { "ô", "Ô" }, { "î", "Î" }, { "û", "Û" }, { "ù", "Ù" }, { "ç", "Ç" } };
            for (auto& p : pairs) r = r.replace (U8 (p[0]), U8 (p[1]));
            return r;
        }

        struct PedalPreset { const char* name; std::vector<std::pair<int, std::array<float, 4>>> chain; };
        const std::vector<PedalPreset>& boardPresets()
        {
            using namespace pedals;
            static const std::vector<PedalPreset> list = {
                { "Rock classique", { { comp, { 0.4f, 0.3f, 0.5f, 0.6f } }, { overdrive, { 0.55f, 0.55f, 0.55f, 0 } }, { delay, { 0.38f, 0.3f, 0.25f, 0 } }, { spring, { 0.4f, 0.5f, 0.2f, 0 } } } },
                { "Métal serré", { { gate, { 0.45f, 0.15f, 0, 0 } }, { boost, { 0.45f, 0.65f, 0, 0 } }, { eq, { 0.45f, 0.62f, 0.55f, 0.5f } } } },
                { "Ambiance", { { comp, { 0.5f, 0.3f, 0.5f, 0.8f } }, { chorus, { 0.25f, 0.45f, 0.45f, 0 } }, { delay, { 0.55f, 0.5f, 0.4f, 0 } }, { spring, { 0.8f, 0.6f, 0.45f, 0 } } } },
                { "Funk", { { comp, { 0.65f, 0.15f, 0.5f, 1.0f } }, { autowah, { 0.6f, 0.6f, 0.55f, 0 } }, { phaser, { 0.25f, 0.5f, 0.3f, 0 } } } },
                { "Fuzz vintage", { { fuzz, { 0.75f, 0.45f, 0.5f, 0 } }, { octave, { 0.0f, 0.5f, 0.8f, 0 } }, { tremolo, { 0.5f, 0.45f, 0.1f, 0 } }, { spring, { 0.5f, 0.45f, 0.3f, 0 } } } },
                { "Clair scintillant", { { comp, { 0.45f, 0.4f, 0.5f, 0.7f } }, { chorus, { 0.3f, 0.55f, 0.5f, 0 } }, { spring, { 0.55f, 0.6f, 0.3f, 0 } } } },
            };
            return list;
        }

        class PedalboardEditor  : public juce::Component, private juce::Timer
        {
        public:
            explicit PedalboardEditor (PedalboardPlugin& p) : pb (p)
            {
                for (auto& pr : boardPresets())
                {
                    auto* b = presetButtons.add (new juce::TextButton (U8 (pr.name)));
                    b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262a31));
                    b->onClick = [this, &pr]
                    {
                        pb.edit.getUndoManager().beginNewTransaction (U8 ("Pédalier « ") + U8 (pr.name) + U8 (" »"));
                        pb.applyPreset (pr.chain);
                        repaint();
                    };
                    addAndMakeVisible (b);
                }
                clearButton.onClick = [this]
                {
                    pb.edit.getUndoManager().beginNewTransaction (U8 ("Vider le pédalier"));
                    pb.applyPreset ({});
                    repaint();
                };
                addAndMakeVisible (clearButton);
                setSize (960, 600);
                startTimerHz (24);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (12).removeFromBottom (32);
                r.removeFromLeft (100);
                clearButton.setBounds (r.removeFromRight (100));
                r.removeFromRight (8);
                const int w = juce::jmin (140, r.getWidth() / juce::jmax (1, presetButtons.size()) - 6);
                for (auto* b : presetButtons) { b->setBounds (r.removeFromLeft (w)); r.removeFromLeft (6); }
            }

            juce::Rectangle<float> slotBounds (int s) const
            {
                auto area = getLocalBounds().reduced (24).withTrimmedTop (52).withTrimmedBottom (44).toFloat();
                const float gapX = 26.0f, gapY = 30.0f;
                const float w = (area.getWidth() - gapX * 3) / 4, h = (area.getHeight() - gapY) / 2;
                const int row = s / 4, col = s % 4;
                return { area.getX() + col * (w + gapX), area.getY() + row * (h + gapY), w, h };
            }
            juce::Rectangle<float> knobBounds (int s, int k) const
            {
                const int n = pedals::info (pb.slotType (s)).numKnobs;
                auto b = slotBounds (s).reduced (12.0f).withTrimmedTop (28.0f);
                auto top = b.removeFromTop (b.getHeight() - 74.0f);
                const int perRow = n <= 3 ? juce::jmax (1, n) : 2, rows = n <= 3 ? 1 : 2;
                const float cw = top.getWidth() / (float) perRow, ch = top.getHeight() / (float) rows;
                const int row = k / perRow, col = k % perRow;
                return { top.getX() + col * cw, top.getY() + row * ch, cw, ch };
            }
            juce::Rectangle<float> footBounds (int s) const
            {
                auto b = slotBounds (s);
                return juce::Rectangle<float> (38.0f, 38.0f).withCentre ({ b.getCentreX(), b.getBottom() - 30.0f });
            }

            void paint (juce::Graphics& g) override
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272e), 0, 0, juce::Colour (0xff121417), 0, (float) getHeight(), false));
                g.fillAll();
                auto head = getLocalBounds().removeFromTop (44).reduced (12, 6);
                theme::drawLogo (g, head.removeFromLeft (head.getHeight()).toFloat().reduced (4.0f));
                head.removeFromLeft (8);
                g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
                g.drawText (pb.getName(), head.removeFromLeft (130), juce::Justification::centredLeft);
                drawMeter (g, head.removeFromRight (170).toFloat(), pb.inputDb.load(), pb.outputDb.load());
                g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
                g.drawFittedText (U8 ("Glissez une pédale pour changer l'ordre · interrupteur pour l'allumer · clic droit pour la changer · double-clic sur un bouton : valeur d'origine"),
                                  head.reduced (6, 0), juce::Justification::centredLeft, 2);

                auto board = getLocalBounds().reduced (10).withTrimmedTop (40).withTrimmedBottom (40).toFloat();
                g.setColour (juce::Colour (0xff17191c)); g.fillRoundedRectangle (board, 10.0f);
                g.setColour (juce::Colour (0x0cffffff));
                for (float y = board.getY() + 12; y < board.getBottom() - 6; y += 8.0f)
                    g.drawHorizontalLine ((int) y, board.getX() + 8, board.getRight() - 8);

                drawCables (g);
                for (int s = 0; s < PedalboardPlugin::numSlots; ++s)
                    if (! (dragging && s == dragSlot)) drawPedal (g, s, slotBounds (s));
                if (dragging && dragSlot >= 0)
                {
                    const int target = slotAt (dragPos);
                    if (target >= 0 && target != dragSlot) { g.setColour (theme::accent); g.drawRoundedRectangle (slotBounds (target).expanded (5.0f), 12.0f, 2.0f); }
                    drawPedal (g, dragSlot, slotBounds (dragSlot).withCentre (dragPos), 0.9f);
                }
                g.setColour (theme::dim); g.setFont (theme::ui (11.0f, true));
                g.drawText (U8 ("PRÉRÉGLAGES"), 14, getHeight() - 40, 100, 28, juce::Justification::centredLeft);
            }

            void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float inDb, float outDb)
            {
                r = r.reduced (0, 4);
                auto bar = [&] (juce::Rectangle<float> b, float db, const char* label)
                {
                    g.setColour (theme::dim); g.setFont (theme::ui (10.0f, true));
                    g.drawText (U8 (label), b.removeFromLeft (50), juce::Justification::centredLeft);
                    g.setColour (theme::display); g.fillRoundedRectangle (b, 2.0f);
                    const float f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                    g.setColour (db > -1.0f ? theme::rec : theme::ok); g.fillRoundedRectangle (b.withWidth (b.getWidth() * f), 2.0f);
                };
                bar (r.removeFromTop (r.getHeight() / 2).reduced (0, 2), inDb, "ENTRÉE");
                bar (r.reduced (0, 2), outDb, "SORTIE");
            }

            void drawCables (juce::Graphics& g)
            {
                // Guitare, pédale 1, …, pédale 8, ampli : câbles noirs à fiches dorées.
                auto cable = [&] (juce::Point<float> a, juce::Point<float> b)
                {
                    juce::Path c; c.startNewSubPath (a);
                    const float sag = 16.0f;
                    c.cubicTo (a.translated (14, sag), b.translated (-14, sag), b);
                    g.setColour (juce::Colour (0xff060708)); g.strokePath (c, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (juce::Colour (0xff2e3136)); g.strokePath (c, juce::PathStrokeType (1.2f));
                    g.setColour (juce::Colour (0xffc9a24a));
                    g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (a)); g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (b));
                };
                for (int s = 0; s + 1 < PedalboardPlugin::numSlots; ++s)
                {
                    auto a = slotBounds (s), b = slotBounds (s + 1);
                    if (s % 4 == 3)   // fin de rangée : le câble redescend au début de la suivante
                    {
                        juce::Path c; const auto p0 = juce::Point<float> (a.getRight() + 3, a.getY() + 50), p1 = juce::Point<float> (b.getX() - 3, b.getY() + 50);
                        c.startNewSubPath (p0);
                        c.cubicTo (p0.translated (30, 40), { a.getRight() - 40, a.getBottom() + 18 }, { (a.getRight() + b.getX()) * 0.5f, a.getBottom() + 14 });
                        c.cubicTo ({ b.getX() + 40, b.getY() - 18 }, p1.translated (-30, -40), p1);
                        g.setColour (juce::Colour (0xff060708)); g.strokePath (c, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                        g.setColour (juce::Colour (0xff2e3136)); g.strokePath (c, juce::PathStrokeType (1.2f));
                        continue;
                    }
                    cable ({ a.getRight() + 3, a.getY() + 50 }, { b.getX() - 3, b.getY() + 50 });
                }
                g.setColour (theme::muted); g.setFont (theme::ui (10.0f, true));
                auto first = slotBounds (0), last = slotBounds (PedalboardPlugin::numSlots - 1);
                g.drawText (U8 ("GUITARE ▸"), juce::Rectangle<float> (first.getX(), first.getY() - 18, 120, 16), juce::Justification::centredLeft);
                g.drawText (U8 ("▸ AMPLI"), juce::Rectangle<float> (last.getRight() - 120, last.getBottom() + 3, 120, 14), juce::Justification::centredRight);
            }

            void drawKnob (juce::Graphics& g, juce::Rectangle<float> r, float v, const char* label, bool darkBody, bool showValue, const juce::String& valueText)
            {
                auto lab = r.removeFromBottom (15.0f);
                const float d = juce::jmin (r.getWidth() - 10.0f, r.getHeight() - 4.0f, 48.0f);
                auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
                g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillEllipse (c.translated (0, 2.5f));
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d4045), c.getX(), c.getY(), juce::Colour (0xff0c0d0f), c.getX(), c.getBottom(), false));
                g.fillEllipse (c);
                g.setColour (juce::Colour (0xff5a5e64)); g.drawEllipse (c.reduced (1.0f), 1.0f);
                const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
                const float ang = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, v);
                const auto ctr = c.getCentre();
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.drawLine (juce::Line<float> (ctr.getPointOnCircumference (d * 0.12f, ang), ctr.getPointOnCircumference (d * 0.44f, ang)), 2.4f);
                const auto ink = darkBody ? juce::Colours::white : juce::Colour (0xff121315);
                g.setColour (ink.withAlpha (0.4f));
                for (int t = 0; t <= 10; ++t)
                {
                    const float a = a0 + (a1 - a0) * (float) t / 10.0f;
                    g.drawLine (juce::Line<float> (ctr.getPointOnCircumference (d * 0.56f, a), ctr.getPointOnCircumference (d * 0.63f, a)), 1.0f);
                }
                g.setColour (ink.withAlpha (0.9f)); g.setFont (theme::ui (10.0f, true));
                g.drawText (showValue ? valueText : upper (U8 (label)), lab, juce::Justification::centred, true);
            }

            void drawPedal (juce::Graphics& g, int s, juce::Rectangle<float> b, float alpha = 1.0f)
            {
                const int type = pb.slotType (s);
                const auto& in = pedals::info (type);
                const auto home = slotBounds (s);
                const auto shift = b.getPosition() - home.getPosition();
                if (type == pedals::empty)
                {
                    const bool hot = hoverSlot == s;
                    g.setColour (juce::Colours::white.withAlpha (hot ? 0.07f : 0.025f)); g.fillRoundedRectangle (b, 12.0f);
                    juce::Path outline; outline.addRoundedRectangle (b.reduced (2.0f), 12.0f);
                    juce::Path dashed; const float pat[] = { 6.0f, 5.0f };
                    juce::PathStrokeType (1.3f).createDashedStroke (dashed, outline, pat, 2);
                    g.setColour (hot ? theme::accent : theme::dim); g.fillPath (dashed);
                    g.setFont (theme::ui (30.0f)); g.drawText ("+", b.withTrimmedBottom (24), juce::Justification::centred);
                    g.setFont (theme::ui (10.5f, true));
                    g.drawText (U8 ("AJOUTER UNE PÉDALE"), b.withTrimmedTop (b.getHeight() * 0.58f), juce::Justification::centredTop);
                    return;
                }
                const bool on = pb.slotOn (s);
                const auto body = juce::Colour (in.colour);
                const bool darkBody = body.getPerceivedBrightness() < 0.6f;
                g.setColour (juce::Colours::black.withAlpha (0.5f * alpha)); g.fillRoundedRectangle (b.translated (0, 5), 12.0f);
                g.setGradientFill (juce::ColourGradient (body.brighter (0.22f).withMultipliedAlpha (alpha), b.getX(), b.getY(),
                                                         body.darker (0.5f).withMultipliedAlpha (alpha), b.getX(), b.getBottom(), false));
                g.fillRoundedRectangle (b, 12.0f);
                g.setColour (juce::Colours::white.withAlpha (0.2f)); g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 1.0f);
                for (auto pt : { b.getTopLeft().translated (9, 9), b.getTopRight().translated (-9, 9), b.getBottomLeft().translated (9, -9), b.getBottomRight().translated (-9, -9) })
                { g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (pt)); }
                auto top = b.reduced (16.0f, 8.0f).removeFromTop (24.0f);
                auto led = juce::Rectangle<float> (10, 10).withCentre ({ top.getRight() - 4, top.getCentreY() });
                if (on) { g.setColour (juce::Colour (0x66ff3020)); g.fillEllipse (led.expanded (5.0f)); }
                g.setColour (on ? juce::Colour (0xffff4a36) : juce::Colour (0xff4a1612)); g.fillEllipse (led);
                g.setColour (darkBody ? juce::Colours::white : juce::Colour (0xff15171a));
                g.setFont (theme::ui (13.0f, true));
                g.drawText (upper (U8 (in.name)), top.withTrimmedRight (16), juce::Justification::centredLeft, true);
                for (int k = 0; k < in.numKnobs; ++k)
                {
                    const float v = pb.slotKnob (s, k);
                    const bool active = (dragKnobSlot == s && dragKnob == k) || (hoverSlot == s && hoverKnob == k);
                    drawKnob (g, knobBounds (s, k) + shift, v, in.knobs[k], darkBody, active, pedals::knobText (type, k, v));
                }
                auto fs = footBounds (s) + shift;
                g.setColour ((darkBody ? juce::Colours::white : juce::Colours::black).withAlpha (0.45f)); g.setFont (theme::ui (9.0f, true));
                g.drawText (U8 ("ONDE · ") + upper (U8 (in.family)), fs.withSizeKeepingCentre (b.getWidth(), 12.0f).translated (0, -30), juce::Justification::centred);
                g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillEllipse (fs.translated (0, 2.5f));
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8eaee), fs.getX(), fs.getY(), juce::Colour (0xff6d7178), fs.getRight(), fs.getBottom(), false));
                g.fillEllipse (fs);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9da1a8), fs.getX(), fs.getY(), juce::Colour (0xfff2f3f5), fs.getRight(), fs.getBottom(), false));
                g.fillEllipse (fs.reduced (6.0f));
                if (! on) { g.setColour (juce::Colours::black.withAlpha (0.38f)); g.fillRoundedRectangle (b, 12.0f); }
            }

            int slotAt (juce::Point<float> p) const
            {
                for (int s = 0; s < PedalboardPlugin::numSlots; ++s) if (slotBounds (s).contains (p)) return s;
                return -1;
            }
            int knobAt (int s, juce::Point<float> p) const
            {
                if (s < 0) return -1;
                for (int k = 0; k < pedals::info (pb.slotType (s)).numKnobs; ++k)
                    if (knobBounds (s, k).reduced (4.0f).contains (p)) return k;
                return -1;
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const auto p = e.position;
                const int s = slotAt (p), k = knobAt (s, p);
                if (s != hoverSlot || k != hoverKnob) { hoverSlot = s; hoverKnob = k; repaint(); }
                const bool pedal = s >= 0 && pb.slotType (s) != pedals::empty;
                setMouseCursor (k >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                       : (pedal && ! footBounds (s).contains (p)) ? juce::MouseCursor::DraggingHandCursor
                                                                                   : juce::MouseCursor::PointingHandCursor);
            }
            void mouseExit (const juce::MouseEvent&) override { hoverSlot = hoverKnob = -1; repaint(); }

            void mouseDown (const juce::MouseEvent& e) override
            {
                const auto p = e.position;
                const int s = slotAt (p);
                dragSlot = -1; dragging = false; dragKnobSlot = dragKnob = -1;
                if (s < 0) return;
                if (e.mods.isPopupMenu()) { showPedalMenu (s); return; }
                if (pb.slotType (s) == pedals::empty) { showAddMenu (s); return; }
                if (footBounds (s).contains (p)) { toggle (s); return; }
                if (const int k = knobAt (s, p); k >= 0)
                {
                    dragKnobSlot = s; dragKnob = k; dragStartValue = pb.slotKnob (s, k);
                    pb.edit.getUndoManager().beginNewTransaction (U8 ("Bouton de pédale"));
                    return;
                }
                dragSlot = s;
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragKnob >= 0)
                {
                    const float sens = e.mods.isShiftDown() ? 0.0008f : 0.005f;
                    const float v = juce::jlimit (0.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() * sens);
                    pb.setStored (knobId (dragKnobSlot, dragKnob), v);
                    repaint(); return;
                }
                if (dragSlot >= 0 && e.getDistanceFromDragStart() > 6) { dragging = true; dragPos = e.position; repaint(); }
            }

            void mouseUp (const juce::MouseEvent& e) override
            {
                if (dragging && dragSlot >= 0)
                {
                    const int target = slotAt (e.position);
                    if (target >= 0 && target != dragSlot) move (dragSlot, target);
                }
                dragging = false; dragSlot = -1; dragKnobSlot = dragKnob = -1;
                repaint();
            }

            void mouseDoubleClick (const juce::MouseEvent& e) override
            {
                const int s = slotAt (e.position), k = knobAt (s, e.position);
                if (k < 0) return;
                pb.edit.getUndoManager().beginNewTransaction (U8 ("Bouton de pédale"));
                pb.setStored (knobId (s, k), pedals::info (pb.slotType (s)).defaults[k]);
                repaint();
            }

            static juce::String knobId (int s, int k) { return PedalboardPlugin::paramId (s, ("k" + juce::String (k + 1)).toRawUTF8()); }

            void toggle (int s)
            {
                pb.edit.getUndoManager().beginNewTransaction (U8 ("Interrupteur de pédale"));
                pb.setStored (PedalboardPlugin::paramId (s, "on"), pb.slotOn (s) ? 0.0f : 1.0f);
                repaint();
            }
            void place (int s, int type, const char* what)
            {
                pb.edit.getUndoManager().beginNewTransaction (U8 (what));
                pb.setPedal (s, type);
                repaint();
            }
            void move (int from, int to)
            {
                pb.edit.getUndoManager().beginNewTransaction (U8 ("Déplacer une pédale"));
                pb.moveSlot (from, to);
                repaint();
            }

            void fillPedalChoices (juce::PopupMenu& m, int s)
            {
                juce::StringArray families;
                for (int t = 1; t < pedals::numTypes; ++t) families.addIfNotAlreadyThere (U8 (pedals::info (t).family));
                juce::Component::SafePointer<PedalboardEditor> safe (this);
                for (auto& f : families)
                {
                    juce::PopupMenu sub;
                    for (int t = 1; t < pedals::numTypes; ++t)
                        if (U8 (pedals::info (t).family) == f)
                            sub.addItem (U8 (pedals::info (t).name), true, pb.slotType (s) == t,
                                         [safe, s, t] { if (safe != nullptr) safe->place (s, t, "Poser une pédale"); });
                    m.addSubMenu (f, sub);
                }
            }

            void showAddMenu (int s)
            {
                juce::PopupMenu m;
                m.addSectionHeader (U8 ("Emplacement ") + juce::String (s + 1));
                fillPedalChoices (m, s);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotBounds (s).toNearestInt())));
            }

            void showPedalMenu (int s)
            {
                juce::PopupMenu m, change;
                fillPedalChoices (change, s);
                juce::Component::SafePointer<PedalboardEditor> safe (this);
                const bool has = pb.slotType (s) != pedals::empty;
                m.addSubMenu (has ? U8 ("Remplacer par") : U8 ("Poser"), change);
                if (has)
                {
                    m.addItem (pb.slotOn (s) ? U8 ("Éteindre") : U8 ("Allumer"), [safe, s] { if (safe != nullptr) safe->toggle (s); });
                    m.addItem (U8 ("Réglages d'origine"), [safe, s] { if (safe != nullptr) safe->place (s, safe->pb.slotType (s), "Réglages d'origine"); });
                    m.addSeparator();
                    m.addItem (U8 ("Avancer (plus tôt dans la chaîne)"), s > 0, false, [safe, s] { if (safe != nullptr) safe->move (s, s - 1); });
                    m.addItem (U8 ("Reculer (plus tard dans la chaîne)"), s + 1 < PedalboardPlugin::numSlots, false, [safe, s] { if (safe != nullptr) safe->move (s, s + 1); });
                    m.addSeparator();
                    m.addItem (U8 ("Retirer"), [safe, s] { if (safe != nullptr) safe->place (s, pedals::empty, "Retirer une pédale"); });
                }
                m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            }

            void timerCallback() override { repaint(); }

            PedalboardPlugin& pb;
            te::Plugin::Ptr keepAlive { &pb };
            juce::OwnedArray<juce::TextButton> presetButtons;
            juce::TextButton clearButton { juce::String::fromUTF8 ("Tout retirer") };
            int hoverSlot = -1, hoverKnob = -1, dragSlot = -1, dragKnobSlot = -1, dragKnob = -1;
            bool dragging = false;
            juce::Point<float> dragPos;
            float dragStartValue = 0;
        };
    }

    std::unique_ptr<juce::Component> createPedalboardEditor (PedalboardPlugin& p) { return std::make_unique<PedalboardEditor> (p); }

    // Préréglages du pédalier pour les chaînes et les tests : index dans la liste, -1 si inconnu.
    bool applyPedalboardPreset (PedalboardPlugin& p, const juce::String& name)
    {
        for (auto& pr : boardPresets())
            if (U8 (pr.name) == name) { p.applyPreset (pr.chain); return true; }
        return false;
    }
}
