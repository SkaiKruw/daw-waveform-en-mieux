// Fenêtre « À propos d'Onde » : logo, nom, version, principes et crédits.
#pragma once
#include "Theme.h"

class AboutPanel  : public juce::Component
{
public:
    AboutPanel();
    void paint (juce::Graphics&) override;
    void resized() override;
    static void show();
    static constexpr const char* version = "0.5";

private:
    juce::TextButton close { "Fermer" };
};
