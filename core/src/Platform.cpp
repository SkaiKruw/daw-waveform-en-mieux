#include "Platform.h"

#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

namespace onde::platform
{
    namespace
    {
        std::wstring widen (const std::string& s)
        {
            if (s.empty()) return {};
            const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
            std::wstring w ((size_t) n, L'\0');
            MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
            return w;
        }

        // Tous les workers rattachés à un « job » : si l'hôte disparaît, Windows les ferme.
        HANDLE workerJob()
        {
            static HANDLE job = []
            {
                HANDLE j = CreateJobObjectW (nullptr, nullptr);
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info {};
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject (j, JobObjectExtendedLimitInformation, &info, sizeof (info));
                return j;
            }();
            return job;
        }
    }

    void* createSharedMemory (const std::string& name, size_t size, intptr_t& handleOut)
    {
        HANDLE h = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) size, widen (name).c_str());
        if (h == nullptr) return nullptr;
        void* mem = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (mem == nullptr) { CloseHandle (h); return nullptr; }
        VirtualLock (mem, size);
        handleOut = (intptr_t) h;
        return mem;
    }

    void* openSharedMemory (const std::string& name, size_t size, intptr_t& handleOut)
    {
        HANDLE h = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, widen (name).c_str());
        if (h == nullptr) return nullptr;
        void* mem = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (mem == nullptr) { CloseHandle (h); return nullptr; }
        handleOut = (intptr_t) h;
        return mem;
    }

    void closeSharedMemory (void* mem, size_t, intptr_t handle, const std::string&, bool)
    {
        if (mem != nullptr) UnmapViewOfFile (mem);
        if (handle != -1 && handle != 0) CloseHandle ((HANDLE) handle);
    }

    intptr_t spawn (const std::string& exe, const std::string& arg1, const std::string& arg2)
    {
        std::wstring cmd = L"\"" + widen (exe) + L"\" \"" + widen (arg1) + L"\" \"" + widen (arg2) + L"\" " + std::to_wstring (GetCurrentProcessId());
        STARTUPINFOW si {}; si.cb = sizeof (si);
        PROCESS_INFORMATION pi {};
        if (! CreateProcessW (widen (exe).c_str(), cmd.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
            return -1;
        AssignProcessToJobObject (workerJob(), pi.hProcess);
        ResumeThread (pi.hThread);
        CloseHandle (pi.hThread);
        return (intptr_t) pi.hProcess;
    }

    void killProcess (intptr_t process)
    {
        if (process != -1 && process != 0)
            TerminateProcess ((HANDLE) process, 0xDEAD);
    }

    void waitForExit (intptr_t process, bool& crashed, int& signal, uint32_t& exitCode)
    {
        WaitForSingleObject ((HANDLE) process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess ((HANDLE) process, &code);
        exitCode = (uint32_t) code;
        crashed = code != 0;
        signal = crashed ? 1 : 0;
    }

    void releaseProcess (intptr_t process)
    {
        if (process != -1 && process != 0) CloseHandle ((HANDLE) process);
    }

    int  currentProcessId()   { return (int) GetCurrentProcessId(); }
    void sleepMs (int ms)     { Sleep ((DWORD) ms); }
}

#else // POSIX ======================================================================
 #include <cerrno>
 #include <fcntl.h>
 #include <signal.h>
 #include <spawn.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <sys/wait.h>
 #include <unistd.h>

extern char** environ;

namespace onde::platform
{
    void* createSharedMemory (const std::string& name, size_t size, intptr_t& handleOut)
    {
        shm_unlink (name.c_str());
        const int fd = shm_open (name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0 || ftruncate (fd, (off_t) size) != 0) return nullptr;
        void* mem = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED) { close (fd); return nullptr; }
        mlock (mem, size);   // évite un défaut de page dans le thread audio
        handleOut = fd;
        return mem;
    }

    void* openSharedMemory (const std::string& name, size_t size, intptr_t& handleOut)
    {
        const int fd = shm_open (name.c_str(), O_RDWR, 0600);
        if (fd < 0) return nullptr;
        void* mem = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED) { close (fd); return nullptr; }
        handleOut = fd;
        return mem;
    }

    void closeSharedMemory (void* mem, size_t size, intptr_t handle, const std::string& name, bool owner)
    {
        if (mem != nullptr) munmap (mem, size);
        if (handle >= 0) close ((int) handle);
        if (owner) shm_unlink (name.c_str());
    }

    intptr_t spawn (const std::string& exe, const std::string& arg1, const std::string& arg2)
    {
        const std::string parent = std::to_string (getpid());
        char* argv[] = { const_cast<char*> (exe.c_str()), const_cast<char*> (arg1.c_str()),
                         const_cast<char*> (arg2.c_str()), const_cast<char*> (parent.c_str()), nullptr };
        pid_t child = -1;
        if (posix_spawn (&child, exe.c_str(), nullptr, nullptr, argv, environ) != 0)
            return -1;
        return child;
    }

    void killProcess (intptr_t process)
    {
        if (process > 0) ::kill ((pid_t) process, SIGKILL);
    }

    void waitForExit (intptr_t process, bool& crashed, int& signal, uint32_t& exitCode)
    {
        int status = 0;
        while (waitpid ((pid_t) process, &status, 0) < 0 && errno == EINTR) {}
        crashed = WIFSIGNALED (status) || (WIFEXITED (status) && WEXITSTATUS (status) != 0);
        signal = WIFSIGNALED (status) ? WTERMSIG (status) : 0;
        exitCode = WIFEXITED (status) ? (uint32_t) WEXITSTATUS (status) : 0;
    }

    void releaseProcess (intptr_t) {}
    int  currentProcessId()   { return (int) getpid(); }
    void sleepMs (int ms)     { usleep ((useconds_t) ms * 1000); }
}
#endif
