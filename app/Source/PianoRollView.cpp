#include "PianoRollView.h"

namespace
{
    const char* names[] = { "Do", "Do#", "R\xc3\xa9", "R\xc3\xa9#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    bool isBlack (int pitch) { const int pc = pitch % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    const int scales[][8] = { { 0, 2, 4, 5, 7, 9, 11, -1 }, { 0, 2, 3, 5, 7, 8, 10, -1 }, { 0, 2, 3, 5, 7, 9, 10, -1 },
                              { 0, 2, 3, 5, 7, 8, 11, -1 }, { 0, 3, 5, 7, 10, -1, -1, -1 }, { 0, 2, 4, 7, 9, -1, -1, -1 } };
    juce::Colour velocityColour (int v)
    {
        const float n = juce::jlimit (0.0f, 1.0f, v / 127.0f);
        return juce::Colour::fromHSV (0.62f - 0.62f * n, 0.62f, 0.55f + 0.4f * n, 1.0f);
    }
    juce::String noteName (int p) { return juce::String::fromUTF8 (names[p % 12]) + juce::String (p / 12 - 2); }   // Do3 = 60
}

PianoRollView::PianoRollView (EditContext& c) : ctx (c)
{
    gridBox.addItem ("Grille 1/4", 1); gridBox.addItem ("Grille 1/8", 2); gridBox.addItem ("Grille 1/16", 3); gridBox.addItem ("Grille 1/32", 4);
    gridBox.setSelectedId (3, juce::dontSendNotification);
    for (int i = 0; i < 12; ++i) rootBox.addItem (juce::String::fromUTF8 (names[i]), i + 1);
    rootBox.setSelectedId (3, juce::dontSendNotification);   // Ré
    scaleBox.addItem ("Majeur", 1); scaleBox.addItem ("Mineur", 2); scaleBox.addItem ("Dorien", 3);
    scaleBox.addItem ("Mineur harmonique", 4); scaleBox.addItem ("Penta mineure", 5); scaleBox.addItem ("Penta majeure", 6);
    scaleBox.setSelectedId (2, juce::dontSendNotification);
    for (auto* b : { &gridBox, &rootBox, &scaleBox }) { b->setWantsKeyboardFocus (false); b->onChange = [this] { repaint(); }; addAndMakeVisible (*b); }
    addAndMakeVisible (scaleToggle);
    scaleToggle.setWantsKeyboardFocus (false);
    scaleToggle.onClick = [this] { repaint(); };
    for (auto* b : { &quantBtn, &fitBtn }) { styleButton (*b); addAndMakeVisible (*b); }
    quantBtn.setTooltip (U ("Quantifier les notes sélectionnées (ou toutes) sur la grille  Q"));
    quantBtn.onClick = [this] { quantise(); };
    fitBtn.onClick = [this] { framedClip = {}; frameNotes(); repaint(); };
    ctx.addChangeListener (this);
    startTimerHz (20);
}

PianoRollView::~PianoRollView() { ctx.removeChangeListener (this); }

void PianoRollView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    validateSelection();
    repaint();
}

void PianoRollView::validateSelection()
{
    auto* mc = ctx.getSelectedMidiClip();
    if (mc == nullptr) { selected.clear(); return; }
    auto& notes = mc->getSequence().getNotes();
    for (int i = selected.size(); --i >= 0;)
        if (! notes.contains (selected[i])) selected.remove (i);
    if (dragNote != nullptr && ! notes.contains (dragNote)) dragNote = nullptr;
}

double PianoRollView::gridBeats() const
{
    const double v[] = { 1.0, 0.5, 0.25, 0.125 };
    return v[juce::jlimit (0, 3, gridBox.getSelectedId() - 1)];
}

double PianoRollView::snapBeat (double b, const juce::ModifierKeys& m) const
{
    if (m.isCtrlDown() || ! ctx.snapOn) return b;
    return std::round (b / gridBeats()) * gridBeats();
}

bool PianoRollView::inScale (int pitch) const
{
    const int root = rootBox.getSelectedId() - 1;
    const auto* s = scales[juce::jlimit (0, 5, scaleBox.getSelectedId() - 1)];
    const int pc = ((pitch - root) % 12 + 12) % 12;
    for (int i = 0; i < 8 && s[i] >= 0; ++i) if (s[i] == pc) return true;
    return false;
}

juce::Rectangle<int> PianoRollView::gridArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarH + rulerH).withTrimmedLeft (keysW).withTrimmedBottom (velH);
}

juce::Rectangle<int> PianoRollView::velArea() const
{
    return getLocalBounds().removeFromBottom (velH).withTrimmedLeft (keysW).withTrimmedTop (4);
}

float PianoRollView::beatToX (double b) const  { return (float) gridArea().getX() + (float) ((b - viewBeat) * pxPerBeat); }
double PianoRollView::xToBeat (float x) const   { return viewBeat + (x - (float) gridArea().getX()) / pxPerBeat; }
int PianoRollView::pitchToY (int p) const        { return gridArea().getY() + (topPitch - p) * rowH; }
int PianoRollView::yToPitch (int y) const        { return topPitch - (int) std::floor ((y - gridArea().getY()) / (double) rowH); }

te::MidiNote* PianoRollView::noteAt (juce::Point<int> pt, bool& edge) const
{
    edge = false;
    auto* mc = ctx.getSelectedMidiClip();
    if (mc == nullptr) return nullptr;
    for (auto* n : mc->getSequence().getNotes())
    {
        const float x0 = beatToX (n->getStartBeat().inBeats()), x1 = beatToX (n->getEndBeat().inBeats());
        const int y = pitchToY (n->getNoteNumber());
        if (pt.y >= y && pt.y < y + rowH && pt.x >= x0 && pt.x <= x1)
        {
            edge = x1 - (float) pt.x < juce::jmin (7.0f, (x1 - x0) * 0.35f);
            return n;
        }
    }
    return nullptr;
}

void PianoRollView::frameNotes()
{
    auto* mc = ctx.getSelectedMidiClip();
    if (mc == nullptr || mc->itemID == framedClip || gridArea().getWidth() <= 0) return;
    framedClip = mc->itemID;
    selected.clear();
    const int visibleRows = juce::jmax (12, gridArea().getHeight() / rowH);
    auto range = mc->getSequence().getNoteNumberRange();
    const int centre = mc->getSequence().getNumNotes() > 0 ? (range.getStart() + range.getEnd()) / 2 : 60;
    topPitch = juce::jlimit (visibleRows, 127, centre + visibleRows / 2);
    const double beats = juce::jmax (4.0, ctx.toBeats (mc->getPosition().getEnd().inSeconds()) - ctx.toBeats (mc->getPosition().getStart().inSeconds()));
    viewBeat = 0.0;
    pxPerBeat = juce::jlimit (6.0, 400.0, (gridArea().getWidth() - 20) / beats);
}

void PianoRollView::audition (int pitch, int velocity)
{
    auto* mc = ctx.getSelectedMidiClip();
    if (mc == nullptr || ctx.edit->getTransport().isPlaying()) return;
    auto* t = dynamic_cast<te::AudioTrack*> (mc->getTrack());
    if (t == nullptr || ctx.getInstrument (*t) == nullptr) return;
    t->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) juce::jlimit (1, 127, velocity)), sourceID);
    juce::Component::SafePointer<PianoRollView> safe (this);
    const auto id = t->itemID;
    juce::Timer::callAfterDelay (220, [safe, id, pitch]
    {
        if (safe == nullptr || safe->ctx.edit == nullptr) return;
        if (auto* tr = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*safe->ctx.edit, id)))
            tr->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, pitch), safe->sourceID);
    });
}

void PianoRollView::resized()
{
    auto t = getLocalBounds().removeFromTop (toolbarH).reduced (8, 5);
    t.removeFromLeft (300);
    gridBox.setBounds (t.removeFromLeft (110)); t.removeFromLeft (6);
    quantBtn.setBounds (t.removeFromLeft (90)); t.removeFromLeft (16);
    scaleToggle.setBounds (t.removeFromLeft (150));
    rootBox.setBounds (t.removeFromLeft (64)); t.removeFromLeft (4);
    scaleBox.setBounds (t.removeFromLeft (140)); t.removeFromLeft (12);
    fitBtn.setBounds (t.removeFromLeft (80));
}

void PianoRollView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1f));
    validateSelection();
    frameNotes();
    auto* mc = ctx.getSelectedMidiClip();

    // Barre d'outils.
    auto bar = getLocalBounds().removeFromTop (toolbarH);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff33363c), 0, 0, juce::Colour (0xff2a2d31), 0, (float) toolbarH, false));
    g.fillRect (bar);
    g.setColour (theme::line); g.drawHorizontalLine (toolbarH - 1, 0.0f, (float) getWidth());
    g.setColour (theme::text); g.setFont (theme::ui (13.0f, true));
    g.drawText (mc ? mc->getName() : U ("Éditeur de notes"), 10, 0, 160, toolbarH, juce::Justification::centredLeft, true);
    g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
    g.drawText (mc ? (mc->getTrack()->getName() + U ("  ·  ") + juce::String (mc->getSequence().getNumNotes()) + " notes" + (selected.isEmpty() ? juce::String() : U ("  ·  ") + juce::String (selected.size()) + U (" sél.")))
                   : U ("Aucun conteneur MIDI sélectionné"), 150, 0, 150, toolbarH, juce::Justification::centredLeft, true);

    auto grid = gridArea();
    const double clipStartBeats = mc ? ctx.toBeats (mc->getPosition().getStart().inSeconds()) : 0.0;
    const double clipBeats = mc ? ctx.toBeats (mc->getPosition().getEnd().inSeconds()) - clipStartBeats : 16.0;
    const int bpb = ctx.beatsPerBar();
    const bool scaleOn = scaleToggle.getToggleState();

    // Lignes de notes + clavier.
    g.saveState();
    g.reduceClipRegion (getLocalBounds().withTrimmedTop (toolbarH + rulerH).withTrimmedBottom (velH));
    for (int p = topPitch; pitchToY (p) < grid.getBottom() && p >= 0; --p)
    {
        const int y = pitchToY (p);
        juce::Colour row = isBlack (p) ? juce::Colour (0xff1c1e21) : juce::Colour (0xff24272b);
        if (scaleOn && inScale (p)) row = row.brighter (0.12f);
        if (scaleOn && ! inScale (p)) row = row.darker (0.25f);
        g.setColour (row); g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
        g.setColour (p % 12 == 0 ? theme::lineBar : juce::Colour (0xff1a1c1f)); g.drawHorizontalLine (y + rowH - 1, (float) grid.getX(), (float) grid.getRight());
        // Touche.
        auto key = juce::Rectangle<int> (0, y, keysW - 1, rowH);
        g.setColour (isBlack (p) ? juce::Colour (0xff17181a) : juce::Colour (0xffe6e8eb)); g.fillRect (key.withTrimmedRight (isBlack (p) ? 22 : 0));
        if (isBlack (p)) { g.setColour (juce::Colour (0xffe6e8eb)); g.fillRect (key.removeFromRight (22)); }
        g.setColour (juce::Colour (0xff9aa0a8)); g.drawHorizontalLine (y + rowH - 1, 0.0f, (float) keysW - 1);
        if (p == lastAuditionPitch) { g.setColour (theme::accent.withAlpha (0.6f)); g.fillRect (0, y, keysW - 1, rowH); }
        if (p % 12 == 0 && rowH >= 9)
        {
            g.setColour (juce::Colour (0xff2a2d31)); g.setFont (theme::ui (juce::jmin (11.0f, rowH - 1.0f)));
            g.drawText (noteName (p), 2, y, keysW - 6, rowH, juce::Justification::centredRight);
        }
    }
    // Zone hors conteneur assombrie.
    if (mc)
    {
        const float xe = beatToX (clipBeats);
        if (xe < grid.getRight()) { g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (xe, (float) grid.getY(), grid.getRight() - xe, (float) grid.getHeight()); }
    }
    // Temps.
    const double step = pxPerBeat * gridBeats() >= 8 ? gridBeats() : (pxPerBeat >= 8 ? 1.0 : (double) bpb);
    for (double b = std::floor (viewBeat / step) * step; beatToX (b) <= grid.getRight(); b += step)
    {
        const float x = beatToX (b);
        if (x < grid.getX()) continue;
        const double abs = b + clipStartBeats;
        const bool barLine = std::abs (std::fmod (abs + 1e-6, (double) bpb)) < 1e-4, beatLine = std::abs (std::fmod (abs + 1e-6, 1.0)) < 1e-4;
        g.setColour (barLine ? juce::Colour (0xff50555d) : beatLine ? juce::Colour (0xff363a40) : juce::Colour (0xff2b2e33));
        g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
    }
    // Notes.
    if (mc != nullptr)
    {
        for (auto* n : mc->getSequence().getNotes())
        {
            const float x0 = beatToX (n->getStartBeat().inBeats()), x1 = beatToX (n->getEndBeat().inBeats());
            const int y = pitchToY (n->getNoteNumber());
            if (y + rowH < grid.getY() || y > grid.getBottom() || x1 < grid.getX() || x0 > grid.getRight()) continue;
            auto r = juce::Rectangle<float> (x0, (float) y, juce::jmax (3.0f, x1 - x0), (float) rowH - 1).reduced (0.5f, 0.5f);
            const bool sel = selected.contains (n);
            g.setColour (velocityColour (n->getVelocity()).withMultipliedBrightness (sel ? 1.25f : 1.0f)); g.fillRect (r);
            g.setColour (sel ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f)); g.drawRect (r, sel ? 1.5f : 1.0f);
            if (rowH >= 12 && r.getWidth() > 34) { g.setColour (juce::Colours::black.withAlpha (0.7f)); g.setFont (theme::ui (9.5f)); g.drawText (noteName (n->getNoteNumber()), r.reduced (3, 0), juce::Justification::centredLeft, false); }
        }
        // Curseur.
        const double t = ctx.edit->getTransport().getPosition().inSeconds();
        const double b = mc->getContentBeatAtTime (te::TimePosition::fromSeconds (t)).inBeats();
        const float cx = beatToX (b);
        if (cx >= grid.getX() && cx <= grid.getRight()) { g.setColour (theme::cursor); g.fillRect (cx, (float) grid.getY() - rulerH, 1.2f, (float) grid.getHeight() + rulerH + velH); }
    }
    if (! rubberRect.isEmpty()) { g.setColour (theme::accent.withAlpha (0.15f)); g.fillRect (rubberRect); g.setColour (theme::accent); g.drawRect (rubberRect, 1); }
    g.restoreState();

    // Règle.
    auto ruler = juce::Rectangle<int> (keysW, toolbarH, getWidth() - keysW, rulerH);
    g.setColour (juce::Colour (0xff34383e)); g.fillRect (ruler);
    g.setColour (juce::Colour (0xff2a2d31)); g.fillRect (0, toolbarH, keysW, rulerH);
    g.setFont (theme::ui (11.0f));
    for (double b = std::ceil ((viewBeat + clipStartBeats) / bpb) * bpb - clipStartBeats; beatToX (b) <= getWidth(); b += bpb)
    {
        const float x = beatToX (b);
        if (x < keysW) continue;
        g.setColour (theme::text.withAlpha (0.6f)); g.drawVerticalLine ((int) x, (float) toolbarH + 6, (float) toolbarH + rulerH);
        g.setColour (theme::text); g.drawText (juce::String (juce::roundToInt ((b + clipStartBeats) / bpb) + 1), (int) x + 3, toolbarH, 40, rulerH, juce::Justification::centredLeft);
    }
    g.setColour (theme::line); g.drawHorizontalLine (toolbarH + rulerH - 1, 0.0f, (float) getWidth());

    // Vélocité.
    auto vel = velArea();
    g.setColour (juce::Colour (0xff202225)); g.fillRect (getLocalBounds().removeFromBottom (velH));
    g.setColour (theme::line); g.drawHorizontalLine (getHeight() - velH, 0.0f, (float) getWidth());
    g.setColour (theme::muted); g.setFont (theme::ui (11.0f, true));
    g.drawText (U ("Vélocité"), 6, vel.getY() + 2, keysW - 10, 14, juce::Justification::left);
    g.setColour (theme::lineSoft); g.drawHorizontalLine (vel.getY() + vel.getHeight() / 2, (float) vel.getX(), (float) vel.getRight());
    if (mc != nullptr)
    {
        for (auto* n : mc->getSequence().getNotes())
        {
            const float x0 = beatToX (n->getStartBeat().inBeats());
            if (x0 < vel.getX() || x0 > vel.getRight()) continue;
            const float h = (n->getVelocity() / 127.0f) * (vel.getHeight() - 6);
            g.setColour (velocityColour (n->getVelocity()).withAlpha (selected.contains (n) || selected.isEmpty() ? 1.0f : 0.4f));
            g.fillRect (x0, (float) vel.getBottom() - h - 2, 3.0f, h);
            g.fillRect (x0 - 2.0f, (float) vel.getBottom() - h - 3, 7.0f, 2.0f);
        }
    }
    else
    {
        g.setColour (theme::muted); g.setFont (theme::ui (14.0f));
        g.drawText (U ("Sélectionnez un conteneur MIDI dans la fenêtre de projet (double-clic pour l'ouvrir ici)."), grid.reduced (40), juce::Justification::centred, true);
    }
}

void PianoRollView::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    auto* n = gridArea().contains (e.getPosition()) ? noteAt (e.getPosition(), edge) : nullptr;
    auto c = juce::MouseCursor::NormalCursor;
    if (ctx.tool == Tool::draw && n == nullptr) c = juce::MouseCursor::CrosshairCursor;
    else if (ctx.tool == Tool::erase) c = juce::MouseCursor::PointingHandCursor;
    else if (n != nullptr) c = edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
    setMouseCursor (c);
}

void PianoRollView::mouseDown (const juce::MouseEvent& e)
{
    auto* mc = ctx.getSelectedMidiClip();
    mode = none;
    if (e.x < keysW && e.y > toolbarH + rulerH && e.y < getHeight() - velH) { lastAuditionPitch = yToPitch (e.y); audition (lastAuditionPitch, 100); return; }
    if (mc == nullptr) return;
    auto* um = &ctx.edit->getUndoManager();
    auto& seq = mc->getSequence();

    if (e.y > getHeight() - velH && e.x > keysW) { um->beginNewTransaction (U ("Vélocité")); mode = velocity; mouseDrag (e); return; }
    if (e.y > toolbarH && e.y < toolbarH + rulerH && e.x > keysW)
    {
        ctx.edit->getTransport().setPosition (mc->getTimeOfContentBeat (te::BeatPosition::fromBeats (juce::jmax (0.0, xToBeat ((float) e.x)))));
        return;
    }
    if (! gridArea().contains (e.getPosition())) return;

    bool edge = false;
    auto* n = noteAt (e.getPosition(), edge);
    if (ctx.tool == Tool::erase || (n != nullptr && (e.mods.isPopupMenu() || e.getNumberOfClicks() == 2)))
    {
        um->beginNewTransaction (U ("Effacer des notes"));
        mode = erase;
        if (n) { selected.removeFirstMatchingValue (n); seq.removeNote (*n, um); }
        dragNote = nullptr;
        return;
    }
    const bool create = n == nullptr && (ctx.tool == Tool::draw || e.getNumberOfClicks() == 2);
    if (n == nullptr && ! create)
    {
        if (! e.mods.isShiftDown()) selected.clear();
        mode = rubber;
        rubberStart = e.getPosition();
        rubberRect = {};
        repaint();
        return;
    }
    um->beginNewTransaction (U ("Modifier les notes"));
    if (create)
    {
        int pitch = yToPitch (e.y);
        if (scaleToggle.getToggleState()) while (! inScale (pitch) && pitch > 0) --pitch;
        const double b = juce::jmax (0.0, std::floor (xToBeat ((float) e.x) / gridBeats()) * gridBeats());
        n = seq.addNote (pitch, te::BeatPosition::fromBeats (e.mods.isCtrlDown() ? xToBeat ((float) e.x) : b), te::BeatDuration::fromBeats (gridBeats()), 100, 0, um);
        selected.clear();
        selected.add (n);
        mode = Mode::create;
    }
    else
    {
        if (e.mods.isShiftDown()) { if (selected.contains (n)) selected.removeFirstMatchingValue (n); else selected.add (n); }
        else if (! selected.contains (n)) { selected.clear(); selected.add (n); }
        mode = edge ? resize : move;
        if (e.mods.isAltDown() && mode == move)
        {
            // Alt + glisser : copie des notes sélectionnées.
            juce::Array<te::MidiNote*> copies;
            for (auto* s : selected) copies.add (seq.addNote (s->getNoteNumber(), s->getStartBeat(), s->getLengthBeats(), s->getVelocity(), 0, um));
            selected = copies;
            n = copies.getFirst();
        }
    }
    dragNote = n;
    lastAuditionPitch = n->getNoteNumber();
    audition (n->getNoteNumber(), n->getVelocity());
    dragStarts.clear(); dragPitches.clear(); dragLens.clear();
    for (auto* s : selected) { dragStarts.add (s->getStartBeat().inBeats()); dragPitches.add (s->getNoteNumber()); dragLens.add (s->getLengthBeats().inBeats()); }
    repaint();
}

void PianoRollView::mouseDrag (const juce::MouseEvent& e)
{
    auto* mc = ctx.getSelectedMidiClip();
    if (mc == nullptr) return;
    auto* um = &ctx.edit->getUndoManager();

    if (mode == velocity)
    {
        auto vel = velArea();
        const int v = juce::jlimit (1, 127, (int) ((vel.getBottom() - 2 - e.y) / (float) (vel.getHeight() - 6) * 127));
        for (auto* n : mc->getSequence().getNotes())
            if ((selected.isEmpty() || selected.contains (n)) && std::abs (beatToX (n->getStartBeat().inBeats()) - (float) e.x) < 6.0f) n->setVelocity (v, um);
        repaint();
        return;
    }
    if (mode == erase)
    {
        bool edge = false;
        if (auto* n = noteAt (e.getPosition(), edge)) { selected.removeFirstMatchingValue (n); mc->getSequence().removeNote (*n, um); }
        repaint();
        return;
    }
    if (mode == rubber)
    {
        rubberRect = juce::Rectangle<int> (rubberStart, e.getPosition());
        for (auto* n : mc->getSequence().getNotes())
        {
            auto r = juce::Rectangle<float> (beatToX (n->getStartBeat().inBeats()), (float) pitchToY (n->getNoteNumber()),
                                             juce::jmax (3.0f, beatToX (n->getEndBeat().inBeats()) - beatToX (n->getStartBeat().inBeats())), (float) rowH);
            const bool inside = rubberRect.toFloat().intersects (r);
            if (inside && ! selected.contains (n)) selected.add (n);
            if (! inside && ! e.mods.isShiftDown()) selected.removeFirstMatchingValue (n);
        }
        repaint();
        return;
    }
    if (dragNote == nullptr) return;
    const double dBeat = (double) e.getDistanceFromDragStartX() / pxPerBeat;
    if (mode == move)
    {
        const int dp = (int) std::round (-(double) e.getDistanceFromDragStartY() / rowH);
        const double anchor = dragStarts[selected.indexOf (dragNote)];
        const double shift = snapBeat (anchor + dBeat, e.mods) - anchor;
        for (int i = 0; i < selected.size() && i < dragStarts.size(); ++i)
        {
            int pitch = juce::jlimit (0, 127, dragPitches[i] + dp);
            if (scaleToggle.getToggleState()) while (! inScale (pitch) && pitch > 0) --pitch;
            selected[i]->setStartAndLength (te::BeatPosition::fromBeats (juce::jmax (0.0, dragStarts[i] + shift)), te::BeatDuration::fromBeats (dragLens[i]), um);
            if (pitch != selected[i]->getNoteNumber()) selected[i]->setNoteNumber (pitch, um);
        }
        if (dragNote->getNoteNumber() != lastAuditionPitch) { lastAuditionPitch = dragNote->getNoteNumber(); audition (lastAuditionPitch, dragNote->getVelocity()); }
    }
    else if (mode == resize || mode == create)
    {
        for (int i = 0; i < selected.size() && i < dragLens.size(); ++i)
        {
            const double g = (e.mods.isCtrlDown() || ! ctx.snapOn) ? 0.02 : gridBeats();
            const double len = juce::jmax (g, snapBeat (dragStarts[i] + dragLens[i] + dBeat, e.mods) - dragStarts[i]);
            selected[i]->setStartAndLength (selected[i]->getStartBeat(), te::BeatDuration::fromBeats (len), um);
        }
    }
    repaint();
}

void PianoRollView::mouseUp (const juce::MouseEvent&)
{
    mode = none;
    rubberRect = {};
    lastAuditionPitch = -1;
    ctx.changed();
}

void PianoRollView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() && e.mods.isShiftDown()) rowH = juce::jlimit (6, 30, rowH + (w.deltaY > 0 ? 1 : -1));
    else if (e.mods.isCtrlDown())
    {
        const double anchor = xToBeat ((float) e.x);
        pxPerBeat = juce::jlimit (4.0, 600.0, pxPerBeat * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
        viewBeat = juce::jmax (0.0, anchor - (e.x - gridArea().getX()) / pxPerBeat);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        viewBeat = juce::jmax (0.0, viewBeat - (w.deltaY + w.deltaX) * 200.0 / pxPerBeat);
    else topPitch = juce::jlimit (12, 127, topPitch + (w.deltaY > 0 ? 3 : -3));
    repaint();
}

bool PianoRollView::deleteSelectedNotes()
{
    auto* mc = ctx.getSelectedMidiClip();
    validateSelection();
    if (mc == nullptr || selected.isEmpty()) return false;
    auto* um = &ctx.edit->getUndoManager();
    um->beginNewTransaction (U ("Supprimer des notes"));
    for (auto* n : selected) mc->getSequence().removeNote (*n, um);
    selected.clear();
    ctx.changed();
    return true;
}

void PianoRollView::transposeSelected (int semis)
{
    auto* mc = ctx.getSelectedMidiClip();
    validateSelection();
    if (mc == nullptr || selected.isEmpty()) return;
    auto* um = &ctx.edit->getUndoManager();
    um->beginNewTransaction ("Transposer");
    for (auto* n : selected) n->setNoteNumber (juce::jlimit (0, 127, n->getNoteNumber() + semis), um);
    audition (selected.getFirst()->getNoteNumber(), selected.getFirst()->getVelocity());
    repaint();
}

void PianoRollView::quantise()
{
    auto* mc = ctx.getSelectedMidiClip();
    if (mc == nullptr) return;
    validateSelection();
    auto* um = &ctx.edit->getUndoManager();
    um->beginNewTransaction ("Quantifier");
    const double g = gridBeats();
    for (auto* n : mc->getSequence().getNotes())
        if (selected.isEmpty() || selected.contains (n))
            n->setStartAndLength (te::BeatPosition::fromBeats (std::round (n->getStartBeat().inBeats() / g) * g),
                                  te::BeatDuration::fromBeats (juce::jmax (g, std::round (n->getLengthBeats().inBeats() / g) * g)), um);
    if (ctx.setStatus) ctx.setStatus (U ("Notes quantifiées sur la grille"));
    ctx.changed();
}
