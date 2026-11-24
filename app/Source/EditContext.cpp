#include "EditContext.h"
#include "OndeSynth.h"
#include "OndeFx2.h"

juce::String Action::keyText() const
{
    if (! key.isValid()) return {};
    auto t = key.getTextDescriptionWithIcons();
    return t.replace ("ctrl", "Ctrl").replace ("shift", "Maj").replace ("alt", "Alt").replace ("spacebar", "Espace")
            .replace ("delete", "Suppr").replace ("home", U ("Début")).replace ("end", "Fin").replace ("escape", U ("Échap"))
            .replace ("backspace", U ("Retour arrière")).replace (" + ", "+");
}

te::AudioTrack* EditContext::getSelectedTrack() const
{
    if (edit == nullptr) return nullptr;
    if (auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, selectedTrackID))) return t;
    auto tracks = te::getAudioTracks (*edit);
    return tracks.isEmpty() ? nullptr : tracks.getFirst();
}

te::Clip* EditContext::getSelectedClip() const
{
    if (edit == nullptr || ! selectedClipID.isValid()) return nullptr;
    return te::findClipForID (*edit, selectedClipID);
}

void EditContext::selectTrack (te::AudioTrack* t)
{
    const auto id = t != nullptr ? t->itemID : te::EditItemID();
    if (id == selectedTrackID && ! selectedFolderID.isValid()) return;
    selectedTrackID = id;
    selectedFolderID = {};
    routeInputsTo (t);
    changed();
}

void EditContext::toggleClipSelection (te::Clip* c)
{
    if (c == nullptr) return;
    if (selectedClips.contains (c->itemID))
    {
        selectedClips.removeFirstMatchingValue (c->itemID);
        if (selectedClipID == c->itemID) selectedClipID = selectedClips.isEmpty() ? te::EditItemID() : selectedClips.getLast();
    }
    else
    {
        selectedClips.add (c->itemID);
        selectedClipID = c->itemID;
    }
    changed();
}

void EditContext::setClipSelection (const juce::Array<te::Clip*>& clips)
{
    selectedClips.clearQuick();
    for (auto* c : clips) if (c != nullptr) selectedClips.addIfNotAlreadyThere (c->itemID);
    selectedClipID = selectedClips.isEmpty() ? te::EditItemID() : selectedClips.getFirst();
    changed();
}

juce::Array<te::Clip*> EditContext::getSelectedClips() const
{
    juce::Array<te::Clip*> r;
    if (edit == nullptr) return r;
    for (auto& id : selectedClips)
        if (auto* c = te::findClipForID (*edit, id)) r.add (c);
    if (r.isEmpty()) if (auto* c = getSelectedClip()) r.add (c);
    return r;
}

void EditContext::selectClip (te::Clip* c)
{
    selectedClips.clearQuick();
    if (c != nullptr) selectedClips.add (c->itemID);
    selectedClipID = c != nullptr ? c->itemID : te::EditItemID();
    if (c != nullptr)
        if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
        {
            selectedTrackID = t->itemID;
            routeInputsTo (t);
        }
    changed();
}

int EditContext::trackNumber (const te::Track& t) const
{
    if (edit == nullptr) return 0;
    return te::getAudioTracks (*edit).indexOf (dynamic_cast<te::AudioTrack*> (const_cast<te::Track*> (&t))) + 1;
}

juce::String EditContext::toolName (Tool t)
{
    switch (t)
    {
        case Tool::select: return U ("Sélection d'objets (1)");
        case Tool::range:  return U ("Sélection de plage (2)");
        case Tool::split:  return U ("Ciseaux (3)");
        case Tool::glue:   return U ("Colle (4)");
        case Tool::erase:  return U ("Gomme (5)");
        case Tool::zoom:   return U ("Zoom (6)");
        case Tool::mute:   return U ("Rendre muet (7)");
        case Tool::draw:   return U ("Crayon (8)");
    }
    return {};
}

double EditContext::bpm() const
{
    if (edit == nullptr) return 120.0;
    return edit->tempoSequence.getTempos()[0]->getBpm();
}

double EditContext::beatSeconds() const { return 60.0 / bpm(); }

int EditContext::beatsPerBar() const
{
    if (edit == nullptr) return 4;
    if (auto* ts = edit->tempoSequence.getTimeSig (0)) return juce::jlimit (1, 32, ts->numerator.get());
    return 4;
}

double EditContext::toBeats (double seconds) const
{
    if (edit == nullptr) return seconds * 2.0;
    return edit->tempoSequence.toBeats (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds))).inBeats();
}

double EditContext::toSeconds (double beats) const
{
    if (edit == nullptr) return beats * 0.5;
    return edit->tempoSequence.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, beats))).inSeconds();
}

double EditContext::bpmAt (double seconds) const
{
    if (edit == nullptr) return 120.0;
    return edit->tempoSequence.getBpmAt (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds)));
}

bool EditContext::hasTempoChanges() const
{
    return edit != nullptr && edit->tempoSequence.getNumTempos() > 1;
}

double EditContext::barStart (double seconds) const
{
    const int bpb = beatsPerBar();
    return toSeconds (std::floor (toBeats (seconds) / bpb + 1.0e-7) * bpb);
}

double EditContext::addBars (double seconds, double bars) const
{
    return toSeconds (toBeats (seconds) + bars * beatsPerBar());
}

double EditContext::snap (double seconds, double beatsPerStep) const
{
    const double b = toBeats (seconds);
    return juce::jmax (0.0, toSeconds (std::round (b / beatsPerStep) * beatsPerStep));
}

double EditContext::snapToGrid (double seconds, bool force) const
{
    if (! snapOn && ! force) return juce::jmax (0.0, seconds);
    return snap (seconds, gridBeats >= 4.0 ? (double) beatsPerBar() : gridBeats);
}

juce::String EditContext::formatBars (double seconds, bool withTicks) const
{
    const double beats = toBeats (seconds) + 1.0e-7;   // évite « 8. 4. 4. 119 » au lieu de « 9. 1. 1. 0 »
    const int bpb = beatsPerBar();
    const int bar = (int) (beats / bpb) + 1;
    const double inBar = beats - (bar - 1) * bpb;
    const int beat = (int) inBar + 1;
    const double inBeat = inBar - (beat - 1);
    const int sixteenth = (int) (inBeat * 4.0) + 1;
    const int ticks = (int) ((inBeat * 4.0 - (sixteenth - 1)) * 120.0);
    auto s = juce::String (bar).paddedLeft (' ', 3) + ". " + juce::String (beat) + ". " + juce::String (sixteenth);
    if (withTicks) s << ". " << juce::String (ticks).paddedLeft (' ', 3);
    return s;
}

juce::String EditContext::formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const int h = (int) (seconds / 3600.0), m = (int) (seconds / 60.0) % 60, s = (int) seconds % 60;
    const int ms = (int) (seconds * 1000.0) % 1000;
    return juce::String::formatted ("%d:%02d:%02d.%03d", h, m, s, ms);
}

bool EditContext::prepareAudioProxies (te::Edit& e, int timeoutMs)
{
    // Tracktion génère ces fichiers depuis un minuteur du fil principal : on le laisse tourner en attendant.
    auto pending = [&e]
    {
        juce::Array<te::AudioClipBase*> r;
        for (auto* t : te::getAudioTracks (e))
            for (auto* c : t->getClips())
                if (auto* a = dynamic_cast<te::AudioClipBase*> (c))
                {
                    a->beginRenderingNewProxyIfNeeded();
                    if (a->isGeneratingPlaybackFile() && ! a->getPlaybackFile().getFile().existsAsFile()) r.add (a);
                }
        return r;
    };
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! pending().isEmpty())
    {
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
    // Laisse le moteur prendre en compte les fichiers prêts.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    return true;
}

bool EditContext::isUserPlugin (te::Plugin* p)
{
    return dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr;
}

juce::Array<te::Plugin*> EditContext::getInserts (te::AudioTrack& t) const
{
    juce::Array<te::Plugin*> r;
    for (auto p : t.pluginList.getPlugins())
        if (isUserPlugin (p) && ! p->isSynth() && dynamic_cast<te::AuxSendPlugin*> (p) == nullptr && dynamic_cast<te::AuxReturnPlugin*> (p) == nullptr)
            r.add (p);
    return r;
}

int EditContext::fxBusOf (te::AudioTrack& t)
{
    for (auto p : t.pluginList.getPlugins())
        if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (p)) return ret->busNumber.get();
    return -1;
}

juce::Array<te::AudioTrack*> EditContext::getFxTracks() const
{
    juce::Array<te::AudioTrack*> r;
    if (edit != nullptr)
        for (auto* t : te::getAudioTracks (*edit)) if (fxBusOf (*t) >= 0) r.add (t);
    return r;
}

te::AuxSendPlugin* EditContext::getSend (te::AudioTrack& t, int bus) const
{
    for (auto p : t.pluginList.getPlugins())
        if (auto* s = dynamic_cast<te::AuxSendPlugin*> (p); s != nullptr && s->getBusNumber() == bus) return s;
    return nullptr;
}

te::AuxSendPlugin* EditContext::ensureSend (te::AudioTrack& t, int bus)
{
    if (auto* s = getSend (t, bus)) return s;
    auto p = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
    auto* s = dynamic_cast<te::AuxSendPlugin*> (p.get());
    if (s == nullptr) return nullptr;
    s->busNumber = bus;
    s->setGainDb (-100.0f);
    // Envoi post-fader : juste après le volume de la piste.
    auto& list = t.pluginList;
    int index = list.size();
    for (int i = 0; i < list.size(); ++i) if (dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) != nullptr) index = i + 1;
    list.insertPlugin (p, index, nullptr);
    return getSend (t, bus);
}

te::AudioTrack* EditContext::addFxTrack (const PluginEntry* effect)
{
    if (edit == nullptr) return nullptr;
    int bus = 0;
    for (auto* t : getFxTracks()) bus = juce::jmax (bus, fxBusOf (*t) + 1);
    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getTopLevelTracks (*edit).getLast()), nullptr);
    if (t == nullptr) return nullptr;
    auto ret = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
    if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (ret.get())) r->busNumber = bus;
    t->pluginList.insertPlugin (ret, 0, nullptr);
    PluginEntry e;
    if (effect != nullptr) e = *effect;
    else for (auto& b : catalog.getBuiltIns (false)) if (b.builtInType == ondefx::SpacePlugin::xmlTypeName) e = b;   // Onde Espace
    if (e.name.isNotEmpty()) PluginCatalog::addToTrack (*t, e);
    // Bus de retour : la réverbération y est entièrement « mouillée » (le son direct reste sur les pistes).
    for (auto* p : t->pluginList) if (auto* sp = dynamic_cast<ondefx::SpacePlugin*> (p)) sp->setStored ("mix", 100.0f);
    t->setName ("FX " + juce::String (bus + 1) + U (" — ") + e.name.upToFirstOccurrenceOf (" (", false, false));
    t->setColour (juce::Colour (0xff8a7ad8));
    selectTrack (t.get());
    changed();
    return t.get();
}

void EditContext::toggleAutomation (const te::Track& t)
{
    const auto id = t.itemID.toString();
    if (automationShown.count (id)) automationShown.erase (id); else automationShown.insert (id);
    changed();
}

void EditContext::setTrackMonitoring (te::AudioTrack& t, bool on)
{
    if (edit == nullptr) return;
    bool any = false;
    for (auto instance : edit->getAllInputDevices())
        if (te::isOnTargetTrack (*instance, t, 0))
        {
            instance->getInputDevice().setMonitorMode (on ? te::InputDevice::MonitorMode::on : te::InputDevice::MonitorMode::automatic);
            any = true;
        }
    if (! any && on && setStatus) setStatus (U ("Aucune entrée n'est reliée à cette piste : armez-la ou choisissez une carte son"));
    changed();
}

bool EditContext::isTrackMonitoring (te::AudioTrack& t) const
{
    if (edit == nullptr) return false;
    for (auto instance : edit->getAllInputDevices())
        if (te::isOnTargetTrack (*instance, t, 0) && instance->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on)
            return true;
    return false;
}

juce::Array<SandboxPlugin*> EditContext::getSandboxPlugins (te::AudioTrack& t) const
{
    juce::Array<SandboxPlugin*> r;
    for (auto p : t.pluginList.getPlugins())
        if (auto* sp = dynamic_cast<SandboxPlugin*> (p)) r.add (sp);
    return r;
}

te::Plugin* EditContext::getInstrument (te::AudioTrack& t) const
{
    for (auto p : t.pluginList.getPlugins())
        if (p->isSynth()) return p;
    return nullptr;
}

void EditContext::routeInputsTo (te::AudioTrack* t)
{
    if (edit == nullptr || t == nullptr) return;
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumMidiInDevices(); ++i)
        if (auto mi = dm.getMidiInDevice (i))
        {
            mi->setEnabled (true);
            // « Toutes les entrées MIDI » (claviers et clavier d'Onde) : écoute directe de la piste sélectionnée.
            mi->setMonitorMode (mi->getDeviceID() == "all_midi_in" ? te::InputDevice::MonitorMode::on : te::InputDevice::MonitorMode::automatic);
        }
    edit->getTransport().ensureContextAllocated();
    // Le clavier MIDI joue la piste sélectionnée (si elle a un instrument) et les autres pistes instrument armées
    // (plusieurs instruments joués et enregistrés ensemble), par l'entrée « toutes les entrées MIDI » seulement
    // (sinon un clavier physique sonnerait deux fois).
    if (getInstrument (*t) != nullptr)
    {
        bool hasAllMidi = false;
        for (auto instance : edit->getAllInputDevices())
            hasAllMidi = hasAllMidi || instance->getInputDevice().getDeviceID() == "all_midi_in";
        for (auto instance : edit->getAllInputDevices())
        {
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice) continue;
            if (hasAllMidi && instance->getInputDevice().getDeviceID() != "all_midi_in")
            {
                for (auto id : instance->getTargets()) [[maybe_unused]] auto r = instance->removeTarget (id, nullptr);
                continue;
            }
            for (auto id : instance->getTargets())
                if (id != t->itemID && ! instance->isRecordingEnabled (id)) [[maybe_unused]] auto r = instance->removeTarget (id, nullptr);
            if (! te::isOnTargetTrack (*instance, *t, 0)) [[maybe_unused]] auto r = instance->setTarget (t->itemID, false, nullptr, 0);
        }
    }
}

juce::StringArray EditContext::audioInputNames() const
{
    juce::StringArray names;
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i))
            if (! w->isTrackDevice()) names.add (w->getName());
    return names;
}

juce::String EditContext::getTrackInput (const te::AudioTrack& t) const
{
    return t.state.getProperty ("ondeInput").toString();
}

void EditContext::setTrackInput (te::AudioTrack& t, const juce::String& name)
{
    const bool wasArmed = isTrackArmed (t);
    if (wasArmed) setTrackArmed (t, false);
    t.state.setProperty ("ondeInput", name, nullptr);       // réglage de matériel : hors historique
    if (wasArmed) setTrackArmed (t, true);
    changed();
}

void EditContext::useMonoInputs (bool mono)
{
    // Une entrée par prise de la carte son (micro sur 1, guitare sur 2…) : un micro n'est plus enregistré sur un
    // seul côté d'une paire stéréo. setDeviceNumChannels recrée les entrées : on recommence jusqu'à stabilité.
    auto& dm = engine.getDeviceManager();
    for (int pass = 0; pass < 64; ++pass)
    {
        te::WaveInputDevice* toChange = nullptr;
        for (int i = 0; i < dm.getNumWaveInDevices() && toChange == nullptr; ++i)
            if (auto* w = dm.getWaveInDevice (i))
                if (! w->isTrackDevice() && (w->getChannelSet().size() == 2) == mono) toChange = w;
        if (toChange == nullptr) break;
        dm.setDeviceNumChannels (*toChange, mono ? 1u : 2u);
    }
}

void EditContext::armOnSeparateInputs()
{
    // Chaque piste audio d'enregistrement reçoit sa propre entrée, dans l'ordre (voix : entrée 1, guitare : entrée 2…).
    if (edit == nullptr) return;
    const auto names = audioInputNames();
    if (names.isEmpty()) return;
    int k = 0;
    for (auto* t : te::getAudioTracks (*edit))
    {
        if (getInstrument (*t) != nullptr || getFxTracks().contains (t)) continue;
        if (k >= names.size()) break;
        t->state.setProperty ("ondeInput", names[k++], nullptr);
        setTrackArmed (*t, true);
    }
}

void EditContext::setTrackArmed (te::AudioTrack& t, bool arm)
{
    if (edit == nullptr) return;
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto w = dm.getWaveInDevice (i)) { w->setEnabled (true); w->setMonitorMode (te::InputDevice::MonitorMode::automatic); }
    edit->getTransport().ensureContextAllocated();

    const bool wantsMidi = getInstrument (t) != nullptr;
    if (wantsMidi)
    {
        // MIDI : une seule entrée par piste, « toutes les entrées MIDI » (claviers physiques et clavier d'Onde
        // réunis) ; deux entrées armées sur une piste donnent deux prises, dont une vide qui remplace l'autre.
        // Plusieurs pistes instrument peuvent être armées ensemble : le clavier les joue et les enregistre toutes.
        bool hasAllMidi = false;
        for (auto instance : edit->getAllInputDevices())
            hasAllMidi = hasAllMidi || instance->getInputDevice().getDeviceID() == "all_midi_in";
        for (auto instance : edit->getAllInputDevices())
        {
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice) continue;
            if (hasAllMidi && instance->getInputDevice().getDeviceID() != "all_midi_in")
            {
                if (te::isOnTargetTrack (*instance, t, 0)) instance->setRecordingEnabled (t.itemID, false);
                continue;
            }
            if (arm && ! te::isOnTargetTrack (*instance, t, 0))
                [[maybe_unused]] auto r = instance->setTarget (t.itemID, false, nullptr, 0);
            if (te::isOnTargetTrack (*instance, t, 0))
                instance->setRecordingEnabled (t.itemID, arm);
            break;
        }
        changed();
        return;
    }

    // Audio : l'entrée choisie pour la piste ; sinon la première qui n'enregistre pas déjà sur une autre piste.
    auto input = getTrackInput (t);
    const auto names = audioInputNames();
    if (arm && ! names.contains (input))
    {
        input = {};
        for (auto instance : edit->getAllInputDevices())
        {
            if (instance->getInputDevice().getDeviceType() != te::InputDevice::waveDevice) continue;
            bool busy = false;
            for (auto id : instance->getTargets()) busy = busy || (id != t.itemID && instance->isRecordingEnabled (id));
            if (! busy && names.contains (instance->getInputDevice().getName())) { input = instance->getInputDevice().getName(); break; }
        }
        if (input.isEmpty() && ! names.isEmpty()) input = names[0];
        t.state.setProperty ("ondeInput", input, nullptr);
    }
    for (auto instance : edit->getAllInputDevices())
    {
        if (instance->getInputDevice().getDeviceType() != te::InputDevice::waveDevice) continue;
        const bool chosen = instance->getInputDevice().getName() == input;
        if (arm && chosen)
        {
            if (! te::isOnTargetTrack (*instance, t, 0)) [[maybe_unused]] auto r = instance->setTarget (t.itemID, false, nullptr, 0);
            instance->setRecordingEnabled (t.itemID, true);
        }
        else if (te::isOnTargetTrack (*instance, t, 0))
        {
            instance->setRecordingEnabled (t.itemID, false);
            if (! chosen) [[maybe_unused]] auto r = instance->removeTarget (t.itemID, nullptr);
        }
    }
    changed();
}

bool EditContext::isTrackArmed (te::AudioTrack& t) const
{
    if (edit == nullptr) return false;
    for (auto instance : edit->getAllInputDevices())
        if (te::isOnTargetTrack (*instance, t, 0) && instance->isRecordingEnabled (t.itemID))
            return true;
    return false;
}

te::TrackInsertPoint EditContext::insertPointForNewTrack() const
{
    // Sous la piste (ou le dossier) sélectionné, au même niveau ; sinon à la fin du projet.
    if (auto* f = getSelectedFolder()) return te::TrackInsertPoint (*f, false);
    if (edit != nullptr)
        if (auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, selectedTrackID)))
            return te::TrackInsertPoint (*t, false);
    return te::TrackInsertPoint (nullptr, edit != nullptr ? te::getTopLevelTracks (*edit).getLast() : nullptr);
}

te::FolderTrack* EditContext::getSelectedFolder() const
{
    return edit != nullptr && selectedFolderID.isValid() ? dynamic_cast<te::FolderTrack*> (te::findTrackForID (*edit, selectedFolderID)) : nullptr;
}

void EditContext::selectFolder (te::FolderTrack* f)
{
    selectedFolderID = f != nullptr ? f->itemID : te::EditItemID();
    changed();
}

int EditContext::depthOf (const te::Track& t)
{
    int d = 0;
    for (auto* p = t.getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack()) ++d;
    return d;
}

void EditContext::toggleCollapsed (const te::Track& f)
{
    auto state = f.state;
    if (isCollapsed (f)) state.removeProperty ("ondeCollapsed", nullptr); else state.setProperty ("ondeCollapsed", true, nullptr);
    changed();
}

te::FolderTrack* EditContext::addFolderTrack (bool submix, bool moveSelectedTrackInside)
{
    if (edit == nullptr) return nullptr;
    auto* sel = moveSelectedTrackInside ? getSelectedTrack() : nullptr;
    auto point = sel != nullptr ? te::TrackInsertPoint (*sel, true) : insertPointForNewTrack();
    auto f = edit->insertNewFolderTrack (point, nullptr, submix);
    if (f == nullptr) return nullptr;
    static int counter = 0;
    f->setName ((submix ? U ("Groupe ") : U ("Dossier ")) + juce::String (++counter));
    f->setColour (submix ? juce::Colour (0xff3fae8f) : juce::Colour (0xffb08a4a));
    if (submix) groupMeter (*f, true);
    if (sel != nullptr) moveTrackIntoFolder (*sel, *f);
    selectedFolderID = f->itemID;
    changed();
    return f.get();
}

void EditContext::moveTrackIntoFolder (te::Track& t, te::FolderTrack& f)
{
    if (edit == nullptr || &t == &f) return;
    for (auto* p = f.getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack())
        if (p == &t) return;                      // pas de dossier dans lui-même
    auto children = f.getAllSubTracks (false);
    edit->moveTrack (&t, te::TrackInsertPoint (&f, children.isEmpty() ? nullptr : children.getLast()));
    changed();
}

void EditContext::moveTrackOutOfFolder (te::Track& t)
{
    auto* f = t.getParentFolderTrack();
    if (edit == nullptr || f == nullptr) return;
    edit->moveTrack (&t, te::TrackInsertPoint (*f, false));
    changed();
}

te::AudioTrack* EditContext::addAudioTrack()
{
    if (edit == nullptr) return nullptr;
    auto tracks = te::getAudioTracks (*edit);
    auto t = edit->insertNewAudioTrack (insertPointForNewTrack(), nullptr);
    if (t == nullptr) return nullptr;
    t->setName ("Audio " + juce::String (tracks.size() + 1));
    static const juce::uint32 colours[] = { 0xffe58a6e, 0xff6ea8fe, 0xffa6d96a, 0xffd493c4, 0xff5fc8b0, 0xfff2c14e, 0xffb9a6f2 };
    t->setColour (juce::Colour (colours[tracks.size() % 7]));
    selectTrack (t.get());
    return t.get();
}

te::AudioTrack* EditContext::addInstrumentTrack (const PluginEntry* instrument)
{
    auto* t = addAudioTrack();
    if (t == nullptr) return nullptr;
    PluginEntry e = instrument != nullptr ? *instrument : catalog.getBuiltIns (true).getFirst();
    PluginCatalog::addToTrack (*t, e);
    t->setName (e.name.upToFirstOccurrenceOf (" (", false, false));
    // Onde Synth : la piste prend le nom du son choisi (« Piano de concert »).
    for (auto* p : t->pluginList)
        if (auto* s = dynamic_cast<ondefx::SynthPlugin*> (p)) t->setName (s->getPresetName());
    routeInputsTo (t);
    return t;
}

Action* EditContext::addAction (const juce::String& id, const juce::String& name, const juce::String& category,
                                juce::KeyPress key, std::function<void()> fn)
{
    auto* a = actions.add (new Action());
    a->id = id; a->name = name; a->category = category; a->key = key; a->perform = std::move (fn);
    return a;
}

Action* EditContext::findAction (const juce::String& id) const
{
    for (auto* a : actions) if (a->id == id) return a;
    return nullptr;
}

bool EditContext::performShortcut (const juce::KeyPress& raw)
{
    // Sous Windows, Ctrl+lettre arrive comme caractère de contrôle (Ctrl+T -> 20), parfois deux fois de suite :
    // on retrouve la lettre et on ignore le doublon immédiat.
    auto k = raw;
    const int code = raw.getKeyCode();
    if (raw.getModifiers().isCtrlDown() && code >= 1 && code <= 26)
        k = juce::KeyPress ('a' + code - 1, raw.getModifiers(), 0);
    const auto now = juce::Time::getMillisecondCounter();
    if (k == lastShortcut && now - lastShortcutMs < 20) return true;
    lastShortcut = k;
    lastShortcutMs = now;

    for (auto* a : actions)
        if ((a->key.isValid() && a->key == k) || a->extraKeys.contains (k)) { a->perform(); return true; }
    return false;
}

void EditContext::destroyEdit (std::unique_ptr<te::Edit>& e)
{
    if (e == nullptr) return;
    auto& dm = e->engine.getDeviceManager();
    auto* callback = (juce::AudioIODeviceCallback*) &dm;          // base privée de te::DeviceManager
    e->getTransport().stop (false, true);
    // Sinon : le fil principal arrête les threads du graphe (clearThreads) pendant que le fil audio attend
    // leur résultat (waitForFinalNode) et les deux tournent à vide indéfiniment.
    dm.deviceManager.removeAudioCallback (callback);
    e->getTransport().freePlaybackContext();
    e.reset();
    dm.deviceManager.addAudioCallback (callback);
}

void EditContext::changeTempo (const std::function<void()>& change)
{
    if (edit == nullptr) { change(); return; }
    te::EditTimecodeRemapperSnapshot snap;
    snap.savePreChangeState (*edit);
    change();
    snap.remapEdit (*edit);
    changed();
}

void EditContext::setMusicalMode (te::WaveAudioClip& c, bool on)
{
    if (c.getAutoTempo() == on) return;
    if (on)
    {
        // Tempo d'origine du fichier : celui du projet là où l'événement commence (enregistré ou importé à ce tempo).
        const auto info = c.getAudioFile().getInfo();
        const double bpm = c.edit.tempoSequence.getBpmAt (c.getPosition().getStart());
        auto loop = c.getLoopInfo();
        loop.setBpm (bpm, info);
        c.setLoopInfo (loop);
    }
    c.setAutoTempo (on);
}

te::LevelMeterPlugin* EditContext::groupMeter (te::FolderTrack& f, bool createIfMissing)
{
    if (! f.isSubmixFolder()) return nullptr;
    if (auto* lm = f.pluginList.getPluginsOfType<te::LevelMeterPlugin>().getLast()) return lm;
    if (! createIfMissing) return nullptr;
    f.pluginList.insertPlugin (te::LevelMeterPlugin::create(), -1);
    return f.pluginList.getPluginsOfType<te::LevelMeterPlugin>().getLast();
}
