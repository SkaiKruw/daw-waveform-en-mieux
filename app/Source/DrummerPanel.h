// Fenêtre du batteur (MIDI › Batteur…) : style, pavé simple / complexe × doux / fort, swing, jeu humain,
// remplissages, éléments joués, nouvelle variation. Agit sur la région de batteur sélectionnée (ou la première
// de la piste sélectionnée) ; chaque changement regénère le jeu aussitôt et s'annule.
#pragma once
#include "Drummer.h"

class DrummerPanel  : public juce::Component, private juce::Timer
{
public:
    explicit DrummerPanel (EditContext&);
    ~DrummerPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::DialogWindow* show (EditContext&);

    te::MidiClip* region() const;                    // région réglée, nullptr s'il n'y en a pas
    void setStyle (int);                              // aussi pour les tests
    void setPad (float complexity, float intensity);
    class Pad;

private:
    void timerCallback() override;
    void syncFromRegion();
    void commit (const juce::String& what);           // réglages de l'interface -> région (annulable)

    EditContext& ctx;
    drummer::Settings settings;
    te::EditItemID shownRegion;
    juce::OwnedArray<juce::TextButton> styleButtons;
    std::unique_ptr<Pad> pad;
    juce::Slider swing, humanize;
    juce::ComboBox fills;
    juce::ToggleButton kick { "Grosse caisse" }, snare { "Caisse claire" }, cymbals { "Cymbales" }, percussion { "Percussions" };
    juce::TextButton variation { "Nouvelle variation" }, addDrummer { "Ajouter un batteur" };
    juce::TextButton autoBtn { "Automatique" }, manualBtn { "Notes modifiables" };   // mode de la région, changeable à tout moment
    juce::Label info;
};
