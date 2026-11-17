// Côté worker : connexion au bloc partagé créé par l'hôte, attente des blocs audio, réponses.
#pragma once
#include "SharedBlock.h"
#include <cstdint>
#include <string>

namespace onde
{
    class WorkerChannel
    {
    public:
        ~WorkerChannel();

        bool open (const std::string& shmName, const char* parentPidArg);
        SharedBlock& block() noexcept { return *shared; }

        enum class Wait { request, timeout, hostGone, shutdown };
        Wait waitForRequest (int timeoutMs);
        void respond (uint64_t seq);

        // Commande en attente (thread non audio) : renvoie Command::none s'il n'y en a pas.
        Command pendingCommand();
        void commandDone (bool ok);

        static void installCrashHandlers();      // Windows : pas de débogueur, fin immédiate du processus
        static void limitRealtimeRuntime();      // Linux : RLIMIT_RTTIME (garde-fou anti-gel)
        static void makeCurrentThreadAudio();    // priorité audio pour le thread appelant

    private:
        SharedBlock* shared = nullptr;
        intptr_t shmHandle = -1;
        std::string name;
        uint32_t lastCommandSeq = 0;
       #ifdef _WIN32
        void* request = nullptr; void* response = nullptr; void* parent = nullptr;
       #else
        int parentPid = 0;
       #endif
    };
}
