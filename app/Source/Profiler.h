// Profileur par échantillonnage (diagnostic, Windows) : 200 relevés/s de la pile d'un fil, agrégés par
// fonction. La pile est copiée pendant que le fil est suspendu (aucune allocation à ce moment-là, le fil
// pouvant tenir le verrou du tas), puis parcourue une fois le fil relancé. Symboles : Onde.pdb.
#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <dbghelp.h>
 #pragma comment (lib, "dbghelp.lib")
 #include <atomic>
 #include <map>
 #include <set>
 #include <thread>

class SamplingProfiler
{
public:
    // À appeler depuis le fil à mesurer.
    void start()
    {
        DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS);
        stopFlag = false;
        self.clear(); inclusive.clear(); samples = 0;
        worker = std::thread ([this] { run(); });
    }

    // Arrête et renvoie les fonctions les plus coûteuses (pourcentages des relevés).
    juce::String stop (int top = 18)
    {
        stopFlag = true;
        if (worker.joinable()) worker.join();
        if (target) { CloseHandle (target); target = nullptr; }
        juce::String r;
        auto list = [&] (const std::map<std::string, int>& m, const char* title)
        {
            std::vector<std::pair<int, std::string>> v;
            for (auto& [k, c] : m) v.push_back ({ c, k });
            std::sort (v.rbegin(), v.rend());
            r << "    " << title << " (" << samples << " relevés)\n";
            for (size_t i = 0; i < v.size() && (int) i < top; ++i)
                r << juce::String::formatted ("    %5.1f%%  ", 100.0 * v[i].first / std::max (1, samples)) << juce::String (v[i].second) << "\n";
        };
        list (self, "temps propre");
        list (inclusive, "temps inclusif");
        return r;
    }

private:
    void run()
    {
        HANDLE proc = GetCurrentProcess();
        static bool symInit = false;
        if (! symInit) { SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS); SymInitialize (proc, nullptr, TRUE); symInit = true; }
        std::vector<unsigned char> copy (256 * 1024);
        static thread_local DWORD64 base = 0; static thread_local size_t size = 0; static thread_local unsigned char* data = nullptr;
        data = copy.data();
        std::map<DWORD64, std::string> names;
        std::vector<DWORD64> pcs; pcs.reserve (64);
        while (! stopFlag)
        {
            Sleep (5);
            if (SuspendThread (target) == (DWORD) -1) continue;
            CONTEXT ctx {}; ctx.ContextFlags = CONTEXT_FULL;
            const bool got = GetThreadContext (target, &ctx) != 0;
            if (got)
            {
                SIZE_T read = 0;
                base = ctx.Rsp;
                ReadProcessMemory (proc, (LPCVOID) base, copy.data(), copy.size(), &read);
                size = read;
            }
            ResumeThread (target);
            if (! got) continue;
            pcs.clear();
            STACKFRAME64 f {};
            f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
            f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
            f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
            auto readMem = [] (HANDLE p, DWORD64 addr, PVOID buf, DWORD n, LPDWORD done) -> BOOL
            {
                if (addr >= base && addr + n <= base + size) { std::memcpy (buf, data + (addr - base), n); if (done) *done = n; return TRUE; }
                SIZE_T r = 0; const BOOL ok = ReadProcessMemory (p, (LPCVOID) addr, buf, n, &r); if (done) *done = (DWORD) r; return ok;
            };
            for (int d = 0; d < 48 && StackWalk64 (IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &f, &ctx, readMem,
                                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++d)
                pcs.push_back (f.AddrPC.Offset);
            if (pcs.empty()) continue;
            ++samples;
            std::set<std::string> seen;
            for (size_t i = 0; i < pcs.size(); ++i)
            {
                auto it = names.find (pcs[i]);
                if (it == names.end())
                {
                    char buf[sizeof (SYMBOL_INFO) + 512] = {};
                    auto* s = (SYMBOL_INFO*) buf; s->SizeOfStruct = sizeof (SYMBOL_INFO); s->MaxNameLen = 511;
                    DWORD64 disp = 0;
                    std::string n = SymFromAddr (proc, pcs[i], &disp, s) ? std::string (s->Name) : "?";
                    if (n.size() > 100) n = n.substr (0, 100);
                    it = names.emplace (pcs[i], n).first;
                }
                if (i == 0) ++self[it->second];
                if (seen.insert (it->second).second) ++inclusive[it->second];
            }
        }
    }

    HANDLE target = nullptr;
    std::thread worker;
    std::atomic<bool> stopFlag { false };
    std::map<std::string, int> self, inclusive;
    int samples = 0;
};

// Temps CPU consommé par le fil appelant (secondes).
inline double threadCpuSeconds()
{
    FILETIME c, e, k, u;
    GetThreadTimes (GetCurrentThread(), &c, &e, &k, &u);
    auto v = [] (FILETIME f) { return (double) (((ULONGLONG) f.dwHighDateTime << 32) | f.dwLowDateTime) / 1.0e7; };
    return v (k) + v (u);
}
inline double processCpuSeconds()
{
    FILETIME c, e, k, u;
    GetProcessTimes (GetCurrentProcess(), &c, &e, &k, &u);
    auto v = [] (FILETIME f) { return (double) (((ULONGLONG) f.dwHighDateTime << 32) | f.dwLowDateTime) / 1.0e7; };
    return v (k) + v (u);
}
#else
class SamplingProfiler { public: void start() {} juce::String stop (int = 0) { return {}; } };
inline double threadCpuSeconds()  { return 0.0; }
inline double processCpuSeconds() { return 0.0; }
#endif
