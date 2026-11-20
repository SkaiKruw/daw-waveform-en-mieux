// Onde — fait tourner un plugin dans un processus séparé.
// Si le plugin plante ou se bloque, seule cette instance se tait : l'hôte continue.
#pragma once
#include "SharedBlock.h"
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>

namespace onde
{
    class PluginSandbox
    {
    public:
        enum class State : int { stopped, starting, running, crashed, hung, restarting };

        struct Options
        {
            std::string workerPath;          // exécutable onde-plugin-worker
            std::string pluginSpec;          // ex. "gain:0.5", "synth-pad", "crash-after:300"
            int  hungAfterMissedBlocks = 8;  // blocs consécutifs sans réponse avant de déclarer « bloqué »
            double deadlineFraction    = 0.6; // part de la durée du bloc accordée au plugin
            // Champs ajoutés en fin de structure (les tests initialisent les précédents par position).
            double hungAfterSeconds    = 0.0; // si > 0 : temps sans réponse avant « bloqué » (remplace le nombre de blocs)
            uint32_t pipelineFrames    = 0;   // si > 0 : traitement anticipé, sortie retardée de ce nombre d'échantillons
                                              // (le plugin dispose d'une période entière ; l'hôte compense ce retard)
        };

        struct Stats
        {
            uint64_t blocksOk = 0, blocksMissed = 0, blocksSilenced = 0;
            int lastSignal = 0;              // POSIX : signal reçu par le worker (SIGSEGV = 11…) ; Windows : 1 si plantage
            uint32_t exitCode = 0;           // Windows : code de sortie du worker
            int restarts = 0;
        };

        explicit PluginSandbox (Options);
        ~PluginSandbox();

        // Thread non temps réel.
        bool start (double sampleRate, uint32_t numChannels);
        void stop();
        bool restart();                      // relance après plantage
        bool waitUntilReady (int timeoutMs);

        // Thread audio : traite sur place. Ne bloque jamais plus que l'échéance.
        // Renvoie false si le bloc a été remplacé par du silence.
        bool process (float* const* channels, uint32_t numChannels, uint32_t numFrames, uint64_t samplePosition,
                      const MidiEvent* midi = nullptr, uint32_t numMidi = 0, bool offline = false) noexcept;

        // Thread non temps réel : commandes envoyées au fil principal du worker.
        bool sendCommand (Command, int timeoutMs = 3000);
        bool saveState (std::vector<uint8_t>& out, int timeoutMs = 3000);
        bool loadState (const std::vector<uint8_t>& data, int timeoutMs = 3000);

        struct Info { std::string name, error; bool isInstrument = false, hasEditor = false, editorVisible = false; };
        Info getInfo() const;

        void setParameter (int index, float value) noexcept;

        // Paramètres du plugin hébergé (VST3), valeurs normalisées 0..1.
        struct ParamDesc { std::string name, label; float defaultValue = 0; int numSteps = 0; uint32_t flags = 0; };
        std::vector<ParamDesc> getPluginParameters() const;              // liste publiée au chargement
        void setPluginParameter (int index, float value) noexcept;       // n'importe quel fil, sans verrou
        float getPluginParameter (int index) const noexcept;             // dernière valeur connue côté plugin
        std::string getPluginParameterText (int index) const;            // texte affiché par le plugin
        // Fil non temps réel : appelle f (index, valeur) pour chaque paramètre changé par le plugin lui-même
        // (sa fenêtre, un préréglage) depuis le dernier appel.
        void collectPluginParameterChanges (const std::function<void (int, float)>& f);

        // Thread audio : transport de l'hôte transmis au plugin (tempo, position, lecture).
        struct Transport
        {
            double bpm = 120.0, ppqPosition = 0.0, ppqBarStart = 0.0, ppqLoopStart = 0.0, ppqLoopEnd = 0.0;
            uint32_t numerator = 4, denominator = 4;
            bool playing = false, looping = false;
        };
        void setTransport (const Transport&) noexcept;
        uint32_t getLatencySamples() const noexcept;          // latence propre du plugin
        uint32_t getPipelineFrames() const noexcept { return pipelineFrames; }

        State getState() const noexcept           { return state.load(); }
        Stats getStats() const;
        std::string describeFailure() const;
        const std::string& getPluginSpec() const   { return options.pluginSpec; }

        // Appelé (hors thread audio) quand l'état change — pour l'interface.
        std::function<void (State)> onStateChanged;

    private:
        bool spawnWorker();
        void supervise (intptr_t process);
        bool createSharedBlock();
        void destroySharedBlock();
        void resetSignals();
        void setState (State);
        void killWorker();

        Options options;
        std::string shmName;
        SharedBlock* block = nullptr;
        intptr_t shmHandle = -1;               // descripteur POSIX ou HANDLE Windows
        void* requestEvent = nullptr;          // Windows uniquement
        void* responseEvent = nullptr;         // Windows uniquement

        std::atomic<intptr_t> workerProcess { -1 };  // pid POSIX ou HANDLE de processus Windows
        std::atomic<State> state { State::stopped };
        std::thread supervisor;

        double sampleRate = 48000.0;
        uint32_t channels = 2;
        int consecutiveMisses = 0;
        double missedSeconds = 0.0;
        bool requestOutstanding = false;
        bool spawnFailed = false;
        std::atomic<bool> inProcess { false };   // le thread audio est dans process()
        void waitForAudioThread();

        // Traitement anticipé (thread audio uniquement).
        bool processPipelined (float* const*, uint32_t, uint32_t, uint64_t, const MidiEvent*, uint32_t, bool) noexcept;
        bool waitForSeq (uint64_t seq, double seconds) noexcept;
        void fillSlot (Slot&, const float* const*, uint32_t nc, uint32_t numFrames, uint64_t pos, const MidiEvent*, uint32_t numMidi, bool offline) noexcept;
        void fifoPush (const float* const* src, uint32_t nc, uint32_t n) noexcept;
        void resetPipeline() noexcept;
        Transport transport;
        uint32_t pipelineFrames = 0;
        std::vector<float> fifo[kMaxChannels];
        uint32_t fifoRead = 0, fifoCount = 0;
        uint64_t posted = 0, collected = 0;
        uint32_t slotFrames[2] { 0, 0 };

        std::atomic<uint64_t> blocksOk { 0 }, blocksMissed { 0 }, blocksSilenced { 0 };
        std::atomic<int> lastSignal { 0 }, restarts { 0 };
        std::atomic<uint32_t> lastExitCode { 0 };  // Windows : code de sortie (0xC0000005 = violation d'accès)
        mutable std::mutex lifecycleLock;
    };

    const char* toString (PluginSandbox::State);
}
