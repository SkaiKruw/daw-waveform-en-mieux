// Traces de diagnostic, actives seulement si la variable d'environnement ONDE_TRACE existe.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace onde
{
    inline bool traceEnabled()
    {
        static const bool on = std::getenv ("ONDE_TRACE") != nullptr;
        return on;
    }

    inline void trace (const char* what, const char* detail = "")
    {
        if (! traceEnabled()) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
        std::fprintf (stderr, "[trace %lld] %s %s\n", (long long) ms, what, detail);
        std::fflush (stderr);
    }
}
