// Éditeur de notes (zone inférieure) : édite le conteneur MIDI sélectionné.
#pragma once
#include "EditContext.h"
#include "EditWatcher.h"

class PianoRollView  : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit PianoRollView (EditContext&);
    ~PianoRollView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void quantise();
    bool deleteSelectedNotes();                     // renvoie false s'il n'y avait aucune note sélectionnée
    void transposeSelected (int semitones);
    int getNumSelected() const { return selected.size(); }

    static constexpr int toolbarH = 34, rulerH = 20, keysW = 70, velH = 110, defaultRowH = 13;

    // Couloir du bas : vélocité (-1) ou type de contrôleur Tracktion (0-127 = CC, ou pitch bend, pression…).
    void setLane (int type);
    int getLane() const { return laneType; }
    static juce::String laneName (int type);
    // Dessine une rampe de contrôleur entre deux temps (début inclus), valeurs 0..1 (aussi pour les tests).
    void drawControllerRamp (double fromBeat, double toBeat, float fromValue, float toValue);
private:
    void timerCallback() override;
    float cursorX() const;
    EditWatcher watcher;
    float lastCursorX = -1.0f;
    double lastViewBeat = -1.0, lastPxPerBeat = -1.0;
    int tick = 0;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velArea() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    int yToPitch (int y) const;
    int pitchToY (int pitch) const;
    te::MidiNote* noteAt (juce::Point<int>, bool& onRightEdge) const;
    double gridBeats() const;
    double snapBeat (double b, const juce::ModifierKeys&) const;
    bool inScale (int pitch) const;
    void frameNotes();
    void audition (int pitch, int velocity);
    void validateSelection();

    EditContext& ctx;
    juce::ComboBox gridBox, rootBox, scaleBox;
    juce::ToggleButton scaleToggle { U ("Assistant de gamme") };
    juce::TextButton quantBtn { "Quantifier" }, fitBtn { U ("Tout voir") };
    int topPitch = 84, rowH = defaultRowH;
    double viewBeat = 0.0, pxPerBeat = 60.0;
    te::EditItemID framedClip;
    juce::Array<te::MidiNote*> selected;
    enum Mode { none, move, resize, velocity, create, rubber, erase, controller, controllerLine, controllerErase } mode = none;
    int laneType = -1;
    juce::TextButton laneBtn;
    double lastDrawBeat = -1.0; float lastDrawValue = 0.0f;
    juce::Point<float> lineStart, lineEnd;
    void showLaneMenu();
    float valueToY (float norm) const;
    float yToValue (float y) const;
    int toRaw (float norm) const;
    float fromRaw (int raw) const;
    double controllerStep() const { return 1.0 / 16.0; }      // une valeur par quadruple croche en dessin libre
    te::MidiNote* dragNote = nullptr;
    juce::Array<double> dragStarts; juce::Array<int> dragPitches; juce::Array<double> dragLens;
    juce::Rectangle<int> rubberRect;
    juce::Point<int> rubberStart;
    int lastAuditionPitch = -1;
    te::MPESourceID sourceID = te::createUniqueMPESourceID();
};
