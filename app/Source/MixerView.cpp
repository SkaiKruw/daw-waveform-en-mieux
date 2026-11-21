#include "MixerView.h"
#include "Panels.h"
#include "OndeFx2.h"

// Tranche de console : inserts, panoramique, état, fader + vumètre, nom.
class MixerView::Strip  : public juce::Component
{
public:
    Strip (EditContext& c, te::AudioTrack* t, te::FolderTrack* g = nullptr) : track (t), group (g), ctx (c)
    {
        volumePlugin = track ? track->getVolumePlugin() : group ? group->getVolumePlugin() : ctx.edit->getMasterVolumePlugin().get();
        // Dossier simple : fader VCA (pilote le volume des pistes du dossier sans additionner leur son).
        if (group != nullptr && ! group->isSubmixFolder()) vca = group->getVCAPlugin();

        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setWantsKeyboardFocus (false);
        fader.setTooltip (U ("Volume · double-clic : 0 dB"));
        fader.onDragStart = [this] { ctx.edit->getUndoManager().beginNewTransaction (U ("Volume")); };
        fader.onValueChange = [this] { if (vca) vca->setVolumeDb ((float) fader.getValue()); else if (volumePlugin) volumePlugin->setVolumeDb ((float) fader.getValue()); };

        addAndMakeVisible (pan);
        pan.setSliderStyle (juce::Slider::LinearBar);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setWantsKeyboardFocus (false);
        pan.setTooltip (U ("Panoramique · double-clic : centre"));
        pan.onDragStart = [this] { ctx.edit->getUndoManager().beginNewTransaction (U ("Panoramique")); };
        pan.onValueChange = [this] { if (volumePlugin) volumePlugin->setPan ((float) pan.getValue()); };

        if (volumePlugin != nullptr && (track != nullptr || (group != nullptr && vca == nullptr)))
        {
            styleStateButton (phase, theme::accent);
            phase.setTooltip (U ("Inverser la polarité (phase) de cette voie"));
            phase.onClick = [this] { ctx.edit->getUndoManager().beginNewTransaction (U ("Polarité")); volumePlugin->polarity = phase.getToggleState(); };
            addAndMakeVisible (phase);
        }
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
            if (vca == nullptr)
                if (auto* lm = EditContext::groupMeter (*group, false)) { meterPlugin = lm; measurer = &lm->measurer; measurer->addClient (client); }
            if (vca != nullptr) { pan.setVisible (false); meter.setVisible (false); fader.setTooltip (U ("VCA : monte ou baisse ensemble toutes les pistes du dossier (leurs réglages restent) · double-clic : 0 dB")); }
        }
        else
        {
            masterTap = std::make_unique<MasterLevelTap> (ctx);
            for (auto& p : ctx.edit->getMasterPluginList().getPlugins())
                if (auto* m = dynamic_cast<ondefx::MasterMeterPlugin*> (p)) analysis = m;
            styleStateButton (monoBtn, theme::accent); styleStateButton (dimBtn, theme::mute);
            styleButton (resetBtn);
            monoBtn.setTooltip (U ("Écouter le mix en mono (vérifie la compatibilité) · n'agit pas sur l'export"));
            dimBtn.setTooltip (U ("Baisser l'écoute de 20 dB · n'agit pas sur l'export"));
            resetBtn.setTooltip (U ("Remettre à zéro la sonie intégrée et la crête maximale"));
            monoBtn.onClick = [this] { if (analysis) analysis->monoListen = monoBtn.getToggleState(); };
            dimBtn.onClick = [this] { if (analysis) analysis->dimListen = dimBtn.getToggleState(); };
            resetBtn.onClick = [this] { if (analysis) analysis->resetMeasurements(); };
            for (auto* b : { &monoBtn, &dimBtn, &resetBtn }) addAndMakeVisible (*b);
        }
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
        if (vca != nullptr) { if (! fader.isMouseButtonDown()) fader.setValue (vca->getVolumeDb(), juce::dontSendNotification); }
        else if (volumePlugin)
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
        if (volumePlugin != nullptr && phase.isVisible()) phase.setToggleState (volumePlugin->polarity.get(), juce::dontSendNotification);
        if (analysis != nullptr)
        {
            monoBtn.setToggleState (analysis->monoListen.load(), juce::dontSendNotification);
            dimBtn.setToggleState (analysis->dimListen.load(), juce::dontSendNotification);
            const int key = juce::roundToInt (analysis->momentary.load() * 10) ^ (juce::roundToInt (analysis->shortTerm.load() * 10) << 10)
                          ^ (juce::roundToInt (analysis->integrated.load() * 10) << 20) ^ (juce::roundToInt (analysis->truePeakMax.load() * 10) << 5)
                          ^ (juce::roundToInt (analysis->correlation.load() * 50) << 15);
            if (key != shownAnalysisKey) { shownAnalysisKey = key; repaint (analysisArea); }
        }
        if (masterTap) { const auto lv = masterTap->read(); meter.setLevels (lv.first, lv.second); }
        else meter.setLevels (client.getAndClearAudioLevel (0).dB, client.getAndClearAudioLevel (1).dB);
        // Valeurs (volume, crête) : redessinées seulement si le texte affiché change.
        const double db = vca ? vca->getVolumeDb() : volumePlugin ? volumePlugin->getVolumeDb() : 0.0;
        const int key = juce::roundToInt (db * 10.0) * 100000 + juce::roundToInt (juce::jmax (-100.0f, meter.getPeak()) * 10.0f);
        if (key != shownValueKey) { shownValueKey = key; repaint (valueArea); }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (4);
        insertsTop = r.getY();
        r.removeFromTop (14);
        if (track == nullptr && group == nullptr)
        {
            analysisArea = r.removeFromTop (150);
            r.removeFromTop (4);
            auto row = r.removeFromTop (20);
            const int w = (row.getWidth() - 4) / 3;
            monoBtn.setBounds (row.removeFromLeft (w)); row.removeFromLeft (2);
            dimBtn.setBounds (row.removeFromLeft (w)); row.removeFromLeft (2);
            resetBtn.setBounds (row);
            r.removeFromTop (8);
        }
        else
        {
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
        }
        r.removeFromTop (8);
        {
            auto pr = r.removeFromTop (14);
            if (phase.isVisible()) { phase.setBounds (pr.removeFromRight (18).withSizeKeepingCentre (18, 16)); pr.removeFromRight (3); }
            pan.setBounds (pr);
        }
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
        if (track == nullptr && group == nullptr) f = f.withSizeKeepingCentre (juce::jmin (f.getWidth(), 92), f.getHeight());
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
        g.drawText (master ? "ANALYSE DU MIX" : "INSERTS", 6, insertsTop, getWidth() - 12, 12, juce::Justification::centredLeft);
        if (master) paintAnalysis (g);

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
        const double db = vca ? vca->getVolumeDb() : volumePlugin ? volumePlugin->getVolumeDb() : 0.0;
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
        g.drawFittedText (named == nullptr ? U ("Sortie stéréo") : named->getName(), n.reduced (3, 0), juce::Justification::centred, 1, 0.7f);
        if (group != nullptr)
        {
            g.setColour (stripe); g.setFont (theme::ui (9.5f, true));
            g.drawText (vca != nullptr ? "VCA" : "GROUPE", n.reduced (3, 0), juce::Justification::topLeft);
        }
        if (track != nullptr)
        {
            g.setColour (theme::dim); g.setFont (theme::ui (9.5f));
            g.drawText (juce::String (ctx.trackNumber (*track)), n.reduced (3, 0), juce::Justification::topLeft);
        }
    }

    // Analyse du mix : sonie EBU R128 (intégrée en grand), crête vraie maximale, corrélation de phase.
    void paintAnalysis (juce::Graphics& g)
    {
        auto a = analysisArea.reduced (2, 0);
        theme::bevel (g, a.toFloat(), theme::display, 3.0f);
        a.reduce (8, 6);
        if (analysis == nullptr) { g.setColour (theme::dim); g.setFont (theme::ui (10.5f)); g.drawFittedText (U ("Analyse indisponible"), a, juce::Justification::centred, 2); return; }
        auto lu = [] (float v) { return v <= -69.9f ? U ("—") : juce::String (v, 1); };
        const float I = analysis->integrated.load(), M = analysis->momentary.load(), S = analysis->shortTerm.load();
        const float tp = analysis->truePeakMax.load(), corr = analysis->correlation.load();
        g.setColour (theme::muted); g.setFont (theme::ui (9.5f, true));
        g.drawText (U ("SONIE INTÉGRÉE"), a.removeFromTop (12), juce::Justification::centredLeft);
        auto big = a.removeFromTop (30);
        g.setColour (theme::lcd); g.setFont (theme::mono (24.0f));
        g.drawText (lu (I), big, juce::Justification::centredLeft);
        g.setColour (theme::dim); g.setFont (theme::ui (10.0f));
        g.drawText ("LUFS", big, juce::Justification::bottomRight);
        a.removeFromTop (4);
        auto row = [&] (const juce::String& name, const juce::String& val, juce::Colour c)
        {
            auto rr = a.removeFromTop (15);
            g.setColour (theme::dim); g.setFont (theme::ui (10.0f)); g.drawText (name, rr, juce::Justification::centredLeft);
            g.setColour (c); g.setFont (theme::mono (10.5f)); g.drawText (val, rr, juce::Justification::centredRight);
        };
        row (U ("Momentanée"), lu (M) + " LU", theme::text);
        row (U ("Court terme"), lu (S) + " LU", theme::text);
        row (U ("Crête vraie max."), tp < -90.0f ? U ("—") : (tp > 0 ? "+" : "") + juce::String (tp, 1) + " dBTP", tp > -1.0f ? theme::rec : theme::text);
        a.removeFromTop (5);
        g.setColour (theme::dim); g.setFont (theme::ui (10.0f));
        auto cr = a.removeFromTop (13);
        g.drawText (U ("Corrélation"), cr, juce::Justification::centredLeft);
        g.setColour (corr < 0.0f ? theme::rec : corr < 0.3f ? theme::warn : theme::text); g.setFont (theme::mono (10.5f));
        g.drawText ((corr >= 0 ? "+" : "") + juce::String (corr, 2), cr, juce::Justification::centredRight);
        auto bar = a.removeFromTop (12).reduced (0, 3).toFloat();
        g.setColour (theme::bg); g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (theme::lineBar); g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() - 2, bar.getBottom() + 2);
        const float x = bar.getX() + (juce::jlimit (-1.0f, 1.0f, corr) + 1.0f) * 0.5f * bar.getWidth();
        const auto left = std::min (x, bar.getCentreX()), right = std::max (x, bar.getCentreX());
        g.setColour (corr < 0.0f ? theme::rec : theme::accent); g.fillRect (juce::Rectangle<float> (left, bar.getY(), juce::jmax (2.0f, right - left), bar.getHeight()));
        g.setColour (theme::muted); g.setFont (theme::ui (8.5f));
        auto lbl = a.removeFromTop (11);
        g.drawText ("-1", lbl, juce::Justification::centredLeft); g.drawText ("0", lbl, juce::Justification::centred); g.drawText ("+1", lbl, juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent&) override { if (track) ctx.selectTrack (track); else if (group) ctx.selectFolder (group); }

    te::AudioTrack* track;
    te::FolderTrack* group;
private:
    EditContext& ctx;
    te::VolumeAndPanPlugin* volumePlugin = nullptr;
    te::VCAPlugin* vca = nullptr;
    juce::Slider fader, pan;
    StereoMeter meter;
    juce::TextButton phase { U ("Ø") }, monoBtn { "Mono" }, dimBtn { "-20 dB" }, resetBtn { U ("Réinit.") };
    ondefx::MasterMeterPlugin* analysis = nullptr;
    juce::Rectangle<int> analysisArea;
    int shownAnalysisKey = 0;
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
            if (f->isSubmixFolder() || f->getVCAPlugin() != nullptr) s << "G" << f->itemID.toString() << f->getName() << ";";
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
        if (f->isSubmixFolder() || f->getVCAPlugin() != nullptr) content.addAndMakeVisible (strips.add (new Strip (ctx, nullptr, f)));    // groupes et VCA
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
        strips[i]->setBounds (x, 0, master ? 200 : 96, h);
        x += master ? 200 : 96;
    }
    content.setSize (juce::jmax (x, viewport.getWidth()), h);
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}
