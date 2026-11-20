#include "MixerView.h"
#include "Panels.h"

// Tranche de console : inserts, panoramique, état, fader + vumètre, nom.
class MixerView::Strip  : public juce::Component
{
public:
    Strip (EditContext& c, te::AudioTrack* t, te::FolderTrack* g = nullptr) : track (t), group (g), ctx (c)
    {
        volumePlugin = track ? track->getVolumePlugin() : group ? group->getVolumePlugin() : ctx.edit->getMasterVolumePlugin().get();

        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setWantsKeyboardFocus (false);
        fader.setTooltip (U ("Volume · double-clic : 0 dB"));
        fader.onDragStart = [this] { ctx.edit->getUndoManager().beginNewTransaction (U ("Volume")); };
        fader.onValueChange = [this] { if (volumePlugin) volumePlugin->setVolumeDb ((float) fader.getValue()); };

        addAndMakeVisible (pan);
        pan.setSliderStyle (juce::Slider::LinearBar);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setWantsKeyboardFocus (false);
        pan.setTooltip (U ("Panoramique · double-clic : centre"));
        pan.onDragStart = [this] { ctx.edit->getUndoManager().beginNewTransaction (U ("Panoramique")); };
        pan.onValueChange = [this] { if (volumePlugin) volumePlugin->setPan ((float) pan.getValue()); };

        addAndMakeVisible (meter);
        if (track)
        {
            styleStateButton (mute, theme::mute); styleStateButton (solo, theme::solo);
            styleStateButton (arm, theme::rec, juce::Colours::white); styleStateButton (mon, theme::monitor);
            mute.onClick = [this] { track->setMute (mute.getToggleState()); ctx.changed(); };
            solo.onClick = [this] { track->setSolo (solo.getToggleState()); ctx.changed(); };
            arm.onClick  = [this] { ctx.setTrackArmed (*track, arm.getToggleState()); };
            mon.onClick  = [this] { ctx.setTrackMonitoring (*track, mon.getToggleState()); };
            for (auto* b : { &mute, &solo, &arm, &mon }) addAndMakeVisible (*b);

            if (auto* lm = track->getLevelMeterPlugin()) { meterPlugin = lm; measurer = &lm->measurer; measurer->addClient (client); }

            if (auto* inst = ctx.getInstrument (*track)) addSlot (inst, true);
            for (auto* p : ctx.getInserts (*track)) addSlot (p, false);
            addAndMakeVisible (addBtn);
            styleButton (addBtn);
            addBtn.setColour (juce::TextButton::buttonColourId, theme::display);
            addBtn.setColour (juce::TextButton::textColourOffId, theme::dim);
            addBtn.onClick = [this] { showAddMenu(); };
        }
        else if (group)
        {
            styleStateButton (mute, theme::mute); styleStateButton (solo, theme::solo);
            mute.onClick = [this] { group->setMute (mute.getToggleState()); ctx.changed(); };
            solo.onClick = [this] { group->setSolo (solo.getToggleState()); ctx.changed(); };
            addAndMakeVisible (mute); addAndMakeVisible (solo);
            if (auto* lm = EditContext::groupMeter (*group, false)) { meterPlugin = lm; measurer = &lm->measurer; measurer->addClient (client); }
        }
        else masterTap = std::make_unique<MasterLevelTap> (ctx);
        sync();
    }

    ~Strip() override { detach(); }
    void detach() { if (measurer) measurer->removeClient (client); measurer = nullptr; meterPlugin = nullptr; if (masterTap) masterTap->detach(); }

    void addSlot (te::Plugin* p, bool isInstrument)
    {
        auto* b = slots.add (new juce::TextButton ((dynamic_cast<SandboxPlugin*> (p) != nullptr ? U ("◆ ") : juce::String()) + p->getName()));
        styleButton (*b);
        b->setColour (juce::TextButton::buttonColourId, isInstrument ? juce::Colour (0xff27384a) : theme::display);
        b->setColour (juce::TextButton::textColourOffId, p->isEnabled() ? theme::text : theme::dim);
        b->setTooltip (U ("Clic : ouvrir · clic droit : contourner / retirer"));
        te::Plugin::Ptr ptr (p);
        b->onClick = [this, ptr, b]
        {
            if (juce::ModifierKeys::currentModifiers.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem (1, U ("Ouvrir"));
                m.addItem (2, ptr->isEnabled() ? U ("Contourner (bypass)") : U ("Activer"));
                m.addItem (3, U ("Retirer"));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b), [this, ptr, b] (int r)
                {
                    if (r == 1 && ctx.openPluginEditor) ctx.openPluginEditor (*ptr, b);
                    if (r == 2) { ctx.edit->getUndoManager().beginNewTransaction ("Bypass"); ptr->setEnabled (! ptr->isEnabled()); ctx.changed(); }
                    if (r == 3) { ctx.edit->getUndoManager().beginNewTransaction (U ("Retirer un plugin")); ptr->deleteFromParent(); ctx.changed(); }
                });
                return;
            }
            if (ctx.openPluginEditor) ctx.openPluginEditor (*ptr, b);
        };
        addAndMakeVisible (b);
    }

    void showAddMenu()
    {
        juce::PopupMenu m;
        auto bi = ctx.catalog.getBuiltIns (false), vf = ctx.catalog.getVST3 (false);
        for (int i = 0; i < bi.size(); ++i) m.addItem (1 + i, bi[i].name);
        if (! vf.isEmpty()) m.addSectionHeader (U ("VST3 (isolés)"));
        for (int i = 0; i < vf.size(); ++i) m.addItem (1000 + i, vf[i].name + "  -  " + vf[i].manufacturer);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn), [this, bi, vf] (int r)
        {
            if (r <= 0) return;
            ctx.edit->getUndoManager().beginNewTransaction (U ("Ajouter un effet"));
            PluginCatalog::addToTrack (*track, r >= 1000 ? vf[r - 1000] : bi[r - 1]);
            ctx.changed();
        });
    }

    void sync()
    {
        if (volumePlugin)
        {
            if (! fader.isMouseButtonDown()) fader.setValue (volumePlugin->getVolumeDb(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())   pan.setValue (volumePlugin->getPan(), juce::dontSendNotification);
        }
        if (track)
        {
            mute.setToggleState (track->isMuted (false), juce::dontSendNotification);
            solo.setToggleState (track->isSolo (false), juce::dontSendNotification);
            arm.setToggleState (ctx.isTrackArmed (*track), juce::dontSendNotification);
            mon.setToggleState (ctx.isTrackMonitoring (*track), juce::dontSendNotification);
        }
        if (group)
        {
            mute.setToggleState (group->isMuted (false), juce::dontSendNotification);
            solo.setToggleState (group->isSolo (false), juce::dontSendNotification);
        }
        if (masterTap) { const auto lv = masterTap->read(); meter.setLevels (lv.first, lv.second); }
        else meter.setLevels (client.getAndClearAudioLevel (0).dB, client.getAndClearAudioLevel (1).dB);
        // Valeurs (volume, crête) : redessinées seulement si le texte affiché change.
        const double db = volumePlugin ? volumePlugin->getVolumeDb() : 0.0;
        const int key = juce::roundToInt (db * 10.0) * 100000 + juce::roundToInt (juce::jmax (-100.0f, meter.getPeak()) * 10.0f);
        if (key != shownValueKey) { shownValueKey = key; repaint (valueArea); }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (4);
        insertsTop = r.getY();
        r.removeFromTop (14);
        // Le rack d'inserts s'adapte à la hauteur : le fader garde au moins 160 px.
        const int rackRows = juce::jlimit (1, 8, (getHeight() - 150 - 160) / 20);
        for (int i = 0; i < slots.size(); ++i)
        {
            slots[i]->setVisible (i < rackRows);
            if (i < rackRows) { slots[i]->setBounds (r.removeFromTop (18)); r.removeFromTop (2); }
        }
        if (track) { addBtn.setVisible (slots.size() < rackRows + 1); if (addBtn.isVisible()) addBtn.setBounds (r.removeFromTop (18)); }
        for (int i = slots.size() + 1; i <= rackRows; ++i) r.removeFromTop (20);
        if (! track) r.removeFromTop (18);
        r.removeFromTop (8);
        pan.setBounds (r.removeFromTop (14));
        r.removeFromTop (6);
        auto states = r.removeFromTop (20);
        if (track)
        {
            const int w = (states.getWidth() - 6) / 4;
            for (auto* b : { &mute, &solo, &arm, &mon }) { b->setBounds (states.removeFromLeft (w)); states.removeFromLeft (2); }
        }
        else if (group)
        {
            const int w = (states.getWidth() - 2) / 2;
            mute.setBounds (states.removeFromLeft (w)); states.removeFromLeft (2); solo.setBounds (states);
        }
        r.removeFromTop (6);
        nameArea = r.removeFromBottom (30);
        valueArea = r.removeFromBottom (20);
        r.removeFromBottom (4);
        auto f = r.reduced (2, 0);
        meter.setBounds (f.removeFromRight (12).reduced (0, 10));
        f.removeFromRight (4);
        scaleArea = f.removeFromRight (20);
        fader.setBounds (f);
    }

    void paint (juce::Graphics& g) override
    {
        const bool master = track == nullptr && group == nullptr;
        const bool selected = (track != nullptr && ctx.selectedTrackID == track->itemID) || (group != nullptr && ctx.selectedFolderID == group->itemID);
        auto r = getLocalBounds();
        g.setColour (selected ? juce::Colour (0xff3a3f46) : master ? juce::Colour (0xff30343a) : theme::panel); g.fillRect (r);
        g.setColour (theme::line); g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        g.setColour (theme::muted); g.setFont (theme::ui (10.0f, true));
        g.drawText ("INSERTS", 6, insertsTop, getWidth() - 12, 12, juce::Justification::centredLeft);

        // Graduation du fader.
        g.setFont (theme::ui (9.0f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float yy = (float) fader.getY() + fader.getPositionOfValue (db);
            g.setColour (theme::lineBar); g.drawHorizontalLine ((int) yy, (float) scaleArea.getRight() - 3, (float) scaleArea.getRight() + 2);
            g.setColour (db == 0.0f ? theme::text : theme::dim);
            g.drawText (juce::String ((int) db), scaleArea.getX(), (int) yy - 5, scaleArea.getWidth(), 10, juce::Justification::centredRight);
        }

        // Valeurs : volume et crête.
        const double db = volumePlugin ? volumePlugin->getVolumeDb() : 0.0;
        auto v = valueArea.reduced (2, 1);
        theme::bevel (g, v.toFloat(), theme::display, 2.0f);
        g.setFont (theme::mono (10.5f));
        g.setColour (theme::lcd);
        g.drawText (db <= -59.9 ? U ("-∞") : juce::String (db, 1), v.removeFromLeft (v.getWidth() / 2), juce::Justification::centred);
        const float pk = meter.getPeak();
        g.setColour (pk > 0.0f ? theme::rec : theme::muted);
        g.drawText (pk < -90.0f ? U ("-∞") : juce::String (pk, 1), v, juce::Justification::centred);

        // Nom + couleur.
        auto n = nameArea;
        te::Track* named = track != nullptr ? (te::Track*) track : (te::Track*) group;
        const auto stripe = named == nullptr ? theme::accent : (named->getColour().isTransparent() ? theme::panel3 : named->getColour());
        g.setColour (stripe); g.fillRect (n.removeFromBottom (5));
        g.setColour (selected ? juce::Colour (0xff4a5058) : juce::Colour (0xff26292d)); g.fillRect (n);
        g.setColour (theme::text); g.setFont (theme::ui (11.5f, true));
        g.drawFittedText (named == nullptr ? juce::String ("Stereo Out") : named->getName(), n.reduced (3, 0), juce::Justification::centred, 1, 0.7f);
        if (group != nullptr)
        {
            g.setColour (stripe); g.setFont (theme::ui (9.5f, true));
            g.drawText ("GROUPE", n.reduced (3, 0), juce::Justification::topLeft);
        }
        if (track != nullptr)
        {
            g.setColour (theme::dim); g.setFont (theme::ui (9.5f));
            g.drawText (juce::String (ctx.trackNumber (*track)), n.reduced (3, 0), juce::Justification::topLeft);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { if (track) ctx.selectTrack (track); else if (group) ctx.selectFolder (group); }

    te::AudioTrack* track;
    te::FolderTrack* group;
private:
    EditContext& ctx;
    te::VolumeAndPanPlugin* volumePlugin = nullptr;
    juce::Slider fader, pan;
    StereoMeter meter;
    juce::TextButton mute { "M" }, solo { "S" }, arm { U ("●") }, mon { U ("◉") }, addBtn { "+ Insert" };
    juce::OwnedArray<juce::TextButton> slots;
    te::LevelMeasurer* measurer = nullptr;
    te::Plugin::Ptr meterPlugin;
    te::LevelMeasurer::Client client;
    std::unique_ptr<MasterLevelTap> masterTap;
    juce::Rectangle<int> nameArea, valueArea, scaleArea;
    int shownValueKey = -1;
    int insertsTop = 0;
};

MixerView::MixerView (EditContext& c) : ctx (c)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    ctx.addChangeListener (this);
    rebuild();
    startTimerHz (30);
}

MixerView::~MixerView()
{
    ctx.removeChangeListener (this);
    strips.clear();
}

juce::String MixerView::signature() const
{
    juce::String s;
    if (ctx.edit)
        for (auto* t : te::getAudioTracks (*ctx.edit))
        {
            s << t->itemID.toString() << ":";
            for (auto p : t->pluginList.getPlugins()) s << p->itemID.toString() << (p->isEnabled() ? "." : "b");
            s << t->getName() << ";";
        }
    if (ctx.edit)
        for (auto* f : te::getTracksOfType<te::FolderTrack> (*ctx.edit, true))
            if (f->isSubmixFolder()) s << "G" << f->itemID.toString() << f->getName() << ";";
    return s;
}

void MixerView::rebuild()
{
    const auto sig = signature();
    if (sig == lastSig && ! strips.isEmpty()) { for (auto* s : strips) s->repaint(); return; }
    strips.clear();
    if (ctx.edit == nullptr) return;
    for (auto* t : te::getAudioTracks (*ctx.edit))
        content.addAndMakeVisible (strips.add (new Strip (ctx, t)));
    for (auto* f : te::getTracksOfType<te::FolderTrack> (*ctx.edit, true))     // pistes de groupe
        if (f->isSubmixFolder()) content.addAndMakeVisible (strips.add (new Strip (ctx, nullptr, f)));
    content.addAndMakeVisible (strips.add (new Strip (ctx, nullptr)));
    lastSig = sig;
    resized();
}

void MixerView::timerCallback()
{
    if (signature() != lastSig) rebuild();
    for (auto* s : strips) s->sync();
}

void MixerView::resized()
{
    viewport.setBounds (getLocalBounds());
    const int h = juce::jmax (300, viewport.getMaximumVisibleHeight());
    int x = 0;
    for (int i = 0; i < strips.size(); ++i)
    {
        const bool master = i == strips.size() - 1;
        if (master) x += 10;
        strips[i]->setBounds (x, 0, master ? 110 : 96, h);
        x += master ? 110 : 96;
    }
    content.setSize (juce::jmax (x, viewport.getWidth()), h);
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}
