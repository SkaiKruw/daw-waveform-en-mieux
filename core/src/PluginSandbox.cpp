#include "onde/PluginSandbox.h"
#include "onde/Trace.h"
#include "Platform.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>

#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <timeapi.h>
#else
 #include <csignal>
 #include <semaphore.h>
#endif

namespace onde
{
    const char* toString (PluginSandbox::State s)
    {
        switch (s)
        {
            case PluginSandbox::State::stopped:    return "arrêté";
            case PluginSandbox::State::starting:   return "démarrage";
            case PluginSandbox::State::running:    return "en marche";
            case PluginSandbox::State::crashed:    return "planté";
            case PluginSandbox::State::hung:       return "bloqué";
            case PluginSandbox::State::restarting: return "redémarrage";
        }
        return "?";
    }

    namespace
    {
        std::atomic<int> instanceCounter { 0 };

       #ifdef _WIN32
        std::wstring eventName (const std::string& shm, const char* suffix)
        {
            std::string n = shm + suffix;
            return std::wstring (n.begin(), n.end());   // noms ASCII uniquement
        }
       #else
        timespec deadlineFromNow (double seconds)
        {
            timespec ts {};
            clock_gettime (CLOCK_REALTIME, &ts);   // sem_timedwait attend CLOCK_REALTIME
            const long ns = (long) (seconds * 1e9);
            ts.tv_nsec += ns % 1000000000L;
            ts.tv_sec  += ns / 1000000000L + ts.tv_nsec / 1000000000L;
            ts.tv_nsec %= 1000000000L;
            return ts;
        }
       #endif
    }

    PluginSandbox::PluginSandbox (Options o) : options (std::move (o))
    {
        const std::string id = std::to_string (platform::currentProcessId()) + "-" + std::to_string (instanceCounter++);
       #ifdef _WIN32
        shmName = "Local\\onde-" + id;
        timeBeginPeriod (1);   // échéances à la milliseconde près
       #else
        shmName = "/onde-" + id;
       #endif
    }

    PluginSandbox::~PluginSandbox()
    {
        stop();
       #ifdef _WIN32
        timeEndPeriod (1);
       #endif
    }

    void PluginSandbox::setState (State s)
    {
        if (state.exchange (s) != s && onStateChanged)
            onStateChanged (s);
    }

    //==============================================================================
    // Signalisation hôte <-> worker
    bool PluginSandbox::createSharedBlock()
    {
        if (block != nullptr)
            return true;
        block = static_cast<SharedBlock*> (platform::createSharedMemory (shmName, sizeof (SharedBlock), shmHandle));
       #ifdef _WIN32
        if (block != nullptr)
        {
            requestEvent  = CreateEventW (nullptr, FALSE, FALSE, eventName (shmName, "-req").c_str());
            responseEvent = CreateEventW (nullptr, FALSE, FALSE, eventName (shmName, "-resp").c_str());
        }
       #endif
        return block != nullptr;
    }

    void PluginSandbox::destroySharedBlock()
    {
        if (block == nullptr)
            return;
       #ifdef _WIN32
        CloseHandle (requestEvent);  requestEvent = nullptr;
        CloseHandle (responseEvent); responseEvent = nullptr;
       #else
        shmSemDestroy (&block->request);
        shmSemDestroy (&block->response);
       #endif
        platform::closeSharedMemory (block, sizeof (SharedBlock), shmHandle, shmName, true);
        block = nullptr;
        shmHandle = -1;
    }

    void PluginSandbox::resetSignals()
    {
       #ifdef _WIN32
        ResetEvent (requestEvent);
        ResetEvent (responseEvent);
       #else
        shmSemInit (&block->request);
        shmSemInit (&block->response);
       #endif
    }

    namespace
    {
        inline void postRequest (SharedBlock* b, void* reqEvent)
        {
           #ifdef _WIN32
            (void) b; SetEvent ((HANDLE) reqEvent);
           #else
            (void) reqEvent; shmSemPost (&b->request);
           #endif
        }

        // true = réponse reçue avant l'échéance.
        inline bool waitResponse (SharedBlock* b, void* respEvent, double seconds)
        {
           #ifdef _WIN32
            (void) b;
            const DWORD ms = (DWORD) std::max (1.0, std::ceil (seconds * 1000.0));
            return WaitForSingleObject ((HANDLE) respEvent, ms) == WAIT_OBJECT_0;
           #else
            (void) respEvent;
            const timespec deadline = deadlineFromNow (seconds);
            while (true)
            {
                if (shmSemTimedWait (&b->response, &deadline) == 0) return true;
                if (errno != EINTR) return false;
            }
           #endif
        }

        inline void drainResponses (SharedBlock* b, void* respEvent)
        {
           #ifdef _WIN32
            (void) b; ResetEvent ((HANDLE) respEvent);
           #else
            (void) respEvent; while (shmSemTryWait (&b->response) == 0) {}
           #endif
        }
    }

    //==============================================================================
    bool PluginSandbox::start (double sr, uint32_t numChannels)
    {
        std::lock_guard<std::mutex> l (lifecycleLock);
        sampleRate = sr;
        channels = std::min (numChannels, kMaxChannels);

        if (! createSharedBlock())
            return false;

        std::memset (static_cast<void*> (block), 0, sizeof (SharedBlock));
        block->magic = kMagic;
        block->maxChannels = kMaxChannels;
        block->maxFrames = kMaxFrames;
        block->sampleRate = sampleRate;
        block->numChannels = channels;
        resetSignals();

        consecutiveMisses = 0;
        missedSeconds = 0.0;
        requestOutstanding = false;
        pipelineFrames = std::min<uint32_t> (options.pipelineFrames, kMaxFrames);
        for (auto& f : fifo) f.assign (4 * kMaxFrames, 0.0f);     // alloué ici, jamais dans le thread audio
        resetPipeline();
        collected = 0;
        setState (State::starting);
        return spawnWorker();
    }

    bool PluginSandbox::spawnWorker()
    {
        const intptr_t child = platform::spawn (options.workerPath, shmName, options.pluginSpec);
        if (child == -1)
        {
            spawnFailed = true;
            setState (State::crashed);
            return false;
        }
        spawnFailed = false;
        workerProcess.store (child);
        if (supervisor.joinable())
            supervisor.join();
        supervisor = std::thread ([this, child] { supervise (child); });
        return true;
    }

    // Thread de surveillance : attend la fin du worker et en déduit la cause.
    void PluginSandbox::supervise (intptr_t child)
    {
        bool crashed = false; int sig = 0; uint32_t code = 0;
        platform::waitForExit (child, crashed, sig, code);

        intptr_t expected = child;
        workerProcess.compare_exchange_strong (expected, -1);
        platform::releaseProcess (child);

        lastSignal.store (sig);
        lastExitCode.store (code);

        const auto s = state.load();
        if (s == State::stopped || s == State::restarting)
            return;                                   // arrêt volontaire
       #ifdef _WIN32
        const bool killedByUs = code == 0xDEADu;
       #else
        const bool killedByUs = sig == SIGKILL;
       #endif
        // Déclaré « bloqué » mais mort d'une vraie erreur : c'était un plantage lent à se terminer.
        if (s != State::hung || ! killedByUs)
            setState (State::crashed);
    }

    bool PluginSandbox::waitUntilReady (int timeoutMs)
    {
        for (int waited = 0; waited < timeoutMs; waited += 2)
        {
            if (block != nullptr && block->workerReady.load() == 1)
            {
                if (state.load() == State::starting)
                    setState (State::running);
                return true;
            }
            if (state.load() == State::crashed)
                return false;
            platform::sleepMs (2);
        }
        return false;
    }


    void PluginSandbox::killWorker()
    {
        platform::killProcess (workerProcess.load());
    }

    void PluginSandbox::waitForAudioThread()
    {
        while (inProcess.load())
            std::this_thread::yield();
    }

    void PluginSandbox::stop()
    {
        trace ("sandbox.stop: verrou", options.pluginSpec.c_str());
        std::lock_guard<std::mutex> l (lifecycleLock);
        setState (State::stopped);
        trace ("sandbox.stop: attente du thread audio");
        waitForAudioThread();
        trace ("sandbox.stop: arrêt du worker");
        if (block != nullptr)
        {
            block->shutdown.store (1);
            postRequest (block, requestEvent);
        }
        // Laisse 200 ms au worker pour quitter proprement, puis on force.
        for (int i = 0; i < 100 && workerProcess.load() != -1; ++i)
            platform::sleepMs (2);
        killWorker();
        trace ("sandbox.stop: attente du superviseur");
        if (supervisor.joinable())
            supervisor.join();
        destroySharedBlock();
        trace ("sandbox.stop: terminé");
    }

    bool PluginSandbox::restart()
    {
        {
            std::lock_guard<std::mutex> l (lifecycleLock);
            setState (State::restarting);
            waitForAudioThread();          // plus aucun accès au bloc partagé après ceci
            killWorker();
            if (supervisor.joinable())
                supervisor.join();
           #ifndef _WIN32
            if (block != nullptr) { shmSemDestroy (&block->request); shmSemDestroy (&block->response); }
           #endif
            restarts++;
            lastSignal.store (0);
            lastExitCode.store (0);
        }
        if (! start (sampleRate, channels))  // réutilise la même mémoire, signaux réinitialisés
            return false;
        return waitUntilReady (3000);
    }

    void PluginSandbox::setTransport (const Transport& t) noexcept
    {
        transport = t;                               // recopié dans l'emplacement du prochain bloc envoyé
    }

    uint32_t PluginSandbox::getLatencySamples() const noexcept
    {
        return block != nullptr && state.load() == State::running ? block->latencySamples.load() : 0u;
    }

    void PluginSandbox::setParameter (int index, float value) noexcept
    {
        if (block != nullptr && index >= 0 && index < (int) kMaxParams)
            block->params[index] = value;
    }

    std::vector<PluginSandbox::ParamDesc> PluginSandbox::getPluginParameters() const
    {
        std::vector<ParamDesc> list;
        if (block == nullptr) return list;
        const uint32_t n = std::min (block->numPluginParams.load (std::memory_order_acquire), kMaxPluginParams);
        list.reserve (n);
        for (uint32_t i = 0; i < n; ++i)
        {
            const auto& info = block->paramInfo[i];
            ParamDesc d;
            d.name.assign (info.name, strnlen (info.name, sizeof (info.name)));
            d.label.assign (info.label, strnlen (info.label, sizeof (info.label)));
            d.defaultValue = info.defaultValue;
            d.numSteps = (int) info.numSteps;
            d.flags = info.flags;
            list.push_back (std::move (d));
        }
        return list;
    }

    void PluginSandbox::setPluginParameter (int index, float value) noexcept
    {
        if (block == nullptr || index < 0 || index >= (int) kMaxPluginParams) return;
        block->hostValues[index] = value;
        block->hostDirty[index >> 5].fetch_or (1u << (index & 31), std::memory_order_release);
    }

    float PluginSandbox::getPluginParameter (int index) const noexcept
    {
        if (block == nullptr || index < 0 || index >= (int) kMaxPluginParams) return 0.0f;
        return block->pluginValues[index];
    }

    std::string PluginSandbox::getPluginParameterText (int index) const
    {
        if (block == nullptr || index < 0 || index >= (int) kMaxPluginParams) return {};
        char text[sizeof (block->valueText[0])];
        std::memcpy (text, block->valueText[index], sizeof (text));
        return std::string (text, strnlen (text, sizeof (text)));
    }

    void PluginSandbox::collectPluginParameterChanges (const std::function<void (int, float)>& f)
    {
        if (block == nullptr) return;
        const uint32_t n = std::min (block->numPluginParams.load (std::memory_order_acquire), kMaxPluginParams);
        for (uint32_t w = 0; w < (n + 31) / 32; ++w)
        {
            const uint32_t bits = block->pluginDirty[w].exchange (0, std::memory_order_acq_rel);
            for (uint32_t b = 0; bits != 0 && b < 32; ++b)
                if ((bits >> b) & 1u)
                {
                    const uint32_t index = w * 32 + b;
                    if (index < n) f ((int) index, block->pluginValues[index]);
                }
        }
    }

    bool PluginSandbox::sendCommand (Command c, int timeoutMs)
    {
        if (block == nullptr || state.load() != State::running)
            return false;
        const uint32_t seq = block->commandSeq.load() + 1;
        block->command.store ((uint32_t) c);
        block->commandSeq.store (seq);
        for (int waited = 0; waited < timeoutMs; waited += 2)
        {
            if (block->commandDoneSeq.load() == seq)
                return block->commandOk.load() != 0;
            if (state.load() != State::running)
                return false;
            platform::sleepMs (2);
        }
        return false;
    }

    bool PluginSandbox::saveState (std::vector<uint8_t>& out, int timeoutMs)
    {
        if (! sendCommand (Command::saveState, timeoutMs))
            return false;
        const uint32_t n = std::min (block->stateSize, kMaxStateBytes);
        out.assign (block->state, block->state + n);
        return true;
    }

    bool PluginSandbox::loadState (const std::vector<uint8_t>& data, int timeoutMs)
    {
        if (block == nullptr || data.empty() || data.size() > kMaxStateBytes)
            return false;
        std::memcpy (block->state, data.data(), data.size());
        block->stateSize = (uint32_t) data.size();
        return sendCommand (Command::loadState, timeoutMs);
    }

    PluginSandbox::Info PluginSandbox::getInfo() const
    {
        Info i;
        if (block != nullptr)
        {
            i.name.assign (block->pluginName, strnlen (block->pluginName, sizeof (block->pluginName)));
            i.error.assign (block->errorText, strnlen (block->errorText, sizeof (block->errorText)));
            i.isInstrument = block->isInstrument != 0;
            i.hasEditor = block->hasEditor != 0;
            i.editorVisible = block->editorVisible.load() != 0;
        }
        return i;
    }

    // Attend que le worker ait répondu à la requête seq (les réponses arrivent dans l'ordre).
    bool PluginSandbox::waitForSeq (uint64_t seq, double seconds) noexcept
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
        while (block->responseSeq.load (std::memory_order_acquire) < seq)
        {
            const double left = std::chrono::duration<double> (deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0.0 || state.load() != State::running) return false;
            waitResponse (block, responseEvent, std::min (left, 0.05));
        }
        return true;
    }

    void PluginSandbox::fillSlot (Slot& s, const float* const* io, uint32_t nc, uint32_t numFrames, uint64_t samplePosition,
                                  const MidiEvent* midi, uint32_t numMidi, bool offline) noexcept
    {
        for (uint32_t c = 0; c < nc; ++c)
            std::memcpy (s.channel (c), io[c], numFrames * sizeof (float));
        s.numFrames = numFrames;
        s.samplePosition = samplePosition;
        s.isOffline = offline ? 1u : 0u;
        s.bpm = transport.bpm;
        s.ppqPosition = transport.ppqPosition;
        s.ppqBarStart = transport.ppqBarStart;
        s.ppqLoopStart = transport.ppqLoopStart;
        s.ppqLoopEnd = transport.ppqLoopEnd;
        s.timeSigNumerator = transport.numerator;
        s.timeSigDenominator = transport.denominator;
        s.isPlaying = transport.playing ? 1u : 0u;
        s.isLooping = transport.looping ? 1u : 0u;
        s.numMidiEvents = std::min (numMidi, kMaxMidiEvents);
        if (s.numMidiEvents > 0)
            std::memcpy (s.midi, midi, s.numMidiEvents * sizeof (MidiEvent));
    }

    void PluginSandbox::resetPipeline() noexcept
    {
        posted = 0;
        slotFrames[0] = slotFrames[1] = 0;
        fifoRead = 0;
        fifoCount = pipelineFrames;                  // le retard annoncé : autant d'échantillons de silence d'avance
        for (auto& f : fifo) std::fill (f.begin(), f.end(), 0.0f);
    }

    void PluginSandbox::fifoPush (const float* const* src, uint32_t nc, uint32_t n) noexcept
    {
        const uint32_t cap = (uint32_t) fifo[0].size();
        for (uint32_t i = 0; i < n && fifoCount < cap; ++i)
        {
            const uint32_t w = (fifoRead + fifoCount) % cap;
            for (uint32_t c = 0; c < kMaxChannels; ++c)
                fifo[c][w] = src != nullptr ? src[std::min (c, nc - 1)][i] : 0.0f;
            ++fifoCount;
        }
    }

    bool PluginSandbox::processPipelined (float* const* io, uint32_t numChannels, uint32_t numFrames, uint64_t samplePosition,
                                          const MidiEvent* midi, uint32_t numMidi, bool offline) noexcept
    {
        const uint32_t nc = std::min (numChannels, channels);
        bool ok = true;

        // 1. Résultat de la requête précédente : le worker a eu toute une période pour le calculer.
        if (posted > 0 && posted > collected)
        {
            const uint64_t prev = posted;
            if (waitForSeq (prev, offline ? 30.0 : options.deadlineFraction * numFrames / sampleRate))
            {
                auto& s = block->slot (prev);
                float* chans[kMaxChannels] = { s.channel (0), s.channel (1) };
                fifoPush (chans, nc, slotFrames[prev & 1]);
                consecutiveMisses = 0;
                missedSeconds = 0.0;
                blocksOk++;
            }
            else
            {
                // En retard : du silence à sa place (l'alignement est conservé) ; sa réponse tardive sera ignorée.
                fifoPush (nullptr, nc, slotFrames[prev & 1]);
                blocksMissed++;
                ++consecutiveMisses;
                missedSeconds += numFrames / sampleRate;
                ok = false;
                const bool hung = options.hungAfterSeconds > 0.0 ? missedSeconds >= options.hungAfterSeconds
                                                                 : consecutiveMisses >= options.hungAfterMissedBlocks;
                if (hung) { setState (State::hung); killWorker(); }
            }
            collected = prev;
        }

        // 2. Le bloc courant part au worker si son emplacement est libre (requête seq-2 terminée).
        const uint64_t seq = posted + 1;
        if (state.load() == State::running && block->responseSeq.load (std::memory_order_acquire) + 2 >= seq)
        {
            auto& s = block->slot (seq);
            fillSlot (s, io, nc, numFrames, samplePosition, midi, numMidi, offline);
            slotFrames[seq & 1] = numFrames;
            block->requestSeq.store (seq, std::memory_order_release);
            postRequest (block, requestEvent);
            posted = seq;
        }
        else
        {
            fifoPush (nullptr, nc, numFrames);      // emplacement encore occupé : ce bloc sera muet
            blocksSilenced++;
            ok = false;
        }

        // 3. Sortie : les échantillons calculés, avec pipelineFrames de retard (compensé par l'hôte).
        const uint32_t cap = (uint32_t) fifo[0].size();
        for (uint32_t i = 0; i < numFrames; ++i)
        {
            const bool have = fifoCount > 0;
            for (uint32_t c = 0; c < numChannels; ++c)
                io[c][i] = have ? fifo[std::min (c, kMaxChannels - 1)][fifoRead] : 0.0f;
            if (have) { fifoRead = (fifoRead + 1) % cap; --fifoCount; }
        }
        return ok;
    }

    bool PluginSandbox::process (float* const* io, uint32_t numChannels, uint32_t numFrames, uint64_t samplePosition,
                                 const MidiEvent* midi, uint32_t numMidi, bool offline) noexcept
    {
        struct Guard { std::atomic<bool>& f; Guard (std::atomic<bool>& x) : f (x) { f.store (true); }
                       ~Guard() { f.store (false); } } guard (inProcess);

        if (pipelineFrames > 0)
        {
            if (state.load() != State::running || block == nullptr || numFrames > kMaxFrames)
            {
                for (uint32_t c = 0; c < numChannels; ++c) std::memset (io[c], 0, numFrames * sizeof (float));
                blocksSilenced++;
                return false;
            }
            return processPipelined (io, numChannels, numFrames, samplePosition, midi, numMidi, offline);
        }

        auto silence = [&]
        {
            for (uint32_t c = 0; c < numChannels; ++c)
                std::memset (io[c], 0, numFrames * sizeof (float));
            blocksSilenced++;
            return false;
        };
        auto missed = [&]
        {
            blocksMissed++;
            ++consecutiveMisses;
            missedSeconds += numFrames / sampleRate;
            // « Bloqué » : N blocs de suite sans réponse, ou (si hungAfterSeconds > 0) ce temps sans réponse.
            const bool hung = options.hungAfterSeconds > 0.0 ? missedSeconds >= options.hungAfterSeconds
                                                             : consecutiveMisses >= options.hungAfterMissedBlocks;
            if (hung)
            {
                setState (State::hung);
                killWorker();
            }
            return silence();
        };

        if (state.load() != State::running || block == nullptr || numFrames > kMaxFrames)
            return silence();

        // Un bloc précédent n'a pas encore été rendu : on ne réécrit pas le tampon partagé.
        if (requestOutstanding)
        {
            // Hors temps réel (export) : on attend le bloc en retard au lieu de le sauter.
            if (offline)
                for (int i = 0; i < 300 && block->responseSeq.load (std::memory_order_acquire) != block->requestSeq.load (std::memory_order_relaxed); ++i)
                    waitResponse (block, responseEvent, 0.1);
            if (block->responseSeq.load (std::memory_order_acquire) != block->requestSeq.load (std::memory_order_relaxed))
                return missed();
            requestOutstanding = false;
            drainResponses (block, responseEvent);   // jette la réponse tardive
        }

        const uint32_t nc = std::min (numChannels, channels);
        const uint64_t seq = block->requestSeq.load (std::memory_order_relaxed) + 1;
        auto& slot = block->slot (seq);
        fillSlot (slot, io, nc, numFrames, samplePosition, midi, numMidi, offline);
        block->requestSeq.store (seq, std::memory_order_release);
        postRequest (block, requestEvent);

        // Temps réel : une fraction de la durée du bloc. Export : jusqu'à 30 s (le plugin peut charger ses échantillons).
        if (! waitResponse (block, responseEvent, offline ? 30.0 : options.deadlineFraction * numFrames / sampleRate))
        {
            // Échéance dépassée : silence pour ce bloc, on récupérera la réponse plus tard.
            requestOutstanding = true;
            return missed();
        }

        if (block->responseSeq.load (std::memory_order_acquire) != seq)
            return silence();

        for (uint32_t c = 0; c < nc; ++c)
            std::memcpy (io[c], slot.channel (c), numFrames * sizeof (float));
        for (uint32_t c = nc; c < numChannels; ++c)
            std::memcpy (io[c], io[0], numFrames * sizeof (float));

        consecutiveMisses = 0;
        missedSeconds = 0.0;
        blocksOk++;
        return true;
    }

    PluginSandbox::Stats PluginSandbox::getStats() const
    {
        Stats s;
        s.blocksOk = blocksOk.load(); s.blocksMissed = blocksMissed.load(); s.blocksSilenced = blocksSilenced.load();
        s.lastSignal = lastSignal.load(); s.restarts = restarts.load(); s.exitCode = lastExitCode.load();
        return s;
    }

    std::string PluginSandbox::describeFailure() const
    {
        switch (state.load())
        {
            case State::hung:    return "ne répond plus (bloqué), processus arrêté";
            case State::crashed:
            {
                if (spawnFailed)
                    return "n'a pas pu démarrer (programme " + options.workerPath + " introuvable)";
                if (block != nullptr && block->errorText[0] != 0)
                    return "n'a pas pu se charger : " + std::string (block->errorText, strnlen (block->errorText, sizeof (block->errorText)));
               #ifdef _WIN32
                switch (lastExitCode.load())
                {
                    case 0xC0000005u: return "a planté (accès mémoire invalide)";
                    case 0xC0000094u: case 0xC000008Eu: return "a planté (erreur de calcul)";
                    case 0xC00000FDu: return "a planté (pile saturée)";
                    case 0xC0000409u: case 3u: return "a planté (arrêt interne du plugin)";
                    case 0xDEADu:     return "a été arrêté";
                    default: break;
                }
                return "s'est arrêté de façon inattendue (code " + std::to_string (lastExitCode.load()) + ")";
               #else
                const int sig = lastSignal.load();
                if (sig == SIGSEGV || sig == SIGBUS) return "a planté (accès mémoire invalide)";
                if (sig == SIGABRT)  return "a planté (arrêt interne du plugin)";
                if (sig == SIGFPE)   return "a planté (erreur de calcul)";
                if (sig == SIGKILL || sig == SIGXCPU) return "a été arrêté (temps processeur dépassé)";
                if (sig != 0)        return "a planté (signal " + std::to_string (sig) + ")";
                return "s'est arrêté de façon inattendue";
               #endif
            }
            default: return {};
        }
    }
}
