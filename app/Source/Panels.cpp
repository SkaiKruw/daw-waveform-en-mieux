#include "Panels.h"

//==============================================================================
void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = active || getToggleState();
    if (lit)            theme::bevel (g, r, on.withAlpha (0.22f).overlaidWith (theme::panel2.withAlpha (0.0f)), 3.0f);
    else if (! flat)    theme::bevel (g, r, over ? theme::panel3 : theme::panel2, 3.0f);
    else if (over)      { g.setColour (theme::panel3); g.fillRoundedRectangle (r, 3.0f); }
    if (lit) { g.setColour (on.withAlpha (0.65f)); g.drawRoundedRectangle (r, 3.0f, 1.0f); }
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRoundedRectangle (r, 3.0f); }
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.56f;
    painter (g, r.withSizeKeepingCentre (s, s), lit ? on.brighter (0.25f) : (isEnabled() ? theme::text : theme::dim));
}

namespace icons
{
    using P = IconButton::Painter;
    using R = juce::Rectangle<float>;

    P tool (Tool t)
    {
        return [t] (juce::Graphics& g, R r, juce::Colour c)
        {
            g.setColour (c);
            juce::Path p;
            const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
            switch (t)
            {
                case Tool::select:
                    p.startNewSubPath (x + w * 0.2f, y);
                    p.lineTo (x + w * 0.2f, y + h * 0.85f); p.lineTo (x + w * 0.42f, y + h * 0.66f);
                    p.lineTo (x + w * 0.58f, y + h); p.lineTo (x + w * 0.72f, y + h * 0.93f);
                    p.lineTo (x + w * 0.56f, y + h * 0.6f); p.lineTo (x + w * 0.85f, y + h * 0.6f); p.closeSubPath();
                    g.fillPath (p); break;
                case Tool::range:
                    g.fillRect (x + w * 0.15f, y + h * 0.1f, 2.0f, h * 0.8f); g.fillRect (x + w * 0.85f - 2, y + h * 0.1f, 2.0f, h * 0.8f);
                    g.setColour (c.withAlpha (0.45f)); g.fillRect (x + w * 0.15f + 2, y + h * 0.3f, w * 0.7f - 4, h * 0.4f); break;
                case Tool::split:
                    g.drawEllipse (x, y + h * 0.62f, w * 0.36f, h * 0.36f, 1.6f); g.drawEllipse (x + w * 0.64f, y + h * 0.62f, w * 0.36f, h * 0.36f, 1.6f);
                    g.drawLine (x + w * 0.3f, y + h * 0.66f, x + w * 0.78f, y, 1.8f); g.drawLine (x + w * 0.7f, y + h * 0.66f, x + w * 0.22f, y, 1.8f); break;
                case Tool::glue:
                    g.fillRoundedRectangle (x + w * 0.25f, y + h * 0.35f, w * 0.5f, h * 0.65f, 2.0f);
                    g.fillRect (x + w * 0.4f, y + h * 0.12f, w * 0.2f, h * 0.25f); g.fillRect (x + w * 0.46f, y, w * 0.08f, h * 0.15f); break;
                case Tool::erase:
                    p.addRectangle (-w * 0.25f, -h * 0.42f, w * 0.5f, h * 0.84f);
                    g.fillPath (p, juce::AffineTransform::rotation (0.75f).translated (r.getCentreX(), r.getCentreY()));
                    g.setColour (theme::panel); g.fillRect (x + w * 0.05f, y + h * 0.92f, w * 0.9f, 1.5f); break;
                case Tool::zoom:
                    g.drawEllipse (x, y, w * 0.68f, h * 0.68f, 1.8f); g.drawLine (x + w * 0.6f, y + h * 0.6f, x + w, y + h, 2.4f); break;
                case Tool::mute:
                    g.drawLine (x + w * 0.12f, y + h * 0.12f, x + w * 0.88f, y + h * 0.88f, 2.4f); g.drawLine (x + w * 0.88f, y + h * 0.12f, x + w * 0.12f, y + h * 0.88f, 2.4f); break;
                case Tool::draw:
                    p.addRectangle (-w * 0.12f, -h * 0.5f, w * 0.24f, h * 0.78f);
                    p.addTriangle (-w * 0.12f, h * 0.3f, w * 0.12f, h * 0.3f, 0.0f, h * 0.52f);
                    g.fillPath (p, juce::AffineTransform::rotation (0.7f).translated (r.getCentreX(), r.getCentreY())); break;
            }
        };
    }

    P toStart()  { return [] (juce::Graphics& g, R r, juce::Colour c) { g.setColour (c); g.fillRect (r.getX(), r.getY(), 2.2f, r.getHeight()); juce::Path p; p.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX() + 3, r.getCentreY()); g.fillPath (p); }; }
    P toEnd()    { return [] (juce::Graphics& g, R r, juce::Colour c) { g.setColour (c); g.fillRect (r.getRight() - 2.2f, r.getY(), 2.2f, r.getHeight()); juce::Path p; p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight() - 3, r.getCentreY()); g.fillPath (p); }; }
    P rewind()   { return [] (juce::Graphics& g, R r, juce::Colour c) { g.setColour (c); juce::Path p; const float m = r.getCentreX(); p.addTriangle (m, r.getY(), m, r.getBottom(), r.getX(), r.getCentreY()); p.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), m, r.getCentreY()); g.fillPath (p); }; }
    P forward()  { return [] (juce::Graphics& g, R r, juce::Colour c) { g.setColour (c); juce::Path p; const float m = r.getCentreX(); p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), m, r.getCentreY()); p.addTriangle (m, r.getY(), m, r.getBottom(), r.getRight(), r.getCentreY()); g.fillPath (p); }; }
    P stop()     { return [] (juce::Graphics& g, R r, juce::Colour c) { g.setColour (c); g.fillRect (r.reduced (r.getWidth() * 0.1f)); }; }
    P play()     { return [] (juce::Graphics& g, R r, juce::Colour c) { g.setColour (c); juce::Path p; p.addTriangle (r.getX() + 2, r.getY(), r.getX() + 2, r.getBottom(), r.getRight(), r.getCentreY()); g.fillPath (p); }; }
    P record()   { return [] (juce::Graphics& g, R r, juce::Colour c) { g.setColour (c == theme::text ? theme::rec.withAlpha (0.85f) : c); g.fillEllipse (r.reduced (r.getWidth() * 0.06f)); }; }
    P cycle()
    {
        return [] (juce::Graphics& g, R r, juce::Colour c)
        {
            g.setColour (c);
            juce::Path p; p.addRoundedRectangle (r.getX(), r.getY() + r.getHeight() * 0.18f, r.getWidth(), r.getHeight() * 0.64f, r.getHeight() * 0.3f);
            g.strokePath (p, juce::PathStrokeType (1.8f));
            juce::Path a; a.addTriangle (r.getCentreX() - 1, r.getY() + r.getHeight() * 0.02f, r.getCentreX() + 4, r.getY() + r.getHeight() * 0.18f, r.getCentreX() - 1, r.getY() + r.getHeight() * 0.34f);
            g.fillPath (a);
        };
    }
    P zone (int which)
    {
        return [which] (juce::Graphics& g, R r, juce::Colour c)
        {
            g.setColour (c); g.drawRect (r, 1.4f);
            auto in = r.reduced (2.5f);
            if (which == 0) g.fillRect (in.removeFromLeft (in.getWidth() * 0.33f));
            else if (which == 1) g.fillRect (in.removeFromBottom (in.getHeight() * 0.38f));
            else g.fillRect (in.removeFromRight (in.getWidth() * 0.33f));
        };
    }
    P magnet()
    {
        return [] (juce::Graphics& g, R r, juce::Colour c)
        {
            g.setColour (c);
            juce::Path p; p.startNewSubPath (r.getX() + 2, r.getY()); p.lineTo (r.getX() + 2, r.getCentreY());
            p.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() * 0.5f - 2, r.getHeight() * 0.45f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
            p.lineTo (r.getRight() - 2, r.getY());
            g.strokePath (p, juce::PathStrokeType (3.0f));
            g.setColour (theme::rec); g.fillRect (r.getX(), r.getY(), 4.5f, 3.0f); g.fillRect (r.getRight() - 4.5f, r.getY(), 4.5f, 3.0f);
        };
    }
    // Téléphone (télécommande) ; le point vert est ajouté par la barre d'outils quand un téléphone est connecté.
    P phone()
    {
        return [] (juce::Graphics& g, R r, juce::Colour c)
        {
            auto body = r.withSizeKeepingCentre (r.getWidth() * 0.55f, r.getHeight());
            g.setColour (c);
            g.drawRoundedRectangle (body.reduced (0.75f), 2.5f, 1.6f);
            g.fillRect (body.getCentreX() - 2.0f, body.getBottom() - 3.5f, 4.0f, 1.4f);
            g.fillRect (body.reduced (2.6f, 3.2f).withTrimmedBottom (2.5f).withHeight (1.4f).withY (body.getY() + 2.6f));
        };
    }
    P metronome()
    {
        return [] (juce::Graphics& g, R r, juce::Colour c)
        {
            g.setColour (c);
            juce::Path p; p.addTriangle (r.getCentreX(), r.getY(), r.getX() + 1, r.getBottom(), r.getRight() - 1, r.getBottom());
            g.strokePath (p, juce::PathStrokeType (1.6f));
            g.drawLine (r.getCentreX(), r.getBottom() - 3, r.getRight() - 1, r.getY() + 2, 1.6f);
        };
    }
    P punch (bool in)
    {
        return [in] (juce::Graphics& g, R r, juce::Colour c)
        {
            g.setColour (c);
            const float x = in ? r.getX() + 2 : r.getRight() - 4;
            g.fillRect (x, r.getY(), 2.0f, r.getHeight());
            juce::Path p;
            if (in) p.addTriangle (r.getX() + 6, r.getY() + 3, r.getX() + 6, r.getBottom() - 3, r.getRight(), r.getCentreY());
            else    p.addTriangle (r.getX(), r.getY() + 3, r.getX(), r.getBottom() - 3, r.getRight() - 6, r.getCentreY());
            g.fillPath (p);
        };
    }
}

//==============================================================================
void StereoMeter::setLevels (float l, float r)
{
    auto smooth = [] (float old, float in) { return in > old ? in : juce::jmax (-100.0f, old - 1.5f); };
    levels[0] = smooth (levels[0], l); levels[1] = smooth (levels[1], r);
    peak[0] = juce::jmax (peak[0], l); peak[1] = juce::jmax (peak[1], r);
    // Redessin seulement si une barre ou une crête bouge d'au moins un pixel (silence : jamais).
    const float len = (float) (isVertical ? getHeight() : getWidth());
    auto px = [len] (float db) { return juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * len); };
    const int shown[4] { px (levels[0]), px (levels[1]), px (peak[0]), px (peak[1]) };
    if (std::memcmp (shown, drawn, sizeof (shown)) != 0) { std::memcpy (drawn, shown, sizeof (shown)); repaint(); }
}

void StereoMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (theme::display); g.fillRect (b);
    const float gap = 1.0f;
    auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
    for (int c = 0; c < 2; ++c)
    {
        juce::Rectangle<float> bar = isVertical ? juce::Rectangle<float> (b.getX() + c * (b.getWidth() + gap) / 2.0f, b.getY(), (b.getWidth() - gap) / 2.0f, b.getHeight())
                                                : juce::Rectangle<float> (b.getX(), b.getY() + c * (b.getHeight() + gap) / 2.0f, b.getWidth(), (b.getHeight() - gap) / 2.0f);
        const float n = norm (levels[c]);
        auto fill = isVertical ? bar.withTop (bar.getBottom() - n * bar.getHeight()) : bar.withWidth (n * bar.getWidth());
        if (isVertical) g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2fb85a), 0, bar.getBottom(), theme::rec, 0, bar.getY(), false));
        else            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2fb85a), bar.getX(), 0, theme::rec, bar.getRight(), 0, false));
        g.fillRect (fill);
        const float pn = norm (peak[c]);
        g.setColour (peak[c] > 0.0f ? theme::rec : theme::text.withAlpha (0.8f));
        if (pn > 0.0f)
        {
            if (isVertical) g.fillRect (bar.getX(), bar.getBottom() - pn * bar.getHeight(), bar.getWidth(), 1.5f);
            else            g.fillRect (bar.getX() + pn * bar.getWidth() - 1.5f, bar.getY(), 1.5f, bar.getHeight());
        }
    }
}

//==============================================================================
ToolbarPanel::ToolbarPanel (EditContext& c) : ctx (c)
{
    const char* zoneIds[] = { "view.inspector", "view.lowerzone", "view.rightzone" };
    const char* zoneTips[] = { "Afficher/masquer la zone gauche (inspecteur)  Alt+I", "Afficher/masquer la zone inférieure  Ctrl+Alt+E", "Afficher/masquer la zone droite (médias)  Ctrl+Alt+R" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = zoneBtns.add (new IconButton (U (zoneTips[i]), icons::zone (i)));
        juce::String id (zoneIds[i]);
        b->onClick = [this, id] { ctx.perform (id); };
        addAndMakeVisible (b);
    }
    struct T { const char* tip; IconButton::Painter p; const char* id; juce::Colour on; };
    const T tr[] = {
        { "Aller au début du projet  (Début)", icons::toStart(), "transport.home", theme::accent },
        { "Rembobiner d'une mesure  (-)",     icons::rewind(),  "transport.rewind", theme::accent },
        { "Avancer d'une mesure  (+)",        icons::forward(), "transport.forward", theme::accent },
        { "Activer/désactiver le cycle  (L ou /)", icons::cycle(), "transport.loop", theme::cycle },
        { "Stop  (0)",                        icons::stop(),    "transport.stop", theme::accent },
        { "Démarrer/arrêter  (Espace)",      icons::play(),    "transport.play", theme::play },
        { "Enregistrer  (R ou *)",           icons::record(),  "transport.record", theme::rec } };
    for (auto& t : tr)
    {
        auto* b = transportBtns.add (new IconButton (U (t.tip), t.p, t.on));
        juce::String id (t.id);
        b->onClick = [this, id] { ctx.perform (id); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 8; ++i)
    {
        auto* b = toolBtns.add (new IconButton (EditContext::toolName ((Tool) i), icons::tool ((Tool) i)));
        b->onClick = [this, i] { ctx.setTool ((Tool) i); };
        addAndMakeVisible (b);
    }
    snapBtn = std::make_unique<IconButton> (U ("Calage activé/désactivé  (J)"), icons::magnet());
    snapBtn->onClick = [this] { ctx.perform ("edit.snap"); };
    addAndMakeVisible (*snapBtn);

    gridBox.addItem ("Mesure", 1); gridBox.addItem ("1/4", 2); gridBox.addItem ("1/8", 3); gridBox.addItem ("1/16", 4);
    gridBox.setTooltip (U ("Type de grille (calage)"));
    gridBox.onChange = [this] { const double v[] = { 4.0, 1.0, 0.5, 0.25 }; ctx.gridBeats = v[juce::jlimit (0, 3, gridBox.getSelectedId() - 1)]; ctx.changed(); };
    quantBox.addItem ("1/4", 1); quantBox.addItem ("1/8", 2); quantBox.addItem ("1/16", 3); quantBox.addItem ("1/32", 4);
    quantBox.setTooltip (U ("Valeur de quantification (Q)"));
    quantBox.onChange = [this] { const double v[] = { 1.0, 0.5, 0.25, 0.125 }; ctx.quantizeBeats = v[juce::jlimit (0, 3, quantBox.getSelectedId() - 1)]; ctx.changed(); };
    for (auto* b : { &gridBox, &quantBox }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    remoteBtn = std::make_unique<IconButton> (U ("Télécommande (téléphone)"), icons::phone());
    remoteBtn->onClick = [this] { ctx.perform ("studio.remote"); };
    addAndMakeVisible (*remoteBtn);
    styleButton (searchBtn);
    searchBtn.setColour (juce::TextButton::textColourOffId, theme::muted);
    searchBtn.onClick = [this] { ctx.perform ("palette.open"); };
    addAndMakeVisible (searchBtn);

    ctx.addChangeListener (this);
    syncTools();
    startTimerHz (15);
}

ToolbarPanel::~ToolbarPanel() { ctx.removeChangeListener (this); }

void ToolbarPanel::syncTools()
{
    for (int i = 0; i < toolBtns.size(); ++i) toolBtns[i]->active = (int) ctx.tool == i;
    snapBtn->active = ctx.snapOn;
    const int gid = ctx.gridBeats >= 4.0 ? 1 : ctx.gridBeats >= 1.0 ? 2 : ctx.gridBeats >= 0.5 ? 3 : 4;
    gridBox.setSelectedId (gid, juce::dontSendNotification);
    const int qid = ctx.quantizeBeats >= 1.0 ? 1 : ctx.quantizeBeats >= 0.5 ? 2 : ctx.quantizeBeats >= 0.25 ? 3 : 4;
    quantBox.setSelectedId (qid, juce::dontSendNotification);
    repaint();
}

void ToolbarPanel::timerCallback()
{
    for (int i = 0; i < 3; ++i) zoneBtns[i]->setActive (isZoneVisible && isZoneVisible (i));
    if (ctx.edit != nullptr)
    {
        auto& t = ctx.edit->getTransport();
        transportBtns[3]->setActive (t.looping.get());
        transportBtns[5]->setActive (t.isPlaying() && ! t.isRecording());
        transportBtns[6]->setActive (t.isRecording());
    }
    // Télécommande : allumée si active ; voyant vert si un téléphone s'est manifesté il y a moins de 3 s.
    const int rs = remoteState ? remoteState() : 0;
    remoteBtn->setActive (rs > 0);
    if (rs != shownRemoteState)
    {
        shownRemoteState = rs;
        remoteBtn->setTooltip (rs == 2 ? U ("Télécommande : téléphone connecté") : rs == 1 ? U ("Télécommande active (en attente d'un téléphone)")
                                                                                : U ("Télécommande (téléphone)"));
        repaint (remoteBtn->getBounds().expanded (2));
    }
    // Titre (nom du projet, « copie de secours il y a … ») : une fois par seconde suffit.
    if (++tick % 15 == 0) repaint (titleX, 0, juce::jmax (0, searchBtn.getX() - titleX), getHeight());
}

void ToolbarPanel::resized()
{
    auto r = getLocalBounds().reduced (6, 5);
    const int bw = r.getHeight();
    for (auto* b : zoneBtns) { b->setBounds (r.removeFromLeft (bw)); r.removeFromLeft (2); }
    r.removeFromLeft (14);
    for (auto* b : transportBtns) { b->setBounds (r.removeFromLeft (bw)); r.removeFromLeft (2); }
    r.removeFromLeft (14);
    toolsX = r.getX();
    for (auto* b : toolBtns) { b->setBounds (r.removeFromLeft (bw)); r.removeFromLeft (1); }
    r.removeFromLeft (14);
    snapX = r.getX();
    snapBtn->setBounds (r.removeFromLeft (bw)); r.removeFromLeft (4);
    gridBox.setBounds (r.removeFromLeft (80).reduced (0, 2)); r.removeFromLeft (30);
    quantBox.setBounds (r.removeFromLeft (64).reduced (0, 2)); r.removeFromLeft (12);
    searchBtn.setBounds (r.removeFromRight (170).reduced (0, 2));
    r.removeFromRight (6);
    remoteBtn->setBounds (r.removeFromRight (bw));
    r.removeFromRight (8);
    titleX = r.getX();
}

void ToolbarPanel::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff33363c), 0, 0, juce::Colour (0xff2a2d31), 0, (float) getHeight(), false));
    g.fillAll();
    g.setColour (theme::line); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    auto sep = [&] (int x) { g.setColour (theme::line); g.drawVerticalLine (x, 6.0f, getHeight() - 6.0f); g.setColour (juce::Colours::white.withAlpha (0.05f)); g.drawVerticalLine (x + 1, 6.0f, getHeight() - 6.0f); };
    if (! transportBtns.isEmpty()) sep (transportBtns[0]->getX() - 8);
    sep (toolsX - 8); sep (snapX - 8);
    g.setColour (theme::muted); g.setFont (theme::ui (11.5f, true));
    g.drawText ("Q", quantBox.getX() - 24, 0, 20, getHeight(), juce::Justification::centredRight);

    // Titre du projet + état de sauvegarde.
    if (getProjectTitle)
    {
        auto area = juce::Rectangle<int> (titleX, 0, remoteBtn->getX() - titleX - 4, getHeight());
        g.setColour (theme::text); g.setFont (theme::ui (13.0f, true));
        g.drawText (getProjectTitle(), area, juce::Justification::centredRight, true);
    }
}

void ToolbarPanel::paintOverChildren (juce::Graphics& g)
{
    // Voyant « téléphone connecté » sur le bouton de la télécommande.
    if (shownRemoteState == 2)
    {
        auto b = remoteBtn->getBounds().toFloat();
        const juce::Rectangle<float> dot (b.getRight() - 9.0f, b.getY() + 1.0f, 8.0f, 8.0f);
        g.setColour (theme::panel); g.fillEllipse (dot.expanded (1.5f));
        g.setColour (theme::play); g.fillEllipse (dot);
    }
}

//==============================================================================
InfoLine::InfoLine (EditContext& c) : ctx (c)
{
    for (auto* l : { &name, &start, &end, &length, &offset }) setupField (*l);
    name.onTextChange = [this] { if (auto* clip = ctx.getSelectedClip()) { ctx.edit->getUndoManager().beginNewTransaction ("Renommer"); clip->setName (name.getText()); } };
    start.onTextChange = [this]
    {
        if (auto* clip = ctx.getSelectedClip())
        {
            ctx.edit->getUndoManager().beginNewTransaction (U ("Déplacer l'événement"));
            clip->setStart (te::TimePosition::fromSeconds (ctx.toSeconds (parseBars (start.getText(), false))), false, true);
        }
    };
    end.onTextChange = [this]
    {
        if (auto* clip = ctx.getSelectedClip())
        {
            const double e = ctx.toSeconds (parseBars (end.getText(), false)), s = clip->getPosition().getStart().inSeconds();
            if (e > s + 0.01) { ctx.edit->getUndoManager().beginNewTransaction ("Fin"); clip->setLength (te::TimeDuration::fromSeconds (e - s), true); }
        }
    };
    length.onTextChange = [this]
    {
        if (auto* clip = ctx.getSelectedClip())
        {
            const double s = clip->getPosition().getStart().inSeconds();
            const double l = ctx.toSeconds (ctx.toBeats (s) + parseBars (length.getText(), true)) - s;
            if (l > 0.01) { ctx.edit->getUndoManager().beginNewTransaction ("Longueur"); clip->setLength (te::TimeDuration::fromSeconds (l), true); }
        }
    };
    offset.setEditable (false, false, false);
    addAndMakeVisible (muteToggle);
    muteToggle.setWantsKeyboardFocus (false);
    muteToggle.onClick = [this] { if (auto* clip = ctx.getSelectedClip()) { ctx.edit->getUndoManager().beginNewTransaction ("Muet"); clip->setMuted (muteToggle.getToggleState()); ctx.changed(); } };
    startTimerHz (10);
}

void InfoLine::setupField (juce::Label& l)
{
    addAndMakeVisible (l);
    l.setEditable (false, true, false);
    l.setFont (theme::mono (12.5f));
    l.setColour (juce::Label::textColourId, theme::text);
    l.setColour (juce::Label::backgroundColourId, theme::display);
    l.setColour (juce::Label::outlineColourId, theme::line);
    l.setTooltip (U ("Double-clic pour modifier"));
}

double InfoLine::parseBars (const juce::String& text, bool isLength) const
{
    auto parts = juce::StringArray::fromTokens (text.trim(), ". :", "");
    parts.removeEmptyStrings();
    double v[4] { isLength ? 0.0 : 1.0, isLength ? 0.0 : 1.0, isLength ? 0.0 : 1.0, 0.0 };
    for (int i = 0; i < juce::jmin (4, parts.size()); ++i) v[i] = parts[i].getDoubleValue();
    const double o = isLength ? 0.0 : 1.0;
    // Renvoie un nombre de temps (position depuis le début du projet, ou durée).
    return juce::jmax (0.0, (v[0] - o) * ctx.beatsPerBar() + (v[1] - o) + (v[2] - o) / 4.0 + v[3] / 480.0);
}

void InfoLine::timerCallback()
{
    auto* clip = ctx.getSelectedClip();
    const bool editing = name.isBeingEdited() || start.isBeingEdited() || end.isBeingEdited() || length.isBeingEdited();
    for (auto* l : { &name, &start, &end, &length, &offset }) l->setVisible (clip != nullptr);
    muteToggle.setVisible (clip != nullptr);
    if (clip == nullptr) { if (shown.isValid()) { shown = {}; repaint(); } return; }
    if (editing) return;
    shown = clip->itemID;
    auto pos = clip->getPosition();
    auto lenText = [this] (double fromSec, double toSec)
    {
        const double beats = ctx.toBeats (toSec) - ctx.toBeats (fromSec) + 1.0e-7;
        const int bpb = ctx.beatsPerBar();
        const int bars = (int) (beats / bpb);
        const double rem = beats - bars * bpb;
        const int b = (int) rem;
        const int six = (int) ((rem - b) * 4.0);
        const int ticks = (int) (((rem - b) * 4.0 - six) * 120.0);
        return juce::String (bars) + ". " + juce::String (b) + ". " + juce::String (six) + ". " + juce::String (ticks);
    };
    name.setText (clip->getName(), juce::dontSendNotification);
    start.setText (ctx.formatBars (pos.getStart().inSeconds()).trim(), juce::dontSendNotification);
    end.setText (ctx.formatBars (pos.getEnd().inSeconds()).trim(), juce::dontSendNotification);
    length.setText (lenText (pos.getStart().inSeconds(), pos.getEnd().inSeconds()), juce::dontSendNotification);
    offset.setText (lenText (pos.getStart().inSeconds() - pos.getOffset().inSeconds(), pos.getStart().inSeconds()), juce::dontSendNotification);
    muteToggle.setToggleState (clip->isMuted(), juce::dontSendNotification);
    repaint();
}

void InfoLine::resized()
{
    auto r = getLocalBounds().reduced (6, 3);
    r.removeFromLeft (110);
    auto field = [&] (juce::Label& l, int labelW, int w) { r.removeFromLeft (labelW); l.setBounds (r.removeFromLeft (w)); r.removeFromLeft (10); };
    field (name, 34, 190);
    field (start, 42, 110);
    field (end, 26, 110);
    field (length, 60, 100);
    field (offset, 54, 100);
    muteToggle.setBounds (r.removeFromLeft (70));
}

void InfoLine::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff27292d));
    g.setColour (theme::line); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    g.setFont (theme::ui (12.0f));
    auto* clip = ctx.getSelectedClip();
    g.setColour (theme::muted);
    if (clip == nullptr)
    {
        g.drawText (U ("Aucun événement sélectionné"), getLocalBounds().reduced (12, 0), juce::Justification::centredLeft);
        return;
    }
    g.setColour (clip->getColour().isTransparent() ? theme::accent : clip->getColour());
    g.setColour (theme::text); g.setFont (theme::ui (12.0f, true));
    g.drawText (dynamic_cast<te::MidiClip*> (clip) ? U ("Conteneur MIDI") : U ("Événement audio"), 12, 0, 100, getHeight(), juce::Justification::centredLeft);
    g.setFont (theme::ui (12.0f)); g.setColour (theme::muted);
    auto lbl = [&] (juce::Label& l, const juce::String& t, int w) { g.drawText (t, l.getX() - w, 0, w - 4, getHeight(), juce::Justification::centredRight); };
    lbl (name, "Nom", 34); lbl (start, U ("Début"), 42); lbl (end, "Fin", 26); lbl (length, "Longueur", 60); lbl (offset, U ("Décalage"), 54);
}

//==============================================================================
Inspector::Inspector (EditContext& c) : ctx (c)
{
    addAndMakeVisible (name);
    name.setFont (theme::ui (14.0f, true));
    name.setEditable (false, true, false);
    name.setColour (juce::Label::textColourId, theme::text);
    name.setTooltip (U ("Double-clic pour renommer la piste"));
    name.onTextChange = [this] { if (auto* t = track()) { ctx.edit->getUndoManager().beginNewTransaction ("Renommer"); t->setName (name.getText()); ctx.changed(); } };

    styleStateButton (muteBtn, theme::mute); styleStateButton (soloBtn, theme::solo); styleStateButton (armBtn, theme::rec);
    styleStateButton (monBtn, theme::monitor); styleStateButton (readBtn, theme::readA); styleStateButton (writeBtn, theme::writeA);
    muteBtn.setTooltip ("Rendre muet"); soloBtn.setTooltip ("Solo"); armBtn.setTooltip ("Activer l'enregistrement"); monBtn.setTooltip ("Monitoring");
    readBtn.setTooltip ("Lire l'automation (tout le projet)"); writeBtn.setTooltip (U ("Écrire l'automation (tout le projet)"));
    muteBtn.onClick = [this] { if (auto* t = track()) { t->setMute (muteBtn.getToggleState()); ctx.changed(); } };
    soloBtn.onClick = [this] { if (auto* t = track()) { t->setSolo (soloBtn.getToggleState()); ctx.changed(); } };
    armBtn.onClick  = [this] { if (auto* t = track()) ctx.setTrackArmed (*t, armBtn.getToggleState()); };
    monBtn.onClick  = [this] { if (auto* t = track()) ctx.setTrackMonitoring (*t, monBtn.getToggleState()); };
    readBtn.onClick = [this] { if (ctx.edit) ctx.edit->getAutomationRecordManager().setReadingAutomation (readBtn.getToggleState()); };
    writeBtn.onClick = [this] { if (ctx.edit) ctx.edit->getAutomationRecordManager().setWritingAutomation (writeBtn.getToggleState()); };
    styleButton (autoBtn);
    autoBtn.setTooltip (U ("Afficher/masquer la piste d'automation (volume, panoramique)  A"));
    autoBtn.onClick = [this] { if (auto* t = track()) ctx.toggleAutomation (*t); };
    for (auto* b : { &muteBtn, &soloBtn, &armBtn, &monBtn, &readBtn, &writeBtn, &autoBtn }) addAndMakeVisible (*b);

    styleButton (instrumentBtn);
    instrumentBtn.onClick = [this]
    {
        auto* t = track();
        if (t == nullptr) return;
        if (juce::ModifierKeys::currentModifiers.isPopupMenu() || ctx.getInstrument (*t) == nullptr) { showInstrumentMenu(); return; }
        if (ctx.openPluginEditor) ctx.openPluginEditor (*ctx.getInstrument (*t), &instrumentBtn);
    };
    addAndMakeVisible (instrumentBtn);

    for (auto* s : { &volume, &pan })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setWantsKeyboardFocus (false);
        addAndMakeVisible (*s);
    }
    volume.setRange (-60.0, 6.0, 0.1); volume.setSkewFactorFromMidPoint (-12.0); volume.setDoubleClickReturnValue (true, 0.0);
    pan.setRange (-1.0, 1.0, 0.01); pan.setDoubleClickReturnValue (true, 0.0);
    volume.onDragStart = pan.onDragStart = [this] { if (ctx.edit) ctx.edit->getUndoManager().beginNewTransaction (U ("Voie")); };
    volume.onValueChange = [this] { if (auto* t = track()) if (auto* v = t->getVolumePlugin()) v->setVolumeDb ((float) volume.getValue()); };
    pan.onValueChange    = [this] { if (auto* t = track()) if (auto* v = t->getVolumePlugin()) v->setPan ((float) pan.getValue()); };
    for (auto* l : { &volLabel, &panLabel }) { l->setFont (theme::mono (11.5f)); l->setJustificationType (juce::Justification::centredRight); l->setColour (juce::Label::textColourId, theme::lcd); addAndMakeVisible (*l); }
    addAndMakeVisible (meter);

    for (int i = 0; i < 8; ++i)
    {
        auto* b = slots.add (new juce::TextButton());
        styleButton (*b);
        b->setColour (juce::TextButton::buttonColourId, theme::display);
        b->onClick = [this, i, b] { showSlotMenu (i, b); };
        addAndMakeVisible (b);
        auto* by = bypass.add (new juce::TextButton());
        styleStateButton (*by, theme::accent);
        by->setTooltip (U ("Activer/contourner l'effet"));
        by->onClick = [this, i]
        {
            if (auto* t = track()) { auto ins = ctx.getInserts (*t); if (i < ins.size()) { ins[i]->setEnabled (! ins[i]->isEnabled()); ctx.changed(); } }
        };
        addAndMakeVisible (by);
    }
    styleButton (addFxBtn);
    addFxBtn.setTooltip (U ("Créer une piste FX (réverbération) et lui envoyer cette piste"));
    addFxBtn.onClick = [this]
    {
        auto* src = track();
        ctx.perform ("track.addFx");
        // On revient sur la piste source, avec un envoi déjà réglé à -10 dB.
        if (src != nullptr && ! ctx.getFxTracks().isEmpty())
        {
            const int bus = EditContext::fxBusOf (*ctx.getFxTracks().getLast());
            if (auto* s = ctx.ensureSend (*src, bus)) s->setGainDb (-10.0f);
            ctx.selectTrack (src);
        }
    };
    addAndMakeVisible (addFxBtn);
    ctx.addChangeListener (this);
    rebuild();
    startTimerHz (20);
}

Inspector::~Inspector()
{
    ctx.removeChangeListener (this);
    detachMeters();
}

void Inspector::detachMeters()
{
    if (measurer) measurer->removeClient (client);
    measurer = nullptr;
    meterPlugin = nullptr;
    signature = {};
    trackID = {};
}

te::AudioTrack* Inspector::track() const
{
    return ctx.edit != nullptr ? dynamic_cast<te::AudioTrack*> (te::findTrackForID (*ctx.edit, trackID)) : nullptr;
}

void Inspector::rebuild()
{
    auto* sel = ctx.getSelectedTrack();
    juce::String sig;
    if (sel) { sig << sel->itemID.toString(); for (auto p : sel->pluginList.getPlugins()) sig << "." << p->itemID.toString() << (p->isEnabled() ? "e" : "b"); }
    for (auto* fx : ctx.getFxTracks()) sig << "|fx" << fx->itemID.toString() << fx->getName();
    if (sig == signature && (sel ? sel->itemID : te::EditItemID()) == trackID) { syncValues(); return; }
    detachMeters();
    signature = sig;
    trackID = sel ? sel->itemID : te::EditItemID();
    if (auto* t = track())
    {
        name.setText (t->getName(), juce::dontSendNotification);
        if (auto* lm = t->getLevelMeterPlugin()) { meterPlugin = lm; measurer = &lm->measurer; measurer->addClient (client); }
        auto ins = ctx.getInserts (*t);
        for (int i = 0; i < slots.size(); ++i)
        {
            const bool has = i < ins.size();
            juce::String txt = has ? ((dynamic_cast<SandboxPlugin*> (ins[i]) ? U ("◆ ") : juce::String()) + ins[i]->getName()) : juce::String();
            slots[i]->setButtonText (txt);
            slots[i]->setColour (juce::TextButton::textColourOffId, has ? (ins[i]->isEnabled() ? theme::text : theme::dim) : theme::muted);
            slots[i]->setTooltip (has ? U ("Clic : ouvrir · clic droit : options") : U ("Clic : ajouter un effet d'insert"));
            bypass[i]->setVisible (has);
            bypass[i]->setToggleState (has && ins[i]->isEnabled(), juce::dontSendNotification);
        }
        auto* inst = ctx.getInstrument (*t);
        instrumentBtn.setButtonText (inst ? ((dynamic_cast<SandboxPlugin*> (inst) ? U ("◆ ") : juce::String()) + inst->getName()) : U ("Aucun instrument"));
        instrumentBtn.setTooltip (U ("Clic : ouvrir l'instrument · clic droit : changer d'instrument"));

        // Envois vers les pistes FX.
        sendSliders.clear(); sendLabels.clear(); sendBuses.clear();
        if (EditContext::fxBusOf (*t) < 0)
            for (auto* fx : ctx.getFxTracks())
            {
                const int bus = EditContext::fxBusOf (*fx);
                auto* l = sendLabels.add (new juce::Label ({}, fx->getName()));
                l->setFont (theme::ui (11.5f)); l->setColour (juce::Label::textColourId, theme::text);
                addAndMakeVisible (l);
                auto* s = sendSliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
                s->setRange (-60.0, 6.0, 0.1); s->setSkewFactorFromMidPoint (-12.0); s->setDoubleClickReturnValue (true, 0.0);
                s->setWantsKeyboardFocus (false);
                s->setColour (juce::Slider::trackColourId, juce::Colour (0xff8a7ad8));
                s->setTooltip (U ("Niveau d'envoi vers « ") + fx->getName() + U (" » (post-fader) · double-clic : 0 dB"));
                s->onDragStart = [this] { ctx.edit->getUndoManager().beginNewTransaction ("Envoi"); };
                s->onValueChange = [this, s, bus]
                {
                    if (auto* tr = track())
                        if (auto* send = ctx.ensureSend (*tr, bus)) send->setGainDb (s->getValue() <= -59.9 ? -100.0f : (float) s->getValue());
                };
                addAndMakeVisible (s);
                sendBuses.add (bus);
            }
    }
    for (auto* comp : getChildren()) comp->setVisible (track() != nullptr);
    if (track()) for (int i = 0; i < bypass.size(); ++i) bypass[i]->setVisible (i < ctx.getInserts (*track()).size());
    syncValues();
    resized();
    repaint();
}

void Inspector::syncValues()
{
    auto* t = track();
    if (t == nullptr) return;
    muteBtn.setToggleState (t->isMuted (false), juce::dontSendNotification);
    soloBtn.setToggleState (t->isSolo (false), juce::dontSendNotification);
    armBtn.setToggleState (ctx.isTrackArmed (*t), juce::dontSendNotification);
    monBtn.setToggleState (ctx.isTrackMonitoring (*t), juce::dontSendNotification);
    readBtn.setToggleState (ctx.edit->getAutomationRecordManager().isReadingAutomation(), juce::dontSendNotification);
    writeBtn.setToggleState (ctx.edit->getAutomationRecordManager().isWritingAutomation(), juce::dontSendNotification);
    autoBtn.setToggleState (ctx.isAutomationShown (*t), juce::dontSendNotification);
    if (auto* v = t->getVolumePlugin())
    {
        if (! volume.isMouseButtonDown()) volume.setValue (v->getVolumeDb(), juce::dontSendNotification);
        if (! pan.isMouseButtonDown()) pan.setValue (v->getPan(), juce::dontSendNotification);
        const float db = v->getVolumeDb();
        volLabel.setText (db <= -59.9f ? U ("-∞ dB") : juce::String (db, 1) + " dB", juce::dontSendNotification);
        const int p = juce::roundToInt (v->getPan() * 100.0f);
        panLabel.setText (p == 0 ? juce::String ("C") : (p < 0 ? "G " + juce::String (-p) : "D " + juce::String (p)), juce::dontSendNotification);
    }
    if (! name.isBeingEdited() && name.getText() != t->getName())
    {
        name.setText (t->getName(), juce::dontSendNotification);
        repaint();                                   // titre de section « Voie — <nom> »
    }
    for (int i = 0; i < sendSliders.size(); ++i)
        if (! sendSliders[i]->isMouseButtonDown())
        {
            auto* send = ctx.getSend (*t, sendBuses[i]);
            sendSliders[i]->setValue (send ? juce::jmax (-60.0f, send->getGainDb()) : -60.0, juce::dontSendNotification);
        }
}

void Inspector::timerCallback()
{
    if (track() == nullptr && ctx.getSelectedTrack() != nullptr) { rebuild(); return; }
    if (track() != ctx.getSelectedTrack()) { rebuild(); return; }
    syncValues();
    meter.setLevels (client.getAndClearAudioLevel (0).dB, client.getAndClearAudioLevel (1).dB);
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    auto head = r.removeFromTop (36);
    colourSwatch = head.removeFromLeft (14).reduced (0, 9);
    head.removeFromLeft (6);
    name.setBounds (head);
    auto row = r.removeFromTop (24);
    const int w = (row.getWidth() - 5 * 3) / 6;
    for (auto* b : { &muteBtn, &soloBtn, &armBtn, &monBtn, &readBtn, &writeBtn }) { b->setBounds (row.removeFromLeft (w)); row.removeFromLeft (3); }
    r.removeFromTop (6);
    autoBtn.setBounds (r.removeFromTop (22));
    r.removeFromTop (8);

    auto* t = track();
    const bool hasInst = t != nullptr && ctx.getInstrument (*t) != nullptr;
    instrumentBtn.setVisible (hasInst);
    instrumentY = -1;
    if (hasInst)
    {
        instrumentY = r.getY();
        r.removeFromTop (20);
        instrumentBtn.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);
    }

    faderY = r.getY();
    r.removeFromTop (22);
    auto v = r.removeFromTop (22); volLabel.setBounds (v.removeFromRight (62)); v.removeFromLeft (30); volume.setBounds (v);
    auto p = r.removeFromTop (22); panLabel.setBounds (p.removeFromRight (62)); p.removeFromLeft (30); pan.setBounds (p);
    r.removeFromTop (4);
    meter.setBounds (r.removeFromTop (8));
    r.removeFromTop (10);

    insertsY = r.getY();
    r.removeFromTop (20);
    // Hauteur disponible : 8 inserts au plus, le reste pour les envois.
    const int sendsH = 30 + juce::jmax (1, sendSliders.size()) * 36;
    const int maxSlots = juce::jlimit (2, 8, (r.getHeight() - sendsH) / 24);
    for (int i = 0; i < slots.size(); ++i)
    {
        const bool show = i < maxSlots && track() != nullptr;
        slots[i]->setVisible (show);
        if (! show) { bypass[i]->setVisible (false); continue; }
        auto s = r.removeFromTop (22); r.removeFromTop (2);
        bypass[i]->setBounds (s.removeFromLeft (20)); s.removeFromLeft (3);
        slots[i]->setBounds (s);
    }
    r.removeFromTop (8);
    sendsY = r.getY();
    r.removeFromTop (22);
    for (int i = 0; i < sendSliders.size(); ++i)
    {
        sendLabels[i]->setBounds (r.removeFromTop (16));
        sendSliders[i]->setBounds (r.removeFromTop (18));
        r.removeFromTop (2);
    }
    addFxBtn.setBounds (r.removeFromTop (22).withTrimmedRight (r.getWidth() / 2));
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line); g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    auto* t = track();
    if (t == nullptr)
    {
        g.setColour (theme::muted); g.setFont (theme::ui (12.5f));
        g.drawFittedText (U ("Sélectionnez une piste pour afficher ses réglages."), getLocalBounds().reduced (16), juce::Justification::centredTop, 3);
        return;
    }
    g.setColour (juce::Colour (0xff35393f)); g.fillRect (0, 0, getWidth() - 1, 36);
    g.setColour (t->getColour()); g.fillRoundedRectangle (colourSwatch.toFloat(), 2.0f);
    auto section = [&] (int y, const juce::String& title)
    {
        auto r = juce::Rectangle<int> (0, y, getWidth() - 1, 18);
        g.setColour (juce::Colour (0xff24272b)); g.fillRect (r);
        juce::Path tri; tri.addTriangle (8, (float) y + 6, 14, (float) y + 6, 11, (float) y + 11);
        g.setColour (theme::muted); g.fillPath (tri);
        g.setFont (theme::ui (11.5f, true)); g.setColour (theme::text);
        g.drawText (title, r.withTrimmedLeft (20), juce::Justification::centredLeft);
    };
    if (instrumentY >= 0) section (instrumentY, "Instrument");
    section (faderY, U ("Voie — ") + t->getName());
    section (insertsY, "Inserts");
    section (sendsY, EditContext::fxBusOf (*t) >= 0 ? U ("Piste FX (retour du bus ") + juce::String (EditContext::fxBusOf (*t) + 1) + ")" : U ("Envois (sends)"));
    if (sendSliders.isEmpty() && EditContext::fxBusOf (*t) < 0)
    {
        g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
        g.drawText (U ("Aucune piste FX"), addFxBtn.getRight() + 8, addFxBtn.getY(), getWidth() - addFxBtn.getRight() - 12, addFxBtn.getHeight(), juce::Justification::centredLeft);
    }
    g.setColour (theme::muted); g.setFont (theme::ui (11.0f));
    g.drawText ("Vol", 10, volume.getY(), 28, volume.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Pan", 10, pan.getY(), 28, pan.getHeight(), juce::Justification::centredLeft);
}

void Inspector::mouseDown (const juce::MouseEvent& e)
{
    auto* t = track();
    if (t == nullptr || ! colourSwatch.expanded (4).contains (e.getPosition())) return;
    static const juce::uint32 cols[] = { 0xffd2574b, 0xffe08a3c, 0xffd9c048, 0xff7dbb4f, 0xff3fae8f, 0xff3f9ad6, 0xff6a6fd8, 0xffa65fc8, 0xffd25f9c, 0xff8c9299 };
    juce::PopupMenu m;
    for (int i = 0; i < 10; ++i)
    {
        juce::PopupMenu::Item it; it.itemID = i + 1; it.text = U ("Couleur ") + juce::String (i + 1); it.colour = juce::Colour (cols[i]);
        m.addItem (it);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (colourSwatch)), [this] (int r)
    {
        if (r > 0) if (auto* tr = track()) { ctx.edit->getUndoManager().beginNewTransaction ("Couleur"); tr->setColour (juce::Colour (cols[r - 1])); ctx.changed(); repaint(); }
    });
}

void Inspector::showInstrumentMenu()
{
    auto* t = track();
    if (t == nullptr) return;
    juce::PopupMenu m;
    auto bi = ctx.catalog.getBuiltIns (true), vi = ctx.catalog.getVST3 (true);
    for (int i = 0; i < bi.size(); ++i) m.addItem (1 + i, bi[i].name);
    if (! vi.isEmpty()) m.addSectionHeader (U ("VST3 (isolés)"));
    for (int i = 0; i < vi.size(); ++i) m.addItem (1000 + i, vi[i].name + "  -  " + vi[i].manufacturer);
    if (ctx.getInstrument (*t)) { m.addSeparator(); m.addItem (5000, U ("Retirer l'instrument")); }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentBtn), [this, bi, vi] (int r)
    {
        auto* tr = track();
        if (r <= 0 || tr == nullptr) return;
        ctx.edit->getUndoManager().beginNewTransaction ("Instrument");
        if (auto* old = ctx.getInstrument (*tr)) old->deleteFromParent();
        if (r != 5000) PluginCatalog::addToTrack (*tr, r >= 1000 ? vi[r - 1000] : bi[r - 1]);
        ctx.routeInputsTo (tr);
        ctx.changed();
    });
}

void Inspector::showSlotMenu (int slot, juce::Component* target)
{
    auto* t = track();
    if (t == nullptr) return;
    auto ins = ctx.getInserts (*t);
    auto bf = ctx.catalog.getBuiltIns (false), vf = ctx.catalog.getVST3 (false);
    juce::PopupMenu add;
    for (int i = 0; i < bf.size(); ++i) add.addItem (1 + i, bf[i].name);
    if (! vf.isEmpty()) add.addSectionHeader (U ("VST3 (isolés)"));
    for (int i = 0; i < vf.size(); ++i) add.addItem (1000 + i, vf[i].name + "  -  " + vf[i].manufacturer);

    if (slot < ins.size())
    {
        te::Plugin::Ptr p (ins[slot]);
        if (! juce::ModifierKeys::currentModifiers.isPopupMenu()) { if (ctx.openPluginEditor) ctx.openPluginEditor (*p, target); return; }
        juce::PopupMenu m;
        m.addItem (6001, U ("Ouvrir l'éditeur"));
        m.addItem (6002, p->isEnabled() ? U ("Contourner (bypass)") : U ("Activer"));
        m.addItem (6003, U ("Retirer l'effet"));
        m.addSubMenu ("Remplacer par", add);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, p, bf, vf, target] (int r)
        {
            auto* tr = track();
            if (r <= 0 || tr == nullptr) return;
            if (r == 6001) { if (ctx.openPluginEditor) ctx.openPluginEditor (*p, target); return; }
            ctx.edit->getUndoManager().beginNewTransaction ("Insert");
            if (r == 6002) p->setEnabled (! p->isEnabled());
            else
            {
                p->deleteFromParent();
                if (r != 6003) PluginCatalog::addToTrack (*tr, r >= 1000 ? vf[r - 1000] : bf[r - 1]);
            }
            ctx.changed();
        });
        return;
    }
    add.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, bf, vf] (int r)
    {
        auto* tr = track();
        if (r <= 0 || tr == nullptr) return;
        ctx.edit->getUndoManager().beginNewTransaction (U ("Ajouter un effet"));
        PluginCatalog::addToTrack (*tr, r >= 1000 ? vf[r - 1000] : bf[r - 1]);
        ctx.changed();
    });
}

//==============================================================================
MediaRack::MediaRack (EditContext& c) : ctx (c)
{
    for (auto* b : { &tabInst, &tabFx, &tabFiles })
    {
        styleButton (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (77);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accentBg);
        b->setColour (juce::TextButton::textColourOnId, theme::accent);
        addAndMakeVisible (*b);
    }
    tabInst.setToggleState (true, juce::dontSendNotification);
    tabInst.onClick = [this] { tab = 0; refill(); };
    tabFx.onClick = [this] { tab = 1; refill(); };
    tabFiles.onClick = [this] { tab = 2; refill(); };
    addAndMakeVisible (filter);
    filter.setTextToShowWhenEmpty (U ("Filtrer…"), theme::dim);
    filter.setFont (theme::ui (13.0f));
    filter.onTextChange = [this] { refill(); };
    addAndMakeVisible (list);
    list.setModel (this);
    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, theme::bg);
    list.setWantsKeyboardFocus (false);
    styleButton (useBtn, true);
    useBtn.onClick = [this] { useItem (list.getSelectedRow()); };
    addAndMakeVisible (useBtn);
    ctx.catalog.addChangeListener (this);
    refill();
}

MediaRack::~MediaRack() { ctx.catalog.removeChangeListener (this); }

void MediaRack::refill()
{
    items.clear();
    const auto q = filter.getText().trim().toLowerCase();
    auto match = [&] (const juce::String& s) { return q.isEmpty() || s.toLowerCase().contains (q); };
    if (tab < 2)
    {
        const bool inst = tab == 0;
        items.add ({ U ("Intégrés"), {}, {}, {}, true });
        for (auto& e : ctx.catalog.getBuiltIns (inst)) if (match (e.name)) items.add ({ e.name, U ("Onde"), e, {} });
        auto v = ctx.catalog.getVST3 (inst);
        items.add ({ U ("VST3 — chaque plugin dans son propre processus"), {}, {}, {}, true });
        for (auto& e : v) if (match (e.name + e.manufacturer)) items.add ({ e.name, e.manufacturer, e, {} });
        if (v.isEmpty()) items.add ({ ctx.catalog.isScanning() ? U ("Analyse en cours…") : U ("Aucun VST3 trouvé (Studio › Rechercher des plugins)"), {}, {}, {}, true });
    }
    else
    {
        juce::Array<juce::File> roots { juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Onde"),
                                        juce::File::getSpecialLocation (juce::File::userDesktopDirectory) };
        for (auto& root : roots)
        {
            if (! root.isDirectory()) continue;
            items.add ({ root.getFileName(), {}, {}, root, true });
            int n = 0;
            for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", juce::File::findFiles))
            {
                const auto f = entry.getFile();
                if (match (f.getFileName())) { items.add ({ f.getFileName(), f.getParentDirectory().getFileName(), {}, f }); if (++n >= 300) break; }
            }
        }
    }
    list.updateContent();
    list.repaint();
}

void MediaRack::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, items.size())) return;
    auto& it = items.getReference (row);
    if (it.isHeader)
    {
        g.fillAll (juce::Colour (0xff27292d));
        g.setColour (theme::muted); g.setFont (theme::ui (11.0f, true));
        g.drawText (it.title, 8, 0, w - 12, h, juce::Justification::centredLeft, true);
        return;
    }
    if (selected) g.fillAll (theme::accentBg);
    g.setColour (theme::text); g.setFont (theme::ui (12.5f));
    g.drawText (it.title, 14, 0, w - 110, h, juce::Justification::centredLeft, true);
    g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
    g.drawText (it.sub, w - 100, 0, 94, h, juce::Justification::centredRight, true);
}

void MediaRack::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { useItem (row); }

void MediaRack::useItem (int row)
{
    if (! juce::isPositiveAndBelow (row, items.size()) || ctx.edit == nullptr) return;
    auto it = items[row];
    if (it.isHeader) return;
    auto& um = ctx.edit->getUndoManager();
    if (tab == 0)
    {
        um.beginNewTransaction (U ("Ajouter une piste instrument"));
        ctx.addInstrumentTrack (&it.entry);
        if (ctx.setStatus) ctx.setStatus (U ("Piste instrument « ") + it.entry.name + U (" » ajoutée"));
    }
    else if (tab == 1)
    {
        auto* t = ctx.getSelectedTrack();
        if (t == nullptr) return;
        um.beginNewTransaction (U ("Ajouter un effet"));
        PluginCatalog::addToTrack (*t, it.entry);
        if (ctx.setStatus) ctx.setStatus (U ("« ") + it.entry.name + U (" » inséré sur « ") + t->getName() + U (" »"));
    }
    else if (it.file.existsAsFile())
    {
        um.beginNewTransaction (U ("Importer de l'audio"));
        auto* t = ctx.getSelectedTrack();
        if (t == nullptr || ctx.getInstrument (*t) != nullptr) { t = ctx.addAudioTrack(); if (t) t->setName (it.file.getFileNameWithoutExtension()); }
        const double start = ctx.edit->getTransport().getPosition().inSeconds();
        const double len = te::AudioFile (ctx.engine, it.file).getLength();
        if (t && len > 0)
            if (auto c = t->insertWaveClip (it.file.getFileNameWithoutExtension(), it.file, { { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start + len) }, te::TimeDuration() }, false))
                ctx.selectClip (c.get());
        if (ctx.setStatus) ctx.setStatus (U ("« ") + it.file.getFileName() + U (" » importé à la position du curseur"));
    }
    ctx.changed();
}

void MediaRack::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromTop (22);
    auto tabs = r.removeFromTop (24);
    const int w = tabs.getWidth() / 3;
    tabInst.setBounds (tabs.removeFromLeft (w)); tabFx.setBounds (tabs.removeFromLeft (w)); tabFiles.setBounds (tabs);
    r.removeFromTop (6);
    filter.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    useBtn.setBounds (r.removeFromBottom (26));
    r.removeFromBottom (6);
    list.setBounds (r);
}

void MediaRack::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line); g.drawVerticalLine (0, 0.0f, (float) getHeight());
    g.setColour (theme::text); g.setFont (theme::ui (12.5f, true));
    g.drawText (U ("Médias & plugins"), 10, 4, getWidth() - 20, 20, juce::Justification::centredLeft);
}

//==============================================================================
TransportPanel::TransportPanel (EditContext& c) : ctx (c)
{
    struct T { const char* tip; IconButton::Painter p; const char* id; juce::Colour on; };
    const T tr[] = {
        { "Aller au début du projet  (Début)", icons::toStart(), "transport.home", theme::accent },
        { "Rembobiner d'une mesure  (-)",     icons::rewind(),  "transport.rewind", theme::accent },
        { "Avancer d'une mesure  (+)",        icons::forward(), "transport.forward", theme::accent },
        { "Aller à la fin du projet  (Fin)",  icons::toEnd(),   "transport.end", theme::accent },
        { "Cycle  (L ou /)",                  icons::cycle(),   "transport.loop", theme::cycle },
        { "Stop  (0)",                        icons::stop(),    "transport.stop", theme::accent },
        { "Démarrer  (Espace)",              icons::play(),    "transport.play", theme::play },
        { "Enregistrer  (R ou *)",           icons::record(),  "transport.record", theme::rec } };
    for (auto& t : tr)
    {
        auto* b = buttons.add (new IconButton (U (t.tip), t.p, t.on));
        juce::String id (t.id);
        b->onClick = [this, id] { ctx.perform (id); };
        addAndMakeVisible (b);
    }
    clickBtn = std::make_unique<IconButton> (U ("Métronome  (C)"), icons::metronome(), theme::accent);
    clickBtn->onClick = [this] { ctx.perform ("transport.click"); };
    punchInBtn = std::make_unique<IconButton> (U ("Punch : n'enregistre qu'entre les délimiteurs gauche et droit"), icons::punch (true), theme::rec);
    punchInBtn->onClick = [this] { if (ctx.edit) ctx.edit->recordingPunchInOut = ! ctx.edit->recordingPunchInOut.get(); };
    addAndMakeVisible (*clickBtn); addAndMakeVisible (*punchInBtn);
    addAndMakeVisible (outMeter);
    startTimerHz (25);
}

TransportPanel::~TransportPanel() { master.detach(); }

//==============================================================================
void MasterLevelTap::detach()
{
    if (listened != nullptr)
    {
        if (context != nullptr && listened->getCurrentPlaybackContext() == context)
            context->masterLevels.removeClient (client);
        listened->getTransport().removeListener (this);
    }
    listened = nullptr;
    context = nullptr;
}

void MasterLevelTap::playbackContextChanged()
{
    // Le contexte a été détruit (ou remplacé) : l'ancien vumètre n'existe plus, on l'oublie sans y toucher.
    if (listened != nullptr && listened->getCurrentPlaybackContext() != context)
        context = nullptr;
}

std::pair<float, float> MasterLevelTap::read()
{
    if (ctx.edit != listened)
    {
        detach();
        listened = ctx.edit;
        if (listened != nullptr) listened->getTransport().addListener (this);
    }
    if (listened == nullptr) return { -100.0f, -100.0f };
    auto* pc = listened->getCurrentPlaybackContext();
    if (pc != context)
    {
        context = pc;
        if (context != nullptr) context->masterLevels.addClient (client);
    }
    if (context == nullptr) return { -100.0f, -100.0f };
    return { client.getAndClearAudioLevel (0).dB, client.getAndClearAudioLevel (1).dB };
}

void TransportPanel::timerCallback()
{
    if (ctx.edit == nullptr) return;
    auto& t = ctx.edit->getTransport();
    buttons[4]->setActive (t.looping.get());
    buttons[6]->setActive (t.isPlaying() && ! t.isRecording());
    buttons[7]->setActive (t.isRecording());
    clickBtn->setActive (ctx.edit->clickTrackEnabled.get());
    punchInBtn->setActive (ctx.edit->recordingPunchInOut.get());

    const auto lv = master.read();
    outMeter.setLevels (lv.first, lv.second);
    // Compteur : à chaque image tant que la position change ; délimiteurs, tempo, signature, charge : 2 fois/s.
    const double pos = t.getPosition().inSeconds();
    if (pos != shownPosition) { shownPosition = pos; repaint (display); }
    if (++tick % 12 == 0) { repaint (perfArea); repaint (locL); repaint (locR); repaint (tempoArea); repaint (sigArea); repaint (display); }
}

void TransportPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    perfArea = r.removeFromLeft (110);
    r.removeFromLeft (12);
    locL = r.removeFromLeft (130).withHeight (r.getHeight() / 2);
    locR = locL.translated (0, r.getHeight() / 2);
    r.removeFromLeft (8);
    punchInBtn->setBounds (r.removeFromLeft (30).withSizeKeepingCentre (30, 30));
    r.removeFromLeft (14);
    for (auto* b : buttons) { b->setBounds (r.removeFromLeft (b == buttons[6] || b == buttons[7] ? 40 : 34).withSizeKeepingCentre (b == buttons[6] || b == buttons[7] ? 40 : 32, 34)); r.removeFromLeft (2); }
    r.removeFromLeft (14);
    display = r.removeFromLeft (240);
    r.removeFromLeft (12);
    clickBtn->setBounds (r.removeFromLeft (34).withSizeKeepingCentre (32, 32));
    r.removeFromLeft (8);
    tempoArea = r.removeFromLeft (110);
    r.removeFromLeft (6);
    sigArea = r.removeFromLeft (64);
    r.removeFromLeft (12);
    outMeter.setBounds (r.removeFromRight (juce::jmin (180, juce::jmax (60, r.getWidth()))).withSizeKeepingCentre (juce::jmin (180, juce::jmax (60, r.getWidth())), 14));
}

void TransportPanel::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34373d), 0, 0, juce::Colour (0xff27292e), 0, (float) getHeight(), false));
    g.fillAll();
    g.setColour (theme::line); g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    if (ctx.edit == nullptr) return;
    auto& tr = ctx.edit->getTransport();
    auto& dm = ctx.engine.getDeviceManager();

    // Performance (charge CPU audio).
    {
        auto r = perfArea.toFloat();
        theme::bevel (g, r, theme::display);
        const float cpu = juce::jlimit (0.0f, 1.0f, (float) dm.getCpuUsage());
        g.setColour (theme::muted); g.setFont (theme::ui (10.5f));
        g.drawText ("Charge audio", r.reduced (6, 3).removeFromTop (14), juce::Justification::centredLeft);
        auto bar = r.reduced (6, 0).withTop (r.getY() + 20).withHeight (7.0f);
        g.setColour (theme::panel); g.fillRect (bar);
        g.setColour (cpu > 0.8f ? theme::rec : cpu > 0.5f ? theme::warn : theme::ok); g.fillRect (bar.withWidth (bar.getWidth() * cpu));
        g.setColour (theme::muted);
        g.drawText (juce::String (dm.getSampleRate() / 1000.0, 1) + " kHz  " + juce::String (dm.getBlockSize()) + " éch.", r.reduced (6, 2).removeFromBottom (12), juce::Justification::centredLeft);
    }

    // Délimiteurs gauche / droit.
    auto loop = tr.getLoopRange();
    auto locator = [&] (juce::Rectangle<int> a, const juce::String& lbl, double t, juce::Colour col)
    {
        auto r = a.toFloat().reduced (0, 1);
        theme::bevel (g, r, theme::display, 2.0f);
        g.setColour (col); g.fillRect (r.getX() + 1, r.getY() + 1, 18.0f, r.getHeight() - 2);
        g.setColour (juce::Colours::white); g.setFont (theme::ui (11.5f, true));
        g.drawText (lbl, r.withWidth (20.0f), juce::Justification::centred);
        g.setColour (theme::lcd); g.setFont (theme::mono (12.5f));
        g.drawText (ctx.formatBars (t), r.withTrimmedLeft (24), juce::Justification::centredLeft);
    };
    locator (locL, "L", loop.getStart().inSeconds(), theme::cycle);
    locator (locR, "R", loop.getEnd().inSeconds(), theme::cycle);

    // Afficheur principal : mesures et temps.
    {
        const double t = tr.getPosition().inSeconds();
        auto r = display.toFloat();
        theme::bevel (g, r, theme::display, 3.0f);
        g.setColour (tr.isRecording() ? theme::rec : theme::lcd); g.setFont (theme::mono (22.0f, true));
        g.drawText (ctx.formatBars (t), r.reduced (10, 0).removeFromTop (r.getHeight() * 0.64f), juce::Justification::centredLeft);
        g.setColour (theme::muted); g.setFont (theme::mono (11.5f));
        g.drawText (EditContext::formatTime (t), r.reduced (10, 2).removeFromBottom (r.getHeight() * 0.36f), juce::Justification::centredLeft);
        g.setFont (theme::ui (10.0f)); g.setColour (theme::dim);
        g.drawText (tr.isRecording() ? U ("ENREGISTREMENT") : tr.isPlaying() ? U ("LECTURE") : U ("ARRÊT"), r.reduced (10, 2).removeFromBottom (r.getHeight() * 0.36f), juce::Justification::centredRight);
    }

    // Tempo et signature.
    {
        auto r = tempoArea.toFloat();
        theme::bevel (g, r, theme::display, 3.0f);
        g.setColour (theme::muted); g.setFont (theme::ui (10.5f));
        g.drawText (ctx.hasTempoChanges() ? U ("Tempo (piste)") : U ("Tempo (fixe)"), r.reduced (8, 3).removeFromTop (13), juce::Justification::centredLeft);
        g.setColour (ctx.hasTempoChanges() ? juce::Colour (0xffff8a72) : theme::lcd); g.setFont (theme::mono (17.0f, true));
        g.drawText (juce::String (ctx.bpmAt (tr.getPosition().inSeconds()), 3), r.reduced (8, 2).removeFromBottom (24), juce::Justification::centredLeft);
        auto s = sigArea.toFloat();
        theme::bevel (g, s, theme::display, 3.0f);
        g.setColour (theme::muted); g.setFont (theme::ui (10.5f));
        g.drawText ("Signature", s.reduced (6, 3).removeFromTop (13), juce::Justification::centredLeft);
        g.setColour (theme::lcd); g.setFont (theme::mono (17.0f, true));
        auto* ts = ctx.edit->tempoSequence.getTimeSig (0);
        g.drawText (ts ? juce::String (ts->numerator.get()) + "/" + juce::String (ts->denominator.get()) : juce::String ("4/4"), s.reduced (6, 2).removeFromBottom (24), juce::Justification::centredLeft);
    }
    g.setColour (theme::muted); g.setFont (theme::ui (10.5f));
    g.drawText (U ("Sortie stéréo"), outMeter.getBounds().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
}

void TransportPanel::mouseDown (const juce::MouseEvent& e)
{
    if (ctx.edit == nullptr) return;
    auto& tr = ctx.edit->getTransport();
    const auto p = e.getPosition();
    if (locL.contains (p) || locR.contains (p))
    {
        const bool left = locL.contains (p);
        if (p.x < locL.getX() + 22 || e.mods.isAltDown())
        {
            // Clic sur la lettre (ou Alt) : place le délimiteur au curseur.
            const auto pos = tr.getPosition();
            if (left) tr.setLoopIn (pos); else tr.setLoopOut (pos);
            if (ctx.setStatus) ctx.setStatus (left ? U ("Délimiteur gauche placé au curseur") : U ("Délimiteur droit placé au curseur"));
        }
        else tr.setPosition (left ? tr.getLoopRange().getStart() : tr.getLoopRange().getEnd());
        return;
    }
    if (tempoArea.contains (p))
    {
        if (e.getNumberOfClicks() == 2) { ctx.perform ("song.tempo"); return; }
        draggingTempo = true;
        dragTempo = ctx.edit->tempoSequence.getTempoAt (tr.getPosition()).getBpm();
        ctx.edit->getUndoManager().beginNewTransaction ("Tempo");
        return;
    }
    if (sigArea.contains (p))
    {
        juce::PopupMenu m;
        const int sigs[][2] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 } };
        for (int i = 0; i < 7; ++i) m.addItem (i + 1, juce::String (sigs[i][0]) + "/" + juce::String (sigs[i][1]));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (sigArea)), [this, sigs] (int r)
        {
            if (r <= 0 || ctx.edit == nullptr) return;
            if (auto* ts = ctx.edit->tempoSequence.getTimeSig (0))
            {
                auto* um = &ctx.edit->getUndoManager();
                um->beginNewTransaction ("Signature");
                ts->numerator.setValue (sigs[r - 1][0], um);
                ts->denominator.setValue (sigs[r - 1][1], um);
                ctx.changed();
            }
        });
        return;
    }
    if (display.contains (p) && e.getNumberOfClicks() == 2) ctx.perform ("transport.home");
}

void TransportPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingTempo || ctx.edit == nullptr) return;
    const double bpm = juce::jlimit (20.0, 400.0, dragTempo - e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.05 : 0.5));
    ctx.changeTempo ([&] { ctx.edit->tempoSequence.getTempoAt (ctx.edit->getTransport().getPosition()).setBpm (std::round (bpm * 100.0) / 100.0); });
    ctx.changed();
}

void TransportPanel::mouseUp (const juce::MouseEvent&) { draggingTempo = false; }
