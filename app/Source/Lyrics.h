// Paroles du morceau : un texte (une ligne par phrase chantée, « [Refrain] » pour un titre de partie, ligne vide
// pour séparer) et le temps musical où commence chaque phrase. Enregistrées avec le projet, annulables.
#pragma once
#include "EditContext.h"

namespace lyrics
{
    struct Line { juce::String text; double beat = -1.0; bool isHeading = false; };   // beat < 0 : pas encore calée

    juce::String getText (const te::Edit&);
    // Remplace le texte ; les calages suivent les phrases par leur rang (annulable).
    void setText (te::Edit&, const juce::String&);
    std::vector<Line> getLines (const te::Edit&);         // phrases et titres, dans l'ordre (lignes vides omises)
    void setBeat (te::Edit&, int lineIndex, double beat); // annulable ; beat < 0 retire le calage
    void clearTimings (te::Edit&);
    int lineAt (const te::Edit&, double beat);           // phrase en cours (dernière calée avant ce temps), -1 sinon
}

class LyricsPanel  : public juce::Component, private juce::Timer, private juce::TextEditor::Listener
{
public:
    explicit LyricsPanel (EditContext&);
    ~LyricsPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    static juce::DialogWindow* show (EditContext&);

    // Calage au tapotement : la phrase suivante commence au curseur de lecture (Entrée en mode calage).
    void tap();
    void startTapping();
    int getNextTapLine() const { return tapLine; }

private:
    void timerCallback() override;
    void textEditorTextChanged (juce::TextEditor&) override;
    void setTapping (bool);
    double currentBeat() const;

    EditContext& ctx;
    juce::TextEditor editor;
    juce::TextButton tapBtn { U ("Caler pendant la lecture") }, clearBtn { U ("Effacer le calage") };
    juce::Label hint;
    bool tapping = false;
    int tapLine = 0, shownLine = -2;
    te::Edit* attached = nullptr;
    juce::uint32 lastTypingMs = 0;
};
