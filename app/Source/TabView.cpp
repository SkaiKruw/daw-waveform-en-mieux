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
        { "Guitare standard (Mi La Ré Sol Si Mi)", { 64, 59, 55, 50, 45, 40 }, trebleOctave },
        { "Guitare drop D (Ré La Ré Sol Si Mi)",   { 64, 59, 55, 50, 45, 38 }, trebleOctave },
        { "Guitare open G (Ré Sol Ré Sol Si Ré)",  { 62, 59, 55, 50, 43, 38 }, trebleOctave },
        { "Guitare 7 cordes (Si Mi La Ré Sol Si Mi)", { 64, 59, 55, 50, 45, 40, 35 }, trebleOctave },
        { "Guitare 8 cordes (Fa# Si Mi La Ré Sol Si Mi)", { 64, 59, 55, 50, 45, 40, 35, 30 }, trebleOctave },
        { "Guitare 12 cordes (accord standard)", { 64, 59, 55, 50, 45, 40 }, trebleOctave },
        { "Basse 4 cordes (Mi La Ré Sol)",          { 43, 38, 33, 28 }, bassOctave },
        { "Basse 5 cordes (Si Mi La Ré Sol)",       { 43, 38, 33, 28, 23 }, bassOctave },
        { "Ukulélé (Sol Do Mi La)",                  { 69, 64, 60, 67 }, treble },
        { "Guitare DADGAD (Ré La Ré Sol La Ré)",    { 62, 57, 55, 50, 45, 38 }, trebleOctave },
        { "Guitare drop C (Do Sol Do Fa La Ré)",    { 62, 57, 53, 48, 43, 36 }, trebleOctave },
        { "Basse 6 cordes (Si Mi La Ré Sol Do)",    { 48, 43, 38, 33, 28, 23 }, bassOctave },
        { "Mandoline (Sol Ré La Mi)",               { 76, 69, 62, 55 }, treble },
        { "Banjo 5 cordes (Sol Ré Sol Si Ré)",      { 62, 59, 55, 50, 67 }, trebleOctave },
        { "Violon (Sol Ré La Mi)",                  { 76, 69, 62, 55 }, treble },
        { "Alto (Do Sol Ré La)",                    { 69, 62, 55, 48 }, alto },
        { "Violoncelle (Do Sol Ré La)",             { 57, 50, 43, 36 }, bass },
    };
    return list;
}

juce::String TabView::clefName (int c)
{
    const char* names[] = { "Clé de sol octaviée (guitare)", "Clé de sol", "Clé de fa octaviée (basse)", "Clé d'ut 3e ligne (alto)", "Clé de fa" };
    return juce::String::fromUTF8 (names[juce::jlimit (0, (int) numClefs - 1, c)]);
}

int TabView::staffStep (int pitch, int c)
{
    // Hauteur écrite (guitare, basse : une octave au-dessus du son), puis degré diatonique (avec dièses).
    const int written = pitch + ((c == trebleOctave || c == bassOctave) ? 12 : 0);
    static const int letter[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    auto diatonic = [] (int m) { return (m / 12) * 7 + letter[((m % 12) + 12) % 12]; };
    const int bottomLine = (c == trebleOctave || c == treble) ? 64 : c == alto ? 53 : 43;     // Mi 4, Fa 3, Sol 2
    return diatonic (written) - diatonic (bottomLine);
}

TabView::TabView (EditContext& c) : ctx (c)
{
    for (size_t i = 0; i < tunings().size(); ++i) tuningBox.addItem (juce::String::fromUTF8 (tunings()[i].name), (int) i + 1);
    tuningBox.setSelectedId (1, juce::dontSendNotification);
    tuningName = juce::String::fromUTF8 (tunings()[0].name);
    tuningBox.onChange = [this] { if (tuningBox.getSelectedId() > 0) setTuning (tuningBox.getSelectedId() - 1); };
    tuneBtn.setTooltip (U ("Accordage libre : la note de chaque corde, le nombre de cordes, la clé de la portée"));
    tuneBtn.onClick = [this] { showTuningEditor(); };

    gridBox.addItem ("Grille 1/4", 1); gridBox.addItem ("Grille 1/8", 2); gridBox.addItem ("Grille 1/16", 3);
    gridBox.setSelectedId (2, juce::dontSendNotification);
    gridBox.onChange = [this] { repaint(); };
    autoBtn.setTooltip (U ("Répartit les notes sur les cordes au plus près de la position de la main"));
    autoBtn.onClick = [this] { autoFingering(); };
    copyBtn.setTooltip (U ("Copie la tablature en texte (pour la partager, l'imprimer, la coller dans un message)"));
    copyBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (asciiTab()); if (ctx.setStatus) ctx.setStatus (U ("Tablature copiée en texte")); };
    for (auto* comp : std::initializer_list<juce::Component*> { &tuningBox, &tuneBtn, &gridBox, &autoBtn, &copyBtn })
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
    const auto& t = tunings()[(size_t) tuning];
    setCustomTuning (t.strings, t.clef, juce::String::fromUTF8 (t.name));
}

void TabView::setCustomTuning (std::vector<int> s, int c, const juce::String& name)
{
    if (s.empty()) return;
    strings = std::move (s);
    clef = juce::jlimit (0, (int) numClefs - 1, c);
    tuningName = name;
    if (tuningName.isEmpty())
    {
        juce::StringArray n;
        for (auto it = strings.rbegin(); it != strings.rend(); ++it) n.add (juce::String::fromUTF8 (noteNames[*it % 12]));
        tuningName = U ("Accordage personnalisé (") + n.joinIntoString (" ") + ")";
    }
    int id = 0;
    for (size_t i = 0; i < tunings().size(); ++i) if (tunings()[i].strings == strings && tunings()[i].clef == clef) { id = (int) i + 1; break; }
    if (id > 0) tuning = id - 1;
    tuningBox.setSelectedId (id, juce::dontSendNotification);
    if (id == 0) tuningBox.setText (tuningName, juce::dontSendNotification);
    caretString = juce::jlimit (0, (int) strings.size() - 1, caretString);
    saveTuningToTrack();
    repaint();
}

void TabView::saveTuningToTrack()
{
    // Accordage retenu par piste (réglage d'affichage : hors historique).
    if (auto* mc = clip())
        if (auto* t = mc->getTrack())
        {
            juce::StringArray n;
            for (int p : strings) n.add (juce::String (p));
            t->state.setProperty ("ondeTuning", tuningName + "|" + juce::String (clef) + "|" + n.joinIntoString (","), nullptr);
            shownTrack = t->itemID;
        }
}

void TabView::loadTuningFromTrack()
{
    auto* mc = clip();
    auto* t = mc != nullptr ? mc->getTrack() : nullptr;
    if (t == nullptr || t->itemID == shownTrack) return;
    shownTrack = t->itemID;
    const auto v = juce::StringArray::fromTokens (t->state.getProperty ("ondeTuning").toString(), "|", "");
    if (v.size() != 3) return;
    std::vector<int> s;
    for (auto& p : juce::StringArray::fromTokens (v[2], ",", "")) s.push_back (p.getIntValue());
    if (! s.empty()) setCustomTuning (s, v[1].getIntValue(), v[0]);
}

double TabView::grid() const
{
    const double v[] = { 1.0, 0.5, 0.25 };
    return v[juce::jlimit (0, 2, gridBox.getSelectedId() - 1)];
}

juce::Rectangle<int> TabView::staff() const
{
    const int n = (int) strings.size();
    auto area = getLocalBounds().withTrimmedTop (toolbarH + rulerH + notationH).withTrimmedLeft (gutterW);
    return area.withHeight (juce::jmin (area.getHeight(), (n + 1) * stringGap));
}

float TabView::beatToX (double b) const  { return (float) staff().getX() + (float) ((b - viewBeat) * pxPerBeat); }
double TabView::xToBeat (float x) const   { return viewBeat + (x - (float) staff().getX()) / pxPerBeat; }
int TabView::stringY (int s) const        { return staff().getY() + stringGap / 2 + s * stringGap + 6; }

std::pair<int, int> TabView::placeOf (const te::MidiNote& n) const
{
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
    out << tuningName << "\n";
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
    loadTuningFromTrack();
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
    paintNotation (g, mc);
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
    tuneBtn.setBounds (t.removeFromLeft (90)); t.removeFromLeft (6);
    gridBox.setBounds (t.removeFromLeft (110)); t.removeFromLeft (6);
    autoBtn.setBounds (t.removeFromLeft (110)); t.removeFromLeft (6);
    copyBtn.setBounds (t.removeFromLeft (130));
}

void TabView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (clip() == nullptr || e.y < staff().getY() || e.x < gutterW) return;
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

//==============================================================================
// Portée : clé de l'instrument, notes à leur hauteur écrite (proportionnelles au temps), lignes supplémentaires.
juce::Rectangle<int> TabView::notation() const
{
    return { gutterW, toolbarH + rulerH, getWidth() - gutterW, notationH };
}

void TabView::paintNotation (juce::Graphics& g, te::MidiClip* mc)
{
    const auto area = notation();
    const float top = (float) area.getY() + 34.0f, bottom = top + 4.0f * lineGap;
    auto yOf = [bottom] (int step) { return bottom - step * (lineGap * 0.5f); };
    g.setColour (juce::Colour (0xff1e2024)); g.fillRect (0, area.getY(), getWidth(), area.getHeight());
    g.setColour (juce::Colour (0xff9aa0a8).withAlpha (0.7f));
    for (int l = 0; l < 5; ++l) g.fillRect ((float) gutterW - 50.0f, top + l * lineGap - 0.5f, (float) getWidth(), 1.0f);
    // Clé (symboles musicaux de la police de Windows), avec le 8 des clés octaviées.
    const juce::Font sym (juce::FontOptions ("Segoe UI Symbol", 52.0f, juce::Font::plain));
    g.setColour (theme::text); g.setFont (sym);
    if (clef == trebleOctave || clef == treble)
        g.drawText (juce::String::fromUTF8 ("\xF0\x9D\x84\x9E"), juce::Rectangle<float> (gutterW - 46.0f, top - 22.0f, 34.0f, 84.0f), juce::Justification::centred, false);
    else if (clef == alto)
        g.drawText (juce::String::fromUTF8 ("\xF0\x9D\x84\xA1"), juce::Rectangle<float> (gutterW - 46.0f, top - 4.0f, 34.0f, 44.0f), juce::Justification::centred, false);
    else
        g.drawText (juce::String::fromUTF8 ("\xF0\x9D\x84\xA2"), juce::Rectangle<float> (gutterW - 46.0f, top - 6.0f, 34.0f, 44.0f), juce::Justification::centred, false);
    if (clef == trebleOctave || clef == bassOctave)
    {
        g.setFont (theme::ui (10.0f, true));
        g.drawText ("8", juce::Rectangle<float> (gutterW - 36.0f, clef == trebleOctave ? bottom + 16.0f : bottom + 2.0f, 14.0f, 12.0f), juce::Justification::centred);
    }
    g.setColour (theme::dim); g.setFont (theme::ui (10.0f));
    g.drawText (clefName (clef), 8, area.getY() + 2, 260, 14, juce::Justification::centredLeft);
    if (mc == nullptr) return;
    // Barres de mesure.
    const int bpb = ctx.edit != nullptr ? juce::jmax (1, ctx.edit->tempoSequence.getTimeSigAt (te::TimePosition()).numerator.get()) : 4;
    g.setColour (juce::Colour (0xff9aa0a8).withAlpha (0.6f));
    for (double b = std::floor (viewBeat / bpb) * bpb; beatToX (b) < getWidth(); b += bpb)
        if (beatToX (b) > gutterW + 2) g.fillRect (beatToX (b) - 0.5f, top, 1.0f, bottom - top);
    // Notes.
    static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    for (auto* n : mc->getSequence().getNotes())
    {
        const float x = beatToX (n->getStartBeat().inBeats()) + 6.0f;
        if (x < gutterW || x > getWidth()) continue;
        const int pitch = n->getNoteNumber(), step = staffStep (pitch, clef);
        const float y = yOf (step);
        g.setColour (juce::Colour (0xff9aa0a8));
        for (int l = -2; l >= step; l -= 2) g.fillRect (x - 8.0f, yOf (l) - 0.5f, 16.0f, 1.0f);
        for (int l = 10; l <= step; l += 2) g.fillRect (x - 8.0f, yOf (l) - 0.5f, 16.0f, 1.0f);
        const double len = n->getLengthBeats().inBeats();
        g.setColour (n->getColour() > 0 ? theme::text : juce::Colour (0xffb8c6d4));
        auto head = juce::Rectangle<float> (x - 5.5f, y - 3.8f, 11.0f, 7.6f);
        if (len >= 2.0) g.drawEllipse (head.reduced (0.6f), 1.6f); else g.fillEllipse (head);
        if (len < 4.0)
        {
            const bool up = step < 4;
            const float sx = up ? head.getRight() - 0.8f : head.getX() + 0.2f;
            g.fillRect (sx, up ? y - 3.5f * lineGap : y, 1.2f, 3.5f * lineGap);
            if (len <= 0.5) g.drawLine (sx, up ? y - 3.5f * lineGap : y + 3.5f * lineGap, sx + 7.0f, up ? y - 2.3f * lineGap : y + 2.3f * lineGap, 1.6f);
        }
        if (black[pitch % 12])
        {
            g.setFont (theme::ui (13.0f, true));
            g.drawText (juce::String::fromUTF8 ("\xE2\x99\xAF"), juce::Rectangle<float> (x - 19.0f, y - 8.0f, 12.0f, 16.0f), juce::Justification::centred);
        }
    }
}

//==============================================================================
// Accordage libre : note de chaque corde, nombre de cordes, clé de la portée.
namespace
{
    juce::String noteWithOctave (int p) { return juce::String::fromUTF8 (noteNames[p % 12]) + juce::String (p / 12 - 1); }

    class TuningEditor  : public juce::Component
    {
    public:
        explicit TuningEditor (TabView& v) : view (&v), strings (v.getStrings()), clef (v.getClef())
        {
            addBtn.onClick = [this] { if (strings.size() < 10) { strings.push_back (juce::jmax (16, strings.back() - 5)); apply(); rebuild(); } };
            removeBtn.onClick = [this] { if (strings.size() > 1) { strings.pop_back(); apply(); rebuild(); } };
            for (int c = 0; c < TabView::numClefs; ++c) clefBox.addItem (TabView::clefName (c), c + 1);
            clefBox.setSelectedId (clef + 1, juce::dontSendNotification);
            clefBox.onChange = [this] { clef = clefBox.getSelectedId() - 1; apply(); };
            for (auto* c : std::initializer_list<juce::Component*> { &addBtn, &removeBtn, &clefBox }) addAndMakeVisible (c);
            rebuild();
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::text); g.setFont (theme::ui (13.0f, true));
            g.drawText (U ("Accordage (corde la plus aiguë en haut)"), 12, 8, getWidth() - 24, 18, juce::Justification::centredLeft);
            g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
            for (int i = 0; i < boxes.size(); ++i) g.drawText (U ("Corde ") + juce::String (i + 1), 12, boxes[i]->getY(), 70, 24, juce::Justification::centredLeft);
        }
        void resized() override
        {
            int y = 34;
            for (auto* b : boxes) { b->setBounds (84, y, getWidth() - 96, 24); y += 28; }
            y += 6;
            addBtn.setBounds (12, y, (getWidth() - 30) / 2, 26);
            removeBtn.setBounds (addBtn.getRight() + 6, y, (getWidth() - 30) / 2, 26);
            clefBox.setBounds (12, y + 34, getWidth() - 24, 26);
        }
    private:
        void rebuild()
        {
            boxes.clear();
            for (size_t i = 0; i < strings.size(); ++i)
            {
                auto* b = boxes.add (new juce::ComboBox());
                for (int p = 16; p <= 88; ++p) b->addItem (noteWithOctave (p), p);
                b->setSelectedId (strings[i], juce::dontSendNotification);
                b->onChange = [this, i, b] { strings[i] = b->getSelectedId(); apply(); };
                addAndMakeVisible (b);
            }
            setSize (300, 34 + (int) strings.size() * 28 + 6 + 26 + 8 + 26 + 12);
            resized();
            repaint();
        }
        void apply() { if (view != nullptr) view->setCustomTuning (strings, clef); }
        juce::Component::SafePointer<TabView> view;
        std::vector<int> strings;
        int clef;
        juce::OwnedArray<juce::ComboBox> boxes;
        juce::TextButton addBtn { U ("+ corde grave") }, removeBtn { U ("- corde") };
        juce::ComboBox clefBox;
    };
}

void TabView::showTuningEditor()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<TuningEditor> (*this), tuneBtn.getScreenBounds(), nullptr);
}
