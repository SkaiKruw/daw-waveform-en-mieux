// Bibliothèque de sons (MIDI › Bibliothèque de sons, Ctrl+Maj+B) : les préréglages d'Onde Synth rangés par
// famille. Un clic pose le son sur la piste sélectionnée (ou crée une piste) et le fait entendre aussitôt.
#pragma once
#include "EditContext.h"

namespace ondefx { struct SynthPreset; class SynthPlugin; }

// Instruments réels : sons enregistrés (bibliothèques libres, Onde Échantillonneur) présentés comme des instruments.
namespace realinstruments
{
    struct Entry { const char* name; const char* family; const char* library; const char* file; float roomSize, roomMix; const char* description; };
    const std::vector<Entry>& catalog();
    juce::File fileFor (const Entry&);
    bool available (const Entry&);                       // bibliothèque installée ?
    const Entry* find (const juce::String& name);
    // Pose l'instrument sur la piste (ou une nouvelle piste), avec sa salle (Onde Espace). Annulable.
    te::AudioTrack* applyToTrack (EditContext&, te::AudioTrack* track, const Entry&);
}

class LibraryPanel  : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit LibraryPanel (EditContext&);
    ~LibraryPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::DialogWindow* show (EditContext&);

    // Pose un son sur une piste : son Onde Synth s'il en a un, sinon un Onde Synth remplace son instrument.
    // Sans piste instrument sélectionnée : nouvelle piste. Annulable. Renvoie la piste.
    static te::AudioTrack* applyToTrack (EditContext&, te::AudioTrack* track, const ondefx::SynthPreset&);
    void selectCategory (int);
    void choosePreset (int row, bool newTrack);
    int numPresetsShown() const    { return isReal() ? shownReal.size() : shown.size(); }
    bool isReal() const            { return category == 0; }

private:
    int getNumRows() override      { return numPresetsShown(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override       { choosePreset (row, false); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choosePreset (row, true); }
    void timerCallback() override;
    void preview (te::AudioTrack&, bool drums);

    EditContext& ctx;
    juce::OwnedArray<juce::TextButton> categoryButtons;
    juce::StringArray categories;
    int category = 0;
    juce::Array<const ondefx::SynthPreset*> shown;
    juce::Array<const realinstruments::Entry*> shownReal;
    te::Plugin::Ptr waitingSampler;                       // aperçu dès que les échantillons sont chargés
    juce::ListBox list;
    juce::TextButton newTrackButton, previewButton { juce::String::fromUTF8 ("Écouter") };
    juce::Label status;
    te::MPESourceID sourceID = te::createUniqueMPESourceID();
    juce::Array<std::pair<double, juce::MidiMessage>> pending;    // aperçu en cours : (instant, message)
    double previewStart = 0;
    te::EditItemID previewTrack;
};
