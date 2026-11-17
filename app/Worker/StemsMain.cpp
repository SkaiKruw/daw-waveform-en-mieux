// onde-stems : séparation de sources (voix, batterie, basse, autres) avec Demucs v4 « fine-tuned »
// (htdemucs_ft, 4 modèles spécialisés) via demucs.cpp (MIT). Processus isolé, lancé par Onde.
//
//   onde-stems <dossier des modèles> <entrée.wav 44,1 kHz> <dossier de sortie> [threads]
//
// Le morceau est découpé en tronçons de 30 s avec 4 s de recouvrement de chaque côté ; chaque
// couple (modèle, tronçon) est une tâche indépendante, réparties sur tous les cœurs. Demucs applique
// en plus sa propre segmentation interne (7,8 s, recouvrement 25 %), comme l'implémentation de référence.
// Les tronçons sont recousus par fondu linéaire sur le recouvrement. Sortie : drums.wav, bass.wav,
// other.wav, vocals.wav (float 32 bits, 44,1 kHz, stéréo). La progression s'écrit sur stdout
// (« PROGRESS 0.42 »), la dernière ligne est « DONE » ou « ERROR <message> ».
#include "model.hpp"
#include "dsp.hpp"
#include <Eigen/Dense>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <chrono>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 #include <immintrin.h>
 #define ONDE_FTZ() _mm_setcsr (_mm_getcsr() | 0x8040)   // flush-to-zero + denormals-are-zero (comme -Ofast)
#else
 #define ONDE_FTZ()
#endif

namespace fs = std::filesystem;

namespace
{
    std::mutex outMutex;
    void say (const std::string& s) { std::lock_guard<std::mutex> l (outMutex); std::cout << s << std::endl; }
    [[noreturn]] void fail (const std::string& s) { say ("ERROR " + s); std::exit (1); }

    // --- WAV minimal : PCM 16/24/32 bits ou float 32 bits, mono ou stéréo -------------------------
    Eigen::MatrixXf readWav (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        if (! in) fail ("entrée introuvable");
        std::vector<char> d ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) || std::memcmp (d.data() + 8, "WAVE", 4)) fail ("WAV invalide");
        auto u16 = [&] (size_t o) { return (uint32_t) (uint8_t) d[o] | ((uint32_t) (uint8_t) d[o + 1] << 8); };
        auto u32 = [&] (size_t o) { return u16 (o) | (u16 (o + 2) << 16); };
        int fmt = 0, ch = 0, rate = 0, bits = 0;
        size_t dataOff = 0, dataLen = 0;
        for (size_t o = 12; o + 8 <= d.size();)
        {
            const uint32_t len = u32 (o + 4);
            if (! std::memcmp (d.data() + o, "fmt ", 4))
            {
                fmt = (int) u16 (o + 8); ch = (int) u16 (o + 10); rate = (int) u32 (o + 12); bits = (int) u16 (o + 22);
                if (fmt == 0xFFFE && len >= 26) fmt = (int) u16 (o + 32);          // WAVE_FORMAT_EXTENSIBLE
            }
            else if (! std::memcmp (d.data() + o, "data", 4))
            {
                dataOff = o + 8; dataLen = std::min<size_t> (len, d.size() - dataOff); break;
            }
            o += 8 + len + (len & 1);
        }
        if (dataOff == 0 || ch < 1 || ch > 2) fail ("WAV : mono ou stéréo attendu");
        if (rate != demucscpp::SUPPORTED_SAMPLE_RATE) fail ("fréquence 44100 Hz attendue");
        const int bps = bits / 8;
        const size_t n = dataLen / (size_t) (bps * ch);
        Eigen::MatrixXf out (2, (Eigen::Index) n);
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const char* s = d.data() + dataOff + (i * ch + std::min (c, ch - 1)) * bps;
                float v = 0.0f;
                if (fmt == 3 && bits == 32) std::memcpy (&v, s, 4);
                else if (bits == 16) { int16_t x; std::memcpy (&x, s, 2); v = x / 32768.0f; }
                else if (bits == 24) { int32_t x = ((uint8_t) s[0] << 8) | ((uint8_t) s[1] << 16) | ((uint8_t) s[2] << 24); v = (x >> 8) / 8388608.0f; }
                else if (bits == 32) { int32_t x; std::memcpy (&x, s, 4); v = (float) (x / 2147483648.0); }
                else fail ("résolution non gérée");
                out (c, (Eigen::Index) i) = v;
            }
        return out;
    }

    void writeWav (const fs::path& p, const Eigen::MatrixXf& a)
    {
        std::ofstream o (p, std::ios::binary);
        if (! o) fail ("écriture impossible : " + p.string());
        const uint32_t n = (uint32_t) a.cols(), dataLen = n * 2 * 4;
        auto w32 = [&] (uint32_t v) { o.write ((const char*) &v, 4); };
        auto w16 = [&] (uint16_t v) { o.write ((const char*) &v, 2); };
        o.write ("RIFF", 4); w32 (36 + dataLen); o.write ("WAVEfmt ", 8);
        w32 (16); w16 (3); w16 (2); w32 (44100); w32 (44100 * 8); w16 (8); w16 (32);
        o.write ("data", 4); w32 (dataLen);
        std::vector<float> buf (n * 2);
        for (uint32_t i = 0; i < n; ++i) { buf[2 * i] = a (0, i); buf[2 * i + 1] = a (1, i); }
        o.write ((const char*) buf.data(), (std::streamsize) buf.size() * 4);
    }
}

#ifdef _WIN32
// --- Profileur par échantillonnage (ONDE_STEMS_PROFILE=1) : 200 échantillons/s de la pile du thread de calcul,
// agrégés par fonction (inclusif, sans doublon par pile). Sert à mettre au point les performances.
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <map>
#pragma comment (lib, "dbghelp.lib")
namespace prof
{
    std::atomic<bool> stop { false };
    HANDLE target = nullptr;
    std::map<std::string, int> inclusive, self;
    int samples = 0;

    void report();
    void sampler()
    {
        HANDLE proc = GetCurrentProcess();
        SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize (proc, nullptr, TRUE);
        std::map<DWORD64, std::string> cache;
        auto name = [&] (DWORD64 a) -> std::string
        {
            auto it = cache.find (a);
            if (it != cache.end()) return it->second;
            char buf[sizeof (SYMBOL_INFO) + 512] = {};
            auto* s = (SYMBOL_INFO*) buf; s->SizeOfStruct = sizeof (SYMBOL_INFO); s->MaxNameLen = 511;
            DWORD64 disp = 0;
            std::string n = SymFromAddr (proc, a, &disp, s) ? std::string (s->Name) : "?";
            if (n.size() > 110) n = n.substr (0, 110);
            return cache[a] = n;
        };
        const auto endAt = std::chrono::steady_clock::now() + std::chrono::seconds (std::max (5, std::atoi (std::getenv ("ONDE_STEMS_PROFILE"))));
        // Pendant la suspension : aucune allocation (le thread peut tenir le verrou du tas). On copie la pile
        // dans un tampon préalloué, on relance le thread, puis on la parcourt depuis la copie.
        static std::vector<unsigned char> stackCopy (512 * 1024);
        static DWORD64 copyBase = 0;
        static size_t copySize = 0;
        ULONG_PTR lowLimit = 0, highLimit = 0;
        std::vector<DWORD64> pcs; pcs.reserve (64);
        while (! stop && std::chrono::steady_clock::now() < endAt)
        {
            Sleep (5);
            if (SuspendThread (target) == (DWORD) -1) continue;
            CONTEXT ctx {}; ctx.ContextFlags = CONTEXT_FULL;
            const bool got = GetThreadContext (target, &ctx) != 0;
            if (got)
            {
                copyBase = ctx.Rsp;
                copySize = std::min<size_t> (stackCopy.size(), 256 * 1024);
                SIZE_T read = 0;
                ReadProcessMemory (proc, (LPCVOID) copyBase, stackCopy.data(), copySize, &read);
                copySize = read;
            }
            ResumeThread (target);
            (void) lowLimit; (void) highLimit;
            if (! got) continue;
            pcs.clear();
            STACKFRAME64 f {};
            f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
            f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
            f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
            auto readMem = [] (HANDLE p, DWORD64 addr, PVOID buf, DWORD n, LPDWORD done) -> BOOL
            {
                if (addr >= copyBase && addr + n <= copyBase + copySize)
                {
                    std::memcpy (buf, stackCopy.data() + (addr - copyBase), n);
                    if (done) *done = n;
                    return TRUE;
                }
                SIZE_T r = 0;
                const BOOL ok = ReadProcessMemory (p, (LPCVOID) addr, buf, n, &r);
                if (done) *done = (DWORD) r;
                return ok;
            };
            for (int d = 0; d < 48 && StackWalk64 (IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &f, &ctx, readMem,
                                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++d)
                pcs.push_back (f.AddrPC.Offset);
            if (pcs.empty()) continue;
            ++samples;
            std::set<std::string> seen;
            for (size_t i = 0; i < pcs.size(); ++i)
            {
                const auto n = name (pcs[i]);
                if (i == 0) ++self[n];
                if (seen.insert (n).second) ++inclusive[n];
            }
        }
        report(); std::fflush (stdout);
    }

    void report()
    {
        auto top = [] (const std::map<std::string, int>& m, const char* title)
        {
            std::vector<std::pair<int, std::string>> v;
            for (auto& [k, c] : m) v.push_back ({ c, k });
            std::sort (v.rbegin(), v.rend());
            std::printf ("PROFILE %s (%d échantillons)\n", title, samples);
            for (size_t i = 0; i < v.size() && i < 30; ++i) std::printf ("PROFILE %5.1f%%  %s\n", 100.0 * v[i].first / std::max (1, samples), v[i].second.c_str());
        };
        top (self, "propre"); top (inclusive, "inclusif");
    }
}

#endif

static int run (int argc, const fs::path* argv)
{
    if (std::getenv ("ONDE_STEMS_GEMMBENCH"))
    {
        Eigen::MatrixXf a = Eigen::MatrixXf::Random (1024, 1024), b = Eigen::MatrixXf::Random (1024, 1024), c;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) c.noalias() = a * b;
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("GEMM %.1f GFLOP/s (SIMD %s) %f\n", 10 * 2.0 * 1024 * 1024 * 1024 / s / 1e9, Eigen::SimdInstructionSetsInUse(), c (0, 0));
        return 0;
    }
    if (argc < 4) { std::cerr << "usage : onde-stems <modèles> <entrée.wav> <sortie> [threads] [precis|rapide[:modèle]]\n"; return 2; }
    const fs::path modelDir = argv[1], input = argv[2], outDir = argv[3];
    const int threads = std::max (1, argc > 4 ? std::atoi (argv[4].string().c_str()) : (int) std::thread::hardware_concurrency());
    // Mode « rapide » : un seul des 4 modèles fournit les 4 sources (4 fois moins de calcul).
    const std::string mode = argc > 5 ? argv[5].string() : std::string ("precis");
    const bool fast = mode.rfind ("rapide", 0) == 0;
    const std::string fastName = fast && mode.size() > 7 ? mode.substr (7) : std::string ("other");

    const char* names[4] = { "drums", "bass", "other", "vocals" };
    say ("STAGE chargement");
    Eigen::MatrixXf audio = readWav (input);
    if (const char* maxSec = std::getenv ("ONDE_STEMS_MAXSEC"))                // banc d'essai : entrée tronquée
        if (std::atof (maxSec) > 0 && audio.cols() > (Eigen::Index) (std::atof (maxSec) * 44100))
            audio = Eigen::MatrixXf (audio.leftCols ((Eigen::Index) (std::atof (maxSec) * 44100)));
    const Eigen::Index total = audio.cols();
    if (total < 4410) fail ("audio trop court");

    int fastIdx = 2;
    for (int m = 0; m < 4; ++m) if (fastName == names[m]) fastIdx = m;
    std::vector<std::unique_ptr<demucscpp::demucs_model>> models (4);
    for (int m = 0; m < 4; ++m)
    {
        if (fast && m != fastIdx) continue;
        const auto file = modelDir / ("ggml-model-htdemucs_ft_" + std::string (names[m]) + "-4s-f16.bin");
        if (! fs::exists (file)) fail ("modèle manquant : " + file.filename().string());
        models[(size_t) m] = std::make_unique<demucscpp::demucs_model>();
        if (! demucscpp::load_demucs_model (file.string(), models[(size_t) m].get())) fail ("modèle illisible : " + file.filename().string());
    }

    // Tronçons de 30 s, 2 s de contexte de chaque côté
    // (autant que le recouvrement interne de Demucs) et fondu croisé linéaire.
    const Eigen::Index sr = demucscpp::SUPPORTED_SAMPLE_RATE, overlap = 2 * sr;
    // Précis : un morceau de 4 min donne 4 x 8 = 32 tâches. Rapide : tronçons plus courts pour occuper tous les cœurs.
    const Eigen::Index chunk = fast ? std::clamp<Eigen::Index> ((total + threads - 1) / threads, 10 * sr, 30 * sr) : 30 * sr;
    const int weightModel = fast ? fastIdx : 0;
    struct Job { int model; Eigen::Index start, end; };          // [start, end[ : partie « utile » du tronçon
    std::vector<Job> jobs;
    for (int m = 0; m < 4; ++m)
        if (! fast || m == fastIdx)
        for (Eigen::Index s = 0; s < total; s += chunk)
            jobs.push_back ({ m, s, std::min (total, s + chunk) });

    std::vector<Eigen::MatrixXf> stems (4, Eigen::MatrixXf::Zero (2, total));
    Eigen::VectorXf weight = Eigen::VectorXf::Zero (total);   // identique pour les 4 modèles
    std::mutex accMutex;
    std::atomic<int> next { 0 }, done { 0 };
    std::vector<double> partial (jobs.size(), 0.0);
    std::atomic<bool> failed { false };

    auto report = [&]
    {
        double sum = 0; for (auto v : partial) sum += v;
        char b[64]; std::snprintf (b, sizeof (b), "PROGRESS %.4f", sum / (double) jobs.size());
        say (b);
    };

    say ("STAGE separation");
    auto worker = [&]
    {
        ONDE_FTZ();      // les dénormaux (silences, queues) rendent les calculs 10 à 100 fois plus lents
#ifdef _WIN32
        static std::thread profiler;
        if (std::getenv ("ONDE_STEMS_PROFILE") && prof::target == nullptr)
        {
            DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &prof::target, 0, FALSE, DUPLICATE_SAME_ACCESS);
            profiler = std::thread (prof::sampler);
            profiler.detach();
        }
#endif
        for (;;)
        {
            const int j = next++;
            if (j >= (int) jobs.size() || failed) return;
            const auto& job = jobs[(size_t) j];
            const Eigen::Index a = std::max<Eigen::Index> (0, job.start - overlap), b = std::min (total, job.end + overlap);
            Eigen::MatrixXf seg = audio.block (0, a, 2, b - a);
            Eigen::Tensor3dXf out;
            try
            {
                out = demucscpp::demucs_inference (*models[(size_t) job.model], seg,
                        [&, j] (float p, const std::string& msg)
                        {
                            partial[(size_t) j] = std::clamp ((double) p, 0.0, 1.0);
                            static const bool verbose = std::getenv ("ONDE_STEMS_VERBOSE") != nullptr;
                            static const auto t0 = std::chrono::steady_clock::now();
                            if (verbose && j == 0)
                                say ("TRACE " + std::to_string (std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count()) + " " + msg);
                        });
            }
            catch (const std::exception& e) { failed = true; say (std::string ("ERROR ") + e.what()); return; }
            // Poids : rampe sur le recouvrement (sauf aux bords du morceau), 1 au milieu.
            std::lock_guard<std::mutex> l (accMutex);
            // Précis : chaque modèle ft ne sert que pour « sa » source. Rapide : le modèle unique donne les 4.
            const int srcFirst = fast ? 0 : job.model, srcLast = fast ? 3 : job.model;
            for (Eigen::Index i = a; i < b; ++i)
            {
                // Fondu croisé centré sur chaque frontière, sur 2 x recouvrement.
                float w = 1.0f;
                if (a > 0) w = std::min (w, (float) (i - a + 1) / (float) (2 * overlap));
                if (b < total) w = std::min (w, (float) (b - i) / (float) (2 * overlap));
                for (int src = srcFirst; src <= srcLast; ++src)
                    for (int c = 0; c < 2; ++c) stems[(size_t) src] (c, i) += w * out (src, c, i - a);
                if (job.model == weightModel) weight (i) += w;
            }
            partial[(size_t) j] = 1.0;
            ++done;
            report();
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back (worker);
    // Progression régulière même pendant un long tronçon.
    std::thread ticker ([&] { while (done < (int) jobs.size() && ! failed) { std::this_thread::sleep_for (std::chrono::milliseconds (1000)); if (done < (int) jobs.size()) report(); } });
    for (auto& t : pool) t.join();
    ticker.join();
    if (failed) return 1;

    say ("STAGE ecriture");
    fs::create_directories (outDir);
    for (int m = 0; m < 4; ++m)
        for (Eigen::Index i = 0; i < total; ++i)
            if (weight (i) > 0) stems[(size_t) m].col (i) /= weight (i);
    if (fast)
    {
        // Un modèle « fine-tuned » n'est calibré que pour sa propre source : la somme des 4 ne redonne pas tout
        // à fait le mix. Le résidu est redistribué au prorata de l'amplitude locale de chaque source.
        for (Eigen::Index i = 0; i < total; ++i)
            for (int c = 0; c < 2; ++c)
            {
                float sum = 0.0f, mag = 0.0f;
                for (int m = 0; m < 4; ++m) { sum += stems[(size_t) m] (c, i); mag += std::abs (stems[(size_t) m] (c, i)); }
                const float r = audio (c, i) - sum;
                for (int m = 0; m < 4; ++m)
                    stems[(size_t) m] (c, i) += mag > 1.0e-9f ? r * std::abs (stems[(size_t) m] (c, i)) / mag : r * 0.25f;
            }
    }
    for (int m = 0; m < 4; ++m)
        writeWav (outDir / (std::string (names[m]) + ".wav"), stems[(size_t) m]);
    say ("DONE");
    return 0;
}

// Windows : arguments en UTF-16 (chemins accentués) ; ailleurs : UTF-8.
#ifdef _WIN32
int wmain (int argc, wchar_t** argv)
{
    std::vector<fs::path> args;
    for (int i = 0; i < argc; ++i) args.emplace_back (argv[i]);
    return run (argc, args.data());
}
#else
int main (int argc, char** argv)
{
    std::vector<fs::path> args;
    for (int i = 0; i < argc; ++i) args.push_back (fs::u8path (argv[i]));
    return run (argc, args.data());
}
#endif
