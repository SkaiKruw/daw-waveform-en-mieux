// Plugins de démonstration exécutés DANS le worker.
#pragma once
#include "SharedBlock.h"
#include <memory>
#include <string>

namespace onde
{
    struct WorkerPlugin
    {
        virtual ~WorkerPlugin() = default;
        virtual void prepare (double sampleRate, uint32_t channels) = 0;
        virtual void process (SharedBlock&, Slot&) = 0;
    };

    // Specs : "gain:<x>", "synth-pad", "crash-after:<blocs>", "hang-after:<blocs>", "crash-at:<secondes>",
    //         "synth-pad,crash-after:<blocs>" (synthé qui plante après N blocs).
    std::unique_ptr<WorkerPlugin> createWorkerPlugin (const std::string& spec);

    // Provoque un vrai accès mémoire invalide (SIGSEGV sous Linux, 0xC0000005 sous Windows).
    [[noreturn]] void crashForTesting();
}
