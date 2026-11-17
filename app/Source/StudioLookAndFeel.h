// Apparence générale d'Onde : boutons, menus, curseurs, barres de défilement au style « studio ».
#pragma once
#include "Theme.h"

class StudioLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel()
    {
        auto scheme = juce::LookAndFeel_V4::ColourScheme (
            theme::panel, theme::bg, theme::panel2, theme::lineBar, theme::text, theme::accent,
            juce::Colours::white, theme::panel2, theme::text);
        setColourScheme (scheme);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff2a2d32));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::textColourId, theme::text);
        setColour (juce::PopupMenu::headerTextColourId, theme::muted);
        setColour (juce::ComboBox::backgroundColourId, theme::panel2);
        setColour (juce::ComboBox::outlineColourId, theme::line);
        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::ComboBox::arrowColourId, theme::muted);
        setColour (juce::TextEditor::backgroundColourId, theme::display);
        setColour (juce::TextEditor::outlineColourId, theme::line);
        setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        setColour (juce::TextEditor::textColourId, theme::text);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::ListBox::backgroundColourId, theme::bg);
        setColour (juce::ScrollBar::thumbColourId, theme::panel3);
        setColour (juce::Slider::thumbColourId, juce::Colour (0xffc9cdd2));
        setColour (juce::Slider::trackColourId, theme::accent);
        setColour (juce::Slider::backgroundColourId, theme::display);
        setColour (juce::Slider::textBoxTextColourId, theme::text);
        setColour (juce::Slider::textBoxBackgroundColourId, theme::display);
        setColour (juce::Slider::textBoxOutlineColourId, theme::line);
        setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, theme::display);
        setColour (juce::ToggleButton::textColourId, theme::text);
        setColour (juce::ToggleButton::tickColourId, theme::accent);
        setColour (juce::ToggleButton::tickDisabledColourId, theme::muted);
        setColour (juce::AlertWindow::backgroundColourId, theme::panel);
        setColour (juce::AlertWindow::textColourId, theme::text);
        setColour (juce::AlertWindow::outlineColourId, theme::line);
        setColour (juce::ResizableWindow::backgroundColourId, theme::panel);
        setColour (juce::DocumentWindow::textColourId, theme::text);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xfff2f3f5));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (0xff15171a));
        setColour (juce::TooltipWindow::outlineColourId, theme::line);
        setColour (juce::CaretComponent::caretColourId, theme::accent);
        setDefaultSansSerifTypefaceName ("Segoe UI");
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override        { return theme::ui (juce::jmin (13.0f, h * 0.55f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override                   { return theme::ui (12.5f); }
    juce::Font getPopupMenuFont() override                                  { return theme::ui (13.5f); }
    juce::Font getLabelFont (juce::Label& l) override                       { return l.getFont(); }
    int getMenuWindowFlags() override                                       { return 0; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgCol, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bgCol;
        if (down) c = c.darker (0.15f); else if (over) c = c.brighter (0.08f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.05f), 0, r.getY(), c.darker (0.06f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (theme::line); g.drawRoundedRectangle (r, 2.5f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                       .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId)); g.fillRoundedRectangle (r, 2.5f);
        g.setColour (theme::line); g.drawRoundedRectangle (r, 2.5f, 1.0f);
        juce::Path p; const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
        p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId)); g.fillPath (p);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (theme::line); g.drawRect (0, 0, w, h);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           const juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            // Fader de console : rail fin + capuchon rainuré.
            const float cx = (float) x + (float) w * 0.5f;
            g.setColour (theme::display); g.fillRoundedRectangle (cx - 2.5f, (float) y, 5.0f, (float) h, 2.0f);
            g.setColour (theme::line); g.drawRoundedRectangle (cx - 2.5f, (float) y, 5.0f, (float) h, 2.0f, 1.0f);
            const float capW = juce::jmin (28.0f, (float) w - 2.0f), capH = 40.0f;
            auto cap = juce::Rectangle<float> (cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6b7079), 0, cap.getY(), juce::Colour (0xff3b3f45), 0, cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (theme::line); g.drawRoundedRectangle (cap, 3.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillRect (cap.getX() + 3, cap.getCentreY() - 1.0f, cap.getWidth() - 6, 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            for (int i = -2; i <= 2; ++i) if (i != 0) g.fillRect (cap.getX() + 5, cap.getCentreY() + i * 5.0f, cap.getWidth() - 10, 1.0f);
            return;
        }
        if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
        {
            auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
            g.setColour (theme::display); g.fillRoundedRectangle (r, 3.0f);
            const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
            const float zeroX = bipolar ? (float) x + (float) w * (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : (float) x;
            g.setColour (s.findColour (juce::Slider::trackColourId));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zeroX, pos), r.getY() + 1, juce::jmax (zeroX, pos), r.getBottom() - 1));
            g.setColour (juce::Colour (0xffd5d8dc)); g.fillRoundedRectangle (pos - 3.0f, (float) y + (float) h * 0.5f - 7.0f, 6.0f, 14.0f, 1.5f);
            return;
        }
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre();
        g.setColour (theme::display); g.fillEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c));
        const float ang = a0 + pos * (a1 - a0);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        juce::Path arc; arc.addCentredArc (c.x, c.y, rad - 2.5f, rad - 2.5f, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId)); g.strokePath (arc, juce::PathStrokeType (3.0f));
        g.setColour (theme::panel3); g.fillEllipse (juce::Rectangle<float> (rad * 1.2f, rad * 1.2f).withCentre (c));
        juce::Path needle; needle.addRectangle (-1.0f, -rad * 0.6f, 2.0f, rad * 0.45f);
        g.setColour (theme::text); g.fillPath (needle, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool) override
    {
        g.setColour (theme::bg); g.fillRect (x, y, w, h);
        auto t = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize) : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
        g.setColour (over ? theme::panel3.brighter (0.1f) : theme::panel3); g.fillRoundedRectangle (t.toFloat(), 3.0f);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
        g.setColour (theme::line); g.drawRect (0, 0, w, h);
        g.setColour (findColour (juce::TooltipWindow::textColourId)); g.setFont (theme::ui (12.5f));
        g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 2), juce::Justification::centredLeft, 3);
    }

    // Barre de menus intégrée à la fenêtre.
    void drawMenuBarBackground (juce::Graphics& g, int w, int h, bool, juce::MenuBarComponent&) override
    {
        g.fillAll (juce::Colour (0xff25282c));
        g.setColour (theme::line); g.drawHorizontalLine (h - 1, 0.0f, (float) w);
    }
    void drawMenuBarItem (juce::Graphics& g, int w, int h, int, const juce::String& text, bool over, bool open, bool, juce::MenuBarComponent&) override
    {
        if (over || open) { g.setColour (open ? theme::accent : theme::panel3); g.fillRect (0, 0, w, h); }
        g.setColour (theme::text); g.setFont (theme::ui (13.0f));
        g.drawText (text, 0, 0, w, h, juce::Justification::centred);
    }
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&) override { return theme::ui (13.0f); }
    int getMenuBarItemWidth (juce::MenuBarComponent&, int, const juce::String& text) override
    {
        return juce::GlyphArrangement::getStringWidthInt (theme::ui (13.0f), text) + 20;
    }
};
