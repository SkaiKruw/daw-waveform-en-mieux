// onde-plugin-worker <mémoire-partagée> <spec> <pid-hôte> — worker léger (plugins de démonstration).
// Le worker qui charge de vrais VST3 est onde-plugin-host (app/), basé sur JUCE.
#include "onde/WorkerChannel.h"
#include "onde/WorkerPlugins.h"
#include <cstdio>
#include <cstring>

using namespace onde;

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: onde-plugin-worker <shm> <spec> [pid-hote]\n"); return 2; }
    WorkerChannel::installCrashHandlers();
    WorkerChannel::limitRealtimeRuntime();

    WorkerChannel channel;
    if (! channel.open (argv[1], argc > 3 ? argv[3] : nullptr))
        return 4;
    auto& b = channel.block();

    auto plugin = createWorkerPlugin (argv[2]);
    plugin->prepare (b.sampleRate, b.numChannels);
    std::snprintf (b.pluginName, sizeof (b.pluginName), "%s", argv[2]);
    b.isInstrument = std::strstr (argv[2], "synth") != nullptr ? 1u : 0u;
    channel.makeCurrentThreadAudio();
    uint64_t lastDone = b.requestSeq.load (std::memory_order_acquire);
    b.workerReady.store (1);

    while (true)
    {
        // Commandes (préréglage = paramètres de démo) traitées entre deux blocs.
        switch (channel.pendingCommand())
        {
            case Command::none: break;
            case Command::saveState:
                std::memcpy (b.state, b.params, sizeof (b.params) - sizeof (float));   // sans le paramètre de plantage
                b.stateSize = sizeof (b.params) - sizeof (float);
                channel.commandDone (true); break;
            case Command::loadState:
                if (b.stateSize == sizeof (b.params) - sizeof (float)) std::memcpy (b.params, b.state, b.stateSize);
                channel.commandDone (true); break;
            default: channel.commandDone (false); break;
        }

        const auto w = channel.waitForRequest (50);
        if (w == WorkerChannel::Wait::hostGone || w == WorkerChannel::Wait::shutdown) return 0;
        if (w != WorkerChannel::Wait::request) continue;

        // Toutes les requêtes en attente, dans l'ordre (au plus deux en traitement anticipé).
        while (lastDone < b.requestSeq.load (std::memory_order_acquire))
        {
            const uint64_t seq = lastDone + 1;
            if (b.params[kCrashTestParam] > 0.5f)
                crashForTesting();                     // bouton de démo « Simuler un plantage »
            plugin->process (b, b.slot (seq));
            channel.respond (seq);
            lastDone = seq;
        }
    }
}
