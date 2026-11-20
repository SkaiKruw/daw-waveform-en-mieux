// Clavier (MIDI › Clavier, Ctrl+Maj+K) : jouer sans clavier MIDI, à la souris sur le clavier à l'écran ou avec
// le clavier de l'ordinateur (AZERTY ou QWERTY). Les notes passent par une entrée MIDI virtuelle d'Onde : elles
// jouent l'instrument de la piste sélectionnée et s'enregistrent comme avec un vrai clavier MIDI.
#pragma once
#include "EditContext.h"

// Clavier à l'écran : les touches jouables à l'ordinateur portent leur lettre ; les Do sont nommés « Do3 ».
class LabelledKeyboard  : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;
    std::function<juce::String (int)> letterFor;
protected:
    juce::String getWhiteNoteText (int note) override
    {
        const auto l = letterFor ? letterFor (note) : juce::String();
        if (l.isNotEmpty()) return l;
        return note % 12 == 0 ? "Do" + juce::String (note / 12 - 1) : juce::String();
    }
    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour line, juce::Colour text) override
    {
        const bool mapped = letterFor && letterFor (note).isNotEmpty();
        if (mapped) { g.setColour (juce::Colour (0xff3fa4ff).withAlpha (0.10f)); g.fillRect (area); }
        juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, line, mapped ? juce::Colour (0xff1c5d99) : text);
    }
    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour fill) override
    {
        juce::MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, fill);
        if (const auto l = letterFor ? letterFor (note) : juce::String(); l.isNotEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (l, area.removeFromBottom (18.0f), juce::Justification::centred, false);
        }
    }
};

class KeyboardPanel  : public juce::Component, private juce::MidiKeyboardState::Listener, private juce::Timer
{
public:
    explicit KeyboardPanel (EditContext&);
    ~KeyboardPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override             { releaseAllKeys(); repaint(); }
    void focusGained (FocusChangeType) override           { repaint(); }
    void visibilityChanged() override;
    static juce::DialogWindow* show (EditContext&);

    // Entrée MIDI virtuelle « Clavier Onde » (créée au besoin, sans relais des claviers physiques).
    static te::MidiInputDevice* getDevice (te::Engine&);

    // Jouer une note comme un clic ou une touche (aussi utilisé par les tests).
    void noteOn (int note, int velocity);
    void noteOff (int note);
    int getBaseNote() const       { return 12 * (octave + 1); }   // note de la touche Q (AZERTY) / A (QWERTY)
    int getVelocity() const       { return velocity; }

private:
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void timerCallback() override;
    void send (const juce::MidiMessage&);
    void setOctave (int);
    void setVelocity (int);
    void releaseAllKeys();
    int offsetFor (juce::juce_wchar) const;          // demi-tons depuis la note de base, -1 si touche non jouée
    bool azerty() const           { return layout.getSelectedId() != 2; }

    EditContext& ctx;
    juce::MidiKeyboardState state;
    LabelledKeyboard keyboard { state, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::ComboBox layout;
    juce::TextButton octDown { "- Octave" }, octUp { "Octave +" }, sustain { "Sustain" };
    juce::Slider velocitySlider;
    juce::Label target;
    std::map<juce::juce_wchar, int> held;            // touche de l'ordinateur -> note jouée
    int octave = 3, velocity = 100;
    te::EditItemID lastRouted;
};
