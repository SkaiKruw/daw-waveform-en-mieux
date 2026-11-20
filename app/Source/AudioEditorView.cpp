#include "AudioEditorView.h"

AudioEditorView::AudioEditorView (EditContext& c) : ctx (c)
{
    formats.registerBasicFormats();
    thumb.addChangeListener (this);

    gain.setSliderStyle (juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gain.setRange (-24.0, 24.0, 0.1);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTooltip (U ("Gain de l'événement (non destructif) · double-clic : 0 dB"));
    gain.onDragStart = [this] { if (ctx.edit) ctx.edit->getUndoManager().beginNewTransaction ("Gain"); };
    gain.onValueChange = [this] { if (auto* c = clip()) { c->setGainDB ((float) gain.getValue()); repaint(); } };

    pitch.setSliderStyle (juce::Slider::LinearHorizontal);
    pitch.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pitch.setRange (-12.0, 12.0, 1.0);
    pitch.setDoubleClickReturnValue (true, 0.0);
    pitch.setTooltip (U ("Transposition en demi-tons (durée conservée) · double-clic : 0"));
    pitch.onDragStart = [this] { if (ctx.edit) ctx.edit->getUndoManager().beginNewTransaction ("Transposition"); };
    pitch.onValueChange = [this] { if (auto* c = clip()) c->setPitchChange ((float) pitch.getValue()); };

    for (auto* s : { &gain, &pitch }) { s->setWantsKeyboardFocus (false); addAndMakeVisible (*s); }
    for (auto* l : { &gainLabel, &pitchLabel })
    {
        l->setFont (theme::mono (11.5f)); l->setColour (juce::Label::textColourId, theme::lcd);
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }

    for (auto* b : { &normaliseBtn, &reverseBtn, &trimBtn, &deleteBtn, &fitBtn }) { styleButton (*b); addAndMakeVisible (*b); }
    reverseBtn.setClickingTogglesState (true);
    reverseBtn.setColour (juce::TextButton::buttonOnColourId, theme::accentBg);
    reverseBtn.setColour (juce::TextButton::textColourOnId, theme::accent);
    normaliseBtn.setTooltip (U ("Ajuste le gain pour que la crête atteigne -0,1 dB"));
    reverseBtn.setTooltip (U ("Lit l'événement à l'envers"));
    trimBtn.setTooltip (U ("Ne garde que la plage sélectionnée (glisser sur la forme d'onde)"));
    deleteBtn.setTooltip (U ("Retire la plage sélectionnée de l'événement (le coupe en deux)"));

    normaliseBtn.onClick = [this]
    {
        if (auto* c = clip())
        {
            ctx.edit->getUndoManager().beginNewTransaction ("Normaliser");
            if (normalise (*c) && ctx.setStatus) ctx.setStatus (U ("Normalisé : gain ") + juce::String (c->getGainDB(), 1) + " dB");
            syncControls(); repaint();
        }
    };
    reverseBtn.onClick = [this]
    {
        if (auto* c = clip())
        {
            ctx.edit->getUndoManager().beginNewTransaction ("Inverser");
            c->setIsReversed (reverseBtn.getToggleState());
            if (ctx.setStatus) ctx.setStatus (reverseBtn.getToggleState() ? U ("Lecture à l'envers (préparation en arrière-plan)") : U ("Lecture normale"));
            repaint();
        }
    };
    trimBtn.onClick = [this] { if (! trimToSelection() && ctx.setStatus) ctx.setStatus (U ("Sélectionnez d'abord une plage dans la forme d'onde")); };
    deleteBtn.onClick = [this] { if (! deleteSelection() && ctx.setStatus) ctx.setStatus (U ("Sélectionnez d'abord une plage dans la forme d'onde")); };
    fitBtn.onClick = [this] { framedClip = {}; frameClip(); repaint(); };

    ctx.addChangeListener (this);
    startTimerHz (20);
}

AudioEditorView::~AudioEditorView()
{
    ctx.removeChangeListener (this);
    thumb.removeChangeListener (this);
}

te::WaveAudioClip* AudioEditorView::clip() const
{
    return dynamic_cast<te::WaveAudioClip*> (ctx.getSelectedClip());
}

juce::File AudioEditorView::sourceFileOf (te::WaveAudioClip& c)
{
    auto f = c.getSourceFileReference().getFile();
    if (! f.existsAsFile()) f = c.getOriginalFile();
    if (! f.existsAsFile()) f = c.getAudioFile().getFile();
    return f;
}

float AudioEditorView::peakOf (te::WaveAudioClip& c)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (sourceFileOf (c)));
    if (r == nullptr) return 0.0f;
    const auto pos = c.getPosition();
    const auto start = (juce::int64) (pos.getOffset().inSeconds() * r->sampleRate);
    const auto num = (juce::int64) (pos.getLength().inSeconds() * c.getSpeedRatio() * r->sampleRate);
    const int nc = (int) juce::jmin (2u, r->numChannels);
    std::vector<juce::Range<float>> levels ((size_t) nc);
    r->readMaxLevels (start, juce::jmin (num, r->lengthInSamples - start), levels.data(), nc);
    float peak = 0.0f;
    for (auto& l : levels) peak = juce::jmax (peak, std::abs (l.getStart()), std::abs (l.getEnd()));
    return peak;
}

bool AudioEditorView::normalise (te::WaveAudioClip& c, float targetDb)
{
    const float peak = peakOf (c);
    if (peak <= 1.0e-6f) return false;
    c.setGainDB (juce::jlimit (-60.0f, 40.0f, targetDb - juce::Decibels::gainToDecibels (peak)));
    return true;
}

double AudioEditorView::clipFileStart() const
{
    auto* c = clip();
    return c ? c->getPosition().getOffset().inSeconds() * c->getSpeedRatio() : 0.0;
}

double AudioEditorView::clipFileEnd() const
{
    auto* c = clip();
    return c ? clipFileStart() + c->getPosition().getLength().inSeconds() * c->getSpeedRatio() : 0.0;
}

bool AudioEditorView::trimToSelection()
{
    auto* c = clip();
    if (c == nullptr || selection.getLength() < 0.005) return false;
    const auto s = selection.getIntersectionWith ({ clipFileStart(), clipFileEnd() });
    if (s.getLength() < 0.005) return false;
    const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
    const double editStart = c->getPosition().getStart().inSeconds();
    ctx.edit->getUndoManager().beginNewTransaction (U ("Rogner"));
    const double newStart = editStart + (s.getStart() - clipFileStart()) / ratio;
    c->setStart (te::TimePosition::fromSeconds (newStart), true, false);   // déplace le début, ajuste le décalage
    c->setLength (te::TimeDuration::fromSeconds (s.getLength() / ratio), true);
    selection = {};
    if (ctx.setStatus) ctx.setStatus (U ("Événement rogné à la sélection"));
    ctx.changed();
    return true;
}

bool AudioEditorView::deleteSelection()
{
    auto* c = clip();
    if (c == nullptr || selection.getLength() < 0.005) return false;
    auto* track = dynamic_cast<te::ClipTrack*> (c->getTrack());
    if (track == nullptr) return false;
    const auto s = selection.getIntersectionWith ({ clipFileStart(), clipFileEnd() });
    if (s.getLength() < 0.005) return false;
    const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
    const double editStart = c->getPosition().getStart().inSeconds();
    const double t0 = editStart + (s.getStart() - clipFileStart()) / ratio;
    const double t1 = editStart + (s.getEnd() - clipFileStart()) / ratio;
    ctx.edit->getUndoManager().beginNewTransaction (U ("Supprimer la plage"));
    // Coupe aux deux bords puis retire le morceau du milieu.
    te::Clip* middle = c;
    if (t0 > editStart + 0.001)
        if (auto* right = track->splitClip (*c, te::TimePosition::fromSeconds (t0)))
            middle = right;
    if (t1 < middle->getPosition().getEnd().inSeconds() - 0.001)
        track->splitClip (*middle, te::TimePosition::fromSeconds (t1));
    te::Clip* keep = nullptr;
    for (auto* o : track->getClips()) if (o != middle && std::abs (o->getPosition().getEnd().inSeconds() - t0) < 0.002) keep = o;
    middle->removeFromParent();
    ctx.selectClip (keep);
    selection = {};
    if (ctx.setStatus) ctx.setStatus (U ("Plage supprimée de l'événement"));
    ctx.changed();
    return true;
}

void AudioEditorView::frameClip()
{
    auto* c = clip();
    if (c == nullptr) return;
    const auto f = sourceFileOf (*c);
    if (f != shownFile)
    {
        shownFile = f;
        thumb.setSource (f.existsAsFile() ? new juce::FileInputSource (f) : nullptr);
        fileLength = 0.0;
        if (auto r = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (f)))
            fileLength = (double) r->lengthInSamples / r->sampleRate;
    }
    if (c->itemID == framedClip) return;
    framedClip = c->itemID;
    selection = {};
    const double s = clipFileStart(), e = clipFileEnd();
    const double margin = (e - s) * 0.04;
    viewStart = juce::jmax (0.0, s - margin);
    viewLength = juce::jmax (0.05, (e - s) + 2 * margin);
    syncControls();
}

void AudioEditorView::syncControls()
{
    auto* c = clip();
    for (auto* comp : getChildren()) comp->setEnabled (c != nullptr);
    if (c == nullptr) return;
    if (! gain.isMouseButtonDown()) gain.setValue (c->getGainDB(), juce::dontSendNotification);
    if (! pitch.isMouseButtonDown()) pitch.setValue (c->getPitchChange(), juce::dontSendNotification);
    reverseBtn.setToggleState (c->getIsReversed(), juce::dontSendNotification);
    gainLabel.setText ("Gain " + juce::String (c->getGainDB(), 1) + " dB", juce::dontSendNotification);
    const int st = juce::roundToInt (c->getPitchChange());
    pitchLabel.setText ("Transp. " + juce::String (st > 0 ? "+" : "") + juce::String (st), juce::dontSendNotification);
}

void AudioEditorView::changeListenerCallback (juce::ChangeBroadcaster*) { repaint(); }

float AudioEditorView::cursorX() const
{
    auto* c = clip();
    if (c == nullptr || ctx.edit == nullptr) return -1.0f;
    const double s = clipFileStart(), e = clipFileEnd();
    const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
    const double ft = s + (ctx.edit->getTransport().getPosition().inSeconds() - c->getPosition().getStart().inSeconds()) * ratio;
    return ft >= s && ft <= e ? fileTimeToX (ft) : -1.0f;
}

void AudioEditorView::timerCallback()
{
    if (! isVisible()) return;
    frameClip();
    syncControls();
    // Redessin complet si l'événement, la vue ou la souris bougent (et 2 fois par seconde) ; sinon la tête seule.
    watcher.watch (ctx.edit);
    const bool moved = viewStart != lastViewStart || viewLength != lastViewLength || framedClip != lastFramed;
    if (watcher.consumeDirty() || moved || isMouseButtonDown (true) || ++tick % 10 == 0)
    {
        lastViewStart = viewStart; lastViewLength = viewLength; lastFramed = framedClip;
        lastCursorX = -1.0f;
        repaint();
        return;
    }
    repaintCursorStrip (*this, lastCursorX, cursorX(), toolbarH, getHeight());
}

juce::Rectangle<int> AudioEditorView::waveArea() const
{
    return getLocalBounds().withTrimmedTop (toolbarH + rulerH).reduced (0, 2);
}

float AudioEditorView::fileTimeToX (double t) const
{
    auto a = waveArea();
    return (float) a.getX() + (float) ((t - viewStart) / viewLength * a.getWidth());
}

double AudioEditorView::xToFileTime (float x) const
{
    auto a = waveArea();
    return viewStart + (x - (float) a.getX()) / juce::jmax (1, a.getWidth()) * viewLength;
}

void AudioEditorView::resized()
{
    auto t = getLocalBounds().removeFromTop (toolbarH).reduced (8, 5);
    t.removeFromLeft (290);
    gainLabel.setBounds (t.removeFromLeft (92)); gain.setBounds (t.removeFromLeft (120)); t.removeFromLeft (8);
    normaliseBtn.setBounds (t.removeFromLeft (90)); t.removeFromLeft (14);
    pitchLabel.setBounds (t.removeFromLeft (78)); pitch.setBounds (t.removeFromLeft (110)); t.removeFromLeft (8);
    reverseBtn.setBounds (t.removeFromLeft (80)); t.removeFromLeft (14);
    trimBtn.setBounds (t.removeFromLeft (150)); t.removeFromLeft (4);
    deleteBtn.setBounds (t.removeFromLeft (160)); t.removeFromLeft (14);
    fitBtn.setBounds (t.removeFromLeft (80));
}

void AudioEditorView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1f));
    auto* c = clip();

    // Barre d'outils.
    auto bar = getLocalBounds().removeFromTop (toolbarH);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff33363c), 0, 0, juce::Colour (0xff2a2d31), 0, (float) toolbarH, false));
    g.fillRect (bar);
    g.setColour (theme::line); g.drawHorizontalLine (toolbarH - 1, 0.0f, (float) getWidth());
    g.setColour (theme::text); g.setFont (theme::ui (13.0f, true));
    g.drawText (c ? c->getName() : U ("Éditeur audio"), 10, 0, 150, toolbarH, juce::Justification::centredLeft, true);
    g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
    g.drawText (c ? (c->getTrack()->getName() + U ("  ·  ") + shownFile.getFileName()) : U ("Aucun événement audio sélectionné"),
                150, 0, 140, toolbarH, juce::Justification::centredLeft, true);

    auto area = waveArea();
    if (c == nullptr || ! shownFile.existsAsFile())
    {
        g.setColour (theme::muted); g.setFont (theme::ui (14.0f));
        g.drawText (U ("Sélectionnez un événement audio dans la fenêtre de projet (double-clic pour l'ouvrir ici)."), area.reduced (40), juce::Justification::centred, true);
        return;
    }

    const auto col = c->getTrack()->getColour().isTransparent() ? theme::accent : c->getTrack()->getColour();
    const double s = clipFileStart(), e = clipFileEnd();
    const float xs = fileTimeToX (s), xe = fileTimeToX (e);

    // Règle : mesures du projet sur la partie jouée.
    auto ruler = juce::Rectangle<int> (0, toolbarH, getWidth(), rulerH);
    g.setColour (juce::Colour (0xff34383e)); g.fillRect (ruler);
    {
        const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
        const double editStart = c->getPosition().getStart().inSeconds();
        auto fileToEdit = [&] (double ft) { return editStart + (ft - s) / ratio; };
        auto editToFile = [&] (double et) { return s + (et - editStart) * ratio; };
        const int bpb = ctx.beatsPerBar();
        g.setFont (theme::ui (11.0f));
        for (int b = juce::jmax (0, (int) (ctx.toBeats (fileToEdit (viewStart)) / bpb)); ; ++b)
        {
            const double et = ctx.toSeconds ((double) b * bpb);
            const float x = fileTimeToX (editToFile (et));
            if (x > getWidth()) break;
            if (x < area.getX()) continue;
            g.setColour (theme::text.withAlpha (0.6f)); g.drawVerticalLine ((int) x, (float) toolbarH + 6, (float) toolbarH + rulerH);
            g.setColour (theme::text); g.drawText (juce::String (b + 1), (int) x + 3, toolbarH, 40, rulerH, juce::Justification::centredLeft);
        }
    }

    // Forme d'onde : le fichier entier en sombre, la partie jouée en couleur (avec le gain appliqué).
    const int nch = juce::jmax (1, juce::jmin (2, thumb.getNumChannels()));
    const float gainLin = juce::Decibels::decibelsToGain (c->getGainDB());
    for (int ch = 0; ch < nch; ++ch)
    {
        auto lane = area.withHeight (area.getHeight() / nch).translated (0, ch * area.getHeight() / nch).reduced (0, 2);
        g.setColour (juce::Colour (0xff202328)); g.fillRect (lane);
        g.setColour (theme::lineSoft); g.drawHorizontalLine (lane.getCentreY(), (float) lane.getX(), (float) lane.getRight());
        g.setColour (theme::dim.withAlpha (0.6f));
        thumb.drawChannel (g, lane, viewStart, viewStart + viewLength, ch, 1.0f);
        g.saveState();
        g.reduceClipRegion (juce::Rectangle<int> ((int) xs, lane.getY(), juce::jmax (0, (int) (xe - xs)), lane.getHeight()));
        g.setColour (col.withMultipliedBrightness (0.35f)); g.fillRect (lane);
        g.setColour (col.brighter (0.2f));
        thumb.drawChannel (g, lane, viewStart, viewStart + viewLength, ch, juce::jmin (8.0f, gainLin));
        g.restoreState();
        g.setColour (theme::muted); g.setFont (theme::ui (10.0f, true));
        g.drawText (nch == 2 ? (ch == 0 ? "G" : "D") : "M", lane.getX() + 4, lane.getY() + 2, 14, 12, juce::Justification::left);
    }
    // Limites de l'événement.
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawVerticalLine ((int) xs, (float) area.getY(), (float) area.getBottom());
    g.drawVerticalLine ((int) xe, (float) area.getY(), (float) area.getBottom());

    // Fondus (courbes et poignées).
    {
        const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
        const double fi = c->getFadeIn().inSeconds() * ratio, fo = c->getFadeOut().inSeconds() * ratio;
        auto curve = [&] (double from, double to, bool in, te::AudioFadeCurve::Type type)
        {
            juce::Path p;
            const float x0 = fileTimeToX (from), x1 = fileTimeToX (to);
            for (int i = 0; i <= 24; ++i)
            {
                const float a = i / 24.0f;
                const float gn = te::AudioFadeCurve::alphaToGainForType (type, in ? a : 1.0f - a);
                const float x = x0 + (x1 - x0) * a, y = (float) area.getBottom() - gn * (float) area.getHeight();
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.strokePath (p, juce::PathStrokeType (1.5f));
        };
        if (fi > 0.0) curve (s, s + fi, true, c->getFadeInType());
        if (fo > 0.0) curve (e - fo, e, false, c->getFadeOutType());
        g.setColour (juce::Colours::white);
        g.fillRect (fileTimeToX (s + fi) - 4.0f, (float) area.getY(), 8.0f, 8.0f);
        g.fillRect (fileTimeToX (e - fo) - 4.0f, (float) area.getY(), 8.0f, 8.0f);
    }

    // Sélection de plage.
    if (selection.getLength() > 0.0)
    {
        const float a = fileTimeToX (selection.getStart()), b = fileTimeToX (selection.getEnd());
        g.setColour (theme::accent.withAlpha (0.22f)); g.fillRect (a, (float) area.getY(), b - a, (float) area.getHeight());
        g.setColour (theme::accent); g.drawVerticalLine ((int) a, (float) area.getY(), (float) area.getBottom()); g.drawVerticalLine ((int) b, (float) area.getY(), (float) area.getBottom());
    }

    // Curseur de lecture (dans l'événement).
    {
        const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
        const double t = ctx.edit->getTransport().getPosition().inSeconds();
        const double ft = s + (t - c->getPosition().getStart().inSeconds()) * ratio;
        if (ft >= s && ft <= e)
        {
            const float x = fileTimeToX (ft);
            g.setColour (theme::cursor); g.fillRect (x, (float) toolbarH, 1.2f, (float) getHeight() - toolbarH);
        }
    }
    g.setColour (theme::dim); g.setFont (theme::ui (10.5f));
    g.drawText (U ("Glisser : sélectionner une plage · poignées blanches : fondus (clic droit : forme) · Ctrl+molette : zoom · Maj+molette : défiler"),
                area.reduced (8, 4), juce::Justification::bottomRight, true);
}

void AudioEditorView::mouseMove (const juce::MouseEvent& e)
{
    auto* c = clip();
    if (c == nullptr) return;
    const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
    const float hi = fileTimeToX (clipFileStart() + c->getFadeIn().inSeconds() * ratio), ho = fileTimeToX (clipFileEnd() - c->getFadeOut().inSeconds() * ratio);
    const bool onHandle = e.y < waveArea().getY() + 14 && (std::abs (e.x - hi) < 7 || std::abs (e.x - ho) < 7);
    setMouseCursor (onHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::IBeamCursor);
}

void AudioEditorView::mouseDown (const juce::MouseEvent& e)
{
    auto* c = clip();
    drag = Drag::none;
    if (c == nullptr || ! waveArea().contains (e.getPosition())) return;
    const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
    const float hi = fileTimeToX (clipFileStart() + c->getFadeIn().inSeconds() * ratio), ho = fileTimeToX (clipFileEnd() - c->getFadeOut().inSeconds() * ratio);
    if (e.y < waveArea().getY() + 14 && (std::abs (e.x - hi) < 7 || std::abs (e.x - ho) < 7))
    {
        const bool in = std::abs (e.x - hi) < std::abs (e.x - ho);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            const char* names[] = { "", "Lin\xc3\xa9" "aire", "Convexe", "Concave", "En S" };
            const auto current = in ? c->getFadeInType() : c->getFadeOutType();
            for (int i = 1; i <= 4; ++i) m.addItem (i, juce::String::fromUTF8 (names[i]), true, (int) current == i);
            m.addSeparator();
            m.addItem (9, U ("Retirer le fondu"));
            te::Clip::Ptr keep (c);
            m.showMenuAsync ({}, [this, keep, in] (int r)
            {
                auto* w = dynamic_cast<te::WaveAudioClip*> (keep.get());
                if (w == nullptr || r <= 0) return;
                ctx.edit->getUndoManager().beginNewTransaction (U ("Fondu"));
                if (r == 9) { if (in) w->setFadeIn ({}); else w->setFadeOut ({}); }
                else if (in) w->setFadeInType ((te::AudioFadeCurve::Type) r); else w->setFadeOutType ((te::AudioFadeCurve::Type) r);
                repaint();
            });
            return;
        }
        ctx.edit->getUndoManager().beginNewTransaction (U ("Fondu"));
        drag = in ? Drag::fadeIn : Drag::fadeOut;
        return;
    }
    if (e.mods.isPopupMenu()) return;
    drag = Drag::select;
    dragAnchor = juce::jlimit (0.0, juce::jmax (0.0, fileLength), xToFileTime ((float) e.x));
    selection = {};
    // Un clic sans glisser place aussi le curseur du projet à cet endroit de l'événement.
    const double editT = c->getPosition().getStart().inSeconds() + (dragAnchor - clipFileStart()) / ratio;
    if (editT >= c->getPosition().getStart().inSeconds() && editT <= c->getPosition().getEnd().inSeconds())
        ctx.edit->getTransport().setPosition (te::TimePosition::fromSeconds (editT));
    repaint();
}

void AudioEditorView::mouseDrag (const juce::MouseEvent& e)
{
    auto* c = clip();
    if (c == nullptr) return;
    const double ratio = juce::jmax (0.0001, c->getSpeedRatio());
    const double t = juce::jlimit (0.0, juce::jmax (0.0, fileLength), xToFileTime ((float) e.x));
    if (drag == Drag::select)
        selection = { juce::jmin (dragAnchor, t), juce::jmax (dragAnchor, t) };
    else if (drag == Drag::fadeIn)
        c->setFadeIn (te::TimeDuration::fromSeconds (juce::jmax (0.0, (t - clipFileStart()) / ratio)));
    else if (drag == Drag::fadeOut)
        c->setFadeOut (te::TimeDuration::fromSeconds (juce::jmax (0.0, (clipFileEnd() - t) / ratio)));
    repaint();
}

void AudioEditorView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::select && selection.getLength() > 0.0 && ctx.setStatus)
        ctx.setStatus (U ("Plage sélectionnée : ") + juce::String (selection.getLength(), 3) + U (" s — « Rogner » ou « Supprimer la sélection »"));
    drag = Drag::none;
}

void AudioEditorView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown())
    {
        const double anchor = xToFileTime ((float) e.x);
        const double factor = w.deltaY > 0 ? 0.8 : 1.25;
        viewLength = juce::jlimit (0.005, juce::jmax (1.0, fileLength * 1.2), viewLength * factor);
        viewStart = juce::jmax (0.0, anchor - (anchor - viewStart) * factor);
    }
    else viewStart = juce::jlimit (0.0, juce::jmax (0.0, fileLength - viewLength * 0.5), viewStart - (w.deltaY + w.deltaX) * viewLength * 0.3);
    repaint();
}
