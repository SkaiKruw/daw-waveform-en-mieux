#include "DrummerPanel.h"
#include "ParamControls.h"

//==============================================================================
// Pavé : horizontalement simple → complexe, verticalement doux (bas) → fort (haut).
class DrummerPanel::Pad  : public juce::Component
{
public:
    explicit Pad (DrummerPanel& o) : owner (o) {}
    float x = 0.4f, y = 0.6f;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2117), r.getX(), r.getBottom(), juce::Colour (0xff3b2412), r.getRight(), r.getY(), false));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0x22ffffff));
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 4), r.getY(), r.getBottom());
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4), r.getX(), r.getRight());
        }
        g.setColour (theme::muted); g.setFont (theme::ui (11.5f, true));
        g.drawText ("Simple", r.reduced (10, 6), juce::Justification::bottomLeft);
        g.drawText ("Complexe", r.reduced (10, 6), juce::Justification::bottomRight);
        g.drawText ("Fort", r.reduced (10, 6), juce::Justification::topLeft);
        g.drawText ("Doux", r.reduced (10, 22), juce::Justification::bottomLeft);
        const float px = r.getX() + x * r.getWidth(), py = r.getBottom() - y * r.getHeight();
        g.setColour (juce::Colour (0xffe08a3c).withAlpha (0.25f)); g.fillEllipse (px - 22, py - 22, 44, 44);
        g.setColour (juce::Colour (0xffe08a3c)); g.fillEllipse (px - 9, py - 9, 18, 18);
        g.setColour (juce::Colours::white); g.drawEllipse (px - 9, py - 9, 18, 18, 2.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { owner.ctx.edit->getUndoManager().beginNewTransaction ("Batteur : jeu"); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat();
        x = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        y = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
        owner.settings.complexity = x; owner.settings.intensity = y;
        if (auto* c = owner.region()) drummer::apply (*c, owner.settings);
        repaint();
    }
private:
    DrummerPanel& owner;
};

//==============================================================================
DrummerPanel::DrummerPanel (EditContext& c) : ctx (c)
{
    for (int i = 0; i < drummer::numStyles; ++i)
    {
        auto* b = styleButtons.add (new juce::TextButton (juce::String::fromUTF8 (drummer::styleNames[i])));
        b->setTooltip (juce::String::fromUTF8 (drummer::styleDescriptions[i]));
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe08a3c));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b->onClick = [this, i] { setStyle (i); };
        addAndMakeVisible (b);
    }
    pad = std::make_unique<Pad> (*this);
    addAndMakeVisible (*pad);
    for (auto* s : { &swing, &humanize })
    {
        s->setRange (0.0, 100.0, 1.0);
        s->setTextValueSuffix (" %");
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        s->setColour (juce::Slider::trackColourId, juce::Colour (0xffe08a3c));
        s->onDragStart = [this] { ctx.edit->getUndoManager().beginNewTransaction ("Batteur"); };
        addAndMakeVisible (*s);
    }
    swing.onValueChange = [this] { settings.swing = (float) swing.getValue() / 100.0f; if (auto* r = region()) drummer::apply (*r, settings); };
    humanize.onValueChange = [this] { settings.humanize = (float) humanize.getValue() / 100.0f; if (auto* r = region()) drummer::apply (*r, settings); };
    fills.addItem ("Pas de remplissage", 1);
    fills.addItem (U ("Remplissage toutes les 4 mesures"), 2);
    fills.addItem (U ("Remplissage toutes les 8 mesures"), 3);
    fills.onChange = [this] { settings.fills = fills.getSelectedId() - 1; commit (U ("Batteur : remplissages")); };
    addAndMakeVisible (fills);
    for (auto* t : { &kick, &snare, &cymbals, &percussion })
    {
        t->onClick = [this]
        {
            settings.kick = kick.getToggleState(); settings.snare = snare.getToggleState();
            settings.cymbals = cymbals.getToggleState(); settings.percussion = percussion.getToggleState();
            commit (U ("Batteur : éléments"));
        };
        addAndMakeVisible (*t);
    }
    variation.setTooltip (U ("Un autre jeu, avec les mêmes réglages"));
    variation.onClick = [this] { settings.variation += 1; commit (U ("Batteur : nouvelle variation")); };
    addAndMakeVisible (variation);
    addDrummer.onClick = [this]
    {
        ctx.edit->getUndoManager().beginNewTransaction (U ("Ajouter un batteur"));
        if (auto* clip = drummer::addDrummerTrack (ctx)) { ctx.selectClip (clip); syncFromRegion(); }
    };
    addAndMakeVisible (addDrummer);
    info.setFont (theme::ui (12.0f));
    info.setColour (juce::Label::textColourId, theme::muted);
    addAndMakeVisible (info);
    setSize (720, 470);
    syncFromRegion();
    startTimerHz (5);
}

DrummerPanel::~DrummerPanel() = default;

te::MidiClip* DrummerPanel::region() const
{
    if (ctx.edit == nullptr) return nullptr;
    if (auto* c = ctx.getSelectedClip(); drummer::isDrummerRegion (c)) return dynamic_cast<te::MidiClip*> (c);
    if (auto* t = ctx.getSelectedTrack())
        for (auto* c : t->getClips()) if (drummer::isDrummerRegion (c)) return dynamic_cast<te::MidiClip*> (c);
    return nullptr;
}

void DrummerPanel::syncFromRegion()
{
    auto* r = region();
    shownRegion = r != nullptr ? r->itemID : te::EditItemID();
    if (r != nullptr) settings = drummer::read (*r);
    for (int i = 0; i < styleButtons.size(); ++i) styleButtons[i]->setToggleState (i == settings.style, juce::dontSendNotification);
    pad->x = settings.complexity; pad->y = settings.intensity; pad->repaint();
    swing.setValue (settings.swing * 100.0, juce::dontSendNotification);
    humanize.setValue (settings.humanize * 100.0, juce::dontSendNotification);
    fills.setSelectedId (settings.fills + 1, juce::dontSendNotification);
    kick.setToggleState (settings.kick, juce::dontSendNotification); snare.setToggleState (settings.snare, juce::dontSendNotification);
    cymbals.setToggleState (settings.cymbals, juce::dontSendNotification); percussion.setToggleState (settings.percussion, juce::dontSendNotification);
    for (auto* comp : getChildren()) if (comp != &addDrummer && comp != &info) comp->setEnabled (r != nullptr);
    info.setText (r != nullptr ? U ("Région « ") + r->getName() + U (" » sur « ") + (r->getTrack() ? r->getTrack()->getName() : juce::String())
                                 + U (" » · lancez la lecture pour l'entendre ; chaque réglage regénère le jeu")
                               : U ("Sélectionnez une région de batteur, ou ajoutez un batteur"), juce::dontSendNotification);
    repaint();
}

void DrummerPanel::timerCallback()
{
    auto* r = region();
    if ((r != nullptr ? r->itemID : te::EditItemID()) != shownRegion) syncFromRegion();
}

void DrummerPanel::commit (const juce::String& what)
{
    auto* r = region();
    if (r == nullptr) return;
    ctx.edit->getUndoManager().beginNewTransaction (what);
    drummer::apply (*r, settings);
    ctx.changed();
}

void DrummerPanel::setStyle (int s)
{
    settings.style = juce::jlimit (0, drummer::numStyles - 1, s);
    for (int i = 0; i < styleButtons.size(); ++i) styleButtons[i]->setToggleState (i == settings.style, juce::dontSendNotification);
    commit (U ("Batteur : style ") + juce::String::fromUTF8 (drummer::styleNames[settings.style]));
    repaint();
}

void DrummerPanel::setPad (float complexity, float intensity)
{
    settings.complexity = juce::jlimit (0.0f, 1.0f, complexity);
    settings.intensity = juce::jlimit (0.0f, 1.0f, intensity);
    pad->x = settings.complexity; pad->y = settings.intensity; pad->repaint();
    commit (U ("Batteur : jeu"));
}

void DrummerPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
    g.drawText ("Batteur", 16, 10, 200, 24, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
    g.drawText (juce::String::fromUTF8 (drummer::styleDescriptions[settings.style]), 120, 10, getWidth() - 136, 24, juce::Justification::centredLeft, true);
    g.setFont (theme::ui (11.5f));
    g.drawText ("Swing", swing.getX(), swing.getY() - 16, 120, 14, juce::Justification::centredLeft);
    g.drawText ("Jeu humain", humanize.getX(), humanize.getY() - 16, 120, 14, juce::Justification::centredLeft);
}

void DrummerPanel::resized()
{
    auto r = getLocalBounds().reduced (16).withTrimmedTop (32);
    auto styles = r.removeFromTop (34);
    const int w = styles.getWidth() / drummer::numStyles;
    for (auto* b : styleButtons) { b->setBounds (styles.removeFromLeft (w).reduced (2, 0)); }
    r.removeFromTop (12);
    info.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (6);
    auto right = r.removeFromRight (250);
    r.removeFromRight (16);
    pad->setBounds (r);
    right.removeFromTop (18);
    swing.setBounds (right.removeFromTop (24)); right.removeFromTop (24);
    humanize.setBounds (right.removeFromTop (24)); right.removeFromTop (12);
    fills.setBounds (right.removeFromTop (26)); right.removeFromTop (10);
    for (auto* t : { &kick, &snare, &cymbals, &percussion }) t->setBounds (right.removeFromTop (24));
    right.removeFromTop (10);
    variation.setBounds (right.removeFromTop (28)); right.removeFromTop (6);
    addDrummer.setBounds (right.removeFromTop (28));
}

juce::DialogWindow* DrummerPanel::show (EditContext& c)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new DrummerPanel (c));
    o.dialogTitle = "Batteur";
    o.dialogBackgroundColour = juce::Colour (0xff1a1d22);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    // Fenêtre flottante : la fenêtre principale reste utilisable.
    return ondeui::showToolWindow (o.dialogTitle, o.content.release(), o.resizable, o.dialogBackgroundColour);
}
