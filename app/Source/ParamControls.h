// Contrôles liés à un paramètre automatisable (bouton rotatif, liste de choix, bouton marche/arrêt) :
// réglage annulable, geste d'automation, suivi de la valeur (automation, modulation, annuler).
#pragma once
#include <JuceHeader.h>
#include "Theme.h"

namespace te = tracktion;

namespace ondeui
{
    // Bouton rotatif lié à un paramètre (automatisation, annulation, double-clic = valeur par défaut).
    class Knob  : public juce::Component, private juce::Timer
    {
    public:
        Knob (te::AutomatableParameter* p, juce::String label, juce::Colour c = theme::accent) : param (p), title (std::move (label)), colour (c)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
            slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff30343c));
            slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
            if (param != nullptr)
            {
                slider.setNormalisableRange (juce::NormalisableRange<double> (param->valueRange.start, param->valueRange.end, param->valueRange.interval, param->valueRange.skew));
                slider.setDoubleClickReturnValue (true, param->getDefaultValue().value_or (param->valueRange.start));
                slider.setValue (param->getCurrentValue(), juce::dontSendNotification);
                slider.onDragStart = [this] { param->getEdit().getUndoManager().beginNewTransaction (title); param->parameterChangeGestureBegin(); };
                slider.onDragEnd = [this] { param->parameterChangeGestureEnd(); };
                slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), juce::sendNotification); repaint(); };
            }
            addAndMakeVisible (slider);
            startTimerHz (15);
        }
        void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (16).withTrimmedBottom (18)); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::muted); g.setFont (theme::ui (11.0f));
            g.drawText (title, getLocalBounds().removeFromTop (16), juce::Justification::centred, true);
            g.setColour (theme::text); g.setFont (theme::mono (11.0f));
            g.drawText (param ? param->getCurrentValueAsString() : juce::String(), getLocalBounds().removeFromBottom (18), juce::Justification::centred, true);
        }
    private:
        void timerCallback() override
        {
            if (param == nullptr || slider.isMouseButtonDown()) return;
            const double v = param->getCurrentValue();
            if (std::abs (v - slider.getValue()) > 1.0e-6) { slider.setValue (v, juce::dontSendNotification); repaint(); }
        }
        te::AutomatableParameter::Ptr param;
        juce::String title;
        juce::Colour colour;
        juce::Slider slider;
    };

    //==========================================================================
    // Liste de choix liée à un paramètre à valeurs entières (mode, pente, style…).
    class ParamChoice  : public juce::Component, private juce::Timer
    {
    public:
        ParamChoice (te::AutomatableParameter* p, juce::String label, juce::StringArray items = {}) : param (p), title (std::move (label))
        {
            if (param != nullptr)
            {
                const int count = juce::roundToInt (param->valueRange.end - param->valueRange.start) + 1;
                for (int i = 0; i < count; ++i) box.addItem (i < items.size() ? items[i] : param->valueToString (param->valueRange.start + (float) i), i + 1);
                box.onChange = [this]
                {
                    param->getEdit().getUndoManager().beginNewTransaction (title);
                    param->setParameter (param->valueRange.start + (float) box.getSelectedItemIndex(), juce::sendNotification);
                    if (onChange) onChange();
                };
            }
            addAndMakeVisible (box);
            sync();
            startTimerHz (10);
        }
        std::function<void()> onChange;
        void resized() override { box.setBounds (getLocalBounds().withTrimmedTop (16)); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::muted); g.setFont (theme::ui (11.0f));
            g.drawText (title, getLocalBounds().removeFromTop (16), juce::Justification::centredLeft, true);
        }
    private:
        void sync()
        {
            if (param == nullptr || box.isPopupActive()) return;
            const int i = juce::roundToInt (param->getCurrentValue() - param->valueRange.start);
            if (box.getSelectedItemIndex() != i) box.setSelectedItemIndex (i, juce::dontSendNotification);
        }
        void timerCallback() override { sync(); }
        te::AutomatableParameter::Ptr param;
        juce::String title;
        juce::ComboBox box;
    };

    // Bouton marche/arrêt lié à un paramètre 0/1.
    class ParamToggle  : public juce::TextButton, private juce::Timer
    {
    public:
        ParamToggle (te::AutomatableParameter* p, const juce::String& text, juce::Colour on = theme::accent) : juce::TextButton (text), param (p)
        {
            setColour (juce::TextButton::buttonOnColourId, on.withAlpha (0.85f));
            setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2e35));
            setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            juce::TextButton::onClick = [this]
            {
                if (param == nullptr) return;
                param->getEdit().getUndoManager().beginNewTransaction (getButtonText());
                param->setParameter (param->getCurrentValue() > 0.5f ? 0.0f : 1.0f, juce::sendNotification);
                sync();
                if (onToggle) onToggle();
            };
            sync();
            startTimerHz (10);
        }
        std::function<void()> onToggle;
    private:
        void sync() { setToggleState (param != nullptr && param->getCurrentValue() > 0.5f, juce::dontSendNotification); }
        void timerCallback() override { sync(); }
        te::AutomatableParameter::Ptr param;
    };
}
