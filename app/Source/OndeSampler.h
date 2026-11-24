// Onde Échantillonneur : instrument qui joue des sons enregistrés (bibliothèques au format SFZ, le format ouvert des
// banques d'échantillons). Couches de vélocité, variations (round robin, aléatoire), échantillons de relâchement,
// articulations par touches de commutation (keyswitch), boucles, enveloppes, groupes exclusifs, pédale de sustain.
// Les échantillons sont chargés en mémoire en arrière-plan ; le son continue pendant le chargement.
#pragma once
#include "OndeEffects.h"

namespace sfz
{
    struct Region
    {
        juce::File sample;
        int lokey = 0, hikey = 127, keycenter = 60, lovel = 1, hivel = 127, transpose = 0;
        int seqLength = 1, seqPosition = 1, group = 0, offBy = 0;
        int swLast = -1, swLokey = -1, swHikey = -1;      // articulation : dernière touche de commutation jouée
        float lorand = 0.0f, hirand = 1.0f;
        float volume = 0.0f, tune = 0.0f, pan = 0.0f, ampVeltrack = 100.0f;
        float attack = 0.001f, decay = 0.0f, sustain = 100.0f, release = 0.05f, hold = 0.0f;
        bool releaseTrigger = false, oneShot = false, loop = false;
        juce::int64 offset = 0, end = -1, loopStart = 0, loopEnd = -1;
        float pitchKeytrack = 100.0f;
    };

    struct Sample { juce::AudioBuffer<float> data; double rate = 44100.0; };

    struct Instrument
    {
        juce::String name;
        juce::File file;
        std::vector<Region> regions;
        std::map<juce::String, std::shared_ptr<Sample>> samples;   // par chemin complet
        int swDefault = -1, swLo = -1, swHi = -1;                   // touches de commutation (articulations)
        juce::StringArray articulations;                            // noms des articulations (sw_label), par touche
        std::array<juce::String, 128> keyswitchNames;
        juce::String error;
        bool hasNote (int note) const;
    };

    int noteFromText (const juce::String&);                      // « 60 », « c4 », « f#3 », « eb2 »
    // Lit un fichier SFZ (#define, #include, <control> <global> <master> <group> <region>) ; sans charger les sons.
    std::shared_ptr<Instrument> parse (const juce::File&);
    // Charge les échantillons utilisés (formats WAV, FLAC, OGG, AIFF). shouldStop : interruption.
    void loadSamples (Instrument&, std::function<bool()> shouldStop = nullptr);
}

namespace ondefx
{
    class SamplerPlugin  : public OndeEffect, private juce::AsyncUpdater
    {
    public:
        explicit SamplerPlugin (te::PluginCreationInfo);
        ~SamplerPlugin() override;
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Échantillonneur"; }
        juce::String getName() const override        { return juce::String::fromUTF8 ("Onde Échantillonneur"); }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Sampler"; }
        juce::String getSelectableDescription() override { return getName(); }
        bool isSynth() override                      { return true; }
        bool takesMidiInput() override               { return true; }
        bool takesAudioInput() override              { return false; }
        bool producesAudioWhenNoAudioInput() override { return true; }
        int getNumOutputChannelsGivenInputs (int) override { return 2; }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;

        // Instrument : chemin du fichier SFZ (annulable) ; chargé en arrière-plan.
        void loadInstrument (const juce::File& sfzFile);
        juce::File getInstrumentFile() const;
        std::shared_ptr<const sfz::Instrument> getInstrument() const;
        bool isLoading() const                       { return loading.load(); }
        float loadProgress() const                   { return progress.load(); }
        bool waitUntilLoaded (int timeoutMs);        // tests, rendu
        void auditionNote (int note, float velocity);
        int getCurrentKeyswitch() const              { return currentSwitch.load(); }

        struct Voice;
    private:
        void handleAsyncUpdate() override;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void startLoading();
        void noteOn (int note, int velocity);
        void noteOff (int note);
        void startRegion (const sfz::Region&, int note, int velocity, float gainScale);
        void renderVoices (float* l, float* r, int start, int n);

        juce::CachedValue<juce::String> sfzPath;
        double rate = 48000.0;
        mutable juce::CriticalSection instrumentLock;
        std::shared_ptr<sfz::Instrument> instrument, pending;
        std::atomic<bool> loading { false }, stopLoading { false };
        std::atomic<float> progress { 0.0f };
        std::thread loader;
        std::vector<std::unique_ptr<Voice>> voices;
        std::array<int, 128> noteVelocity {}, seqCounter {};
        std::array<bool, 128> noteHeld {};
        std::atomic<int> currentSwitch { -1 };
        bool sustainDown = false;
        float pitchBend = 0.0f, lpState[2] {};
        juce::uint32 voiceCounter = 0;
        juce::Random random;
        juce::AbstractFifo auditionFifo { 64 };
        std::array<std::pair<int, float>, 64> auditionQueue {};
        juce::String loadedPath;
    };

    std::unique_ptr<juce::Component> createSamplerEditor (SamplerPlugin&);
    std::function<void()>& openLibrariesHook();          // « Bibliothèques… » (fourni par la fenêtre principale)
    // Dossier des bibliothèques d'Onde (%APPDATA%\Onde\Bibliothèques) et dossiers ajoutés par l'utilisateur.
    juce::File librariesFolder();
    bool& librariesTestMode();                         // tests : dossier séparé
    juce::Array<juce::File> libraryRoots();
    juce::Array<juce::File> findInstruments (const juce::File& root);   // fichiers .sfz d'un dossier (récursif)
}
