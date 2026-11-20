// Modulateurs (Projet › Modulateurs de la piste…, Ctrl+Maj+L, ou clic droit sur une piste) : LFO, pas à pas,
// suiveur d'enveloppe et aléatoire, posés sur une piste et affectés à n'importe quel paramètre (volume,
// panoramique, réglages des effets), avec une profondeur positive ou négative par affectation.
// Réglages et affectations sont enregistrés avec le projet et annulables.
#pragma once
#include "EditContext.h"

class ModulatorPanel  : public juce::Component, private juce::Timer
{
public:
    explicit ModulatorPanel (EditContext&);
    ~ModulatorPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::DialogWindow* show (EditContext&);

    enum class Kind { lfo, step, envelope, random };
    static const char* kindName (Kind);
    // Ajoute un modulateur sur la piste (annulable) ; nullptr si la piste n'en accepte pas.
    static te::Modifier::Ptr addModulator (te::Track&, Kind);
    // Affecte un modulateur à un paramètre (profondeur -1..1, en proportion de la course du paramètre).
    static te::AutomatableParameter::ModifierAssignment::Ptr assign (te::Modifier&, te::AutomatableParameter&, float depth);
    static juce::String describe (te::Modifier&);            // « LFO 1 », « Pas à pas 2 »…

    te::Track* currentTrack() const;
    int numCards() const;

    class Card;
private:
    void timerCallback() override;
    void rebuild();
    juce::String signature() const;

    EditContext& ctx;
    te::Edit* attached = nullptr;
    juce::String lastSignature;
    juce::Label title;
    juce::TextButton addButtons[4];
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Card> cards;
};
