// Couleurs et polices d'Onde : thème « studio » inspiré des stations de travail pro (gris anthracite, accents bleus).
#pragma once
#include <JuceHeader.h>

namespace theme
{
    // Fonds
    inline const juce::Colour bg       { 0xff1e2024 };   // zone des événements
    inline const juce::Colour bgAlt    { 0xff232529 };   // pistes paires
    inline const juce::Colour panel    { 0xff2c2f34 };   // barres d'outils, inspecteur
    inline const juce::Colour panel2   { 0xff383c42 };   // boutons, champs
    inline const juce::Colour panel3   { 0xff454a51 };   // survol, piste sélectionnée
    inline const juce::Colour header   { 0xff33363b };   // en-têtes de piste
    inline const juce::Colour line     { 0xff141518 };   // séparateurs sombres
    inline const juce::Colour lineSoft { 0xff2a2d31 };   // lignes de grille
    inline const juce::Colour lineBar  { 0xff3a3e44 };   // lignes de mesure
    inline const juce::Colour display  { 0xff101215 };   // afficheurs (compteur)

    // Textes
    inline const juce::Colour text     { 0xffdfe2e6 };
    inline const juce::Colour muted    { 0xff8e949c };
    inline const juce::Colour dim      { 0xff5d636b };

    // Accents
    inline const juce::Colour accent   { 0xff3fa4ff };   // sélection, cycle, éléments actifs
    inline const juce::Colour accentBg { 0xff1d3a57 };
    inline const juce::Colour cycle    { 0xff2f7fd6 };
    inline const juce::Colour play     { 0xff3ccf6a };
    inline const juce::Colour rec      { 0xffe8473f };
    inline const juce::Colour mute     { 0xffe8b52c };
    inline const juce::Colour solo     { 0xffe8473f };
    inline const juce::Colour monitor  { 0xfff08c2e };
    inline const juce::Colour readA    { 0xff4fc06b };
    inline const juce::Colour writeA   { 0xffe8473f };
    inline const juce::Colour cursor   { 0xffeef1f4 };
    inline const juce::Colour ok       { 0xff4fc06b };
    inline const juce::Colour warn     { 0xfff5a524 };
    inline const juce::Colour lcd      { 0xffa9e3ff };   // chiffres des afficheurs

    inline juce::Font ui (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
    inline juce::Font mono (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Cadre biseauté discret façon matériel de studio.
    inline void bevel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float radius = 3.0f)
    {
        g.setColour (fill); g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
        g.setColour (line); g.drawRoundedRectangle (r, radius, 1.0f);
    }
}

// Logo d'Onde (Resources/logo.svg, embarqué) : dessiné vectoriellement à n'importe quelle taille.
namespace theme
{
    std::unique_ptr<juce::Drawable> makeLogo();
    void drawLogo (juce::Graphics&, juce::Rectangle<float> area);
}
