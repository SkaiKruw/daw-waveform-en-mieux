#include "TabView.h"

namespace
{
    const char* noteNames[] = { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    juce::String shortName (int pitch)
    {
        static const char* letters[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return letters[pitch % 12];
    }
}

const std::vector<TabView::Tuning>& TabView::tunings()
{
    // Cordes de la plus aiguë (en haut de la tablature) à la plus grave.
    static const std::vector<Tuning> list = {
        { "Guitare standard (Mi La Ré Sol Si Mi)", { 64, 59, 55, 50, 45, 40 } },
        { "Guitare drop D (Ré La Ré Sol Si Mi)",   { 64, 59, 55, 50, 45, 38 } },
        { "Guitare open G (Ré Sol Ré Sol Si Ré)",  { 62, 59, 55, 50, 43, 38 } },
        { "Guitare 7 cordes (Si Mi La Ré Sol Si Mi)", { 64, 59, 55, 50, 45, 40, 35 } },
        { "Guitare 8 cordes (Fa# Si Mi La Ré Sol Si Mi)", { 64, 59, 55, 50, 45, 40, 35, 30 } },
        { "Guitare 12 cordes (accord standard)", { 64, 59, 55, 50, 45, 40 } },
        { "Basse 4 cordes (Mi La Ré Sol)",          { 43, 38, 33, 28 } },
        { "Basse 5 cordes (Si Mi La Ré Sol)",       { 43, 38, 33, 28, 23 } },
        { "Ukulélé (Sol Do Mi La)",                  { 69, 64, 60, 67 } },
    };
    return list;
}

TabView::TabView (EditContext& c) : ctx (c)
{
    for (size_t i = 0; i < tunings().size(); ++i) tuningBox.addItem (juce::String::fromUTF8 (tunings()[i].name), (int) i + 1);
    tuningBox.setSelectedId (1, juce::dontSendNotification);
    tuningBox.onChange = [this] { setTuning (tuningBox.getSelectedId() - 1); };
    gridBox.addItem ("Grille 1/4", 1); gridBox.addItem ("Grille 1/8", 2); gridBox.addItem ("Grille 1/16", 3);
    gridBox.setSelectedId (2, juce::dontSendNotification);
    gridBox.onChange = [this] { repaint(); };
    autoBtn.setTooltip (U ("Répartit les notes sur les cordes au plus près de la position de la main"));
    autoBtn.onClick = [this] { autoFingering(); };
    copyBtn.setTooltip (U ("Copie la tablature en texte (pour la partager, l'imprimer, la coller dans un message)"));
    copyBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (asciiTab()); if (ctx.setStatus) ctx.setStatus (U ("Tablature copiée en texte")); };
    for (auto* comp : std::initializer_list<juce::Component*> { &tuningBox, &gridBox, &autoBtn, &copyBtn })
    {
        comp->setWantsKeyboardFocus (false);
        addAndMakeVisible (comp);
    }
    setWantsKeyboardFocus (true);
    ctx.addChangeListener (this);
    startTimerHz (20);
}

TabView::~TabView() { ctx.removeChangeListener (this); }

te::MidiClip* TabView::clip() const { return ctx.edit != nullptr ? ctx.getSelectedMidiClip() : nullptr; }

void TabView::setTuning (int index)
{
    tuning = juce::jlimit (0, (int) tunings().size() - 1, index);
    tuningBox.setSelectedId (tuning + 1, juce::dontSendNotification);
    caretString = juce::jlimit (0, (int) tunings()[(size_t) tuning].strings.size() - 1, caretString);
    repaint();
}

double TabView::grid() const
{
    const double v[] = { 1.0, 0.5, 0.25 };
    return v[juce::jlimit (0, 2, gridBox.getSelectedId() - 1)];
}

juce::Rectangle<int> TabView::staff() const
{
    const int n = (int) tunings()[(size_t) tuning].strings.size();
    auto area = getLocalBounds().withTrimmedTop (toolbarH + rulerH).withTrimmedLeft (gutterW);
    return area.withHeight (juce::jmin (area.getHeight(), (n + 1) * stringGap));
}

float TabView::beatToX (double b) const  { return (float) staff().getX() + (float) ((b - viewBeat) * pxPerBeat); }
double TabView::xToBeat (float x) const   { return viewBeat + (x - (float) staff().getX()) / pxPerBeat; }
int TabView::stringY (int s) const        { return staff().getY() + stringGap / 2 + s * stringGap + 6; }

std::pair<int, int> TabView::placeOf (const te::MidiNote& n) const
{
    const auto& strings = tunings()[(size_t) tuning].strings;
    const int pitch = n.getNoteNumber();
    const int stored = n.getColour() - 1;
    if (juce::isPositiveAndBelow (stored, (int) strings.size()))
        if (const int fret = pitch - strings[(size_t) stored]; fret >= 0 && fret <= 24) return { stored, fret };
    // Doigté automatique : la case la plus basse possible.
    int best = -1, bestFret = 99;
    for (int s = 0; s < (int) strings.size(); ++s)
        if (const int fret = pitch - strings[(size_t) s]; fret >= 0 && fret <= 24 && fret < bestFret) { best = s; bestFret = fret; }
    return { best, bestFret };
}

te::MidiNote* TabView::noteAt (int string, double beat) const
{
    if (auto* mc = clip())
        for (auto* n : mc->getSequence().getNotes())
            if (std::abs (n->getStartBeat().inBeats() - beat) < 1.0e-6 && placeOf (*n).first == string) return n;
    return nullptr;
}

void TabView::audition (int pitch)
{
    auto* mc = clip();
    if (mc == nullptr || ctx.edit->getTransport().isPlaying()) return;
    if (auto* t = dynamic_cast<te::AudioTrack*> (mc->getTrack()))
    {
        t->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100), sourceID);
        const auto id = t->itemID;
        juce::Component::SafePointer<TabView> safe (this);
        juce::Timer::callAfterDelay (300, [safe, id, pitch]
        {
            if (safe == nullptr || safe->ctx.edit == nullptr) return;
            if (auto* tr = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*safe->ctx.edit, id)))
                tr->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, pitch), safe->sourceID);
        });
    }
}

te::MidiNote* TabView::writeFret (int string, double beat, int fret)
{
    auto* mc = clip();
    const auto& strings = tunings()[(size_t) tuning].strings;
    if (mc == nullptr || ! juce::isPositiveAndBelow (string, (int) strings.size())) return nullptr;
    fret = juce::jlimit (0, 24, fret);
    auto* um = &ctx.edit->getUndoManager();
    um->beginNewTransaction (U ("Tablature"));
    const int pitch = strings[(size_t) string] + fret;
    te::MidiNote* n = noteAt (string, beat);
    if (n != nullptr) { n->setNoteNumber (pitch, um); n->setColour (string + 1, um); }
    else n = mc->getSequence().addNote (pitch, te::BeatPosition::fromBeats (beat), te::BeatDuration::fromBeats (grid()), 100, string + 1, um);
    audition (pitch);
    repaint();
    return n;
}

void TabView::autoFingering()
{
    auto* mc = clip();
    if (mc == nullptr) return;
    const auto& strings = tunings()[(size_t) tuning].strings;
    auto* um = &ctx.edit->getUndoManager();
    um->beginNewTransaction (U ("Doigté automatique"));
    // Accords (même départ) : cordes différentes ; chaque note au plus près de la position de la main.
    juce::Array<te::MidiNote*> notes;
    for (auto* n : mc->getSequence().getNotes()) notes.add (n);
    std::sort (notes.begin(), notes.end(), [] (te::MidiNote* a, te::MidiNote* b)
               { return a->getStartBeat() != b->getStartBeat() ? a->getStartBeat() < b->getStartBeat() : a->getNoteNumber() > b->getNoteNumber(); });
    double hand = -1.0;
    for (int i = 0; i < notes.size();)
    {
        int j = i;
        while (j < notes.size() && std::abs (notes[j]->getStartBeat().inBeats() - notes[i]->getStartBeat().inBeats()) < 1.0e-6) ++j;
        std::vector<bool> used (strings.size(), false);
        double sum = 0; int count = 0;
        for (int k = i; k < j; ++k)
        {
            int best = -1; double bestCost = 1.0e9;
            for (int s = 0; s < (int) strings.size(); ++s)
            {
                const int fret = notes[k]->getNoteNumber() - strings[(size_t) s];
                if (used[(size_t) s] || fret < 0 || fret > 24) continue;
                const double cost = hand < 0 ? fret : std::abs (fret - hand) + (fret == 0 ? -0.5 : 0.0);
                if (cost < bestCost) { bestCost = cost; best = s; }
            }
            if (best < 0) continue;
            used[(size_t) best] = true;
            notes[k]->setColour (best + 1, um);
            const int fret = notes[k]->getNoteNumber() - strings[(size_t) best];
            if (fret > 0) { sum += fret; ++count; }
        }
        if (count > 0) hand = hand < 0 ? sum / count : hand * 0.6 + (sum / count) * 0.4;
        i = j;
    }
    repaint();
}

juce::String TabView::asciiTab() const
{
    auto* mc = clip();
    if (mc == nullptr) return {};
    const auto& strings = tunings()[(size_t) tuning].strings;
    const double step = 0.25;                                   // une colonne par double croche
    const double lengthBeats = juce::jmax (4.0, ctx.toBeats (mc->getPosition().getEnd().inSeconds()) - ctx.toBeats (mc->getPosition().getStart().inSeconds()));
    const int columns = (int) std::ceil (lengthBeats / step);
    std::vector<std::vector<juce::String>> cells (strings.size(), std::vector<juce::String> ((size_t) columns, "-"));
    for (auto* n : mc->getSequence().getNotes())
    {
        const auto [s, fret] = placeOf (*n);
        const int col = juce::roundToInt (n->getStartBeat().inBeats() / step);
        if (s >= 0 && juce::isPositiveAndBelow (col, columns)) cells[(size_t) s][(size_t) col] = juce::String (fret);
    }
    juce::String out;
    out << juce::String::fromUTF8 (tunings()[(size_t) tuning].name) << "\n";
    for (size_t s = 0; s < strings.size(); ++s)
    {
        juce::String line = (s == 0 ? shortName (strings[s]).toLowerCase() : shortName (strings[s])).paddedRight (' ', 2) + "|";
        for (int c = 0; c < columns; ++c)
        {
            line << cells[s][(size_t) c].paddedRight ('-', 3);
            if ((c + 1) % 16 == 0) line << "|";
        }
        out << line << "\n";
    }
    return out;
}

void TabView::timerCallback()
{
    if (isShowing() && ctx.edit != nullptr && ctx.edit->getTransport().isPlaying()) repaint (staff().withX (staff().getX()));
}

void TabView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1f));
    auto* mc = clip();
    g.setColour (theme::text); g.setFont (theme::ui (12.5f, true));
    g.drawText (mc ? mc->getName() : U ("Tablature"), 8, 0, 160, toolbarH, juce::Justification::centredLeft, true);
    g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
    g.drawText (U ("Clic sur une corde · chiffres = case · ↑↓ autre corde · ←→ avancer · Suppr effacer"),
                copyBtn.getRight() + 12, 0, getWidth() - copyBtn.getRight() - 20, toolbarH, juce::Justification::centredLeft, true);
    const auto& strings = tunings()[(size_t) tuning].strings;
    auto st = staff();
    // Règle (mesures).
    auto ruler = juce::Rectangle<int> (gutterW, toolbarH, getWidth() - gutterW, rulerH);
    g.setColour (juce::Colour (0xff34383e)); g.fillRect (ruler);
    const int bpb = ctx.edit != nullptr ? juce::jmax (1, ctx.edit->tempoSequence.getTimeSigAt (te::TimePosition()).numerator.get()) : 4;
    g.setFont (theme::ui (11.0f));
    for (double b = std::floor (viewBeat / bpb) * bpb; beatToX (b) < getWidth(); b += bpb)
    {
        const float x = beatToX (b);
        if (x < gutterW) continue;
        g.setColour (theme::text); g.drawText (juce::String (juce::roundToInt (b / bpb) + 1), (int) x + 3, toolbarH, 40, rulerH, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff4a4f57)); g.drawVerticalLine ((int) x, (float) st.getY(), (float) st.getBottom());
    }
    // Grille fine.
    for (double b = std::floor (viewBeat / grid()) * grid(); beatToX (b) < getWidth(); b += grid())
        if (beatToX (b) >= gutterW) { g.setColour (juce::Colour (0xff24272c)); g.drawVerticalLine ((int) beatToX (b), (float) st.getY() + 6, (float) st.getBottom() - 6); }
    // Cordes et noms.
    for (int s = 0; s < (int) strings.size(); ++s)
    {
        const int y = stringY (s);
        g.setColour (juce::Colour (0xff8a9099).withAlpha (0.55f + 0.06f * s));
        g.fillRect ((float) gutterW, (float) y - 0.5f - s * 0.12f, (float) (getWidth() - gutterW), 1.0f + s * 0.25f);
        g.setColour (theme::muted); g.setFont (theme::ui (12.0f, true));
        g.drawText (juce::String::fromUTF8 (noteNames[strings[(size_t) s] % 12]), 8, y - 9, gutterW - 16, 18, juce::Justification::centredRight);
    }
    if (mc == nullptr)
    {
        g.setColour (theme::muted); g.setFont (theme::ui (14.0f));
        g.drawText (U ("Sélectionnez un conteneur MIDI (ou créez-en un) pour écrire la tablature."), st.withY (st.getBottom()).withHeight (40), juce::Justification::centred, true);
        return;
    }
    // Curseur d'écriture.
    if (hasKeyboardFocus (true))
    {
        auto caret = juce::Rectangle<float> (beatToX (caretBeat) - 2.0f, (float) stringY (caretString) - 10.0f, (float) (grid() * pxPerBeat) + 4.0f, 20.0f);
        g.setColour (theme::accent.withAlpha (0.25f)); g.fillRoundedRectangle (caret, 4.0f);
        g.setColour (theme::accent); g.drawRoundedRectangle (caret, 4.0f, 1.5f);
    }
    // Notes : numéro de case sur la corde, trait de durée.
    g.setFont (theme::mono (12.5f, true));
    for (auto* n : mc->getSequence().getNotes())
    {
        const auto [s, fret] = placeOf (*n);
        if (s < 0) continue;
        const float x0 = beatToX (n->getStartBeat().inBeats()), x1 = beatToX (n->getEndBeat().inBeats());
        if (x1 < gutterW || x0 > getWidth()) continue;
        const int y = stringY (s);
        g.setColour (juce::Colour (0xff3fa4ff).withAlpha (0.35f)); g.fillRect (x0 + 10.0f, (float) y - 1.5f, juce::jmax (0.0f, x1 - x0 - 12.0f), 3.0f);
        const auto text = juce::String (fret);
        const float w = text.length() > 1 ? 22.0f : 16.0f;
        auto box = juce::Rectangle<float> (x0 - 1.0f, (float) y - 9.0f, w, 18.0f);
        g.setColour (juce::Colour (0xff1a1c1f)); g.fillRoundedRectangle (box, 4.0f);
        g.setColour (n->getColour() > 0 ? theme::text : juce::Colour (0xffb8c6d4)); g.drawText (text, box, juce::Justification::centred);
    }
    // Tête de lecture.
    const double playBeat = ctx.toBeats (ctx.edit->getTransport().getPosition().inSeconds()) - ctx.toBeats (mc->getPosition().getStart().inSeconds());
    const float px = beatToX (playBeat);
    if (px >= gutterW) { g.setColour (theme::cursor); g.fillRect (px, (float) st.getY(), 1.2f, (float) st.getHeight()); }
}

void TabView::resized()
{
    auto t = getLocalBounds().removeFromTop (toolbarH).reduced (8, 5);
    t.removeFromLeft (170);
    tuningBox.setBounds (t.removeFromLeft (290)); t.removeFromLeft (6);
    gridBox.setBounds (t.removeFromLeft (110)); t.removeFromLeft (6);
    autoBtn.setBounds (t.removeFromLeft (110)); t.removeFromLeft (6);
    copyBtn.setBounds (t.removeFromLeft (130));
}

void TabView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (clip() == nullptr || e.y < staff().getY() || e.x < gutterW) return;
    const auto& strings = tunings()[(size_t) tuning].strings;
    const int s = juce::jlimit (0, (int) strings.size() - 1, (e.y - staff().getY() - 6) / stringGap);
    const double b = juce::jmax (0.0, std::floor (xToBeat ((float) e.x) / grid() + 0.25) * grid());
    if (e.mods.isPopupMenu())
    {
        if (auto* n = noteAt (s, b))
        {
            ctx.edit->getUndoManager().beginNewTransaction (U ("Effacer une note"));
            clip()->getSequence().removeNote (*n, &ctx.edit->getUndoManager());
        }
    }
    moveCaret (s, b);
    digits.clear();
}

void TabView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown()) pxPerBeat = juce::jlimit (20.0, 400.0, pxPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
    else viewBeat = juce::jmax (0.0, viewBeat - (w.deltaY != 0 ? w.deltaY : w.deltaX) * 8.0);
    repaint();
}

bool TabView::keyPressed (const juce::KeyPress& k)
{
    auto* mc = clip();
    if (mc == nullptr) return false;
    const auto& strings = tunings()[(size_t) tuning].strings;
    const auto c = k.getTextCharacter();
    if (c >= '0' && c <= '9' && ! k.getModifiers().isCommandDown())
    {
        // Deux chiffres tapés vite = case à deux chiffres (12, 15…).
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastDigitMs > 700 || digits.length() >= 2) digits.clear();
        digits << juce::String::charToString (c);
        lastDigitMs = now;
        writeFret (caretString, caretBeat, juce::jmin (24, digits.getIntValue()));
        return true;
    }
    if (k == juce::KeyPress::leftKey)  { moveCaret (caretString, juce::jmax (0.0, caretBeat - grid())); digits.clear(); return true; }
    if (k == juce::KeyPress::rightKey) { moveCaret (caretString, caretBeat + grid()); digits.clear(); return true; }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        const int target = caretString + (k == juce::KeyPress::upKey ? -1 : 1);
        if (! juce::isPositiveAndBelow (target, (int) strings.size())) return true;
        if (auto* n = noteAt (caretString, caretBeat))
        {
            // Même note sur la corde voisine (si elle y est jouable).
            const int fret = n->getNoteNumber() - strings[(size_t) target];
            if (fret >= 0 && fret <= 24)
            {
                ctx.edit->getUndoManager().beginNewTransaction (U ("Changer de corde"));
                n->setColour (target + 1, &ctx.edit->getUndoManager());
            }
        }
        moveCaret (target, caretBeat);
        return true;
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        if (auto* n = noteAt (caretString, caretBeat))
        {
            ctx.edit->getUndoManager().beginNewTransaction (U ("Effacer une note"));
            mc->getSequence().removeNote (*n, &ctx.edit->getUndoManager());
            repaint();
        }
        return true;
    }
    return false;
}
