// Générateur de QR code (ISO/IEC 18004) minimal : mode octets, correction d'erreur M, versions 1 à 6
// (jusqu'à 106 octets, largement assez pour une adresse de télécommande). Algorithme d'après la
// description de référence de Project Nayuki (domaine public / MIT).
#pragma once
#include <JuceHeader.h>

class QrCode
{
public:
    // Renvoie un code vide (size() == 0) si le texte est trop long.
    static QrCode encode (const juce::String& utf8Text);

    int size() const noexcept                        { return n; }
    int version() const noexcept                     { return ver; }
    bool dark (int x, int y) const noexcept          { return x >= 0 && y >= 0 && x < n && y < n && modules[(size_t) (y * n + x)]; }

    // Dessine le code dans area (carré), avec la marge blanche réglementaire de 4 modules.
    void draw (juce::Graphics&, juce::Rectangle<float> area) const;

private:
    int n = 0, ver = 0;
    std::vector<bool> modules, isFunction;

    void setFunction (int x, int y, bool d);
    void drawFunctionPatterns();
    void drawFormatBits (int mask);
    void drawCodewords (const std::vector<uint8_t>&);
    void applyMask (int mask);
    long penalty() const;
};
