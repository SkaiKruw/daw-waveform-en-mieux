// Primitives système utilisées par l'isolation, une version par plateforme.
#pragma once
#include <cstdint>
#include <string>

namespace onde::platform
{
    // Mémoire partagée nommée.
    void* createSharedMemory (const std::string& name, size_t size, intptr_t& handleOut);
    void* openSharedMemory (const std::string& name, size_t size, intptr_t& handleOut);
    void  closeSharedMemory (void* mem, size_t size, intptr_t handle, const std::string& name, bool owner);

    // Lancement / arrêt / attente d'un processus. Renvoie -1 en cas d'échec.
    intptr_t spawn (const std::string& exe, const std::string& arg1, const std::string& arg2);
    void     killProcess (intptr_t process);
    // Bloque jusqu'à la fin du processus. crashed = fin anormale ; signalOrCode = signal POSIX ou code Windows.
    void     waitForExit (intptr_t process, bool& crashed, int& signal, uint32_t& exitCode);
    void     releaseProcess (intptr_t process);

    int      currentProcessId();
    void     sleepMs (int ms);
}
