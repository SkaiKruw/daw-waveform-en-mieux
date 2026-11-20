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
