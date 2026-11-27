#include "LibraryPanel.h"
#include "ParamControls.h"
#include "OndeSynth.h"
#include "Chains.h"
#include "OndeSampler.h"
#include "OndeFx2.h"

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


//==============================================================================
// Instruments réels : sons enregistrés des bibliothèques libres (Onde Échantillonneur), nommés comme un musicien
// les cherche, avec la salle qui va bien (Onde Espace) déjà posée.
namespace realinstruments
{
    const std::vector<Entry>& catalog()
    {
        static const std::vector<Entry> list = {
            { "Piano à queue de concert", "Pianos", "salamander", "Salamander Grand Piano V3.sfz", 0.45f, 14.0f, "Grand piano de concert, 16 couches de vélocité" },
            { "Piano droit", "Pianos", "vsco2", "Instruments Onde/Keys - Upright Nr1 UR1.sfz", 0.35f, 12.0f, "Piano droit intime, son boisé" },
            { "Piano droit sombre", "Pianos", "scarypiano", "Programs/Scarypiano.sfz", 0.35f, 12.0f, "Vieux piano au caractère marqué" },
            { "Violons (ensemble)", "Cordes", "vsco2", "Instruments Onde/Violin Section - susVib VlnEns susVib.sfz", 0.7f, 22.0f, "Section de violons, notes tenues avec vibrato" },
            { "Violons spiccato", "Cordes", "vsco2", "Instruments Onde/Violin Section - Spic VlnEns Spic.sfz", 0.6f, 18.0f, "Coups d'archet courts et rebondis" },
            { "Violons pizzicato", "Cordes", "vsco2", "Instruments Onde/Violin Section - Pizz VlnEns Pizz.sfz", 0.6f, 18.0f, "Cordes pincées" },
            { "Violons trémolo", "Cordes", "vsco2", "Instruments Onde/Violin Section - Trem VlnEns Trem.sfz", 0.7f, 20.0f, "Archet rapide, tension" },
            { "Altos (ensemble)", "Cordes", "vsco2", "Instruments Onde/Viola Section - susvib ViolaEns susvib.sfz", 0.7f, 22.0f, "Section d'altos, tenu" },
            { "Violoncelles (ensemble)", "Cordes", "vsco2", "Instruments Onde/Cello Section - susvib.sfz", 0.7f, 22.0f, "Section de violoncelles, tenu" },
            { "Cordes graves serrées", "Cordes", "vsco2", "Instruments Onde/Cello Section - spic.sfz", 0.55f, 16.0f, "Violoncelles en spiccato : ostinatos rythmés, musique de film" },
            { "Violoncelle solo", "Cordes", "cello", "Programs/01- Bowed (velocity layer).sfz", 0.55f, 18.0f, "Violoncelle à l'archet, expressif" },
            { "Violon solo", "Cordes", "vsco2", "Instruments Onde/Solo Violin - Arco Vib LLVln ArcoVib.sfz", 0.55f, 18.0f, "Violon solo avec vibrato" },
            { "Contrebasse à l'archet", "Cordes", "doublebass", "d_smolken_rubner_bass_arco.sfz", 0.5f, 18.0f, "Contrebasse classique, archet" },
            { "Harpe", "Cordes", "vsco2", "Instruments Onde/Strings - Harp KSHarp.sfz", 0.6f, 20.0f, "Harpe de concert" },
            { "Trompette", "Cuivres", "vsco2", "Instruments Onde/Trumpet - sus Sum SHTrumpet sus.sfz", 0.6f, 18.0f, "Trompette, notes tenues" },
            { "Trompette sourdine", "Cuivres", "vsco2", "Instruments Onde/Trumpet - straightM-sus Sum SHTrumpet straightM sus.sfz", 0.5f, 16.0f, "Sourdine sèche, jazz" },
            { "Cor", "Cuivres", "vsco2", "Instruments Onde/F Horn - sus MOHorn sus.sfz", 0.7f, 22.0f, "Cor d'harmonie, ample" },
            { "Trombone", "Cuivres", "vsco2", "Instruments Onde/Tenor Trombone - sus tenortbn sus.sfz", 0.6f, 18.0f, "Trombone ténor" },
            { "Tuba", "Cuivres", "vsco2", "Instruments Onde/Tuba - sus Tuba3 sus.sfz", 0.55f, 18.0f, "Tuba, grave des cuivres" },
            { "Tuba de guerre", "Cuivres", "wartuba", "Programs/2-solo-poly.sfz", 0.5f, 16.0f, "Tuba puissant, dynamique, très expressif" },
            { "Flûte", "Bois", "vsco2", "Instruments Onde/Flute - susvib LDFlute susvib.sfz", 0.6f, 18.0f, "Flûte traversière avec vibrato" },
            { "Hautbois", "Bois", "vsco2", "Instruments Onde/Oboe - Vib Oboe Vib.sfz", 0.6f, 18.0f, "Hautbois, chantant" },
            { "Clarinette", "Bois", "vsco2", "Instruments Onde/Clarinet - susLong DCClar susLong.sfz", 0.6f, 18.0f, "Clarinette, son rond" },
            { "Basson", "Bois", "vsco2", "Instruments Onde/Bassoon - sus PSBassoon.sfz", 0.6f, 18.0f, "Basson, grave des bois" },
            { "Piccolo", "Bois", "vsco2", "Instruments Onde/Piccolo - Sus.sfz", 0.6f, 18.0f, "Petite flûte, brillante" },
            { "Saxophone alto", "Bois", "weresax", "Programs/Sax.sfz", 0.4f, 14.0f, "Saxophone alto, souffle et vibrato réglables" },
            { "Basse électrique", "Basses", "growlybass", "growlybass_clean.sfz", 0.15f, 8.0f, "Basse électrique jouée aux doigts" },
            { "Basse électrique saturée", "Basses", "growlybass", "growlybass_dirty.sfz", 0.15f, 8.0f, "La même, branchée dans un ampli poussé" },
            { "Contrebasse jazz", "Basses", "doublebass", "d_smolken_rubner_bass_pizz.sfz", 0.35f, 14.0f, "Contrebasse pincée, walking bass" },
            { "Basse électrique Meatbass", "Basses", "meatbass", "Programs/04_pizz.sfz", 0.15f, 8.0f, "Basse électrique pincée, grave rond et épais" },
            { "Guitare Shiny (acoustique et électrique)", "Guitares", "shinyguitar", "Programs/main.sfz", 0.3f, 12.0f, "Guitare enregistrée en acoustique et en électrique, mélange réglable (CC 100)" },
            { "Guitare électrique (Emily)", "Guitares", "emilyguitar", "emily_clean.sfz", 0.3f, 12.0f, "Guitare électrique, notes et accords" },
            { "Batterie acoustique", "Batteries et percussions", "virtuosity", "Programs/02-full-kit.sfz", 0.2f, 10.0f, "Batterie complète enregistrée, plusieurs micros" },
            { "Glockenspiel", "Batteries et percussions", "vsco2", "Instruments Onde/Percussion - Glock glock medium.sfz", 0.6f, 20.0f, "Lames métalliques, scintillant" },
            { "Marimba", "Batteries et percussions", "vsco2", "Instruments Onde/Percussion - Marimba Marimba hit Outrigger.sfz", 0.5f, 18.0f, "Lames de bois, chaleureux" },
            { "Xylophone", "Batteries et percussions", "vsco2", "Instruments Onde/Percussion - Xylo Xylo Medium.sfz", 0.5f, 16.0f, "Lames de bois, sec et brillant" },
        };
        return list;
    }

    juce::File fileFor (const Entry& e)
    {
        for (auto& root : ondefx::libraryRoots())
        {
            const auto f = root.getChildFile (e.library).getChildFile (juce::String::fromUTF8 (e.file));
            if (f.existsAsFile()) return f;
        }
        return ondefx::librariesFolder().getChildFile (e.library).getChildFile (juce::String::fromUTF8 (e.file));
    }

    bool available (const Entry& e) { return fileFor (e).existsAsFile(); }

    const Entry* find (const juce::String& name)
    {
        for (auto& e : catalog()) if (juce::String::fromUTF8 (e.name) == name) return &e;
        return nullptr;
    }

    te::AudioTrack* applyToTrack (EditContext& c, te::AudioTrack* t, const Entry& e)
    {
        if (c.edit == nullptr || ! available (e)) return nullptr;
        auto& um = c.edit->getUndoManager();
        const auto name = juce::String::fromUTF8 (e.name);
        um.beginNewTransaction (U ("Instrument « ") + name + U (" »"));
        ondefx::SamplerPlugin* sampler = nullptr;
        if (t != nullptr) for (auto* p : t->pluginList) if (auto* s = dynamic_cast<ondefx::SamplerPlugin*> (p)) sampler = s;
        const bool freshTrack = t == nullptr || (sampler == nullptr && c.getInstrument (*t) == nullptr);
        if (freshTrack)
        {
            for (auto& entry : c.catalog.getBuiltIns (true))
                if (entry.builtInType == ondefx::SamplerPlugin::xmlTypeName) { t = c.addInstrumentTrack (&entry); break; }
        }
        else if (sampler == nullptr)
        {
            auto* old = c.getInstrument (*t);
            const int index = t->pluginList.indexOf (old);
            old->deleteFromParent();
            auto p = c.edit->getPluginCache().createNewPlugin (ondefx::SamplerPlugin::xmlTypeName, {});
            t->pluginList.insertPlugin (p, juce::jmax (0, index), nullptr);
        }
        if (t == nullptr) return nullptr;
        for (auto* p : t->pluginList) if (auto* s = dynamic_cast<ondefx::SamplerPlugin*> (p)) sampler = s;
        if (sampler == nullptr) return t;
        sampler->loadInstrument (fileFor (e));
        // La salle : une réverbération Onde Espace après l'instrument (réglée pour cet instrument, ou mise à jour).
        ondefx::OndeEffect* space = nullptr;
        for (auto* p : t->pluginList) if (p->getPluginType() == ondefx::SpacePlugin::xmlTypeName) space = dynamic_cast<ondefx::OndeEffect*> (p);
        if (space == nullptr)
        {
            auto p = c.edit->getPluginCache().createNewPlugin (ondefx::SpacePlugin::xmlTypeName, {});
            t->pluginList.insertPlugin (p, t->pluginList.indexOf (sampler) + 1, nullptr);
            space = dynamic_cast<ondefx::OndeEffect*> (p.get());
        }
        if (space != nullptr)
        {
            space->setStored ("size", e.roomSize);
            space->setStored ("decay", 0.8f + e.roomSize * 2.4f);
            space->setStored ("mix", e.roomMix);
        }
        const auto oldName = t->getName();
        if (freshTrack || oldName.startsWith ("Instrument") || oldName == "Onde Synth" || oldName.containsIgnoreCase (U ("Échantillonneur")) || find (oldName) != nullptr
            || ondefx::SynthPlugin::findPreset (oldName) != nullptr)
            t->setName (name);
        c.selectTrack (t);
        c.routeInputsTo (t);
        c.changed();
        return t;
    }
}

LibraryPanel::LibraryPanel (EditContext& c) : ctx (c)
{
    categories = ondefx::SynthPlugin::categories();
    categories.insert (0, U ("Instruments réels"));
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
            {
                if (isReal()) { if (auto* e = shownReal[list.getSelectedRow()]) preview (*t, juce::String::fromUTF8 (e->family).startsWith ("Batteries")); }
                else if (auto* p = shown[list.getSelectedRow()]) preview (*t, describe (*p) == "Batterie");
            }
    };
    addAndMakeVisible (newTrackButton);
    addAndMakeVisible (previewButton);
    status.setColour (juce::Label::textColourId, theme::muted);
    status.setFont (theme::ui (12.0f));
    addAndMakeVisible (status);
    selectCategory (0);
    setSize (720, 600);
    startTimer (10);
}

LibraryPanel::~LibraryPanel() { list.setModel (nullptr); }

void LibraryPanel::selectCategory (int i)
{
    category = juce::jlimit (0, categories.size() - 1, i);
    for (int k = 0; k < categoryButtons.size(); ++k) categoryButtons[k]->setToggleState (k == category, juce::dontSendNotification);
    shown.clear();
    shownReal.clear();
    if (isReal()) for (auto& e : realinstruments::catalog()) shownReal.add (&e);
    else for (auto& p : ondefx::SynthPlugin::presets())
        if (juce::String::fromUTF8 (p.category) == categories[category]) shown.add (&p);
    list.updateContent();
    list.deselectAllRows();
    repaint();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (isReal())
    {
        auto* e = shownReal[row];
        if (e == nullptr) return;
        const bool ok = realinstruments::available (*e);
        const auto colour = juce::Colour (0xffe5a04e);
        if (selected) { g.setColour (colour.withAlpha (0.22f)); g.fillRect (0, 0, w, h); }
        // Nouvelle famille : petit titre au-dessus de la ligne.
        g.setColour (ok ? colour : theme::dim); g.fillEllipse (10.0f, h * 0.5f - 5.0f, 10.0f, 10.0f);
        g.setColour (ok ? theme::text : theme::muted); g.setFont (theme::ui (14.0f, selected));
        g.drawText (juce::String::fromUTF8 (e->name), 30, 2, w - 200, h / 2 + 2, juce::Justification::bottomLeft, true);
        g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
        g.drawText (juce::String::fromUTF8 (e->description), 30, h / 2 + 2, w - 200, h / 2 - 4, juce::Justification::topLeft, true);
        g.setFont (theme::ui (11.5f));
        g.setColour (ok ? theme::dim : theme::warn);
        g.drawText (ok ? juce::String::fromUTF8 (e->family) : U ("à télécharger"), w - 165, 0, 155, h, juce::Justification::centredRight);
        return;
    }
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
    if (isReal())
    {
        auto* e = shownReal[row];
        if (e == nullptr || ctx.edit == nullptr) return;
        list.selectRow (row, true, true);
        if (! realinstruments::available (*e))
        {
            status.setText (U ("« ") + juce::String::fromUTF8 (e->name) + U (" » fait partie d'une bibliothèque à télécharger (gratuite) : ")
                            + U ("la fenêtre des bibliothèques s'ouvre, un clic suffit."), juce::dontSendNotification);
            ctx.perform ("midi.libraries");
            return;
        }
        auto* t = realinstruments::applyToTrack (ctx, newTrack ? nullptr : ctx.getSelectedTrack(), *e);
        if (t == nullptr) { status.setText (U ("Impossible de poser cet instrument"), juce::dontSendNotification); return; }
        status.setText (U ("« ") + juce::String::fromUTF8 (e->name) + U (" » sur la piste « ") + t->getName()
                        + U (" » · chargement des sons enregistrés…"), juce::dontSendNotification);
        for (auto* p : t->pluginList) if (dynamic_cast<ondefx::SamplerPlugin*> (p) != nullptr) waitingSampler = p;
        previewTrack = t->itemID;
        return;
    }
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
    if (waitingSampler != nullptr)
    {
        auto* s = dynamic_cast<ondefx::SamplerPlugin*> (waitingSampler.get());
        if (s == nullptr || ctx.edit == nullptr) waitingSampler = nullptr;
        else if (! s->isLoading())
        {
            waitingSampler = nullptr;
            if (auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*ctx.edit, previewTrack)))
            {
                status.setText (U ("« ") + t->getName() + U (" » prêt · jouez-le avec le clavier (Ctrl+Maj+K) ou un clavier MIDI"), juce::dontSendNotification);
                auto* e = realinstruments::find (t->getName());
                preview (*t, e != nullptr && juce::String::fromUTF8 (e->family).startsWith ("Batteries"));
            }
        }
    }
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
    g.drawText (isReal() ? U ("Instruments enregistrés (vrais musiciens) · clic : poser sur la piste et écouter · double-clic : nouvelle piste")
                         : U ("Sons de synthèse d'Onde · clic : poser le son sur la piste sélectionnée et l'écouter · double-clic : nouvelle piste"),
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
