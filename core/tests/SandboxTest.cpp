// Test de robustesse : 3 pistes, dont une qui plante et une qui se bloque,
// simulées en temps réel (48 kHz, blocs de 256 échantillons).
#include "onde/PluginSandbox.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <sched.h>
 #include <sys/resource.h>
#endif

using namespace onde;
using clk = std::chrono::steady_clock;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("  ÉCHEC : %s\n", msg); ++failures; } else std::printf ("  ok     : %s\n", msg); } while (0)

int main (int argc, char** argv)
{
    const std::string worker = argc > 1 ? argv[1] : "./onde-plugin-worker";
    const double sr = 48000; const uint32_t n = 256; const int blocks = 1200;  // ~6,4 s
    const double blockSec = n / sr;

    {   // Thread « audio » du test en haute priorité, avec le même garde-fou que les workers.
       #ifdef _WIN32
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
       #else
        rlimit rt { 200000, 200000 };
        if (setrlimit (RLIMIT_RTTIME, &rt) == 0) { sched_param sp {}; sp.sched_priority = 80; sched_setscheduler (0, SCHED_FIFO, &sp); }
       #endif
    }

    PluginSandbox gain  ({ worker, "gain:0.5" });
    PluginSandbox crash ({ worker, "synth-pad,crash-after:300" });
    PluginSandbox hang  ({ worker, "gain:1,hang-after:500" });

    for (auto* s : { &gain, &crash, &hang })
    {
        if (! s->start (sr, 2) || ! s->waitUntilReady (3000)) { std::printf ("démarrage impossible\n"); return 1; }
    }

    std::vector<float> L (n), R (n);
    float* io[] = { L.data(), R.data() };
    double worstGainMs = 0; int gainWrong = 0;
    std::string crashReason;
    int crashDetectedAt = -1, hangDetectedAt = -1, restartedAt = -1, crashAudioAfterRestart = 0;

    auto next = clk::now();
    for (int b = 0; b < blocks; ++b)
    {
        // Piste 1 : gain 0,5 sur un signal constant de 1,0 → doit rendre 0,5.
        std::fill (L.begin(), L.end(), 1.0f); std::fill (R.begin(), R.end(), 1.0f);
        const auto t0 = clk::now();
        const bool okGain = gain.process (io, 2, n, (uint64_t) b * n);
        worstGainMs = std::max (worstGainMs, std::chrono::duration<double, std::milli> (clk::now() - t0).count());
        if (! okGain || std::fabs (L[0] - 0.5f) > 1e-6f || std::fabs (R[n - 1] - 0.5f) > 1e-6f) ++gainWrong;

        // Piste 2 : synthé qui plante au bloc 300.
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        const bool okCrash = crash.process (io, 2, n, (uint64_t) b * n);
        if (crashDetectedAt < 0 && crash.getState() == PluginSandbox::State::crashed) { crashDetectedAt = b; crashReason = crash.describeFailure(); }
        if (restartedAt > 0 && okCrash) { float e = 0; for (float v : L) e += std::fabs (v); if (e > 0.01f) ++crashAudioAfterRestart; }

        // Piste 3 : effet qui se bloque au bloc 500.
        std::fill (L.begin(), L.end(), 1.0f); std::fill (R.begin(), R.end(), 1.0f);
        hang.process (io, 2, n, (uint64_t) b * n);
        if (hangDetectedAt < 0 && hang.getState() == PluginSandbox::State::hung) hangDetectedAt = b;

        // L'utilisateur clique (le même plugin replantera 300 blocs plus tard : c'est voulu) « Relancer le plugin » 100 blocs après le plantage (thread UI).
        if (crashDetectedAt > 0 && restartedAt < 0 && b == crashDetectedAt + 100)
        {
            std::thread ([&] { crash.restart(); }).detach();
            restartedAt = b;
        }

        next += std::chrono::nanoseconds ((long) (blockSec * 1e9));
        std::this_thread::sleep_until (next);
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    const auto gs = gain.getStats(), cs = crash.getStats(), hs = hang.getStats();
    std::printf ("\nRésultats (%d blocs, %.1f ms par bloc)\n", blocks, blockSec * 1000);
    std::printf ("  Piste gain   : %llu blocs ok, %llu manqués, pire aller-retour %.3f ms\n",
                 (unsigned long long) gs.blocksOk, (unsigned long long) gs.blocksMissed, worstGainMs);
    std::printf ("  Piste synthé : planté au bloc %d (%s), relancé au bloc %d, %d blocs audibles ensuite, %d redémarrage(s)\n",
                 crashDetectedAt, crashReason.c_str(),
                 restartedAt, crashAudioAfterRestart, cs.restarts);
    std::printf ("  Piste bloquée: détectée au bloc %d (%s)\n\n", hangDetectedAt, hang.describeFailure().c_str());

    CHECK (gainWrong <= blocks / 200, "la piste saine rend le bon signal sur au moins 99,5 % des blocs, malgré les deux pannes");
    CHECK (crashDetectedAt >= 300 && crashDetectedAt <= 303, "le plantage est détecté en moins de 3 blocs (< 16 ms)");
    CHECK (cs.restarts == 1 && crashAudioAfterRestart >= 250, "après « Relancer », le synthé produit à nouveau du son");
    CHECK (hangDetectedAt >= 500 && hangDetectedAt <= 510, "un plugin bloqué est arrêté en moins de 10 blocs");
    CHECK (hang.getState() == PluginSandbox::State::hung, "la piste bloquée reste en sourdine");
    std::printf ("\n%s\n", failures == 0 ? "TOUS LES TESTS PASSENT — l'hôte a survécu." : "DES TESTS ÉCHOUENT");
    return failures == 0 ? 0 : 1;
}
