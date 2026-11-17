#include "CommandPalette.h"

namespace
{
    // Comparaison sans accents ni majuscules : « reverb » trouve « Réverbération ».
    juce::String fold (juce::String s)
    {
        s = s.toLowerCase();
        const char* from[] = { "é", "è", "ê", "ë", "à", "â", "î", "ï", "ô", "ù", "û", "ç" };
        const char* to[]   = { "e", "e", "e", "e", "a", "a", "i", "i", "o", "u", "u", "c" };
        for (int i = 0; i < 12; ++i) s = s.replace (juce::String::fromUTF8 (from[i]), to[i]);
        return s;
    }
}

CommandPalette::CommandPalette (EditContext& c) : ctx (c)
{
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty (U ("Rechercher une action… (ex. : piste, export, réverb, annuler)"), theme::muted);
    search.setFont (theme::ui (17.0f));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, theme::text);
    search.addListener (this);
    search.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);
    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setVisible (false);
}

juce::Rectangle<int> CommandPalette::boxBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (660, getWidth() - 40), juce::jmin (480, getHeight() - 120)).withY (70);
}

void CommandPalette::open()
{
    setVisible (true);
    toFront (true);
    search.setText ({}, false);
    filter();
    search.grabKeyboardFocus();
}

void CommandPalette::close()
{
    setVisible (false);
    if (auto* p = getParentComponent()) p->grabKeyboardFocus();
}

void CommandPalette::filter()
{
    results.clear();
    const auto q = fold (search.getText().trim());
    auto words = juce::StringArray::fromTokens (q, " ", {});
    for (auto* a : ctx.actions)
    {
        const auto hay = fold (a->name + " " + a->category);
        bool ok = true;
        for (auto& w : words) ok &= hay.contains (w);
        if (ok) results.add (a);
    }
    list.updateContent();
    list.selectRow (0);
    repaint();
}

void CommandPalette::runRow (int row)
{
    if (! juce::isPositiveAndBelow (row, results.size())) return;
    auto fn = results[row]->perform;
    close();
    juce::MessageManager::callAsync (fn);
}

bool CommandPalette::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::upKey)   { list.selectRow (juce::jmax (0, list.getSelectedRow() - 1)); return true; }
    if (k == juce::KeyPress::downKey) { list.selectRow (juce::jmin (results.size() - 1, list.getSelectedRow() + 1)); return true; }
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

void CommandPalette::resized()
{
    auto b = boxBounds();
    search.setBounds (b.removeFromTop (54).reduced (16, 10).withTrimmedLeft (26));
    list.setBounds (b.reduced (8).withTrimmedBottom (30));
}

void CommandPalette::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0x8c08090b));
    auto b = boxBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (b.translated (0, 10), 12.0f);
    g.setColour (theme::panel); g.fillRoundedRectangle (b, 12.0f);
    g.setColour (juce::Colour (0xff3a3f47)); g.drawRoundedRectangle (b, 12.0f, 1.0f);
    g.drawHorizontalLine ((int) b.getY() + 54, b.getX(), b.getRight());
    g.setColour (theme::muted);
    g.drawEllipse (b.getX() + 18, b.getY() + 20, 12, 12, 1.8f);
    g.drawLine (b.getX() + 28, b.getY() + 30, b.getX() + 33, b.getY() + 35, 1.8f);
    g.setFont (theme::ui (12.0f));
    g.drawText (U ("↑ ↓ naviguer · Entrée exécuter · Échap fermer · ") + juce::String (results.size()) + U (" action(s)"),
                b.removeFromBottom (30).reduced (16, 0).toNearestInt(), juce::Justification::centredLeft);
}

void CommandPalette::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, results.size())) return;
    auto* a = results[row];
    if (selected) { g.setColour (theme::accentBg); g.fillRoundedRectangle (2.0f, 1.0f, w - 4.0f, h - 2.0f, 7.0f); }
    g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
    g.drawText (a->category, 14, 0, 110, h, juce::Justification::centredLeft);
    g.setColour (theme::text); g.setFont (theme::ui (14.5f));
    g.drawText (a->name, 126, 0, w - 250, h, juce::Justification::centredLeft, true);
    const auto key = a->keyText();
    if (key.isNotEmpty())
    {
        g.setFont (theme::mono (11.5f));
        const int kw = juce::GlyphArrangement::getStringWidthInt (theme::mono (11.5f), key) + 14;
        juce::Rectangle<float> kr ((float) (w - kw - 14), (float) h / 2 - 11, (float) kw, 22.0f);
        g.setColour (theme::line); g.drawRoundedRectangle (kr, 4.0f, 1.0f);
        g.setColour (theme::muted); g.drawText (key, kr.toNearestInt(), juce::Justification::centred);
    }
}
