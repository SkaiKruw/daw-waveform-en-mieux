// Onde Pédalier : un pédalier de guitare comme en vrai. 8 emplacements, 16 pédales (créations d'Onde) à choisir et à
// ranger dans l'ordre voulu (glisser-déposer), interrupteur au pied pour chacune, 4 boutons par pédale. Le signal
// passe de la première à la dernière, puis vers l'ampli (Onde Ampli ou un VST).
#pragma once
#include "OndeEffects.h"

namespace ondefx
{
    namespace pedals
    {
        enum Type { empty = 0, gate, comp, overdrive, distortion, fuzz, octave, autowah, chorus, phaser, flanger,
                    tremolo, delay, spring, eq, boost, volume, numTypes };
        struct Info { const char* name; const char* family; juce::uint32 colour; int numKnobs; const char* knobs[4]; float defaults[4]; };
        const Info& info (int type);
        juce::String knobText (int type, int knob, float v);   // texte d'un bouton (v entre 0 et 1)
    }

    class PedalboardPlugin  : public OndeEffect
    {
    public:
        static constexpr int numSlots = 8;
        explicit PedalboardPlugin (te::PluginCreationInfo);
        ~PedalboardPlugin() override;
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Pedalier"; }
        juce::String getName() const override        { return juce::String::fromUTF8 ("Onde Pédalier"); }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return juce::String::fromUTF8 ("Pédales"); }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;

        int slotType (int slot) const;
        bool slotOn (int slot) const;
        float slotKnob (int slot, int knob) const;
        static juce::String paramId (int slot, const char* what);   // « s3type », « s3on », « s3k1 »…
        // Pose une pédale (annulable : transaction de l'appelant) ; type 0 = emplacement vide.
        void setPedal (int slot, int type);
        void moveSlot (int from, int to);                            // déplace une pédale (les autres glissent)
        void applyPreset (const std::vector<std::pair<int, std::array<float, 4>>>& chain);
        std::atomic<float> inputDb { -100.0f }, outputDb { -100.0f };

        struct State;
    private:
        double rate = 48000.0;
        std::vector<std::unique_ptr<State>> states;
        std::array<int, numSlots> lastType {};
    };

    std::unique_ptr<juce::Component> createPedalboardEditor (PedalboardPlugin&);
    bool applyPedalboardPreset (PedalboardPlugin&, const juce::String& name);   // « Rock classique », « Métal serré »…
}
