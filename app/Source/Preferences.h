// Paramètres d'Onde (Fichier › Paramètres…, Ctrl+,) : réglages de l'application, gardés d'une session à l'autre
// dans %APPDATA%\Onde\parametres.xml. Onglets : Général, Audio et MIDI, Interface, Plugins, Modèles.
#pragma once
#include "EditContext.h"

class MainComponent;

namespace prefs
{
    juce::PropertiesFile& file();
    double newProjectBpm();                 // tempo des nouveaux projets
    int autosaveSeconds();                  // 0 : sauvegarde automatique désactivée
    juce::File projectsFolder();            // dossier proposé pour enregistrer et ouvrir
    float uiScale();                        // taille de l'interface (1 = 100 %)
    void applyUiScale();
    bool monoInputs();                      // une entrée par prise de la carte son (sinon paires stéréo)
    // Métronome : son (fabriqué par Onde), réglages par défaut des nouveaux projets.
    juce::StringArray clickSoundNames();
    void applyClickSound (te::Engine&);
    void applyMetronomeDefaults (te::Edit&);
}

class SettingsPanel  : public juce::Component
{
public:
    explicit SettingsPanel (MainComponent&);
    ~SettingsPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::DialogWindow* show (MainComponent&, int tab = 0);
    void showTab (int);
    int currentTab() const { return tab; }
    static juce::StringArray tabNames();
    static constexpr int metronomeTab = 5;

    class Page;
private:
    MainComponent& main;
    juce::OwnedArray<juce::Component> pages;
    juce::Rectangle<int> nav;
    int tab = 0, hover = -1;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint (nav); }
    juce::Rectangle<int> navItem (int) const;
};
