// Petit message rapide en bas au centre (« Enregistré ✓ »), qui apparaît puis s'efface tout seul.
#pragma once
#include "Theme.h"

class Toast  : public juce::Component, private juce::Timer
{
public:
    Toast() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); }

    void show (const juce::String& message, juce::Colour accent = theme::ok)
    {
        text = message; colour = accent;
        shownAt = juce::Time::getMillisecondCounter();
        if (auto* p = getParentComponent())
        {
            const int w = juce::jmax (180, juce::GlyphArrangement::getStringWidthInt (theme::ui (14.0f, true), text) + 64);
            setBounds ((p->getWidth() - w) / 2, p->getHeight() - 140, w, 40);
        }
        setVisible (true);
        toFront (false);
        startTimerHz (30);
        repaint();
    }
    juce::String getText() const { return isVisible() ? text : juce::String(); }

    void paint (juce::Graphics& g) override
    {
        const float a = alpha();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff1c1f24).withAlpha (0.96f * a)); g.fillRoundedRectangle (r, 10.0f);
        g.setColour (colour.withAlpha (0.8f * a)); g.drawRoundedRectangle (r, 10.0f, 1.2f);
        // Coche.
        juce::Path tick;
        const float x = r.getX() + 18.0f, y = r.getCentreY();
        tick.startNewSubPath (x, y); tick.lineTo (x + 5.0f, y + 5.0f); tick.lineTo (x + 14.0f, y - 6.0f);
        g.setColour (colour.withAlpha (a)); g.strokePath (tick, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::text.withAlpha (a)); g.setFont (theme::ui (14.0f, true));
        g.drawText (text, r.withTrimmedLeft (44.0f).withTrimmedRight (14.0f), juce::Justification::centredLeft, true);
    }

private:
    float alpha() const
    {
        const auto t = (int) (juce::Time::getMillisecondCounter() - shownAt);
        if (t < 120) return t / 120.0f;
        if (t > 1300) return juce::jmax (0.0f, 1.0f - (t - 1300) / 400.0f);
        return 1.0f;
    }
    void timerCallback() override
    {
        if (juce::Time::getMillisecondCounter() - shownAt > 1700) { stopTimer(); setVisible (false); return; }
        repaint();
    }
    juce::String text;
    juce::Colour colour = theme::ok;
    juce::uint32 shownAt = 0;
};
