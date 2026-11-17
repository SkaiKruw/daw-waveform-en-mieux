#include "onde/WorkerPlugins.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace onde
{
    namespace
    {
        struct Gain : WorkerPlugin
        {
            float gain;
            explicit Gain (float g) : gain (g) {}
            void prepare (double, uint32_t) override {}
            void process (SharedBlock& b, Slot& s) override
            {
                for (uint32_t c = 0; c < b.numChannels; ++c)
                    for (uint32_t i = 0; i < s.numFrames; ++i)
                        s.channel (c)[i] *= gain;
            }
        };

        // Petit synthé de nappe : accord Ré mineur, enveloppe lente, ajouté au signal entrant.
        struct SynthPad : WorkerPlugin
        {
            double sr = 48000;
            void prepare (double s, uint32_t) override { sr = s; }
            void process (SharedBlock& b, Slot& s) override
            {
                static const double freqs[] = { 146.83, 174.61, 220.00, 293.66 }; // Ré, Fa, La, Ré
                const float level = b.params[0] > 0 ? b.params[0] : 0.12f;
                for (uint32_t i = 0; i < s.numFrames; ++i)
                {
                    const double t = (double) (s.samplePosition + i) / sr;
                    const double lfo = 0.6 + 0.4 * std::sin (2 * 3.14159265358979323846 * 0.25 * t);
                    double acc = 0;
                    for (double f : freqs)
                        acc += std::sin (2 * 3.14159265358979323846 * f * t) + 0.3 * std::sin (2 * 3.14159265358979323846 * f * 2.003 * t);
                    const float v = (float) (acc / 5.2 * lfo) * level;
                    for (uint32_t c = 0; c < b.numChannels; ++c)
                        s.channel (c)[i] += v;
                }
            }
        };

        // Enveloppe qui déclenche une panne après N blocs — pour la démo et les tests.
        struct Faulty : WorkerPlugin
        {
            std::unique_ptr<WorkerPlugin> inner;
            long after; bool hang; double atSeconds; long count = 0;
            Faulty (std::unique_ptr<WorkerPlugin> i, long a, bool h, double t = -1)
                : inner (std::move (i)), after (a), hang (h), atSeconds (t) {}
            void prepare (double s, uint32_t c) override { if (inner) inner->prepare (s, c); }
            void process (SharedBlock& b, Slot& s) override
            {
                const bool timeReached = atSeconds >= 0 && (double) (s.samplePosition + s.numFrames) / b.sampleRate > atSeconds;
                if ((after >= 0 && ++count > after) || timeReached)
                {
                    if (hang) { volatile bool forever = true; while (forever) {} }
                    crashForTesting();      // simule un accès mémoire invalide dans le plugin
                }
                if (inner) inner->process (b, s);
            }
        };
    }

    void crashForTesting()
    {
        // Adresse opaque pour le compilateur : il ne peut pas remplacer l'accès par une autre instruction.
        static volatile std::uintptr_t address = 0;
        *reinterpret_cast<volatile int*> (address) = 42;
        std::abort();
    }

    std::unique_ptr<WorkerPlugin> createWorkerPlugin (const std::string& spec)
    {
        std::unique_ptr<WorkerPlugin> base;
        long faultAfter = -1; bool hang = false; double faultAt = -1;

        std::stringstream ss (spec);
        std::string part;
        while (std::getline (ss, part, ','))
        {
            const auto colon = part.find (':');
            const std::string key = part.substr (0, colon);
            const std::string val = colon == std::string::npos ? "" : part.substr (colon + 1);

            if (key == "gain")             base = std::make_unique<Gain> (std::stof (val));
            else if (key == "synth-pad")   base = std::make_unique<SynthPad>();
            else if (key == "crash-after") { faultAfter = std::stol (val); hang = false; }
            else if (key == "hang-after")  { faultAfter = std::stol (val); hang = true; }
            else if (key == "crash-at")    { faultAt = std::stod (val); hang = false; }   // en secondes de lecture
        }

        if (! base)
            base = std::make_unique<Gain> (1.0f);
        if (faultAfter >= 0 || faultAt >= 0)
            return std::make_unique<Faulty> (std::move (base), faultAfter, hang, faultAt);
        return base;
    }
}
