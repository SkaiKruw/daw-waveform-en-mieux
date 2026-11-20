// Effets intégrés d'Onde (code original) : égaliseur paramétrique avec analyseur, compresseur, limiteur.
// Ce sont des plugins Tracktion : paramètres automatisables, enregistrés dans le projet, annulables, et
// fenêtres de réglage dédiées (courbe, analyseur, réduction de gain).
#pragma once
#include <JuceHeader.h>

namespace te = tracktion;

namespace ondefx
{
    // Biquad (forme transposée II), coefficients normalisés (a0 = 1). Aucun calcul alloué en temps réel.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] {}, z2[2] {};
        inline float process (float x, int ch) noexcept
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };

    enum class FilterType { peak, lowShelf, highShelf, highPass, lowPass };
    Biquad design (FilterType, double sampleRate, double freq, double q, double gainDb);
    double magnitudeDb (const Biquad&, double sampleRate, double freq);   // réponse en un point (courbe)

    // Base commune : paramètres déclarés d'une ligne (valeur enregistrée + paramètre automatisable).
    class OndeEffect  : public te::Plugin
    {
    public:
        using te::Plugin::Plugin;
        ~OndeEffect() override;
        int getNumOutputChannelsGivenInputs (int numInputChannels) override  { return juce::jmin (numInputChannels, 2); }
        BusLayout getBusses() const override   { return BusLayout::singleStereoInOut(); }
        juce::String getSelectableDescription() override { return getName(); }

        struct Param { juce::CachedValue<float> value; te::AutomatableParameter::Ptr param; };
        te::AutomatableParameter* param (const juce::String& id) const;
        float value (const juce::String& id) const   { if (auto* p = param (id)) return p->getCurrentValue(); return 0.0f; }

    protected:
        te::AutomatableParameter* declare (const juce::String& id, const juce::String& name, juce::NormalisableRange<float>,
                                           float defaultValue, std::function<juce::String (float)> format);
        juce::OwnedArray<Param> params;
        juce::StringArray ids;
    };

    //==============================================================================
    class EqPlugin  : public OndeEffect
    {
    public:
        explicit EqPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde EQ"; }
        juce::String getName() const override        { return "Onde EQ"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "EQ"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;

        static constexpr int numBands = 7;           // 0 coupe-bas, 1 étagère grave, 2-4 cloches, 5 étagère aiguë, 6 coupe-haut
        static FilterType typeOf (int band);
        static juce::String bandId (int band, const char* what) { return "b" + juce::String (band) + what; }
        // Courbe totale (dB) à une fréquence, pour l'affichage (fil de l'interface).
        double responseDb (double freq) const;
        // Analyseur : échantillons (somme mono, après l'égaliseur) lus par la fenêtre.
        int readAnalyser (float* dest, int max);
        double getRate() const                       { return rate; }

    private:
        void updateFilters (bool force);
        double rate = 48000.0;
        Biquad filters[numBands][2];                 // 2 étages : 24 dB/oct pour les coupe-bas/haut
        float shown[numBands][4] {};                 // réglages des coefficients en cours
        juce::AbstractFifo fifo { 8192 };
        std::vector<float> fifoData = std::vector<float> (8192);
    };

    //==============================================================================
    class CompPlugin  : public OndeEffect
    {
    public:
        explicit CompPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Comp"; }
        juce::String getName() const override        { return "Onde Comp"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Comp"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override                        { env = 0; grDb = 0; sc.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        // Courbe statique (sortie en fonction de l'entrée, dB) pour l'affichage.
        float transferDb (float inDb) const;
        std::atomic<float> gainReductionDb { 0 }, inputDb { -100 }, outputDb { -100 };

    private:
        double rate = 48000.0;
        float env = 0, grDb = 0, scFreq = -1;
        Biquad sc;
    };

    //==============================================================================
    class LimiterPlugin  : public OndeEffect
    {
    public:
        explicit LimiterPlugin (te::PluginCreationInfo);
        static const char* xmlTypeName;
        static const char* getPluginName()           { return "Onde Limiteur"; }
        juce::String getName() const override        { return "Onde Limiteur"; }
        juce::String getPluginType() override        { return xmlTypeName; }
        juce::String getShortName (int) override     { return "Limit"; }
        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override {}
        void reset() override;
        double getLatencySeconds() override          { return lookahead / rate; }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::atomic<float> gainReductionDb { 0 }, outputDb { -100 };
        static constexpr double lookaheadMs = 1.5;

    private:
        double rate = 48000.0;
        int lookahead = 72, pos = 0;
        std::vector<float> delay[2], peaks;
        float gain = 1.0f;
    };

    // Enregistrement des types auprès du moteur, et fenêtre de réglage dédiée (nullptr si pas un effet Onde).
    void registerTypes (te::Engine&);
    std::unique_ptr<juce::Component> createEditor (te::Plugin&);
}
