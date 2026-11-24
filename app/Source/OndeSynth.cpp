#include "OndeSynth.h"
#include "ParamControls.h"
#include "Chains.h"
#include "Theme.h"

namespace ondefx
{
    namespace
    {
        constexpr int maxVoices = 24, maxUnison = 7, maxPartials = 26, chunk = 16;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        juce::String hzText (float v)  { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
        juce::String secText (float v) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; }
        juce::String pct (float v)     { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        juce::String U8str (const char* s) { return juce::String::fromUTF8 (s); }
        juce::NormalisableRange<float> logRange (float lo, float hi) { juce::NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi)); return r; }
        juce::NormalisableRange<float> timeRange (float lo, float hi) { juce::NormalisableRange<float> r (lo, hi); r.setSkewForCentre (lo + (hi - lo) * 0.08f); return r; }

        const char* waveNames[] = { "Sinus", "Triangle", "Dent de scie", "Carré", "Impulsion" };
        const char* kitNames[]  = { "Acoustique", "808", "Électro" };
        const char* bodyNames[] = { "Électrique", "Folk (acier)", "Classique (nylon)", "Basse électrique", "Harpe", "Ukulélé" };
        constexpr int ksSize = 4096, ksMask = ksSize - 1;

        inline float polyBlep (double t, double dt) noexcept
        {
            if (t < dt)        { t /= dt; return (float) (t + t - t * t - 1.0); }
            if (t > 1.0 - dt)  { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
            return 0.0f;
        }

        inline float oscillator (int wave, double t, double dt) noexcept
        {
            switch (wave)
            {
                case 0:  return (float) std::sin (twoPi * t);
                case 1:  return (float) (4.0 * std::abs (t - 0.5) - 1.0);
                case 2:  return (float) (2.0 * t - 1.0) - polyBlep (t, dt);
                case 3:  { double t2 = t + 0.5; if (t2 >= 1.0) t2 -= 1.0; return (t < 0.5 ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (t2, dt); }
                default: { double t2 = t + 0.75; if (t2 >= 1.0) t2 -= 1.0; return (t < 0.25 ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (t2, dt); }
            }
        }

        // Enveloppe : attaque linéaire, décroissance et relâchement exponentiels (-60 dB au temps réglé).
        struct Env
        {
            int stage = 0;           // 0 repos, 1 attaque, 2 décroissance, 3 maintien, 4 relâchement
            float v = 0;
            void on() noexcept  { stage = 1; }
            void off() noexcept { if (stage != 0) stage = 4; }
            inline float next (float aInc, float dCoef, float s, float rCoef) noexcept
            {
                switch (stage)
                {
                    case 1: v += aInc; if (v >= 1.0f) { v = 1.0f; stage = 2; } break;
                    case 2: v = s + (v - s) * dCoef; if (std::abs (v - s) < 1.0e-4f) { v = s; stage = 3; } break;
                    case 3: v = s; break;
                    case 4: v *= rCoef; if (v < 1.0e-4f) { v = 0; stage = 0; } break;
                    default: break;
                }
                return v;
            }
        };

        // Filtre à variable d'état (topologie TPT), sortie passe-bas.
        struct SVF
        {
            float ic1 = 0, ic2 = 0;
            inline float process (float x, float a1, float a2, float a3) noexcept
            {
                const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
                return v2;
            }
            void reset() noexcept { ic1 = ic2 = 0; }
        };

        // Oscillateur sinusoïdal récursif (rotation d'un phaseur) : très peu coûteux pour les partiels du piano.
        struct Rotor
        {
            double c = 1, s = 0, cr = 1, sr = 0;
            void set (double inc, double phase) { cr = std::cos (twoPi * inc); sr = std::sin (twoPi * inc); c = std::cos (phase); s = std::sin (phase); }
            inline float next() noexcept { const double nc = c * cr - s * sr; s = s * cr + c * sr; c = nc; return (float) s; }
            void normalise() noexcept { const double m = 1.0 / std::sqrt (c * c + s * s); c *= m; s *= m; }
        };

        float coefFor (float seconds, double fs) { return (float) std::exp (-6.907755 / (juce::jmax (0.0005, (double) seconds) * fs)); }   // -60 dB

        enum class Drum { kick, snare, clap, closedHat, openHat, tom, crash, ride, rim, cowbell };
        Drum drumFor (int note)
        {
            switch (note)
            {
                case 35: case 36: return Drum::kick;
                case 38: case 40: return Drum::snare;
                case 39:          return Drum::clap;
                case 42: case 44: return Drum::closedHat;
                case 46:          return Drum::openHat;
                case 49: case 52: case 55: case 57: return Drum::crash;
                case 51: case 53: case 59: return Drum::ride;
                case 37:          return Drum::rim;
                case 56:          return Drum::cowbell;
                default:          return Drum::tom;
            }
        }
    }

    //==============================================================================
    struct SynthPlugin::Voice
    {
        int note = -1, engine = 0;
        float velocity = 0;
        bool active = false, held = false;
        uint32_t age = 0;
        Env amp, filt;
        // Analogique
        double phase[maxUnison][2] {};
        double subPhase = 0;
        SVF f1[2], f2[2];
        // FM
        double cPhase = 0, mPhase = 0, tPhase = 0;
        float lastMod = 0, modEnv = 1, tineEnv = 1;
        // Piano
        int partials = 0;
        Rotor strings[maxPartials][2];
        float pAmp[maxPartials] {}, fastEnv[maxPartials] {}, slowEnv[maxPartials] {}, fastCoef[maxPartials] {}, slowCoef[maxPartials] {};
        float damper = 1, hammer = 0, hammerLP = 0, pan = 0;
        // Batterie
        Drum drum = Drum::kick;
        double dPhase[6] {};
        float dEnv = 0, dEnv2 = 0, dPitch = 0, nState1 = 0, nState2 = 0, clapClock = 0;
        // Cordes pincées : deux guides d'onde (deux polarisations de la corde, ou les deux cordes d'un chœur de 12 cordes).
        float ks[2][ksSize] {};
        int ksPos = 0, ksN[2] {}, excLen = 0, excPos = 0, pickupK = 0;
        float apC[2] {}, apX[2] {}, apY[2] {}, lpS[2] {}, loopA = 0, t60 = 1, polW = 0.35f, pickupAmt = 0, ratio2 = 1, t60k2 = 1;
        float exc[ksSize] {};
    };

    //==============================================================================
    const char* SynthPlugin::xmlTypeName = "ondeSynth";

    SynthPlugin::SynthPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        presetName.referTo (state, "preset", getUndoManager(), "Piano de concert");
        auto choice = [] (int n) { return juce::NormalisableRange<float> (0.0f, (float) (n - 1), 1.0f); };
        auto names = [] (const char* const* list, int n) { return [list, n] (float v) { return juce::String::fromUTF8 (list[juce::jlimit (0, n - 1, juce::roundToInt (v))]); }; };
        auto num = [] (int decimals, const char* suffix) { return [decimals, suffix] (float v) { return juce::String (v, decimals) + suffix; }; };
        const auto U8 = [] (const char* s) { return juce::String::fromUTF8 (s); };

        declare ("engine", "Moteur", choice (numEngines), 2.0f, names (engineNames, numEngines));
        declare ("wave1", "Oscillateur 1", choice (5), 2.0f, names (waveNames, 5));
        declare ("wave2", "Oscillateur 2", choice (5), 2.0f, names (waveNames, 5));
        declare ("semi2", "Osc. 2 : demi-tons", { -24.0f, 24.0f, 1.0f }, 0.0f, num (0, " dt"));
        declare ("detune", U8 ("Désaccord"), { 0.0f, 50.0f }, 7.0f, num (1, " ct"));
        declare ("mix2", U8 ("Mélange osc. 2"), { 0.0f, 1.0f }, 0.5f, pct);
        declare ("sub", "Sous-oscillateur", { 0.0f, 1.0f }, 0.0f, pct);
        declare ("noise", "Bruit", { 0.0f, 1.0f }, 0.0f, pct);
        declare ("unison", "Unisson", { 1.0f, (float) maxUnison, 1.0f }, 1.0f, num (0, " voix"));
        declare ("spread", "Largeur d'unisson", { 0.0f, 40.0f }, 15.0f, num (1, " ct"));
        declare ("width", U8 ("Stéréo"), { 0.0f, 1.0f }, 0.5f, pct);
        declare ("cutoff", U8 ("Coupure"), logRange (40.0f, 18000.0f), 4000.0f, hzText);
        declare ("reso", U8 ("Résonance"), { 0.0f, 1.0f }, 0.2f, pct);
        declare ("fenv", "Enveloppe du filtre", { -6.0f, 6.0f }, 2.0f, num (1, " oct"));
        declare ("fa", "Filtre : attaque", timeRange (0.001f, 5.0f), 0.005f, secText);
        declare ("fd", U8 ("Filtre : décroissance"), timeRange (0.01f, 8.0f), 0.4f, secText);
        declare ("fs", "Filtre : maintien", { 0.0f, 1.0f }, 0.3f, pct);
        declare ("fr", U8 ("Filtre : relâchement"), timeRange (0.01f, 8.0f), 0.4f, secText);
        declare ("keytrack", "Suivi du clavier", { 0.0f, 1.0f }, 0.5f, pct);
        declare ("a", "Attaque", timeRange (0.001f, 5.0f), 0.005f, secText);
        declare ("d", U8 ("Décroissance"), timeRange (0.01f, 10.0f), 0.3f, secText);
        declare ("s", "Maintien", { 0.0f, 1.0f }, 0.8f, pct);
        declare ("r", U8 ("Relâchement"), timeRange (0.01f, 8.0f), 0.3f, secText);
        declare ("ratio", "FM : rapport", { 0.5f, 16.0f, 0.5f }, 1.0f, num (1, ""));
        declare ("index", "FM : intensité", { 0.0f, 10.0f }, 2.0f, num (2, ""));
        declare ("idecay", U8 ("FM : décroissance"), timeRange (0.01f, 6.0f), 1.0f, secText);
        declare ("feedback", U8 ("FM : rétroaction"), { 0.0f, 1.0f }, 0.0f, pct);
        declare ("bright", U8 ("Brillance"), { 0.0f, 1.0f }, 0.6f, pct);
        declare ("hard", U8 ("Dureté du marteau"), { 0.0f, 1.0f }, 0.5f, pct);
        declare ("drive", "Saturation", { 0.0f, 1.0f }, 0.0f, pct);
        declare ("vibrato", "Vibrato", { 0.0f, 1.0f }, 0.0f, pct);
        declare ("velsens", U8 ("Sensibilité au toucher"), { 0.0f, 1.0f }, 0.8f, pct);
        declare ("kit", "Kit", choice (3), 0.0f, names (kitNames, 3));
        declare ("dtune", "Accord de la batterie", { -12.0f, 12.0f }, 0.0f, num (1, " dt"));
        declare ("ddecay", U8 ("Résonance de la batterie"), { 0.3f, 3.0f }, 1.0f, num (2, "x"));
        declare ("body", "Instrument", choice (6), 1.0f, names (bodyNames, 6));
        declare ("pickpos", U8 ("Position du médiator"), { 0.04f, 0.5f }, 0.16f, [] (float v) { return v < 0.1f ? U8str ("près du chevalet") : v > 0.35f ? U8str ("vers le manche") : juce::String (juce::roundToInt (v * 100)) + " %"; });
        declare ("pick", U8 ("Attaque du médiator"), { 0.0f, 1.0f }, 0.5f, pct);
        declare ("ring", U8 ("Résonance des cordes"), { 0.2f, 3.0f }, 1.0f, num (2, "x"));
        declare ("mute", U8 ("Étouffement"), { 0.0f, 1.0f }, 0.0f, pct);
        declare ("double", U8 ("Cordes doublées"), choice (2), 0.0f, [] (float v) { return v > 0.5f ? U8str ("oui (12 cordes)") : juce::String ("non"); });
        declare ("volume", "Volume", { -24.0f, 6.0f }, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });

        for (int i = 0; i < maxVoices; ++i) voices.push_back (std::make_unique<Voice>());
        if (! state.hasProperty ("engine"))         // nouvel instrument : piano de concert
            if (auto* p = findPreset ("Piano de concert")) applyPreset (*p);
    }

    SynthPlugin::~SynthPlugin() = default;

    void SynthPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        rate = info.sampleRate;
        pluckL.assign ((size_t) juce::jmax (4096, info.blockSizeSamples), 0.0f);
        pluckR.assign (pluckL.size(), 0.0f);
        bodyType = -1;
        reset();
    }

    void SynthPlugin::setupBody (int type)
    {
        // Modes de caisse (air, table, harmonique, brillance) : fréquence, sélectivité, niveau.
        struct Mode { float f, q, g; };
        static const Mode modes[7][4] = {
            { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } },                               // électrique : pas de caisse
            { { 100, 12, 0.9f }, { 205, 10, 0.6f }, { 420, 6, 0.35f }, { 2700, 2, 0.12f } },      // folk
            { { 95, 12, 1.0f }, { 190, 10, 0.7f }, { 380, 6, 0.35f }, { 1800, 2, 0.1f } },         // classique
            { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } },                               // basse électrique
            { { 150, 6, 0.4f }, { 300, 5, 0.3f }, { 600, 4, 0.2f }, { 1500, 2, 0.1f } },           // harpe
            { { 260, 10, 0.7f }, { 520, 8, 0.5f }, { 1100, 4, 0.25f }, { 3000, 2, 0.1f } },      // ukulélé
            { { 95, 6, 0.45f }, { 220, 5, 0.35f }, { 520, 4, 0.25f }, { 1400, 2.5f, 0.15f } } };     // table d'harmonie du piano
        static const float dry[7] = { 1.0f, 0.75f, 0.7f, 1.0f, 0.85f, 0.75f, 0.85f };
        bodyType = type; bodyDry = dry[type];
        for (int k = 0; k < 4; ++k)
        {
            const auto& md = modes[type][k];
            bodyGain[k] = md.g;
            body[k] = {};
            if (md.f <= 0) continue;
            const double w = juce::MathConstants<double>::twoPi * md.f / rate, alpha = std::sin (w) / (2.0 * md.q), a0 = 1.0 + alpha;
            body[k].b0 = (float) (alpha / a0); body[k].a1 = (float) (-2.0 * std::cos (w) / a0); body[k].a2 = (float) ((1.0 - alpha) / a0);
        }
    }

    void SynthPlugin::auditionNote (int note, float velocity)
    {
        const auto w = auditionFifo.write (1);
        if (w.blockSize1 > 0) auditionQueue[(size_t) w.startIndex1] = { note, velocity };
    }

    int SynthPlugin::engineOf (const SynthPreset& p)
    {
        for (auto& [id, v] : p.values) if (juce::String (id) == "engine") return juce::roundToInt (v);
        return 2;
    }

    void SynthPlugin::reset()
    {
        for (auto& v : voices) { v->active = false; v->held = false; }
        sustainDown = false; pitchBend = 0; modWheel = 0;
        ccVolume = ccExpression = ccGain = 1.0f; ccPan = pressure = 0.0f;
    }

    //==============================================================================
    void SynthPlugin::noteOn (int note, float velocity)
    {
        const int engine = juce::jlimit (0, numEngines - 1, juce::roundToInt (value ("engine")));
        Voice* v = nullptr;
        if (engine != 3)
            for (auto& x : voices) if (x->active && x->note == note) { v = x.get(); break; }          // même note : rejouée
        if (v == nullptr) for (auto& x : voices) if (! x->active) { v = x.get(); break; }
        if (v == nullptr)                                                                          // la plus ancienne
        {
            v = voices[0].get();
            for (auto& x : voices) if (x->age < v->age) v = x.get();
        }
        if (engine == 3 && (drumFor (note) == Drum::closedHat))                                    // la charleston fermée étouffe l'ouverte
            for (auto& x : voices) if (x->active && x->engine == 3 && x->drum == Drum::openHat) x->dEnv *= 0.02f;

        const bool restrike = v->active && v->note == note && v->engine == 4 && engine == 4;   // corde qui sonne encore : pincée à nouveau
        v->note = note; v->engine = engine; v->velocity = velocity;
        v->active = true; v->held = true; v->age = ++voiceCounter;
        v->amp = {}; v->filt = {}; v->amp.on(); v->filt.on();
        for (auto& f : v->f1) f.reset();
        for (auto& f : v->f2) f.reset();
        for (auto& p : v->phase) { p[0] = random.nextDouble(); p[1] = random.nextDouble(); }
        v->subPhase = 0;
        v->cPhase = v->mPhase = v->tPhase = 0; v->lastMod = 0; v->modEnv = 1; v->tineEnv = 1;
        v->damper = 1;
        const float width = value ("width");
        v->pan = juce::jlimit (-1.0f, 1.0f, (note - 64) / 40.0f) * width;

        if (engine == 2)
        {
            // Piano : partiels légèrement inharmoniques, deux cordes un peu désaccordées (battements), décroissance
            // en deux temps (attaque brillante puis son prolongé), plus courte dans l'aigu.
            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const double B = 0.00006 + 0.0005 * juce::jlimit (0.0, 1.0, (note - 40) / 50.0);
            const float bright = value ("bright") * (0.35f + 0.65f * velocity);
            const float T0 = juce::jlimit (0.8f, 16.0f, 10.0f * std::pow (0.5f, (note - 36) / 16.0f));
            v->partials = 0;
            for (int n = 1; n <= maxPartials; ++n)
            {
                const double fn = n * f0 * std::sqrt (1.0 + B * n * n);
                if (fn > rate * 0.45) break;
                const int i = v->partials++;
                const double detune = std::pow (2.0, (0.4 + 0.08 * n) / 1200.0);
                v->strings[i][0].set (fn / detune / rate, random.nextDouble() * twoPi);
                v->strings[i][1].set (fn * detune / rate, random.nextDouble() * twoPi);
                const float hammerComb = std::abs (std::sin (juce::MathConstants<float>::pi * n * 0.118f)) + 0.12f;
                v->pAmp[i] = std::pow ((float) n, -(1.9f - 1.3f * bright)) * hammerComb;
                const float Tn = T0 / (1.0f + 0.22f * (n - 1));
                v->fastCoef[i] = coefFor (Tn * 0.18f, rate);
                v->slowCoef[i] = coefFor (Tn, rate);
                v->fastEnv[i] = 0.65f; v->slowEnv[i] = 0.35f;
            }
            float norm = 0;
            for (int i = 0; i < v->partials; ++i) norm += v->pAmp[i];
            for (int i = 0; i < v->partials; ++i) v->pAmp[i] /= juce::jmax (0.5f, norm) * 0.85f;
            v->hammer = 1.0f; v->hammerLP = 0;
        }
        if (engine == 3)
        {
            v->drum = drumFor (note);
            v->dEnv = 1; v->dEnv2 = 1; v->dPitch = 1; v->nState1 = v->nState2 = 0; v->clapClock = 0;
            for (auto& p : v->dPhase) p = 0;
            v->pan = 0;
            if (v->drum == Drum::tom) v->pan = juce::jlimit (-0.5f, 0.5f, (note - 45) / 10.0f);
            if (v->drum == Drum::closedHat || v->drum == Drum::openHat) v->pan = 0.25f;
            if (v->drum == Drum::crash) v->pan = -0.3f;
            if (v->drum == Drum::ride) v->pan = 0.35f;
        }
        if (engine == 4)
        {
            // Corde pincée (Karplus-Strong étendu) : l'excitation est un bruit filtré (plus clair quand on attaque fort),
            // creusé par la position du médiator (les harmoniques qui ont un nœud à cet endroit disparaissent).
            const int type = juce::jlimit (0, 5, juce::roundToInt (value ("body")));
            const double f0 = juce::jmax (25.0, 440.0 * std::pow (2.0, (note - 69) / 12.0));
            const int P = juce::jlimit (2, ksSize - 8, (int) std::lround (rate / f0));
            const float pick = value ("pick"), mute = value ("mute"), bright = value ("bright");
            static const float baseT60[] = { 3.5f, 3.0f, 2.2f, 4.5f, 3.6f, 1.3f };
            static const float dark[]    = { 0.05f, 0.0f, 0.18f, 0.12f, 0.02f, 0.1f };
            v->t60 = juce::jlimit (0.04f, 15.0f, baseT60[type] * value ("ring") * std::pow (2.0f, -(note - 40) / 30.0f) * (1.0f - 0.94f * mute));
            v->loopA = juce::jlimit (0.0f, 0.93f, 0.03f + (1.0f - bright) * 0.5f + dark[type] + mute * 0.35f);
            const bool dbl = value ("double") > 0.5f;
            v->ratio2 = dbl ? (note < 60 ? 2.0f : 1.0012f) : 1.00029f;      // chœur : octave dans le grave, unisson dans l'aigu
            v->polW = dbl ? 0.45f : 0.3f;
            v->t60k2 = dbl ? 0.9f : 1.2f;                                   // seconde polarisation : un peu plus longue (décroissance en deux temps)
            v->pickupK = (type == 0 || type == 3) ? juce::jmax (1, (int) std::lround (P * (type == 0 ? 0.11 : 0.18))) : 0;
            v->pickupAmt = v->pickupK > 0 ? 0.85f : 0.0f;
            if (! restrike) { std::memset (v->ks, 0, sizeof (v->ks)); v->ksPos = 0; for (int p = 0; p < 2; ++p) v->apX[p] = v->apY[p] = v->lpS[p] = 0; }
            else for (auto& line : v->ks) for (auto& x : line) x *= 0.3f;                // le doigt arrête presque la corde
            const float c = juce::jlimit (0.04f, 0.95f, 0.06f + 0.55f * pick * (0.4f + 0.6f * velocity) + 0.3f * velocity * velocity) * (type == 2 ? 0.6f : 1.0f);
            const int K = juce::jlimit (1, P - 1, (int) std::lround (value ("pickpos") * P));
            float lp = 0, sum = 0;
            for (int i = 0; i < P; ++i) { lp += c * ((random.nextFloat() * 2.0f - 1.0f) - lp); v->exc[i] = lp; sum += lp; }
            const float mean = sum / (float) P;
            float energy = 0;
            for (int i = P - 1; i >= 0; --i) { v->exc[i] = (v->exc[i] - mean) - (i >= K ? v->exc[i - K] - mean : 0.0f); energy += v->exc[i] * v->exc[i]; }
            const float norm = 0.45f / std::sqrt (juce::jmax (1.0e-9f, energy / (float) P));
            for (int i = 0; i < P; ++i) v->exc[i] *= norm;
            v->excLen = P; v->excPos = 0;
            v->pan = juce::jlimit (-1.0f, 1.0f, (note - 52) / 30.0f) * value ("width") * 0.8f;
        }
    }

    void SynthPlugin::noteOff (int note)
    {
        for (auto& v : voices)
            if (v->active && v->held && v->note == note)
            {
                v->held = false;
                if (! sustainDown && v->engine != 3) { v->amp.off(); v->filt.off(); }
            }
    }

    //==============================================================================
    void SynthPlugin::renderVoices (float* L, float* R, int start, int n)
    {
        if (pluckL.empty()) { pluckL.assign (4096, 0.0f); pluckR.assign (4096, 0.0f); }   // sans initialise (rare)
        if (n > (int) pluckL.size())         // très grands blocs : rendus par morceaux (tampons préparés à l'initialisation)
        {
            for (int o = 0; o < n; o += (int) pluckL.size()) renderVoices (L, R, start + o, juce::jmin ((int) pluckL.size(), n - o));
            return;
        }
        const double fs = rate;
        // Réglages lus une fois par bloc.
        const int wave1 = juce::roundToInt (value ("wave1")), wave2 = juce::roundToInt (value ("wave2"));
        const float semi2 = value ("semi2"), detune = value ("detune"), mix2 = value ("mix2"), sub = value ("sub"), noise = value ("noise");
        const int unison = juce::jlimit (1, maxUnison, juce::roundToInt (value ("unison")));
        const float spread = value ("spread"), width = value ("width");
        const float cutoff = value ("cutoff"), reso = value ("reso"), fenv = value ("fenv"), keytrack = value ("keytrack");
        const float aInc = 1.0f / (float) juce::jmax (1.0, value ("a") * fs), dCoef = coefFor (value ("d"), fs), sus = value ("s"), rCoef = coefFor (value ("r"), fs);
        const float faInc = 1.0f / (float) juce::jmax (1.0, value ("fa") * fs), fdCoef = coefFor (value ("fd"), fs), fsus = value ("fs"), frCoef = coefFor (value ("fr"), fs);
        const float ratio = value ("ratio"), index = value ("index"), idCoef = coefFor (value ("idecay"), fs), feedback = value ("feedback");
        const float bright = value ("bright"), hard = value ("hard"), drive = value ("drive"), velsens = value ("velsens");
        const int kit = juce::roundToInt (value ("kit"));
        const float dtune = std::pow (2.0f, value ("dtune") / 12.0f), ddecay = value ("ddecay");
        const float vibDepth = value ("vibrato") * 25.0f + modWheel * 40.0f + pressure * 30.0f;   // centièmes de demi-ton (aftertouch : vibrato)
        const float bend = std::pow (2.0f, pitchBend * 2.0f / 12.0f);
        const float tineCoef = coefFor (0.25f, fs), damperCoef = coefFor (0.12f, fs), hammerCoef = coefFor (0.006f, fs);
        // Batterie : décroissances calculées une fois par bloc (et non à chaque échantillon).
        const float dkK = value ("ddecay") * (kit == 1 ? 1.6f : kit == 2 ? 0.7f : 1.0f);
        const float cKickPitch = coefFor (kit == 1 ? 0.12f : 0.06f, fs), cKick = coefFor ((kit == 1 ? 1.4f : 0.45f) * dkK, fs), cClick = coefFor (0.004f, fs);
        const float cSnTone = coefFor (0.12f * dkK, fs), cSnNoise = coefFor ((kit == 1 ? 0.22f : 0.18f) * dkK, fs), cClap = coefFor (0.2f * dkK, fs);
        const float cHatC = coefFor (0.06f * dkK, fs), cHatO = coefFor (0.45f * dkK, fs), cCrash = coefFor (1.8f * dkK, fs), cRide = coefFor (1.3f * dkK, fs), cBell = coefFor (0.25f * dkK, fs);
        const float cRim = coefFor (0.03f * dkK, fs), cTomPitch = coefFor (0.15f, fs), cTom = coefFor (0.5f * dkK, fs), cTomClick = coefFor (0.01f, fs);
        // Cordes pincées : rendues à part pour passer ensemble dans la caisse de résonance.
        const float relTime = value ("r");
        bool anyPluck = false;
        std::fill (pluckL.begin(), pluckL.begin() + n, 0.0f);
        std::fill (pluckR.begin(), pluckR.begin() + n, 0.0f);

        for (auto& vp : voices)
        {
            auto& v = *vp;
            if (! v.active) continue;
            const float velGain = (1.0f - velsens) + velsens * v.velocity;
            const float gl = std::sqrt (0.5f * (1.0f - v.pan)), gr = std::sqrt (0.5f * (1.0f + v.pan));

            for (int c0 = 0; c0 < n && v.active; c0 += chunk)
            {
                const int m = std::min (chunk, n - c0);
                // Hauteur du morceau de 16 échantillons (vibrato, pitch bend).
                const float vib = vibDepth > 0.0f ? std::pow (2.0f, vibDepth * (float) std::sin (twoPi * (vibPhase + 5.5 * c0 / fs)) / 1200.0f) : 1.0f;
                const double freq = 440.0 * std::pow (2.0, (v.note - 69) / 12.0) * bend * vib;

                if (v.engine == 0)
                {
                    double inc[maxUnison][2];
                    float panU[maxUnison], gainLU[maxUnison], gainRU[maxUnison];
                    for (int u = 0; u < unison; ++u)
                    {
                        const float pos = unison > 1 ? 2.0f * u / (unison - 1) - 1.0f : 0.0f;
                        const double det = pos * spread;
                        inc[u][0] = freq * std::pow (2.0, det / 1200.0) / fs;
                        inc[u][1] = freq * std::pow (2.0, (semi2 * 100.0 + detune + det) / 1200.0) / fs;
                        panU[u] = pos * width;
                        gainLU[u] = std::sqrt (0.5f * (1.0f - panU[u])); gainRU[u] = std::sqrt (0.5f * (1.0f + panU[u]));
                    }
                    const double subInc = freq * 0.5 / fs;
                    // Filtre : coupure modulée par l'enveloppe et le suivi du clavier (recalculée par morceau).
                    const float envNow = v.filt.v;
                    const float fc = juce::jlimit (20.0f, (float) fs * 0.45f,
                                                   cutoff * std::pow (2.0f, fenv * envNow + keytrack * (v.note - 60) / 12.0f));
                    const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
                    const float k1 = 2.0f - 1.9f * reso, k2 = 1.41f;
                    const float a11 = 1.0f / (1.0f + g * (g + k1)), a21 = g * a11, a31 = g * a21;
                    const float a12 = 1.0f / (1.0f + g * (g + k2)), a22 = g * a12, a32 = g * a22;
                    const float uNorm = 1.0f / std::sqrt ((float) unison);
                    for (int i = 0; i < m; ++i)
                    {
                        float l = 0, r = 0;
                        for (int u = 0; u < unison; ++u)
                        {
                            float s = 0;
                            for (int o = 0; o < 2; ++o)
                            {
                                auto& ph = v.phase[u][o];
                                const float x = oscillator (o == 0 ? wave1 : wave2, ph, inc[u][o]);
                                s += o == 0 ? x * (1.0f - mix2) : x * mix2;
                                ph += inc[u][o]; if (ph >= 1.0) ph -= 1.0;
                            }
                            l += s * gainLU[u];
                            r += s * gainRU[u];
                        }
                        l *= uNorm; r *= uNorm;
                        const float subS = sub * (float) std::sin (twoPi * v.subPhase) * 0.7f;
                        v.subPhase += subInc; if (v.subPhase >= 1.0) v.subPhase -= 1.0;
                        const float nz = noise > 0.0f ? noise * (random.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f;
                        l += subS + nz; r += subS + nz;
                        l = v.f2[0].process (v.f1[0].process (l, a11, a21, a31), a12, a22, a32);
                        r = v.f2[1].process (v.f1[1].process (r, a11, a21, a31), a12, a22, a32);
                        v.filt.next (faInc, fdCoef, fsus, frCoef);
                        const float e = v.amp.next (aInc, dCoef, sus, rCoef) * velGain * 0.5f;
                        L[start + c0 + i] += l * e * gl * 1.41f;
                        R[start + c0 + i] += r * e * gr * 1.41f;
                    }
                    if (v.amp.stage == 0) v.active = false;
                }
                else if (v.engine == 1)
                {
                    // FM : porteuse + modulateur (rapport, intensité qui décroît) + « lame » brillante très brève.
                    const double cInc = freq / fs, mInc = freq * ratio / fs, tInc = freq * 14.0 / fs;
                    const float idx = index * (0.35f + 0.65f * v.velocity);
                    for (int i = 0; i < m; ++i)
                    {
                        const float mod = (float) std::sin (twoPi * v.mPhase + feedback * 2.0f * v.lastMod);
                        v.lastMod = mod;
                        const float tine = (float) std::sin (twoPi * v.tPhase) * bright * 1.5f * v.tineEnv * v.velocity;
                        const float s = (float) std::sin (twoPi * v.cPhase + idx * v.modEnv * mod + tine);
                        v.cPhase += cInc; if (v.cPhase >= 1.0) v.cPhase -= 1.0;
                        v.mPhase += mInc; if (v.mPhase >= 1.0) v.mPhase -= 1.0;
                        v.tPhase += tInc; if (v.tPhase >= 1.0) v.tPhase -= 1.0;
                        v.modEnv = 0.15f + (v.modEnv - 0.15f) * idCoef;
                        v.tineEnv *= tineCoef;
                        const float e = v.amp.next (aInc, dCoef, sus, rCoef) * velGain * 0.45f;
                        L[start + c0 + i] += s * e * gl * 1.41f;
                        R[start + c0 + i] += s * e * gr * 1.41f;
                    }
                    if (v.amp.stage == 0) v.active = false;
                }
                else if (v.engine == 2)
                {
                    const bool damped = ! v.held && ! sustainDown;
                    const float hammerCut = 0.05f + 0.6f * hard * v.velocity;
                    for (int i = 0; i < m; ++i)
                    {
                        float s = 0;
                        for (int p = 0; p < v.partials; ++p)
                        {
                            const float env = v.fastEnv[p] + v.slowEnv[p];
                            s += (v.strings[p][0].next() + v.strings[p][1].next()) * 0.5f * v.pAmp[p] * env;
                            v.fastEnv[p] *= v.fastCoef[p];
                            v.slowEnv[p] *= v.slowCoef[p];
                        }
                        // Marteau : bref bruit filtré, plus dur et plus clair quand on joue fort.
                        if (v.hammer > 1.0e-4f)
                        {
                            v.hammerLP += hammerCut * ((random.nextFloat() * 2.0f - 1.0f) - v.hammerLP);
                            s += v.hammerLP * v.hammer * 0.25f * (0.3f + hard);
                            v.hammer *= hammerCoef;
                        }
                        if (damped) v.damper *= damperCoef;
                        const float e = s * v.damper * velGain * (0.4f + 0.6f * v.velocity) * 0.6f;
                        pluckL[(size_t) (c0 + i)] += e * gl * 1.41f;      // piano : passe par la table d'harmonie (comme la caisse des cordes pincées)
                        pluckR[(size_t) (c0 + i)] += e * gr * 1.41f;
                    }
                    for (int p = 0; p < v.partials; ++p) { v.strings[p][0].normalise(); v.strings[p][1].normalise(); }
                    // Les partiels aigus s'éteignent les premiers : ceux devenus inaudibles ne sont plus calculés.
                    while (v.partials > 1 && v.fastEnv[v.partials - 1] + v.slowEnv[v.partials - 1] < 1.0e-5f) --v.partials;
                    anyPluck = true;
                    if (v.damper < 1.0e-4f || (v.partials > 0 && v.slowEnv[0] < 1.0e-4f)) v.active = false;
                }
                else if (v.engine == 3)
                {
                    // Batterie synthétisée : chaque son est calculé à partir de quelques oscillateurs et de bruit.
                    const float tuneK = dtune * (kit == 1 ? 0.8f : kit == 2 ? 1.15f : 1.0f);
                    for (int i = 0; i < m; ++i)
                    {
                        const float nz = random.nextFloat() * 2.0f - 1.0f;
                        float s = 0;
                        switch (v.drum)
                        {
                            case Drum::kick:
                            {
                                const float f = (45.0f + (kit == 1 ? 90.0f : 140.0f) * v.dPitch) * tuneK;
                                v.dPhase[0] += f / fs; if (v.dPhase[0] >= 1.0) v.dPhase[0] -= 1.0;
                                s = (float) std::sin (twoPi * v.dPhase[0]) * v.dEnv + nz * v.dEnv2 * (kit == 2 ? 0.5f : 0.3f);
                                if (kit == 1) s = std::tanh (s * 1.6f);
                                v.dPitch *= cKickPitch;
                                v.dEnv *= cKick;
                                v.dEnv2 *= cClick;
                                break;
                            }
                            case Drum::snare:
                            {
                                v.dPhase[0] += 185.0 * tuneK / fs; v.dPhase[1] += 330.0 * tuneK / fs;
                                const float tone = (float) (std::sin (twoPi * v.dPhase[0]) + 0.5 * std::sin (twoPi * v.dPhase[1]));
                                v.nState1 += 0.35f * (nz - v.nState1);
                                const float hp = nz - v.nState1;
                                s = tone * v.dEnv * (kit == 2 ? 0.3f : 0.6f) + hp * v.dEnv2 * 0.9f;
                                v.dEnv *= cSnTone;
                                v.dEnv2 *= cSnNoise;
                                break;
                            }
                            case Drum::clap:
                            {
                                // Trois claquements rapprochés, puis une queue.
                                v.clapClock += 1.0f / (float) fs;
                                const float burst = v.clapClock < 0.03f ? (std::fmod (v.clapClock, 0.01f) < 0.002f ? 1.0f : 0.2f) : 1.0f;
                                v.nState1 += 0.3f * (nz - v.nState1);
                                v.nState2 += 0.08f * (v.nState1 - v.nState2);
                                s = (v.nState1 - v.nState2) * v.dEnv * burst * 2.2f;
                                if (v.clapClock > 0.03f) v.dEnv *= cClap;
                                break;
                            }
                            case Drum::closedHat:
                            case Drum::openHat:
                            case Drum::crash:
                            case Drum::ride:
                            case Drum::cowbell:
                            {
                                // Son métallique : ondes carrées à rapports non harmoniques, puis passe-haut.
                                static const double ratios[6] = { 1.0, 1.342, 1.783, 2.413, 2.627, 3.919 };
                                const bool bell = v.drum == Drum::cowbell;
                                const double base = (bell ? 560.0 : v.drum == Drum::ride ? 330.0 : 300.0) * tuneK;
                                float metal = 0;
                                for (int o = 0; o < (bell ? 2 : 6); ++o)
                                {
                                    v.dPhase[o] += base * (bell ? (o == 0 ? 1.0 : 1.48) : ratios[o]) / fs;
                                    if (v.dPhase[o] >= 1.0) v.dPhase[o] -= 1.0;
                                    metal += v.dPhase[o] < 0.5 ? 1.0f : -1.0f;
                                }
                                metal = metal / (bell ? 2.0f : 6.0f) + (bell ? 0.0f : nz * (v.drum == Drum::crash ? 0.8f : 0.4f));
                                const float hpCoef = bell ? 0.2f : 0.55f;
                                v.nState1 += hpCoef * (metal - v.nState1);
                                s = (bell ? v.nState1 : metal - v.nState1) * v.dEnv * (bell ? 0.6f : 0.45f);
                                v.dEnv *= v.drum == Drum::closedHat ? cHatC : v.drum == Drum::openHat ? cHatO : v.drum == Drum::crash ? cCrash : v.drum == Drum::ride ? cRide : cBell;
                                break;
                            }
                            case Drum::rim:
                            {
                                v.dPhase[0] += 1700.0 * tuneK / fs;
                                s = (float) std::sin (twoPi * v.dPhase[0]) * v.dEnv + nz * v.dEnv * 0.3f;
                                v.dEnv *= cRim;
                                break;
                            }
                            case Drum::tom:
                            {
                                const float base = (70.0f + (v.note - 41) * 9.0f) * tuneK;
                                v.dPhase[0] += base * (1.0f + 0.6f * v.dPitch) / fs; if (v.dPhase[0] >= 1.0) v.dPhase[0] -= 1.0;
                                s = (float) std::sin (twoPi * v.dPhase[0]) * v.dEnv + nz * v.dEnv2 * 0.2f;
                                v.dPitch *= cTomPitch;
                                v.dEnv *= cTom;
                                v.dEnv2 *= cTomClick;
                                break;
                            }
                        }
                        const float e = s * velGain * 0.8f;
                        L[start + c0 + i] += e * gl * 1.41f;
                        R[start + c0 + i] += e * gr * 1.41f;
                    }
                    if (v.dEnv < 1.0e-4f) v.active = false;
                }
                else
                {
                    // Cordes pincées : longueur de la boucle (hauteur, pitch bend et vibrato compris), gain par passage
                    // pour la durée de résonance voulue (plus courte une fois la note relâchée : le doigt étouffe la corde).
                    const bool ringing = v.held || sustainDown;
                    const float a = v.loopA;
                    float g[2];
                    for (int p = 0; p < 2; ++p)
                    {
                        const double f = juce::jlimit (20.0, fs * 0.3, freq * (p == 0 ? 1.0 : (double) v.ratio2));
                        const double w = twoPi * f / fs;
                        const double mag = (1.0 - a) / std::sqrt (1.0 - 2.0 * a * std::cos (w) + (double) a * a);
                        const double lpDelay = std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w;
                        const double D = juce::jlimit (2.5, (double) ksSize - 4.0, fs / f - lpDelay);
                        const int N = (int) std::floor (D - 0.2);
                        const double frac = D - N;
                        v.ksN[p] = N; v.apC[p] = (float) ((1.0 - frac) / (1.0 + frac));
                        double T = v.t60 * (p == 0 ? 1.0f : v.t60k2) * (0.6 + 0.4 * ringing);
                        if (! ringing) T = juce::jmin (T, (double) juce::jmax (0.02f, relTime));
                        g[p] = (float) juce::jmin (0.99995, std::pow (10.0, -3.0 / (f * T)) / mag);
                    }
                    const float w2 = v.polW, w1 = 1.0f - w2;
                    float peak = 0;
                    for (int i = 0; i < m; ++i)
                    {
                        const float e = v.excPos < v.excLen ? v.exc[v.excPos++] : 0.0f;
                        float out = 0;
                        for (int p = 0; p < 2; ++p)
                        {
                            const float x = v.ks[p][(v.ksPos - v.ksN[p]) & ksMask];
                            const float y = v.apC[p] * x + v.apX[p] - v.apC[p] * v.apY[p];
                            v.apX[p] = x; v.apY[p] = y;
                            v.lpS[p] = (1.0f - a) * y + a * v.lpS[p];
                            const float s = v.lpS[p] * g[p] + e;
                            v.ks[p][v.ksPos] = s;
                            out += s * (p == 0 ? w1 : w2);
                        }
                        if (v.pickupK > 0)                                  // micro magnétique : prise près du chevalet
                            out -= v.pickupAmt * (v.ks[0][(v.ksPos - v.pickupK) & ksMask] * w1 + v.ks[1][(v.ksPos - v.pickupK) & ksMask] * w2);
                        v.ksPos = (v.ksPos + 1) & ksMask;
                        peak = std::max (peak, std::abs (out));
                        const float o = out * velGain * 0.8f;
                        pluckL[(size_t) (c0 + i)] += o * gl * 1.41f;
                        pluckR[(size_t) (c0 + i)] += o * gr * 1.41f;
                    }
                    anyPluck = true;
                    if (v.excPos >= v.excLen && peak < 2.0e-5f) v.active = false;
                }
            }
        }
        if (anyPluck)
        {
            // Piano : table d'harmonie ; cordes pincées : caisse choisie.
            const int type = juce::roundToInt (value ("engine")) == 2 ? 6 : juce::jlimit (0, 5, juce::roundToInt (value ("body")));
            if (type != bodyType) setupBody (type);
            float* io[2] = { pluckL.data(), pluckR.data() };
            float* dst[2] = { L + start, R + start };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = io[c][i];
                    float y = x * bodyDry;
                    for (int k = 0; k < 4; ++k)
                    {
                        if (bodyGain[k] == 0.0f) continue;
                        auto& rz = body[k];
                        const float r = rz.b0 * x + rz.z1[c];
                        rz.z1[c] = -rz.a1 * r + rz.z2[c];
                        rz.z2[c] = -rz.b0 * x - rz.a2 * r;
                        y += r * bodyGain[k];
                    }
                    dst[c][i] += y;
                }
        }
        vibPhase = std::fmod (vibPhase + 5.5 * n / fs, 1.0);
        // Sortie : marge (plusieurs voix s'additionnent), saturation douce qui ne dépasse jamais -1 dBFS, volume avant.
        const float gain = std::pow (10.0f, value ("volume") / 20.0f) * 0.45f;
        const float k = 1.0f + drive * 4.0f, makeup = 0.89f / std::sqrt (k);
        // Volume et expression MIDI (lissés : pas de clic), puis balance du panoramique MIDI.
        const float ccTarget = ccVolume * ccExpression, ccSmooth = 1.0f - std::exp (-1.0f / (0.005f * (float) fs));
        const float panL = juce::jmin (1.0f, 1.0f - ccPan), panR = juce::jmin (1.0f, 1.0f + ccPan);
        for (int i = start; i < start + n; ++i)
        {
            ccGain += ccSmooth * (ccTarget - ccGain);
            L[i] = std::tanh (L[i] * gain * k) * makeup * ccGain * panL;
            R[i] = std::tanh (R[i] * gain * k) * makeup * ccGain * panR;
        }
    }

    void SynthPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr) return;
        auto& buf = *fc.destBuffer;
        const int n = fc.bufferNumSamples, start = fc.bufferStartSample;
        buf.clear (start, n);
        if (buf.getNumChannels() == 0) return;
        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : nullptr;
        std::vector<float> monoScratch;
        if (R == nullptr) { monoScratch.assign ((size_t) (start + n), 0.0f); R = monoScratch.data(); }

        // Évènements MIDI dans l'ordre, l'audio rendu entre deux évènements.
        int pos = 0;
        auto renderTo = [&] (int end) { if (end > pos) { renderVoices (L, R, start + pos, end - pos); pos = end; } };
        if (fc.bufferForMidiMessages != nullptr)
        {
            fc.bufferForMidiMessages->sortByTimestamp();
            for (auto& msg : *fc.bufferForMidiMessages)
            {
                const int at = juce::jlimit (0, n, juce::roundToInt (msg.getTimeStamp() * rate));
                renderTo (at);
                if (msg.isNoteOn())                  noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
                else if (msg.isNoteOff())            noteOff (msg.getNoteNumber());
                else if (msg.isSustainPedalOn())     sustainDown = true;
                else if (msg.isSustainPedalOff())
                {
                    sustainDown = false;
                    for (auto& v : voices) if (v->active && ! v->held && v->engine != 3) { v->amp.off(); v->filt.off(); }
                }
                else if (msg.isPitchWheel())         pitchBend = (msg.getPitchWheelValue() - 8192) / 8192.0f;
                else if (msg.isControllerOfType (1)) modWheel = msg.getControllerValue() / 127.0f;
                else if (msg.isControllerOfType (7)) ccVolume = msg.getControllerValue() / 127.0f;
                else if (msg.isControllerOfType (11)) ccExpression = msg.getControllerValue() / 127.0f;
                else if (msg.isControllerOfType (10)) ccPan = juce::jlimit (-1.0f, 1.0f, (msg.getControllerValue() - 64) / 63.0f);
                else if (msg.isChannelPressure())    pressure = msg.getChannelPressureValue() / 127.0f;
                else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) v->active = false;
            }
        }
        // Notes jouées depuis la fenêtre de l'instrument (clavier, pads).
        {
            const auto rd = auditionFifo.read (auditionFifo.getNumReady());
            for (int k = 0; k < rd.blockSize1 + rd.blockSize2; ++k)
            {
                const auto& [note, vel] = auditionQueue[(size_t) (k < rd.blockSize1 ? rd.startIndex1 + k : rd.startIndex2 + k - rd.blockSize1)];
                if (vel > 0.0f) noteOn (note, vel); else noteOff (note);
            }
        }
        renderTo (n);
        if (buf.getNumChannels() == 1) for (int i = start; i < start + n; ++i) L[i] = 0.5f * (L[i] + R[i]);
    }

    //==============================================================================
    const std::vector<SynthPreset>& SynthPlugin::presets()
    {
        // Préréglages Onde : réglages d'origine, conçus pour sonner tout de suite.
        static const std::vector<SynthPreset> list = {
            { "Pianos", "Piano de concert",          { { "engine", 2 }, { "bright", 0.6f }, { "hard", 0.5f }, { "velsens", 0.85f }, { "width", 0.6f } } },
            { "Pianos", "Piano à queue",            { { "engine", 2 }, { "bright", 0.72f }, { "hard", 0.55f }, { "velsens", 0.9f }, { "width", 0.85f } } },
            { "Pianos", "Piano droit",              { { "engine", 2 }, { "bright", 0.45f }, { "hard", 0.5f }, { "velsens", 0.8f }, { "width", 0.45f } } },
            { "Pianos", "Piano doux",                { { "engine", 2 }, { "bright", 0.3f }, { "hard", 0.2f }, { "velsens", 0.7f }, { "width", 0.5f } } },
            { "Pianos", "Piano brillant",            { { "engine", 2 }, { "bright", 0.95f }, { "hard", 0.85f }, { "velsens", 0.8f }, { "width", 0.7f } } },
            { "Pianos", "Piano électrique",          { { "engine", 1 }, { "ratio", 1 }, { "index", 1.6f }, { "idecay", 1.2f }, { "bright", 0.5f }, { "a", 0.002f }, { "d", 3.0f }, { "s", 0.25f }, { "r", 0.35f }, { "width", 0.6f } } },
            { "Pianos", "Piano électrique doux",     { { "engine", 1 }, { "ratio", 1 }, { "index", 0.9f }, { "idecay", 2.0f }, { "bright", 0.2f }, { "a", 0.003f }, { "d", 3.5f }, { "s", 0.3f }, { "r", 0.4f }, { "vibrato", 0.15f } } },
            { "Pianos", "Piano électrique saturé",   { { "engine", 1 }, { "ratio", 1 }, { "index", 2.5f }, { "idecay", 0.8f }, { "bright", 0.8f }, { "drive", 0.5f }, { "a", 0.002f }, { "d", 2.5f }, { "s", 0.3f }, { "r", 0.25f } } },
            { "Claviers", "Orgue doux",              { { "engine", 0 }, { "wave1", 0 }, { "wave2", 0 }, { "semi2", 12 }, { "mix2", 0.45f }, { "sub", 0.5f }, { "cutoff", 9000 }, { "fenv", 0 }, { "a", 0.005f }, { "d", 0.1f }, { "s", 1 }, { "r", 0.08f }, { "vibrato", 0.25f }, { "unison", 1 } } },
            { "Claviers", "Orgue rock",              { { "engine", 0 }, { "wave1", 1 }, { "wave2", 3 }, { "semi2", 12 }, { "mix2", 0.35f }, { "sub", 0.6f }, { "drive", 0.4f }, { "cutoff", 5000 }, { "fenv", 0 }, { "a", 0.003f }, { "s", 1 }, { "r", 0.05f }, { "vibrato", 0.35f } } },
            { "Claviers", "Clavinet synthé",         { { "engine", 0 }, { "wave1", 4 }, { "wave2", 4 }, { "detune", 3 }, { "cutoff", 1800 }, { "reso", 0.45f }, { "fenv", 3 }, { "fd", 0.25f }, { "fs", 0 }, { "a", 0.001f }, { "d", 0.6f }, { "s", 0 }, { "r", 0.1f } } },
            { "Guitares", "Guitare folk",             { { "engine", 4 }, { "body", 1 }, { "pick", 0.55f }, { "bright", 0.65f }, { "pickpos", 0.18f }, { "r", 0.35f }, { "width", 0.6f } } },
            { "Guitares", "Guitare folk 12 cordes",   { { "engine", 4 }, { "body", 1 }, { "double", 1 }, { "pick", 0.6f }, { "bright", 0.75f }, { "ring", 1.1f }, { "r", 0.4f }, { "width", 0.8f } } },
            { "Guitares", "Guitare classique (nylon)", { { "engine", 4 }, { "body", 2 }, { "pick", 0.3f }, { "bright", 0.45f }, { "pickpos", 0.22f }, { "r", 0.4f }, { "width", 0.5f } } },
            { "Guitares", "Guitare électrique claire", { { "engine", 4 }, { "body", 0 }, { "pick", 0.5f }, { "bright", 0.7f }, { "pickpos", 0.14f }, { "ring", 1.1f }, { "r", 0.25f }, { "width", 0.5f } } },
            { "Guitares", "Guitare électrique saturée", { { "engine", 4 }, { "body", 0 }, { "pick", 0.7f }, { "bright", 0.6f }, { "drive", 0.75f }, { "ring", 1.6f }, { "r", 0.2f }, { "width", 0.4f } } },
            { "Guitares", "Guitare 7 cordes (métal)",  { { "engine", 4 }, { "body", 0 }, { "pick", 0.8f }, { "bright", 0.55f }, { "drive", 0.85f }, { "ring", 1.4f }, { "mute", 0.15f }, { "r", 0.15f }, { "width", 0.3f } } },
            { "Guitares", "Guitare 8 cordes (djent)",  { { "engine", 4 }, { "body", 0 }, { "pick", 0.85f }, { "bright", 0.5f }, { "drive", 0.8f }, { "ring", 1.2f }, { "mute", 0.35f }, { "r", 0.1f }, { "width", 0.3f } } },
            { "Guitares", "Guitare étouffée",          { { "engine", 4 }, { "body", 0 }, { "pick", 0.7f }, { "bright", 0.55f }, { "drive", 0.6f }, { "mute", 0.7f }, { "r", 0.08f }, { "width", 0.3f } } },
            { "Basses", "Basse ronde",               { { "engine", 0 }, { "wave1", 1 }, { "wave2", 0 }, { "mix2", 0.5f }, { "sub", 0.6f }, { "cutoff", 900 }, { "reso", 0.1f }, { "fenv", 1 }, { "keytrack", 0.3f }, { "a", 0.003f }, { "d", 0.5f }, { "s", 0.7f }, { "r", 0.12f }, { "width", 0 } } },
            { "Basses", "Basse synthé",              { { "engine", 0 }, { "wave1", 2 }, { "wave2", 3 }, { "semi2", -12 }, { "mix2", 0.4f }, { "cutoff", 500 }, { "reso", 0.5f }, { "fenv", 3 }, { "fd", 0.25f }, { "fs", 0.1f }, { "a", 0.002f }, { "r", 0.1f }, { "width", 0 } } },
            { "Basses", "Basse FM",                  { { "engine", 1 }, { "ratio", 1 }, { "index", 3 }, { "idecay", 0.4f }, { "feedback", 0.3f }, { "bright", 0 }, { "a", 0.002f }, { "d", 0.8f }, { "s", 0.6f }, { "r", 0.1f }, { "width", 0 } } },
            { "Basses", "Sub 808",                   { { "engine", 0 }, { "wave1", 0 }, { "mix2", 0 }, { "cutoff", 2000 }, { "fenv", 0 }, { "a", 0.002f }, { "d", 2.5f }, { "s", 0 }, { "r", 0.3f }, { "drive", 0.3f }, { "width", 0 } } },
            { "Basses", "Basse acide",               { { "engine", 0 }, { "wave1", 2 }, { "mix2", 0 }, { "cutoff", 350 }, { "reso", 0.8f }, { "fenv", 4 }, { "fd", 0.2f }, { "fs", 0 }, { "drive", 0.4f }, { "a", 0.001f }, { "r", 0.08f }, { "width", 0 } } },
            { "Basses", "Basse électrique (doigts)", { { "engine", 4 }, { "body", 3 }, { "pick", 0.25f }, { "bright", 0.35f }, { "pickpos", 0.25f }, { "r", 0.15f }, { "width", 0 } } },
            { "Basses", "Basse au médiator",         { { "engine", 4 }, { "body", 3 }, { "pick", 0.75f }, { "bright", 0.6f }, { "pickpos", 0.12f }, { "r", 0.12f }, { "width", 0 } } },
            { "Basses", "Contrebasse pizzicato",     { { "engine", 4 }, { "body", 2 }, { "pick", 0.15f }, { "bright", 0.25f }, { "pickpos", 0.35f }, { "ring", 0.8f }, { "r", 0.25f }, { "width", 0 } } },
            { "Nappes", "Nappe chaude",              { { "engine", 0 }, { "wave1", 2 }, { "wave2", 2 }, { "detune", 12 }, { "unison", 5 }, { "spread", 18 }, { "width", 0.9f }, { "cutoff", 1800 }, { "reso", 0.15f }, { "fenv", 1 }, { "fa", 0.8f }, { "fd", 1.5f }, { "fs", 0.6f }, { "a", 0.8f }, { "d", 1 }, { "s", 0.9f }, { "r", 1.5f }, { "vibrato", 0.1f } } },
            { "Nappes", "Nappe aérienne",            { { "engine", 0 }, { "wave1", 3 }, { "wave2", 2 }, { "semi2", 12 }, { "unison", 7 }, { "spread", 25 }, { "width", 1 }, { "cutoff", 3500 }, { "fenv", -1 }, { "fa", 2 }, { "fs", 0.5f }, { "a", 1.5f }, { "s", 0.9f }, { "r", 2.5f }, { "noise", 0.05f } } },
            { "Nappes", "Nappe sombre",              { { "engine", 0 }, { "wave1", 2 }, { "wave2", 2 }, { "detune", 9 }, { "unison", 5 }, { "spread", 15 }, { "width", 0.8f }, { "cutoff", 700 }, { "reso", 0.3f }, { "fenv", 0.5f }, { "a", 1.2f }, { "s", 0.9f }, { "r", 2 } } },
            { "Cordes et cuivres", "Cordes synthé",  { { "engine", 0 }, { "wave1", 2 }, { "wave2", 2 }, { "detune", 8 }, { "unison", 4 }, { "spread", 14 }, { "width", 0.8f }, { "cutoff", 3000 }, { "fenv", 0.5f }, { "a", 0.35f }, { "d", 0.5f }, { "s", 0.85f }, { "r", 0.6f }, { "vibrato", 0.2f } } },
            { "Cordes et cuivres", "Ensemble de cordes", { { "engine", 0 }, { "wave1", 2 }, { "wave2", 2 }, { "detune", 10 }, { "unison", 7 }, { "spread", 20 }, { "width", 1 }, { "cutoff", 2200 }, { "fenv", 0.3f }, { "a", 0.5f }, { "s", 0.9f }, { "r", 0.9f }, { "vibrato", 0.15f } } },
            { "Cordes et cuivres", "Cuivres synthé", { { "engine", 0 }, { "wave1", 2 }, { "wave2", 2 }, { "detune", 6 }, { "unison", 2 }, { "spread", 8 }, { "cutoff", 900 }, { "reso", 0.2f }, { "fenv", 2.5f }, { "fa", 0.06f }, { "fd", 0.5f }, { "fs", 0.5f }, { "a", 0.03f }, { "d", 0.3f }, { "s", 0.8f }, { "r", 0.2f } } },
            { "Cordes pincées", "Harpe",              { { "engine", 4 }, { "body", 4 }, { "pick", 0.25f }, { "bright", 0.55f }, { "pickpos", 0.3f }, { "ring", 1.3f }, { "r", 1.5f }, { "width", 0.9f } } },
            { "Cordes pincées", "Ukulélé",            { { "engine", 4 }, { "body", 5 }, { "pick", 0.45f }, { "bright", 0.6f }, { "pickpos", 0.2f }, { "r", 0.3f }, { "width", 0.5f } } },
            { "Cordes pincées", "Pizzicato de cordes", { { "engine", 4 }, { "body", 4 }, { "pick", 0.3f }, { "bright", 0.4f }, { "pickpos", 0.3f }, { "ring", 0.5f }, { "r", 0.2f }, { "width", 0.8f } } },
            { "Leads", "Lead carré",                 { { "engine", 0 }, { "wave1", 3 }, { "wave2", 3 }, { "detune", 5 }, { "cutoff", 3500 }, { "reso", 0.25f }, { "fenv", 1.5f }, { "a", 0.003f }, { "r", 0.15f }, { "vibrato", 0.3f }, { "width", 0.3f } } },
            { "Leads", "Lead scie",                  { { "engine", 0 }, { "wave1", 2 }, { "wave2", 2 }, { "detune", 10 }, { "unison", 3 }, { "spread", 10 }, { "cutoff", 5000 }, { "drive", 0.3f }, { "vibrato", 0.3f }, { "a", 0.003f }, { "r", 0.15f } } },
            { "Leads", "Lead doux",                  { { "engine", 0 }, { "wave1", 1 }, { "wave2", 0 }, { "cutoff", 2500 }, { "fenv", 0.5f }, { "a", 0.02f }, { "r", 0.25f }, { "vibrato", 0.35f } } },
            { "Cloches et percussions", "Cloches FM", { { "engine", 1 }, { "ratio", 3.5f }, { "index", 3 }, { "idecay", 1.5f }, { "bright", 0 }, { "a", 0.001f }, { "d", 4 }, { "s", 0 }, { "r", 2 }, { "width", 0.7f } } },
            { "Cloches et percussions", "Marimba",   { { "engine", 1 }, { "ratio", 4 }, { "index", 1.2f }, { "idecay", 0.08f }, { "bright", 0 }, { "a", 0.001f }, { "d", 0.6f }, { "s", 0 }, { "r", 0.3f } } },
            { "Cloches et percussions", "Boîte à musique", { { "engine", 1 }, { "ratio", 7 }, { "index", 1.5f }, { "idecay", 0.3f }, { "bright", 0 }, { "a", 0.001f }, { "d", 2 }, { "s", 0 }, { "r", 1 } } },
            { "Cloches et percussions", "Pluck",     { { "engine", 0 }, { "wave1", 2 }, { "wave2", 3 }, { "mix2", 0.3f }, { "cutoff", 600 }, { "reso", 0.3f }, { "fenv", 5 }, { "fd", 0.15f }, { "fs", 0 }, { "a", 0.001f }, { "d", 0.5f }, { "s", 0 }, { "r", 0.3f } } },
            { "Batteries", "Batterie acoustique",    { { "engine", 3 }, { "kit", 0 }, { "velsens", 0.8f } } },
            { "Batteries", "Batterie 808",           { { "engine", 3 }, { "kit", 1 }, { "velsens", 0.6f } } },
            { "Batteries", "Batterie électro",       { { "engine", 3 }, { "kit", 2 }, { "velsens", 0.6f } } },
        };
        return list;
    }

    juce::StringArray SynthPlugin::categories()
    {
        juce::StringArray c;
        for (auto& p : presets()) c.addIfNotAlreadyThere (juce::String::fromUTF8 (p.category));
        return c;
    }

    const SynthPreset* SynthPlugin::findPreset (const juce::String& name)
    {
        for (auto& p : presets()) if (juce::String::fromUTF8 (p.name) == name) return &p;
        return nullptr;
    }

    void SynthPlugin::applyPreset (const SynthPreset& p)
    {
        // Toutes les valeurs reviennent au défaut, puis celles du préréglage.
        for (auto* prm : getAutomatableParameters())
            if (auto d = prm->getDefaultValue()) setStored (prm->paramID, *d);
        for (auto& [id, v] : p.values)
            setStored (id, v);
        presetName = juce::String::fromUTF8 (p.name);
    }

}
