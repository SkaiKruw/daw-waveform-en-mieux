// Historique des modifications (Édition › Historique des modifications, ou Ctrl+K) : toutes les actions du projet, de la plus
// ancienne à la plus récente, celles annulées en grisé ; un clic ramène le projet à ce point (annuler ou
// rétablir autant de fois que nécessaire).
#pragma once
#include "EditContext.h"

class HistoryPanel  : public juce::Component, private juce::ListBoxModel, private juce::Timer, private juce::ChangeListener
{
public:
    explicit HistoryPanel (EditContext&);
    ~HistoryPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::DialogWindow* show (EditContext&);

    // Ramène le projet à l'état qui suit l'entrée « index » (0 = état initial).
    void goTo (int index);
    int numEntries() const     { return entries.size(); }
    int currentIndex() const   { return current; }

private:
    int getNumRows() override  { return entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { goTo (row); }
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void attach();
    void refresh();

    EditContext& ctx;
    te::Edit* attached = nullptr;
    juce::StringArray entries;       // [0] état initial, puis actions faites, puis actions annulées
    int current = 0;                 // index de l'état actuel
    juce::ListBox list;
};
