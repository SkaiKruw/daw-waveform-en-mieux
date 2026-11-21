#include "Lyrics.h"
#include "ParamControls.h"

namespace lyrics
{
    namespace
    {
        const juce::Identifier nodeID ("ONDELYRICS"), textID ("text"), beatsID ("beats");

        juce::ValueTree node (te::Edit& e)
        {
            auto n = e.state.getChildWithName (nodeID);
            if (! n.isValid()) { n = juce::ValueTree (nodeID); e.state.appendChild (n, nullptr); }
            return n;
        }
        juce::ValueTree nodeConst (const te::Edit& e) { return e.state.getChildWithName (nodeID); }

        juce::Array<double> beatsOf (const juce::ValueTree& n)
        {
            juce::Array<double> r;
            for (auto& s : juce::StringArray::fromTokens (n[beatsID].toString(), ";", {})) r.add (s.getDoubleValue());
            return r;
        }
        juce::String encode (const juce::Array<double>& b)
        {
            juce::StringArray s;
            for (double v : b) s.add (juce::String (v, 4));
            return s.joinIntoString (";");
        }
    }

    juce::String getText (const te::Edit& e) { return nodeConst (e)[textID].toString(); }

    void setText (te::Edit& e, const juce::String& text)
    {
        node (e).setProperty (textID, text, &e.getUndoManager());
    }

    std::vector<Line> getLines (const te::Edit& e)
    {
        std::vector<Line> lines;
        const auto n = nodeConst (e);
        const auto beats = beatsOf (n);
        int sung = 0;
        for (auto& raw : juce::StringArray::fromLines (n[textID].toString()))
        {
            const auto t = raw.trim();
            if (t.isEmpty()) continue;
            Line l;
            l.text = t;
            l.isHeading = t.startsWithChar ('[') && t.endsWithChar (']');
            if (! l.isHeading) { l.beat = sung < beats.size() ? beats[sung] : -1.0; ++sung; }
            lines.push_back (l);
        }
        return lines;
    }

    void setBeat (te::Edit& e, int lineIndex, double beat)
    {
        // lineIndex : rang parmi les phrases chantées (titres de partie exclus).
        auto n = node (e);
        auto beats = beatsOf (n);
        while (beats.size() <= lineIndex) beats.add (-1.0);
        beats.set (lineIndex, beat);
        n.setProperty (beatsID, encode (beats), &e.getUndoManager());
    }

    void clearTimings (te::Edit& e) { node (e).setProperty (beatsID, juce::String(), &e.getUndoManager()); }

    int lineAt (const te::Edit& e, double beat)
    {
        int best = -1, sung = 0;
        double bestBeat = -1.0;
        for (auto& l : getLines (e))
        {
            if (l.isHeading) continue;
            if (l.beat >= 0.0 && l.beat <= beat + 1.0e-6 && l.beat >= bestBeat) { best = sung; bestBeat = l.beat; }
            ++sung;
        }
        return best;
    }
}

//==============================================================================
LyricsPanel::LyricsPanel (EditContext& c) : ctx (c)
{
    editor.setMultiLine (true, true);
    editor.setReturnKeyStartsNewLine (true);
    editor.setFont (theme::ui (15.0f));
    editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff15171b));
    editor.setColour (juce::TextEditor::textColourId, theme::text);
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2e35));
    editor.setTextToShowWhenEmpty (U ("Tapez ou collez les paroles : une ligne par phrase chantée, [Refrain] pour titrer une partie."), theme::dim);
    editor.addListener (this);
    addAndMakeVisible (editor);
    tapBtn.setClickingTogglesState (false);
    tapBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8697a));
    tapBtn.setTooltip (U ("Lancez la lecture, puis appuyez sur Entrée au début de chaque phrase"));
    tapBtn.onClick = [this] { if (tapping) setTapping (false); else startTapping(); };
    clearBtn.onClick = [this]
    {
        if (ctx.edit == nullptr) return;
        ctx.edit->getUndoManager().beginNewTransaction (U ("Effacer le calage des paroles"));
        lyrics::clearTimings (*ctx.edit);
        repaint();
    };
    addAndMakeVisible (tapBtn);
    addAndMakeVisible (clearBtn);
    hint.setFont (theme::ui (12.0f));
    hint.setColour (juce::Label::textColourId, theme::muted);
    addAndMakeVisible (hint);
    setWantsKeyboardFocus (true);
    setSize (900, 520);
    timerCallback();
    startTimerHz (20);
}

LyricsPanel::~LyricsPanel() { editor.removeListener (this); }

double LyricsPanel::currentBeat() const
{
    if (ctx.edit == nullptr) return 0.0;
    return ctx.edit->tempoSequence.toBeats (ctx.edit->getTransport().getPosition()).inBeats();
}

void LyricsPanel::textEditorTextChanged (juce::TextEditor&)
{
    if (ctx.edit == nullptr || tapping) return;
    // Une saisie continue = une seule entrée d'historique (nouvelle transaction après 2 s de pause).
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastTypingMs > 2000) ctx.edit->getUndoManager().beginNewTransaction (U ("Paroles"));
    lastTypingMs = now;
    lyrics::setText (*ctx.edit, editor.getText());
    repaint();
}

void LyricsPanel::startTapping()
{
    tapLine = 0;
    setTapping (true);
}

void LyricsPanel::setTapping (bool on)
{
    tapping = on;
    editor.setReadOnly (on);
    editor.setWantsKeyboardFocus (! on);
    tapBtn.setToggleState (on, juce::dontSendNotification);
    tapBtn.setButtonText (on ? U ("Terminer le calage") : U ("Caler pendant la lecture"));
    if (on) grabKeyboardFocus();
    repaint();
}

void LyricsPanel::tap()
{
    if (ctx.edit == nullptr) return;
    int sungCount = 0;
    for (auto& l : lyrics::getLines (*ctx.edit)) if (! l.isHeading) ++sungCount;
    if (tapLine >= sungCount) { setTapping (false); return; }
    ctx.edit->getUndoManager().beginNewTransaction (U ("Caler une phrase"));
    lyrics::setBeat (*ctx.edit, tapLine, currentBeat());
    ++tapLine;
    if (tapLine >= sungCount) setTapping (false);
    repaint();
}

bool LyricsPanel::keyPressed (const juce::KeyPress& k)
{
    if (tapping && k == juce::KeyPress::returnKey) { tap(); return true; }
    if (tapping && k == juce::KeyPress::escapeKey) { setTapping (false); return true; }
    return false;
}

void LyricsPanel::timerCallback()
{
    if (ctx.edit != attached)
    {
        attached = ctx.edit;
        if (attached != nullptr) editor.setText (lyrics::getText (*attached), juce::dontSendNotification);
    }
    else if (attached != nullptr && ! editor.hasKeyboardFocus (true) && editor.getText() != lyrics::getText (*attached))
        editor.setText (lyrics::getText (*attached), juce::dontSendNotification);      // annuler / rétablir
    const int line = ctx.edit != nullptr ? lyrics::lineAt (*ctx.edit, currentBeat()) : -1;
    if (line != shownLine) { shownLine = line; repaint(); }
    hint.setText (tapping ? U ("Calage : appuyez sur Entrée au début de la phrase ") + juce::String (tapLine + 1) + U (" (Échap pour arrêter)")
                          : U ("Le prompteur suit la lecture · calez les phrases en tapant Entrée pendant la lecture"), juce::dontSendNotification);
}

void LyricsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
    g.drawText ("Paroles", 16, 10, 200, 24, juce::Justification::centredLeft);
    // Prompteur : phrase en cours en grand, la précédente estompée, les deux suivantes.
    auto r = getLocalBounds().reduced (16).withTrimmedTop (36).withTrimmedBottom (44);
    r.removeFromLeft (editor.getWidth() + 16);
    g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (r.toFloat(), 10.0f);
    if (ctx.edit == nullptr) return;
    const auto lines = lyrics::getLines (*ctx.edit);
    std::vector<const lyrics::Line*> sung;
    juce::StringArray headingBefore;
    juce::String heading;
    for (auto& l : lines) { if (l.isHeading) heading = l.text; else { sung.push_back (&l); headingBefore.add (heading); } }
    auto inner = r.reduced (20, 16);
    if (sung.empty())
    {
        g.setColour (theme::muted); g.setFont (theme::ui (14.0f));
        g.drawFittedText (U ("Le prompteur affichera ici la phrase à chanter, en grand, pendant la lecture."), inner, juce::Justification::centred, 3);
        return;
    }
    const int current = tapping ? juce::jmax (0, tapLine - 1) : juce::jmax (-1, shownLine);
    auto drawLine = [&] (int i, float size, juce::Colour c, bool bold)
    {
        if (i < 0 || i >= (int) sung.size()) { inner.removeFromTop ((int) (size * 1.6f)); return; }
        auto row = inner.removeFromTop ((int) (size * 1.6f));
        g.setColour (c); g.setFont (theme::ui (size, bold));
        g.drawFittedText (sung[(size_t) i]->text, row, juce::Justification::centredLeft, 2);
        if (sung[(size_t) i]->beat < 0.0) { g.setColour (theme::dim); g.setFont (theme::ui (10.0f)); g.drawText (U ("non calée"), row, juce::Justification::topRight); }
    };
    const auto part = current >= 0 && current < headingBefore.size() ? headingBefore[current] : (headingBefore.isEmpty() ? juce::String() : headingBefore[0]);
    g.setColour (juce::Colour (0xffe8697a)); g.setFont (theme::ui (12.0f, true));
    g.drawText (part.isNotEmpty() ? part : U (" "), inner.removeFromTop (20), juce::Justification::centredLeft);
    drawLine (current - 1, 15.0f, theme::dim, false);
    drawLine (juce::jmax (0, current), 28.0f, current >= 0 ? theme::text : theme::muted, true);
    drawLine (juce::jmax (0, current) + 1, 18.0f, theme::muted, false);
    drawLine (juce::jmax (0, current) + 2, 15.0f, theme::dim, false);
}

void LyricsPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (36);
    auto bottom = r.removeFromBottom (32);
    tapBtn.setBounds (bottom.removeFromLeft (200)); bottom.removeFromLeft (8);
    clearBtn.setBounds (bottom.removeFromLeft (150)); bottom.removeFromLeft (12);
    hint.setBounds (bottom);
    r.removeFromBottom (12);
    editor.setBounds (r.removeFromLeft (360));
}

juce::DialogWindow* LyricsPanel::show (EditContext& c)
{
    return ondeui::showToolWindow ("Paroles", new LyricsPanel (c), true);
}
