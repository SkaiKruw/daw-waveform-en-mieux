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
        // Nouveaux coefficients sans perdre l'état (pas de clic quand un réglage bouge).
        void setCoefficients (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    };

    enum class FilterType { peak, lowShelf, highShelf, highPass, lowPass, bandPass };

    // Crête vraie (inter-échantillons, ITU-R BS.1770-4) : suréchantillonnage x4 par filtre polyphase
    // (sinus cardinal fenêtré, 12 points par phase). Retard ≈ 5 échantillons.
    struct TruePeakDetector
    {
        static constexpr int taps = 12, phases = 4, delaySamples = 7;
        TruePeakDetector();
        float process (float x, int ch) noexcept;    // plus grande valeur absolue reconstruite à ce pas
        void reset() noexcept;
    private:
        float coef[phases][taps] {};
        float hist[2][taps] {};
    };
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
        static bool isCut (int band)                 { return band == 0 || band == numBands - 1; }
        static juce::String bandId (int band, const char* what) { return "b" + juce::String (band) + what; }
        // Courbe totale (dB) à une fréquence, pour l'affichage (fil de l'interface) ; dynamique = gain actuel.
        double responseDb (double freq, bool withDynamics = true) const;
        // Analyseur : échantillons (somme mono, après l'égaliseur) lus par la fenêtre.
        int readAnalyser (float* dest, int max);
        double getRate() const                       { return rate; }
        // Gain dynamique en cours de chaque bande (dB ajoutés au gain réglé), pour l'affichage.
        std::atomic<float> dynamicDb[numBands] {};
        static constexpr int maxStages = 4;          // coupe-bas/haut jusqu'à 48 dB/oct (4 étages)
        static int stagesForSlope (int slopeIndex)   { return slopeIndex <= 0 ? 1 : slopeIndex == 1 ? 2 : 4; }
        static const double* cutQs (int slopeIndex); // Q des étages de Butterworth (12, 24 ou 48 dB/oct)
        static constexpr const char* modeNames[5]  = { "Stéréo", "Gauche", "Droite", "Mid", "Side" };
        static constexpr const char* slopeNames[3] = { "12 dB/oct", "24 dB/oct", "48 dB/oct" };

    private:
        void designBand (int band, float freq, float q, float gainDb, int stages);
        void runBand (int band, int stages, int mode, float* const* d, int chans, int start, int len) noexcept;
        double rate = 48000.0;
        Biquad filters[numBands][maxStages];
        Biquad detector[numBands];                   // passe-bande de détection des bandes dynamiques
        float shown[numBands][6] {};                 // réglages des coefficients en cours
        float envDb[numBands] {};                    // niveau détecté (dB) par bande
        float dynAmount[numBands] {};                // 0..1 : part de la plage dynamique appliquée
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
        void reset() override;
        double getLatencySeconds() override          { return value ("lookahead") > 0.5f ? lookaheadMs / 1000.0 : 0.0; }
        void applyToBuffer (const te::PluginRenderContext&) override;
        // Courbe statique (sortie en fonction de l'entrée, dB) pour l'affichage.
        float transferDb (float inDb) const;
        float gainComputer (float inDb, float knee) const;          // réduction voulue (dB, ≥ 0)
        std::atomic<float> gainReductionDb { 0 }, inputDb { -100 }, outputDb { -100 };
        static constexpr double lookaheadMs = 5.0;
        static constexpr const char* styleNames[3] = { "Propre", "Opto", "Vintage" };

    private:
        double rate = 48000.0;
        float env[2] {}, rms[2] {}, grFast[2] {}, grSlow[2] {}, scFreq = -1;
        Biquad sc;
        std::vector<float> delay[2];
        int delayPos = 0, delayLen = 0;
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
        std::atomic<float> gainReductionDb { 0 }, outputDb { -100 }, truePeakDb { -100 }, truePeakMaxDb { -100 };
        // Sonie ITU-R BS.1770 / EBU R128 (LUFS) de la sortie : momentanée (400 ms), court terme (3 s), intégrée.
        std::atomic<float> momentaryLufs { -100 }, shortTermLufs { -100 }, integratedLufs { -100 };
        void resetLoudness()                         { resetRequested = true; }
        static constexpr double lookaheadMs = 1.5;
        static constexpr float targets[3] = { -14.0f, -16.0f, -23.0f };   // plateformes, podcast, diffusion (EBU R128)
        float targetLufs() const                     { return targets[juce::jlimit (0, 2, juce::roundToInt (value ("target")))]; }

    private:
        void measureLoudness (const float* l, const float* r, int n);
        double rate = 48000.0;
        int lookahead = 72, pos = 0;
        std::vector<float> delay[2], peaks;
        float gain = 1.0f;
        TruePeakDetector tpIn, tpOut;
        // Sonie : filtre K (étagère + passe-haut, en double précision), énergie par tranches de 100 ms.
        struct BiquadD
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
            inline double process (double x, int ch) noexcept
            {
                const double y = b0 * x + z1[ch];
                z1[ch] = b1 * x - a1 * y + z2[ch];
                z2[ch] = b2 * x - a2 * y;
                return y;
            }
            void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
        };
        BiquadD kShelf, kHigh;
        double hopEnergy = 0; int hopCount = 0, hopLen = 4800;
        double hops[30] {};                          // 30 dernières tranches (3 s)
        int hopPos = 0, hopsFilled = 0;
        // Intégrée : histogramme des blocs de 400 ms (pas de 0,1 LU de -70 à +10), sans allocation.
        static constexpr int numBins = 800;
        double binEnergy[numBins] {};
        int binCount[numBins] {};
        float tpMax = 0;
        std::atomic<bool> resetRequested { false };
        void clearLoudness();
    };

    // Enregistrement des types auprès du moteur, et fenêtre de réglage dédiée (nullptr si pas un effet Onde).
    void registerTypes (te::Engine&);
    std::unique_ptr<juce::Component> createEditor (te::Plugin&);
}
