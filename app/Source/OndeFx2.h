// Effets Onde, deuxième série (code original) : saturation, écho (délai calé sur le tempo), espace (réverbération).
#pragma once
#include "OndeEffects.h"

namespace ondefx
{
    //==============================================================================
    // Saturation : quatre caractères (douce, lampe, bande, fuzz), suréchantillonnage x2 contre le repliement,
    // tonalité après la saturation, mélange parallèle.
    class SaturationPlugin  : public OndeEffect
    {
    public:
        explicit SaturationPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Saturation"; }
        juce::String getName() const override        { return "Onde Saturation"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Satur"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        double getLatencySeconds() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        static float shape (int character, float x) noexcept;     // courbe de transfert (aussi pour l'affichage)
        static constexpr const char* characterNames[4] = { "Douce", "Lampe", "Bande", "Fuzz" };
        std::atomic<float> inputDb { -100.0f }, outputDb { -100.0f };       // crêtes du dernier bloc (fenêtre)
    private:
        double rate = 48000.0;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        float dc[2] {}, dcIn[2] {}, tiltLP[2] {}, tapeLP[2] {};
        int preparedBlock = 0;
        std::vector<float> dryLine[2], dryScratch[2];   // signal direct retardé de la latence du suréchantillonnage
        int dryPos = 0;
    };

    //==============================================================================
    // Écho : durée en valeurs de note (suit le tempo) ou en millisecondes, réinjection filtrée (passe-haut,
    // passe-bas), ping-pong, glissement doux quand la durée change.
    class EchoPlugin  : public OndeEffect
    {
    public:
        explicit EchoPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Écho"; }
        juce::String getName() const override        { return juce::String::fromUTF8 ("Onde Écho"); }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Echo"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        double currentDelaySeconds (double bpm) const;
        static constexpr const char* divisionNames[8] = { "1/2", "1/4", "1/4 pointée", "1/4 triolet", "1/8", "1/8 pointée", "1/8 triolet", "1/16" };
        static constexpr double divisionBeats[8] = { 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25 };
    private:
        double rate = 48000.0;
        std::vector<float> line[2];
        int writePos = 0;
        float smoothedDelay = -1;
        float hp[2] {}, lp[2] {};
    };

    //==============================================================================
    // Modulation : chorus, ensemble (3 voix, effet « cordes »), flanger, phaser 6 étages, vibrato. Oscillateurs décalés
    // entre gauche et droite (largeur), réinjection pour le flanger et le phaser.
    class ModulationPlugin  : public OndeEffect
    {
    public:
        explicit ModulationPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Modulation"; }
        juce::String getName() const override        { return "Onde Modulation"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Modul."; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        static constexpr const char* modeNames[5] = { "Chorus", "Ensemble", "Flanger", "Phaser", "Vibrato" };
        std::atomic<float> lfoPhase { 0 };       // pour l'affichage
        // Retard (ms) ou fréquence du phaser (Hz) d'une voix, pour une phase d'oscillateur donnée (affichage et traitement).
        float delayMsAt (int mode, double phase) const;
    private:
        double rate = 48000.0, phase = 0, phase2 = 0;
        std::vector<float> line[2];
        int writePos = 0, mask = 0;
        float fbState[2] {}, ap[2][6] {}, wetLP[2] {};
    };

    //==============================================================================
    // Espace : réverbération à réseau de 8 lignes à retard (matrice de Hadamard), lignes légèrement modulées
    // (queue sans « métal »), diffusion d'entrée, amortissement des aigus, pré-délai, temps de réverbération réglable.
    class SpacePlugin  : public OndeEffect
    {
    public:
        explicit SpacePlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Espace"; }
        juce::String getName() const override        { return "Onde Espace"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Espace"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        static constexpr int numLines = 8;
    private:
        double rate = 48000.0;
        std::vector<float> lines[numLines];
        int lineLen[numLines] {}, pos[numLines] {};
        float damp[numLines] {};
        std::vector<float> pre;
        int prePos = 0;
        struct Allpass { std::vector<float> buf; int pos = 0; float g = 0.6f; };
        Allpass diffusers[4];
        double lfoPhase = 0;
        float lowCut[2] {};
    };

    // Fenêtre simple (boutons rotatifs) pour ces effets ; nullptr si le plugin n'en est pas un.
    std::unique_ptr<juce::Component> createFx2Editor (te::Plugin&);
}

namespace ondefx
{
    //==============================================================================
    // Analyse du mix (posée sur la sortie du projet) : sonie EBU R128 (momentanée, court terme, intégrée), crête vraie,
    // corrélation de phase. Écoute : « Mono » et « Atténuer » (-20 dB) n'agissent qu'à l'écoute, jamais à l'export.
    class MasterMeterPlugin  : public OndeEffect
    {
    public:
        explicit MasterMeterPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Analyse"; }
        juce::String getName() const override        { return "Onde Analyse"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Analyse"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        void resetMeasurements()                     { resetRequested = true; }
        std::atomic<bool> monoListen { false }, dimListen { false };   // aides d'écoute, non enregistrées, jamais à l'export

        std::atomic<float> momentary { -100 }, shortTerm { -100 }, integrated { -100 }, truePeakMax { -100 }, correlation { 1.0f };
        static MasterMeterPlugin* findOrCreate (te::Edit&);   // une seule par projet, sur la sortie

    private:
        double rate = 48000.0;
        TruePeakDetector tp;
        struct BiquadD { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
                         double process (double x, int c) noexcept { const double y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return y; }
                         void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; } } shelf, high;
        double hopEnergy = 0, hops[30] {}, binEnergy[800] {};
        int hopCount = 0, hopLen = 4800, hopPos = 0, hopsFilled = 0, binCount[800] {};
        double sLL = 0, sRR = 0, sLR = 0;
        float tpMax = 0;
        std::atomic<bool> resetRequested { false };
        void clear();
    };
}

namespace ondefx
{
    //==============================================================================
    // Ampli : simulation d'ampli guitare / basse (code original). Porte de bruit, pédale de boost (bosse dans les
    // médiums, grave resserré), préampli à 1 à 4 étages à lampe suréchantillonné x4, égaliseur (graves, médiums,
    // aigus, présence), étage de puissance, baffle simulé (résonance du haut-parleur, coupure des aigus selon la
    // position du micro). Cinq modèles : clair, crunch, lead, moderne (très saturé, serré), basse.
    class AmpPlugin  : public OndeEffect
    {
    public:
        explicit AmpPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Ampli"; }
        juce::String getName() const override        { return "Onde Ampli"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Ampli"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        double getLatencySeconds() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        static constexpr const char* modelNames[5] = { "Clair", "Crunch", "Lead", "Moderne", "Basse" };
        static constexpr const char* cabNames[5] = { "Sans baffle", "1x12 ouvert", "2x12", "4x12 fermé", "Basse 8x10" };
        static constexpr const char* micNames[3] = { "Dynamique", "Condensateur", "Ruban" };
        float responseDb (float hz) const;           // égaliseur + baffle (affichage)
        std::atomic<float> gateOpen { 1.0f }, inputDb { -100.0f };

        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
            inline float process (float x, int c) noexcept { const float y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return y; }
            void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
            float magnitudeDb (double hz, double fs) const;
            void set (int type, double f, double q, double gainDb, double fs);   // 0 passe-bas 1 passe-haut 2 cloche 3 plateau grave 4 plateau aigu
        };
    private:
        void updateFilters();
        double rate = 48000.0;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        int preparedBlock = 0;
        Biquad tone[4], cab[5], boostMid, inputHP, tight, micEq[3];
        std::vector<float> roomLine[2];
        int roomPos = 0, roomMask = 0;
        float sagEnv[2] {}, roomLP[2] {};
        float stageHP[4][2] {}, stageHPx[4][2] {}, stageLP[4][2] {}, gateEnv = 0, gateGain = 1, lowKeep[2] {};
        juce::String filterKey;
    };

    std::unique_ptr<juce::Component> createAmpEditor (AmpPlugin&);
}
