// Onde Synth : instrument intégré d'Onde (code et sons originaux), quatre moteurs de synthèse :
//   Analogique (oscillateurs, filtre 24 dB, unisson) · FM (pianos électriques, cloches) ·
//   Piano (partiels inharmoniques, deux cordes, marteau) · Batterie (sons synthétisés, notes General MIDI).
// Les préréglages (bibliothèque) ne sont que des jeux de valeurs de paramètres : rien à télécharger.
#pragma once
#include "OndeEffects.h"

namespace ondefx
{
    struct SynthPreset
    {
        const char* category;
        const char* name;
        std::vector<std::pair<const char*, float>> values;    // paramètres modifiés (le reste : valeur par défaut)
    };

    class SynthPlugin  : public OndeEffect
    {
    public:
        explicit SynthPlugin (te::PluginCreationInfo);
        ~SynthPlugin() override;
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Synth"; }
        juce::String getName() const override        { return "Onde Synth"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Synth"; }
        juce::String getSelectableDescription() override { return "Onde Synth"; }
        bool isSynth() override                      { return true; }
        bool takesMidiInput() override               { return true; }
        bool takesAudioInput() override              { return false; }
        bool producesAudioWhenNoAudioInput() override { return true; }
        int getNumOutputChannelsGivenInputs (int) override { return 2; }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;

        // Bibliothèque de préréglages.
        static const std::vector<SynthPreset>& presets();
        static juce::StringArray categories();
        static const SynthPreset* findPreset (const juce::String& name);
        void applyPreset (const SynthPreset&);                      // annulable (transaction de l'appelant)
        juce::String getPresetName() const                          { return presetName.get(); }
        static constexpr const char* engineNames[4] = { "Analogique", "FM", "Piano", "Batterie" };

        struct Voice;
    private:
        juce::CachedValue<juce::String> presetName;
        double rate = 48000.0;
        std::vector<std::unique_ptr<Voice>> voices;
        bool sustainDown = false;
        float pitchBend = 0, modWheel = 0;
        double vibPhase = 0;
        // Contrôleurs MIDI : volume (CC7), expression (CC11), panoramique (CC10), pression (aftertouch).
        float ccVolume = 1.0f, ccExpression = 1.0f, ccPan = 0.0f, pressure = 0.0f, ccGain = 1.0f;
        uint32_t voiceCounter = 0;
        juce::Random random;
        void noteOn (int note, float velocity);
        void noteOff (int note);
        void renderVoices (float* l, float* r, int start, int n);
    };

    std::unique_ptr<juce::Component> createSynthEditor (SynthPlugin&);
}
