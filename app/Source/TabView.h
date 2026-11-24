// Tablature (zone inférieure, onglet « Tablature ») : le conteneur MIDI sélectionné vu et écrit comme une tablature
// (guitares, basses, ukulélé, mandoline, banjo, violon…), avec au-dessus la portée dans la clé de l'instrument
// (sol octaviée pour la guitare, fa pour la basse, ut pour l'alto…). Clic sur une corde, chiffres = case, ↑↓ change
// de corde (même note), ←→ avance sur la grille, Suppr efface. L'accordage est libre (Accorder…) et retenu pour
// chaque piste ; la corde choisie est retenue pour chaque note, sinon un doigté automatique est proposé.
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

    // Clés de la portée : sol octaviée (guitare : on écrit une octave plus haut que le son), sol, fa octaviée
    // (basse), ut 3e ligne (alto), fa (violoncelle).
    enum Clef { trebleOctave = 0, treble, bassOctave, alto, bass, numClefs };
    static juce::String clefName (int);
    struct Tuning { const char* name; std::vector<int> strings; int clef; };   // cordes de la plus aiguë à la plus grave (MIDI)
    static const std::vector<Tuning>& tunings();
    void setTuning (int index);
    // Accordage libre (de 1 à 10 cordes, de la plus aiguë à la plus grave) ; retenu pour la piste du conteneur.
    void setCustomTuning (std::vector<int> strings, int clef, const juce::String& name = {});
    const std::vector<int>& getStrings() const   { return strings; }
    int getClef() const                          { return clef; }
    void showTuningEditor();
    // Écriture (aussi pour les tests) : case « fret » sur la corde « string » (0 = la plus aiguë) au temps « beat ».
    te::MidiNote* writeFret (int string, double beat, int fret);
    // Corde et case d'une note (corde retenue, sinon doigté automatique).
    std::pair<int, int> placeOf (const te::MidiNote&) const;
    juce::String asciiTab() const;                                  // tablature texte (copier-coller)
    void autoFingering();                                           // doigté automatique (position de la main)
    void moveCaret (int string, double beat)                        { caretString = string; caretBeat = beat; repaint(); }
    // Position verticale d'une note sur la portée (en demi-interlignes au-dessus de la ligne du bas) : tests.
    static int staffStep (int pitch, int clef);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    te::MidiClip* clip() const;
    juce::Rectangle<int> staff() const;
    juce::Rectangle<int> notation() const;
    float beatToX (double) const;
    double xToBeat (float) const;
    int stringY (int s) const;
    double grid() const;
    te::MidiNote* noteAt (int string, double beat) const;
    void audition (int pitch);
    void paintNotation (juce::Graphics&, te::MidiClip*);
    void saveTuningToTrack();
    void loadTuningFromTrack();

    EditContext& ctx;
    juce::ComboBox tuningBox, gridBox;
    juce::TextButton tuneBtn { U ("Accorder…") }, autoBtn { U ("Doigté auto") }, copyBtn { U ("Copier en texte") };
    std::vector<int> strings { 64, 59, 55, 50, 45, 40 };
    juce::String tuningName;
    int tuning = 0, clef = trebleOctave, caretString = 0;
    te::EditItemID shownTrack;
    double caretBeat = 0.0, viewBeat = 0.0, pxPerBeat = 80.0;
    juce::String digits;                          // chiffres tapés (case à deux chiffres)
    juce::uint32 lastDigitMs = 0;
    te::MPESourceID sourceID = te::createUniqueMPESourceID();
    static constexpr int toolbarH = 34, rulerH = 20, gutterW = 70, stringGap = 24, notationH = 112, lineGap = 9;
};
