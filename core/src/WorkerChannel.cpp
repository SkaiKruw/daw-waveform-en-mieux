#include "onde/WorkerChannel.h"
#include "Platform.h"
#include <cstdlib>

#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <avrt.h>
#else
 #include <ctime>
 #include <pthread.h>
 #include <sched.h>
 #include <semaphore.h>
 #include <sys/resource.h>
 #include <unistd.h>
#endif

namespace onde
{
   #ifdef _WIN32
    // Sur une exception non gérée, Windows peut lancer un débogueur (rapport d'erreur, Visual Studio…)
    // qui fige le worker au lieu de le terminer : l'hôte ne verrait pas le plantage.
    static LONG WINAPI terminateOnCrash (EXCEPTION_POINTERS* info)
    {
        TerminateProcess (GetCurrentProcess(), info != nullptr ? info->ExceptionRecord->ExceptionCode : 0xC0000001u);
        return EXCEPTION_EXECUTE_HANDLER;
    }
   #endif

    void WorkerChannel::installCrashHandlers()
    {
       #ifdef _WIN32
        SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        SetUnhandledExceptionFilter (terminateOnCrash);
       #endif
    }

    void WorkerChannel::limitRealtimeRuntime()
    {
       #ifndef _WIN32
        // GARDE-FOU : un plugin bloqué en priorité temps réel gèlerait toute la machine.
        // Le noyau tue le worker s'il calcule plus de 100 ms d'affilée sans se mettre en attente.
        rlimit rt { 100000, 100000 };
        setrlimit (RLIMIT_RTTIME, &rt);
       #endif
    }

    void WorkerChannel::makeCurrentThreadAudio()
    {
       #ifdef _WIN32
        // MMCSS « Pro Audio » : priorité audio, mais une part du processeur reste au système.
        DWORD taskIndex = 0;
        AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
       #else
        rlimit rt {};
        if (getrlimit (RLIMIT_RTTIME, &rt) == 0 && rt.rlim_cur != RLIM_INFINITY)   // seulement avec le garde-fou
        {
            sched_param sp {}; sp.sched_priority = 70;
            pthread_setschedparam (pthread_self(), SCHED_FIFO, &sp);
        }
       #endif
    }

    WorkerChannel::~WorkerChannel()
    {
        if (shared != nullptr)
            platform::closeSharedMemory (shared, sizeof (SharedBlock), shmHandle, name, false);
       #ifdef _WIN32
        if (request)  CloseHandle (request);
        if (response) CloseHandle (response);
        if (parent)   CloseHandle (parent);
       #endif
    }

    bool WorkerChannel::open (const std::string& shmName, const char* parentPidArg)
    {
        name = shmName;
        shared = static_cast<SharedBlock*> (platform::openSharedMemory (shmName, sizeof (SharedBlock), shmHandle));
        if (shared == nullptr || shared->magic != kMagic)
            return false;
       #ifdef _WIN32
        auto wname = [&] (const char* suffix) { std::string n = shmName + suffix; return std::wstring (n.begin(), n.end()); };
        request  = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, wname ("-req").c_str());
        response = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, wname ("-resp").c_str());
        if (parentPidArg != nullptr)
            parent = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) std::strtoul (parentPidArg, nullptr, 10));
        return request != nullptr && response != nullptr;
       #else
        parentPid = parentPidArg != nullptr ? (int) std::strtol (parentPidArg, nullptr, 10) : (int) getppid();
        return true;
       #endif
    }

    WorkerChannel::Wait WorkerChannel::waitForRequest (int timeoutMs)
    {
       #ifdef _WIN32
        HANDLE waits[2] = { request, parent };
        const DWORD r = WaitForMultipleObjects (parent ? 2 : 1, waits, FALSE, (DWORD) timeoutMs);
        if (r == WAIT_OBJECT_0 + 1) return Wait::hostGone;
        if (r != WAIT_OBJECT_0)     return Wait::timeout;
       #else
        timespec ts {};
        clock_gettime (CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long) (timeoutMs % 1000) * 1000000L;
        ts.tv_sec  += timeoutMs / 1000 + ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
        if (sem_timedwait (&shared->request, &ts) != 0)
            return getppid() != parentPid ? Wait::hostGone : Wait::timeout;
       #endif
        return shared->shutdown.load() ? Wait::shutdown : Wait::request;
    }

    void WorkerChannel::respond (uint64_t seq)
    {
        shared->responseSeq.store (seq, std::memory_order_release);
       #ifdef _WIN32
        SetEvent (response);
       #else
        sem_post (&shared->response);
       #endif
    }

    Command WorkerChannel::pendingCommand()
    {
        const uint32_t seq = shared->commandSeq.load();
        if (seq == lastCommandSeq)
            return Command::none;
        lastCommandSeq = seq;
        return (Command) shared->command.load();
    }

    void WorkerChannel::commandDone (bool ok)
    {
        shared->commandOk.store (ok ? 1u : 0u);
        shared->commandDoneSeq.store (lastCommandSeq);
    }
}
