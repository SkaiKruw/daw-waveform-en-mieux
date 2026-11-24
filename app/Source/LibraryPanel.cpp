#include "LibraryPanel.h"
#include "ParamControls.h"
#include "OndeSynth.h"
#include "Chains.h"

namespace
{
    const juce::Colour categoryColours[] = { juce::Colour (0xff3fa4ff), juce::Colour (0xffb98cff), juce::Colour (0xffe5a04e), juce::Colour (0xff2de2c6),
                                             juce::Colour (0xffe8697a), juce::Colour (0xff9be35b), juce::Colour (0xfff2d34a), juce::Colour (0xffe8473f) };
    juce::String describe (const ondefx::SynthPreset& p)
    {
        for (auto& [id, v] : p.values)
            if (juce::String (id) == "engine")
                return juce::String::fromUTF8 (ondefx::SynthPlugin::engineNames[juce::jlimit (0, ondefx::SynthPlugin::numEngines - 1, juce::roundToInt (v))]);
        return "Analogique";
    }
}

LibraryPanel::LibraryPanel (EditContext& c) : ctx (c)
{
    categories = ondefx::SynthPlugin::categories();
    for (int i = 0; i < categories.size(); ++i)
    {
        auto* b = categoryButtons.add (new juce::TextButton (categories[i]));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, categoryColours[i % 8].withAlpha (0.9f));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b->onClick = [this, i] { selectCategory (i); };
        addAndMakeVisible (b);
    }
    list.setModel (this);
    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff15171b));
    addAndMakeVisible (list);
    newTrackButton.setButtonText (U ("Nouvelle piste avec ce son"));
    newTrackButton.onClick = [this] { if (list.getSelectedRow() >= 0) choosePreset (list.getSelectedRow(), true); };
    previewButton.onClick = [this]
    {
        if (auto* t = ctx.getSelectedTrack())
            if (ctx.getInstrument (*t) != nullptr)
                if (auto* p = shown[list.getSelectedRow()]) preview (*t, describe (*p) == "Batterie");
    };
    addAndMakeVisible (newTrackButton);
    addAndMakeVisible (previewButton);
    status.setColour (juce::Label::textColourId, theme::muted);
    status.setFont (theme::ui (12.0f));
    addAndMakeVisible (status);
    selectCategory (0);
    setSize (640, 520);
    startTimer (10);
}

LibraryPanel::~LibraryPanel() { list.setModel (nullptr); }

void LibraryPanel::selectCategory (int i)
{
    category = juce::jlimit (0, categories.size() - 1, i);
    for (int k = 0; k < categoryButtons.size(); ++k) categoryButtons[k]->setToggleState (k == category, juce::dontSendNotification);
    shown.clear();
    for (auto& p : ondefx::SynthPlugin::presets())
        if (juce::String::fromUTF8 (p.category) == categories[category]) shown.add (&p);
    list.updateContent();
    list.deselectAllRows();
    repaint();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    auto* p = shown[row];
    if (p == nullptr) return;
    const auto colour = categoryColours[category % 8];
    if (selected) { g.setColour (colour.withAlpha (0.22f)); g.fillRect (0, 0, w, h); }
    g.setColour (colour); g.fillRoundedRectangle (10.0f, h * 0.5f - 5.0f, 10.0f, 10.0f, 3.0f);
    g.setColour (theme::text); g.setFont (theme::ui (14.0f, selected));
    g.drawText (juce::String::fromUTF8 (p->name), 30, 0, w - 160, h, juce::Justification::centredLeft, true);
    g.setColour (theme::dim); g.setFont (theme::ui (11.5f));
    g.drawText (describe (*p), w - 130, 0, 120, h, juce::Justification::centredRight);
}

te::AudioTrack* LibraryPanel::applyToTrack (EditContext& c, te::AudioTrack* t, const ondefx::SynthPreset& preset)
{
    if (c.edit == nullptr) return nullptr;
    auto& um = c.edit->getUndoManager();
    um.beginNewTransaction (U ("Son « ") + juce::String::fromUTF8 (preset.name) + U (" »"));
    ondefx::SynthPlugin* synth = nullptr;
    if (t != nullptr)
        for (auto* p : t->pluginList) if (auto* s = dynamic_cast<ondefx::SynthPlugin*> (p)) synth = s;
    if (synth == nullptr)
    {
        if (t == nullptr || c.getInstrument (*t) == nullptr)
        {
            // Pas de piste instrument : une nouvelle, avec Onde Synth.
            for (auto& e : c.catalog.getBuiltIns (true))
                if (e.builtInType == ondefx::SynthPlugin::xmlTypeName) { t = c.addInstrumentTrack (&e); break; }
        }
        else
        {
            // L'instrument de la piste est remplacé par Onde Synth (à la même place).
            auto* old = c.getInstrument (*t);
            const int index = t->pluginList.indexOf (old);
            old->deleteFromParent();
            auto p = c.edit->getPluginCache().createNewPlugin (ondefx::SynthPlugin::xmlTypeName, {});
            t->pluginList.insertPlugin (p, juce::jmax (0, index), nullptr);
        }
        if (t != nullptr)
            for (auto* p : t->pluginList) if (auto* s = dynamic_cast<ondefx::SynthPlugin*> (p)) synth = s;
    }
    if (synth == nullptr || t == nullptr) return t;
    const auto oldPreset = synth->getPresetName();
    synth->applyPreset (preset);
    chains::rebuildMacros (*t);                    // commandes rapides de l'instrument (selon son moteur)
    // Une piste qui portait le nom de l'ancien son (ou « Onde Synth ») prend le nom du nouveau.
    if (t->getName() == oldPreset || t->getName() == "Onde Synth" || t->getName().startsWith ("Instrument"))
        t->setName (juce::String::fromUTF8 (preset.name));
    c.selectTrack (t);
    c.routeInputsTo (t);
    c.changed();
    return t;
}

void LibraryPanel::choosePreset (int row, bool newTrack)
{
    auto* p = shown[row];
    if (p == nullptr || ctx.edit == nullptr) return;
    list.selectRow (row, true, true);
    auto* t = newTrack ? nullptr : ctx.getSelectedTrack();
    t = applyToTrack (ctx, t, *p);
    if (t == nullptr) { status.setText (U ("Impossible de poser ce son"), juce::dontSendNotification); return; }
    status.setText (U ("« ") + juce::String::fromUTF8 (p->name) + U (" » sur la piste « ") + t->getName()
                    + U (" » · jouez-le avec le clavier (Ctrl+Maj+K) ou un clavier MIDI"), juce::dontSendNotification);
    preview (*t, describe (*p) == "Batterie");
}

void LibraryPanel::preview (te::AudioTrack& t, bool drums)
{
    // Courte phrase jouée en direct sur la piste (rien n'est enregistré) : arpège et accord, ou un rythme.
    if (ctx.edit == nullptr || ctx.edit->getTransport().isPlaying()) return;
    for (auto& [at, m] : pending) if (m.isNoteOff()) t.injectLiveMidiMessage (m, sourceID);
    pending.clear();
    ctx.edit->getTransport().ensureContextAllocated();
    auto note = [this] (double at, double len, int pitch, int vel)
    {
        pending.add ({ at, juce::MidiMessage::noteOn (1, pitch, (juce::uint8) vel) });
        pending.add ({ at + len, juce::MidiMessage::noteOff (1, pitch) });
    };
    if (drums)
    {
        const double step = 0.125;
        for (int i = 0; i < 16; ++i)
        {
            note (i * step, 0.05, 42, i % 2 == 0 ? 90 : 60);
            if (i % 8 == 0 || i == 10) note (i * step, 0.05, 36, 115);
            if (i % 8 == 4) note (i * step, 0.05, 38, 105);
        }
        note (2.0, 0.05, 36, 115); note (2.0, 0.05, 49, 100);
    }
    else
    {
        int k = 0;
        for (int pitch : { 60, 64, 67, 72 }) note (0.18 * k++, 0.16, pitch, 96);
        for (int pitch : { 48, 60, 64, 67 }) note (0.8, 1.1, pitch, 90);
    }
    std::sort (pending.begin(), pending.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    previewStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
    previewTrack = t.itemID;
}

void LibraryPanel::timerCallback()
{
    if (pending.isEmpty() || ctx.edit == nullptr) return;
    auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*ctx.edit, previewTrack));
    if (t == nullptr) { pending.clear(); return; }
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001 - previewStart;
    while (! pending.isEmpty() && pending.getFirst().first <= now)
    {
        t->injectLiveMidiMessage (pending.getFirst().second, sourceID);
        pending.remove (0);
    }
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    g.setColour (theme::text); g.setFont (theme::ui (15.0f, true));
    g.drawText (U ("Bibliothèque de sons"), 14, 8, 300, 24, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
    g.drawText (U ("Clic : poser le son sur la piste sélectionnée et l'écouter · double-clic : nouvelle piste"),
                14, 30, getWidth() - 28, 18, juce::Justification::centredLeft, true);
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (12).withTrimmedTop (44);
    auto bottom = r.removeFromBottom (58);
    auto left = r.removeFromLeft (190);
    for (auto* b : categoryButtons) { b->setBounds (left.removeFromTop (34)); left.removeFromTop (4); }
    r.removeFromLeft (10);
    list.setBounds (r);
    auto row = bottom.removeFromTop (30).withTrimmedTop (4);
    newTrackButton.setBounds (row.removeFromRight (210));
    row.removeFromRight (8);
    previewButton.setBounds (row.removeFromRight (90));
    status.setBounds (bottom);
}

juce::DialogWindow* LibraryPanel::show (EditContext& c)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new LibraryPanel (c));
    o.dialogTitle = U ("Bibliothèque de sons");
    o.dialogBackgroundColour = juce::Colour (0xff1a1d22);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    // Fenêtre flottante : la fenêtre principale reste utilisable.
    return ondeui::showToolWindow (o.dialogTitle, o.content.release(), o.resizable, o.dialogBackgroundColour);
}
