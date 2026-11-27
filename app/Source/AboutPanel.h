// Fenêtre « À propos d'Onde » : logo, version, nouveautés de la version, mises à jour (vérifier, télécharger),
// principes et crédits (bibliothèques et licences).
#pragma once
#include "Theme.h"

class AboutPanel  : public juce::Component
{
public:
    AboutPanel();
    void paint (juce::Graphics&) override;
    void resized() override;
    static void show();
    static constexpr const char* version = "0.7";
    static juce::StringArray whatsNew();             // nouveautés de cette version
    void checkNow();

private:
    juce::TextButton close { "Fermer" }, checkBtn, downloadBtn;
    juce::ToggleButton autoCheck;
    juce::String updateText, updateUrl;
    juce::Colour updateColour;
};
