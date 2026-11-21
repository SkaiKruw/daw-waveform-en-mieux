// Tablature (zone inférieure, onglet « Tablature ») : le conteneur MIDI sélectionné vu et écrit comme une tablature
// de guitare, de basse ou de ukulélé. Clic sur une corde, chiffres = case, ↑↓ change de corde (même note), ←→
// avance sur la grille, Suppr efface. La corde choisie est retenue pour chaque note ; sinon un doigté automatique
// (la case la plus basse près de la position de la main) est proposé.
#pragma once
#include "EditContext.h"

class TabView  : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit TabView (EditContext&);
    ~TabView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    struct Tuning { const char* name; std::vector<int> strings; };   // cordes de la plus aiguë à la plus grave (MIDI)
    static const std::vector<Tuning>& tunings();
    void setTuning (int index);
    // Écriture (aussi pour les tests) : case « fret » sur la corde « string » (0 = la plus aiguë) au temps « beat ».
    te::MidiNote* writeFret (int string, double beat, int fret);
    // Corde et case d'une note (corde retenue, sinon doigté automatique).
    std::pair<int, int> placeOf (const te::MidiNote&) const;
    juce::String asciiTab() const;                                  // tablature texte (copier-coller)
    void autoFingering();                                           // doigté automatique (position de la main)
    void moveCaret (int string, double beat)                        { caretString = string; caretBeat = beat; repaint(); }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    te::MidiClip* clip() const;
    juce::Rectangle<int> staff() const;
    float beatToX (double) const;
    double xToBeat (float) const;
    int stringY (int s) const;
    double grid() const;
    te::MidiNote* noteAt (int string, double beat) const;
    void audition (int pitch);

    EditContext& ctx;
    juce::ComboBox tuningBox, gridBox;
    juce::TextButton autoBtn { U ("Doigté auto") }, copyBtn { U ("Copier en texte") };
    int tuning = 0, caretString = 0;
    double caretBeat = 0.0, viewBeat = 0.0, pxPerBeat = 80.0;
    juce::String digits;                          // chiffres tapés (case à deux chiffres)
    juce::uint32 lastDigitMs = 0;
    te::MPESourceID sourceID = te::createUniqueMPESourceID();
    static constexpr int toolbarH = 34, rulerH = 20, gutterW = 70, stringGap = 24;
};
