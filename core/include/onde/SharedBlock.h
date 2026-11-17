// Onde — mémoire partagée entre l'hôte et un processus de plugin isolé.
#pragma once
#include <atomic>
#include <cstdint>
#ifndef _WIN32
 #include <semaphore.h>
#endif

namespace onde
{
    constexpr uint32_t kMagic        = 0x4F4E4445; // "ONDE"
    constexpr uint32_t kMaxChannels  = 2;
    constexpr uint32_t kMaxFrames    = 4096;
    constexpr uint32_t kMaxParams    = 16;
    constexpr int      kCrashTestParam = 15;  // démo : > 0,5 fait planter le worker volontairement
    constexpr uint32_t kMaxMidiEvents = 1024;
    constexpr uint32_t kMaxStateBytes = 4u << 20;  // état d'un plugin (préréglage), 4 Mo max

    struct MidiEvent { uint32_t offset; uint8_t size; uint8_t data[3]; };

    // Commandes non temps réel, traitées par le fil principal du worker.
    enum class Command : uint32_t { none = 0, showEditor, hideEditor, saveState, loadState };

    // Un bloc à traiter : audio sur place (entrée -> sortie), MIDI et transport de l'hôte.
    // Deux emplacements alternent (requête n -> emplacement n % 2) : le worker peut calculer un bloc
    // pendant que l'hôte prépare le suivant (traitement anticipé).
    struct Slot
    {
        uint32_t numFrames;
        uint64_t samplePosition;             // position de lecture (pour les synthés)
        double   bpm;
        double   ppqPosition, ppqBarStart, ppqLoopStart, ppqLoopEnd;
        uint32_t timeSigNumerator, timeSigDenominator, isPlaying, isLooping;
        uint32_t isOffline;                  // 1 pendant un export : le plugin peut prendre son temps
        uint32_t  numMidiEvents;
        MidiEvent midi[kMaxMidiEvents];
        float audio[kMaxChannels * kMaxFrames];
        float* channel (uint32_t ch) noexcept { return audio + ch * kMaxFrames; }
    };

    struct SharedBlock
    {
        uint32_t magic;
        uint32_t maxChannels;
        uint32_t maxFrames;

       #ifndef _WIN32
        sem_t request;   // l'hôte poste : « un bloc est prêt »
        sem_t response;  // le worker poste : « bloc traité »
       #endif            // Windows : deux événements nommés, dérivés du nom de la mémoire partagée

        std::atomic<uint64_t> requestSeq;
        std::atomic<uint64_t> responseSeq;
        std::atomic<uint32_t> workerReady;   // 1 quand le plugin est chargé
        std::atomic<uint32_t> shutdown;      // 1 = le worker doit quitter

        uint32_t numChannels;
        double   sampleRate;
        float    params[kMaxParams];
        std::atomic<uint32_t> latencySamples;   // publié par le worker (compensation de latence)

        Slot slots[2];
        Slot& slot (uint64_t seq) noexcept { return slots[seq & 1]; }

        // Commandes : l'hôte écrit command puis incrémente commandSeq ; le worker répond via commandDoneSeq.
        std::atomic<uint32_t> command;
        std::atomic<uint32_t> commandSeq;
        std::atomic<uint32_t> commandDoneSeq;
        std::atomic<uint32_t> commandOk;

        // Informations publiées par le worker au démarrage.
        char     pluginName[128];
        char     errorText[256];
        uint32_t isInstrument;
        uint32_t hasEditor;
        std::atomic<uint32_t> editorVisible;

        uint32_t stateSize;
        uint8_t  state[kMaxStateBytes];
    };

    static_assert (std::atomic<uint64_t>::is_always_lock_free, "atomics must be lock-free across processes");
}
