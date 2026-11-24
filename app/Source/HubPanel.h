// Écran d'accueil (« Hub ») : modèles de projet, projets récents, ouvrir. Affiché au démarrage
// (désactivable) et par Fichier › Écran d'accueil (Ctrl+Maj+H).
#pragma once
#include "Theme.h"

class MainComponent;

class HubPanel  : public juce::Component, private juce::ListBoxModel
{
public:
    explicit HubPanel (MainComponent&);
    ~HubPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::DialogWindow* show (MainComponent&);

    static bool showAtStartup();
    static void setShowAtStartup (bool);

    class Card;
    int numRecent() const { return recent.size(); }

private:
    int getNumRows() override { return recent.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { openRecent (row); }
    void returnKeyPressed (int row) override { openRecent (row); }
    void openRecent (int row);
    void closeHub();
    void showTemplatesMenu();

    MainComponent& main;
    juce::OwnedArray<Card> cards;
    juce::StringArray recent;
    juce::ListBox list;
    juce::TextButton openBtn, closeBtn;
    juce::ToggleButton startup;
    juce::Rectangle<int> side, recentTitle, newTitle;
};
