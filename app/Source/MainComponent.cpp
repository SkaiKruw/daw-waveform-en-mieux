#include "MainComponent.h"
#include "DemoContent.h"

using State = onde::PluginSandbox::State;

// Éditeur générique pour les effets intégrés (un curseur par paramètre).
class ParamEditor  : public juce::Component
{
public:
    explicit ParamEditor (te::Plugin& p) : plugin (&p)
    {
        for (auto param : p.getAutomatableParameters())
        {
            auto* l = labels.add (new juce::Label ({}, param->getParameterName()));
            l->setColour (juce::Label::textColourId, theme::text);
            l->setFont (theme::ui (13.0f));
            addAndMakeVisible (l);
            auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
            s->setRange (param->valueRange.start, param->valueRange.end, 0.0);
            s->setValue (param->getCurrentValue(), juce::dontSendNotification);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            te::AutomatableParameter::Ptr ptr (param);
            s->onValueChange = [ptr, s] { ptr->setParameter ((float) s->getValue(), juce::sendNotification); };
            addAndMakeVisible (s);
        }
        setSize (440, juce::jmax (60, 44 + labels.size() * 34));
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10).withTrimmedTop (30);
        for (int i = 0; i < labels.size(); ++i)
        {
            auto row = r.removeFromTop (30); r.removeFromTop (4);
            labels[i]->setBounds (row.removeFromLeft (140));
            sliders[i]->setBounds (row);
        }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::text); g.setFont (theme::ui (14.0f, true));
        g.drawText (plugin->getName(), 12, 8, getWidth() - 24, 20, juce::Justification::centredLeft);
        if (labels.isEmpty()) { g.setColour (theme::muted); g.drawText (U ("Pas de réglage pour ce plugin."), getLocalBounds(), juce::Justification::centred); }
    }
private:
    te::Plugin::Ptr plugin;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> sliders;
};

//==============================================================================
MainComponent::MainComponent (te::Engine& e, PluginCatalog& c) : engine (e), catalog (c), ctx (e, c)
{
    addAndMakeVisible (menuBar);
    addAndMakeVisible (toolbar);
    addAndMakeVisible (infoLine);
    addAndMakeVisible (inspector);
    addChildComponent (mediaRack);
    addAndMakeVisible (transport);
    toolbar.isZoneVisible = [this] (int z) { return isZoneVisible (z); };
    toolbar.remoteState = [this] { return ! remote.isRunning() ? 0 : remote.phoneConnected() ? 2 : 1; };
    toolbar.getProjectTitle = [this]
    {
        const bool dirty = edit && edit->hasChangedSinceSaved();
        const int ago = (int) (juce::Time::getCurrentTime() - lastAutosave).inSeconds();
        return projectName() + (dirty ? " *" : "") + U ("   ·   ") + (untitled ? U ("copie de secours il y a ") : U ("sauvegarde auto il y a ")) + juce::String (ago) + " s";
    };

    for (auto* b : { &lowerMixTab, &lowerEditTab, &lowerAudioTab })
    {
        styleButton (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (91);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accentBg);
        b->setColour (juce::TextButton::textColourOnId, theme::accent);
        addChildComponent (*b);
    }
    styleButton (lowerClose);
    addChildComponent (lowerClose);
    lowerMixTab.onClick = [this] { showView (1); };
    lowerEditTab.onClick = [this] { showView (2); };
    lowerAudioTab.onClick = [this] { showView (3); };
    lowerClose.onClick = [this] { setZone (1, false); };

    ctx.showView = [this] (int v) { showView (v); };
    ctx.setStatus = [this] (const juce::String& s) { setStatus (s); };
    ctx.openPluginEditor = [this] (te::Plugin& p, juce::Component* target) { openPluginEditor (p, target); };
    ctx.addChangeListener (this);
    catalog.addChangeListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    addChildComponent (palette);
    registerActions();
    // Développement : ONDE_REMOTE_LOCAL=<port> démarre la télécommande sur 127.0.0.1 (aperçu sans pare-feu).
    if (const int p = juce::SystemStats::getEnvironmentVariable ("ONDE_REMOTE_LOCAL", {}).getIntValue(); p > 0 && remote.start (p, true))
    {
        std::printf ("Telecommande locale : http://127.0.0.1:%d/?k=%s\n", p, remote.getCode().toRawUTF8());
        std::fflush (stdout);
    }
    setWantsKeyboardFocus (true);
    setOpaque (true);
    setSize (1600, 940);
    lastAutosave = juce::Time::getCurrentTime();
    startTimerHz (25);
}

MainComponent::~MainComponent()
{
    onde::trace ("~MainComponent: début");
    if (remoteWindow != nullptr) delete remoteWindow.getComponent();
    if (hubWindow != nullptr) delete hubWindow.getComponent();
    if (historyWindow != nullptr) delete historyWindow.getComponent();
    if (modulatorWindow != nullptr) delete modulatorWindow.getComponent();
    for (auto& [id, w] : effectWindows) if (w != nullptr) delete w.getComponent();
    remote.stop();
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    catalog.removeChangeListener (this);
    ctx.removeChangeListener (this);
    transport.detachMeters();
    inspector.detachMeters();
    arrangement = nullptr; mixer = nullptr; pianoRoll = nullptr; audioEditor = nullptr;
    onde::trace ("~MainComponent: vues détruites");
    ctx.edit = nullptr;
    EditContext::destroyEdit (edit);
    onde::trace ("~MainComponent: projet fermé");
}

juce::File MainComponent::projectsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Onde");
}

juce::File MainComponent::recoveryFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde").getChildFile (U ("récupération.onde"));
}

juce::String MainComponent::projectName() const
{
    return untitled ? U ("Sans titre") : projectFile.getFileNameWithoutExtension();
}

// Tracktion écrit les chemins relatifs depuis le fichier du projet (« ..\son.wav » pour un son posé à côté)
// mais les relit depuis son dossier : on accepte les deux conventions, et on cherche aussi à côté du projet.
static te::Edit::FilePathResolver makeResolver (std::function<juce::File()> editFileGetter)
{
    return [editFileGetter] (const juce::String& path) -> juce::File
    {
        if (juce::File::isAbsolutePath (path)) return juce::File (path);
        const auto editFile = editFileGetter ? editFileGetter() : juce::File();
        if (editFile == juce::File()) return {};
        const auto name = path.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
        for (auto f : { editFile.getChildFile (path), editFile.getSiblingFile (path), editFile.getSiblingFile (name),
                        editFile.getSiblingFile (editFile.getFileNameWithoutExtension() + " - Audio").getChildFile (name) })
            if (f.existsAsFile()) return f;
        return editFile.getChildFile (path);
    };
}

void MainComponent::installPathResolver (te::Edit& e)
{
    te::Edit* ep = &e;
    e.filePathResolver = makeResolver ([ep] { return ep->editFileRetriever ? ep->editFileRetriever() : juce::File(); });
}

std::unique_ptr<te::Edit> MainComponent::loadProject (te::Engine& engine, const juce::File& f)
{
    auto state = te::loadEditFromFile (engine, f, te::ProjectItemID{});
    if (! state.isValid()) return {};
    auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);
    if (! id.isValid()) id = te::ProjectItemID::createNewID (te::ProjectID{});
    return te::Edit::createEdit (te::Edit::Options { engine, state, id, te::Edit::forEditing, nullptr, te::Edit::getDefaultNumUndoLevels(),
                                                     [f] { return f; }, makeResolver ([f] { return f; }) });
}

void MainComponent::setEdit (std::unique_ptr<te::Edit> newEdit, const juce::File& file, bool isUntitled)
{
    transport.detachMeters();
    inspector.detachMeters();
    arrangement = nullptr; mixer = nullptr; pianoRoll = nullptr; audioEditor = nullptr;
    ctx.edit = nullptr;
    ctx.selectedClipID = {}; ctx.selectedTrackID = {};
    ctx.automationShown.clear();
    ctx.selectedFolderID = {};
    EditContext::destroyEdit (edit);
    edit = std::move (newEdit);
    ctx.edit = edit.get();
    ctx.tempoTrackShown = ctx.hasTempoChanges();     // un projet avec des changements de tempo montre sa piste de tempo
    projectFile = file;
    untitled = isUntitled;
    ctx.edit = edit.get();
    if (edit == nullptr) return;
    {
        // Anciens projets : les pistes de groupe reçoivent leur vumètre (sans entrée dans l'historique).
        bool added = false;
        for (auto* f : te::getTracksOfType<te::FolderTrack> (*edit, true))
            if (f->isSubmixFolder() && EditContext::groupMeter (*f, false) == nullptr) { EditContext::groupMeter (*f, true); added = true; }
        if (added) edit->getUndoManager().clearUndoHistory();
    }

    // Les fichiers audio sont référencés relativement au projet : Tracktion doit savoir où il se trouve.
    edit->editFileRetriever = [file] { return file; };
    installPathResolver (*edit);
    edit->getTransport().ensureContextAllocated();
    edit->playInStopEnabled = true;
    edit->getAutomationRecordManager().setReadingAutomation (true);
    edit->getAutomationRecordManager().setWritingAutomation (false);     // comme une console : écriture désactivée par défaut
    auto& tr = edit->getTransport();
    if (tr.getLoopRange().isEmpty() || tr.getLoopRange().getLength().inSeconds() < 0.01)
        tr.setLoopRange ({ te::TimePosition(), te::TimePosition() + juce::jmax (te::TimeDuration::fromSeconds (ctx.barSeconds() * 4), edit->getLength()) });

    arrangement = std::make_unique<ArrangementView> (ctx);
    mixer = std::make_unique<MixerView> (ctx);
    pianoRoll = std::make_unique<PianoRollView> (ctx);
    addAndMakeVisible (*arrangement); addChildComponent (*mixer); addChildComponent (*pianoRoll);
    audioEditor = std::make_unique<AudioEditorView> (ctx);
    addChildComponent (*audioEditor);
    if (auto* t = ctx.getSelectedTrack()) { ctx.selectedTrackID = t->itemID; ctx.routeInputsTo (t); }
    palette.toFront (false);
    showView (lowerVisible ? lowerTab : 0);
    resized();
    ctx.changed();
    menuItemsChanged();
    if (onTitleChanged) onTitleChanged (projectName());
}

bool MainComponent::isZoneVisible (int which) const
{
    return which == 0 ? inspectorVisible : which == 1 ? lowerVisible : rightVisible;
}

void MainComponent::setZone (int which, bool visible)
{
    if (which == 0) inspectorVisible = visible;
    else if (which == 1) lowerVisible = visible;
    else rightVisible = visible;
    resized();
}

void MainComponent::showView (int v)
{
    if (v >= 1 && v <= 3) { lowerTab = v; lowerVisible = true; }
    (lowerTab == 1 ? lowerMixTab : lowerTab == 2 ? lowerEditTab : lowerAudioTab).setToggleState (true, juce::dontSendNotification);
    resized();
    if (arrangement && v == 0) arrangement->grabKeyboardFocus();
    grabKeyboardFocus();
}

void MainComponent::openPalette (const juce::String& q)
{
    palette.setBounds (getLocalBounds());
    palette.open();
    if (q.isNotEmpty()) palette.setQuery (q);
}

void MainComponent::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getCurrentTime();
    repaint (0, getHeight() - statusH, getWidth(), statusH);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* b)
{
    if (b == &catalog) { registerActions(); repaint (0, getHeight() - statusH, getWidth(), statusH); }
}

void MainComponent::globalFocusChanged (juce::Component* focused)
{
    // Les boutons, curseurs et listes ne gardent pas le clavier : Espace, R, L… restent des commandes.
    if (focused == nullptr || focused == this || ! isParentOf (focused)) return;
    if (dynamic_cast<juce::TextEditor*> (focused) != nullptr) return;
    if (dynamic_cast<juce::Button*> (focused) || dynamic_cast<juce::Slider*> (focused) || dynamic_cast<juce::ComboBox*> (focused)
        || dynamic_cast<juce::ListBox*> (focused) || dynamic_cast<juce::Viewport*> (focused))
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe) safe->grabKeyboardFocus(); });
    }
}

//==============================================================================
void MainComponent::registerActions()
{
    ctx.actions.clear();
    using KP = juce::KeyPress;
    const int ctrl = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier, alt = juce::ModifierKeys::altModifier;
    auto tr = [this]() -> te::TransportControl* { return edit ? &edit->getTransport() : nullptr; };

    // --- Fichier
    ctx.addAction ("file.new", "Nouveau projet", "Fichier", KP ('n', ctrl, 0), [this] { newProject(); });
    ctx.addAction ("file.hub", U ("Écran d'accueil (modèles, projets récents)…"), "Fichier", KP ('h', ctrl | shift, 0), [this] { showHub(); });
    ctx.addAction ("file.open", U ("Ouvrir…"), "Fichier", KP ('o', ctrl, 0), [this] { openProject(); });
    ctx.addAction ("file.save", "Enregistrer", "Fichier", KP ('s', ctrl, 0), [this] { saveProject(); })->separatorBefore = true;
    ctx.addAction ("file.saveas", U ("Enregistrer sous…"), "Fichier", KP ('s', ctrl | shift, 0), [this] { saveProjectAs(); });
    ctx.addAction ("file.import", U ("Importer › Fichier audio…"), "Fichier", KP ('i', ctrl | shift, 0), [this] { importAudio(); })->separatorBefore = true;
    ctx.addAction ("file.export", U ("Exporter › Mixage audio (WAV, FLAC, MP3, OGG)…"), "Fichier", KP ('e', ctrl | shift, 0), [this] { exportWav(); });
    ctx.addAction ("file.quit", "Quitter", "Fichier", KP ('q', ctrl, 0), [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); })->separatorBefore = true;

    // --- Édition
    ctx.addAction ("edit.undo", "Annuler", U ("Édition"), KP ('z', ctrl, 0), [this] { if (edit) { edit->getUndoManager().undo(); setStatus ("Annuler : " + edit->getUndoManager().getRedoDescription()); ctx.changed(); } });
    ctx.addAction ("edit.redo", U ("Rétablir"), U ("Édition"), KP ('y', ctrl, 0), [this] { if (edit) { edit->getUndoManager().redo(); setStatus (U ("Rétabli")); ctx.changed(); } })
        ->extraKeys.add (KP ('z', ctrl | shift, 0));
    ctx.addAction ("edit.history", U ("Historique des modifications…"), U ("Édition"), {}, [this]
    {
        if (historyWindow != nullptr) { historyWindow->toFront (true); return; }
        historyWindow = HistoryPanel::show (ctx);
    });
    ctx.addAction ("clip.split", U ("Couper au curseur"), U ("Édition"), KP ('x', alt, 0), [this] { splitSelectedClip(); })->separatorBefore = true;
    ctx.findAction ("clip.split")->extraKeys.add (KP ('s'));
    ctx.addAction ("edit.cut", "Couper", U ("Édition"), KP ('x', ctrl, 0), [this]
    {
        if (ctx.getSelectedClips().isEmpty()) return;
        copySelection(); deleteSelectedClip(); setStatus (U ("Sélection coupée"));
    })->separatorBefore = true;
    ctx.addAction ("edit.copy", "Copier", U ("Édition"), KP ('c', ctrl, 0), [this] { copySelection(); });
    ctx.addAction ("edit.paste", U ("Coller"), U ("Édition"), KP ('v', ctrl, 0), [this] { pasteClip(); });
    ctx.addAction ("edit.selectAll", U ("Tout sélectionner"), U ("Édition"), KP ('a', ctrl, 0), [this]
    {
        if (! edit) return;
        juce::Array<te::Clip*> all;
        for (auto* t : te::getAudioTracks (*edit)) for (auto* c : t->getClips()) all.add (c);
        ctx.setClipSelection (all);
        setStatus (juce::String (all.size()) + U (" événement(s) sélectionné(s)"));
    });
    ctx.addAction ("clip.glue", U ("Coller à l'événement suivant (outil Colle)"), U ("Édition"), {}, [this] { glueSelectedClip(); })->separatorBefore = true;
    ctx.addAction ("clip.duplicate", "Dupliquer", U ("Édition"), KP ('d', ctrl, 0), [this] { duplicateSelectedClip(); });
    ctx.addAction ("clip.delete", "Supprimer", U ("Édition"), KP (KP::deleteKey), [this] { deleteSelectedClip(); })->extraKeys.add (KP (KP::backspaceKey));
    ctx.addAction ("clip.mute", U ("Rendre muet / audible"), U ("Édition"), KP ('m', shift, 0), [this]
    {
        auto sel = ctx.getSelectedClips();
        if (sel.isEmpty()) return;
        edit->getUndoManager().beginNewTransaction ("Muet");
        const bool mute = ! sel.getFirst()->isMuted();
        for (auto* c : sel) c->setMuted (mute);
        ctx.changed();
    });
    ctx.addAction ("edit.deselect", U ("Tout désélectionner"), U ("Édition"), KP (KP::escapeKey), [this] { ctx.selectClip (nullptr); });
    for (int i = 0; i < 8; ++i)
    {
        auto* a = ctx.addAction ("tool." + juce::String (i), U ("Outil : ") + EditContext::toolName ((Tool) i).upToFirstOccurrenceOf (" (", false, false),
                                 U ("Édition"), KP ('1' + i), [this, i] { ctx.setTool ((Tool) i); setStatus (U ("Outil : ") + EditContext::toolName ((Tool) i)); });
        a->isTicked = [this, i] { return (int) ctx.tool == i; };
        if (i == 0) a->separatorBefore = true;
    }
    auto* snap = ctx.addAction ("edit.snap", U ("Calage activé"), U ("Édition"), KP ('j'), [this] { ctx.snapOn = ! ctx.snapOn; ctx.changed(); setStatus (ctx.snapOn ? U ("Calage activé") : U ("Calage désactivé")); });
    snap->isTicked = [this] { return ctx.snapOn; };
    snap->separatorBefore = true;

    // --- Projet
    ctx.addAction ("track.addAudio", U ("Ajouter une piste › Audio"), "Projet", KP ('t', ctrl, 0), [this] { if (edit) { edit->getUndoManager().beginNewTransaction(); ctx.addAudioTrack(); } });
    ctx.addAction ("track.addInst", U ("Ajouter une piste › Instrument"), "Projet", KP ('t', ctrl | shift, 0), [this] { if (edit) { edit->getUndoManager().beginNewTransaction(); ctx.addInstrumentTrack(); } });
    ctx.addAction ("track.addFx", U ("Ajouter une piste › FX (bus d'effet, réverbération)"), "Projet", {}, [this]
    {
        if (! edit) return;
        edit->getUndoManager().beginNewTransaction (U ("Ajouter une piste FX"));
        if (auto* fx = ctx.addFxTrack()) setStatus (U ("« ") + fx->getName() + U (" » créée : réglez les envois dans l'inspecteur de chaque piste"));
    });
    ctx.addAction ("track.addFolder", U ("Ajouter une piste › Dossier"), "Projet", {}, [this]
    {
        if (! edit) return;
        edit->getUndoManager().beginNewTransaction (U ("Ajouter un dossier"));
        ctx.addFolderTrack (false, false);
    });
    ctx.addAction ("track.addGroup", U ("Ajouter une piste › Groupe (sous-mixage)"), "Projet", {}, [this]
    {
        if (! edit) return;
        edit->getUndoManager().beginNewTransaction (U ("Ajouter un groupe"));
        ctx.addFolderTrack (true, false);
    });
    ctx.addAction ("track.toFolder", U ("Mettre la piste sélectionnée dans un nouveau dossier"), "Projet", KP ('f', ctrl | shift, 0), [this]
    {
        if (! edit || ctx.getSelectedTrack() == nullptr) return;
        edit->getUndoManager().beginNewTransaction (U ("Nouveau dossier"));
        if (auto* f = ctx.addFolderTrack (false, true)) setStatus (U ("« ") + ctx.getSelectedTrack()->getName() + U (" » rangée dans « ") + f->getName() + U (" »"));
    });
    ctx.addAction ("track.duplicate", U ("Dupliquer la piste"), "Projet", KP ('d', ctrl | shift, 0), [this]
    {
        auto* t = ctx.getSelectedTrack();
        if (t == nullptr || edit == nullptr) return;
        edit->getUndoManager().beginNewTransaction (U ("Dupliquer la piste"));
        if (auto copy = edit->copyTrack (t, te::TrackInsertPoint (t->getParentTrack(), t)))
            if (auto* at = dynamic_cast<te::AudioTrack*> (copy.get())) { at->setName (t->getName() + U (" (copie)")); ctx.selectTrack (at); }
        ctx.changed();
    });
    ctx.addAction ("track.delete", U ("Supprimer la piste sélectionnée"), "Projet", KP (KP::deleteKey, shift, 0), [this] { deleteSelectedTrack(); });
    ctx.addAction ("track.mute", U ("Piste : rendre muette"), "Projet", KP ('m'), [this] { if (auto* t = ctx.getSelectedTrack()) { t->setMute (! t->isMuted (false)); ctx.changed(); } })->separatorBefore = true;
    ctx.addAction ("track.solo", "Piste : solo", "Projet", KP ('s', shift, 0), [this] { if (auto* t = ctx.getSelectedTrack()) { t->setSolo (! t->isSolo (false)); ctx.changed(); } });
    ctx.addAction ("track.automation", U ("Piste : afficher l'automation"), "Projet", KP ('a'), [this] { if (auto* t = ctx.getSelectedTrack()) ctx.toggleAutomation (*t); });
    ctx.addAction ("track.modulators", U ("Modulateurs de la piste (LFO, pas à pas, enveloppe)…"), "Projet", KP ('l', ctrl | shift, 0), [this]
    {
        if (modulatorWindow != nullptr) { modulatorWindow->toFront (true); return; }
        modulatorWindow = ModulatorPanel::show (ctx);
    });
    ctx.addAction ("track.prev", U ("Sélectionner la piste précédente"), "Projet", KP (KP::upKey), [this]
    {
        if (pianoRoll && pianoRoll->isVisible() && pianoRoll->getNumSelected() > 0) { pianoRoll->transposeSelected (1); return; }
        if (! edit) return; auto ts = te::getAudioTracks (*edit); const int i = ts.indexOf (ctx.getSelectedTrack());
        if (i > 0) ctx.selectTrack (ts[i - 1]);
    })->separatorBefore = true;
    ctx.addAction ("track.next", U ("Sélectionner la piste suivante"), "Projet", KP (KP::downKey), [this]
    {
        if (pianoRoll && pianoRoll->isVisible() && pianoRoll->getNumSelected() > 0) { pianoRoll->transposeSelected (-1); return; }
        if (! edit) return; auto ts = te::getAudioTracks (*edit); const int i = ts.indexOf (ctx.getSelectedTrack());
        if (i >= 0 && i < ts.size() - 1) ctx.selectTrack (ts[i + 1]);
    });
    ctx.addAction ("song.tempo", U ("Tempo au curseur…"), "Projet", {}, [this] { changeTempo(); })->separatorBefore = true;
    auto* tt = ctx.addAction ("song.tempoTrack", U ("Piste de tempo"), "Projet", KP ('t', alt, 0), [this]
    {
        ctx.tempoTrackShown = ! ctx.tempoTrackShown;
        ctx.changed();
    });
    tt->isTicked = [this] { return ctx.tempoTrackShown; };
    ctx.addAction ("song.insertTempo", U ("Insérer un changement de tempo au curseur"), "Projet", {}, [this]
    {
        if (! edit) return;
        edit->getUndoManager().beginNewTransaction ("Tempo");
        const double beat = std::round (ctx.toBeats (edit->getTransport().getPosition().inSeconds()));
        const double here = ctx.bpmAt (edit->getTransport().getPosition().inSeconds());
        ctx.changeTempo ([&] { edit->tempoSequence.insertTempo (te::BeatPosition::fromBeats (beat), here, 1.0f); });
        ctx.tempoTrackShown = true;
        setStatus (U ("Changement de tempo inséré : faites glisser son point dans la piste de tempo"));
        ctx.changed();
    });

    // --- Audio
    ctx.addAction ("audio.import", U ("Importer un fichier audio…"), "Audio", {}, [this] { importAudio(); });
    ctx.addAction ("audio.arm", U ("Activer l'enregistrement de la piste sélectionnée"), "Audio", KP ('r', shift, 0), [this]
    {
        if (auto* t = ctx.getSelectedTrack()) ctx.setTrackArmed (*t, ! ctx.isTrackArmed (*t));
    });
    ctx.addAction ("audio.monitor", U ("Monitoring de la piste sélectionnée"), "Audio", {}, [this]
    {
        if (auto* t = ctx.getSelectedTrack()) ctx.setTrackMonitoring (*t, ! ctx.isTrackMonitoring (*t));
    });
    ctx.addAction ("audio.editor", U ("Éditeur audio"), "Audio", {}, [this] { showView (3); })->separatorBefore = true;
    ctx.addAction ("audio.normalise", U ("Normaliser les événements sélectionnés"), "Audio", {}, [this]
    {
        if (! edit) return;
        int n = 0;
        edit->getUndoManager().beginNewTransaction ("Normaliser");
        for (auto* c : ctx.getSelectedClips()) if (auto* w = dynamic_cast<te::WaveAudioClip*> (c)) n += AudioEditorView::normalise (*w) ? 1 : 0;
        setStatus (n > 0 ? juce::String (n) + U (" événement(s) normalisé(s) à -0,1 dB") : U ("Sélectionnez un événement audio"));
        ctx.changed();
    });
    ctx.addAction ("audio.reverse", U ("Inverser les événements sélectionnés"), "Audio", {}, [this]
    {
        if (! edit) return;
        edit->getUndoManager().beginNewTransaction ("Inverser");
        for (auto* c : ctx.getSelectedClips()) if (auto* w = dynamic_cast<te::WaveAudioClip*> (c)) w->setIsReversed (! w->getIsReversed());
        ctx.changed();
    });
    ctx.addAction ("audio.fades", U ("Fondus d'entrée et de sortie (1 temps)"), "Audio", {}, [this]
    {
        if (! edit) return;
        edit->getUndoManager().beginNewTransaction ("Fondus");
        for (auto* c : ctx.getSelectedClips())
            if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
            {
                w->setFadeIn (te::TimeDuration::fromSeconds (ctx.beatSeconds()));
                w->setFadeOut (te::TimeDuration::fromSeconds (ctx.beatSeconds()));
            }
        ctx.changed();
    });
    ctx.addAction ("audio.musical", U ("Mode musical (l'événement suit le tempo)"), "Audio", KP ('m', ctrl | shift, 0), [this]
    {
        if (! edit) return;
        juce::Array<te::WaveAudioClip*> clips;
        for (auto* c : ctx.getSelectedClips()) if (auto* w = dynamic_cast<te::WaveAudioClip*> (c)) clips.add (w);
        if (clips.isEmpty()) { setStatus (U ("Sélectionnez un événement audio")); return; }
        const bool on = ! clips.getFirst()->getAutoTempo();
        edit->getUndoManager().beginNewTransaction (U ("Mode musical"));
        for (auto* w : clips) EditContext::setMusicalMode (*w, on);
        EditContext::prepareAudioProxies (*edit, 20000);
        setStatus (on ? U ("Mode musical activé : l'événement s'étire avec le tempo")
                      : U ("Mode musical désactivé : l'événement garde sa durée"));
        ctx.changed();
    });
    ctx.addAction ("audio.bounce", U ("Rendre définitif (réécrire l'événement dans un fichier)"), "Audio", {}, [this]
    {
        auto* w = dynamic_cast<te::WaveAudioClip*> (ctx.getSelectedClip());
        if (w == nullptr) { setStatus (U ("Sélectionnez un événement audio")); return; }
        juce::String err;
        if (auto* nc = AudioEditorView::bounce (ctx, *w, err)) setStatus (U ("« ") + nc->getName() + U (" » : gain, fondus et traitements appliqués au fichier"));
        else setStatus (U ("Rendre définitif : ") + err);
    });
    ctx.addAction ("audio.separate", U ("Séparer les sources (voix, batterie, basse, autres)…"), "Audio", KP ('u', ctrl | shift, 0),
                   [this] { stems::separateSelectedClip (ctx); })->separatorBefore = true;

    // --- MIDI
    // Ctrl+E / Entrée : l'éditeur qui convient à l'événement sélectionné (notes pour le MIDI, audio sinon).
    ctx.addAction ("midi.editor", U ("Ouvrir l'éditeur (notes ou audio)"), "MIDI", KP ('e', ctrl, 0), [this]
    {
        showView (dynamic_cast<te::WaveAudioClip*> (ctx.getSelectedClip()) != nullptr ? 3 : 2);
    })->extraKeys.add (KP (KP::returnKey));
    ctx.addAction ("clip.quantise", U ("Quantifier"), "MIDI", KP ('q'), [this] { quantiseSelectedClip(); });
    ctx.addAction ("midi.newPart", U ("Créer un conteneur MIDI au curseur"), "MIDI", KP ('n', ctrl | shift, 0), [this]
    {
        auto* t = ctx.getSelectedTrack();
        if (t == nullptr || ctx.getInstrument (*t) == nullptr) { setStatus (U ("Sélectionnez une piste instrument")); return; }
        edit->getUndoManager().beginNewTransaction (U ("Nouveau conteneur MIDI"));
        const double s = ctx.snapToGrid (edit->getTransport().getPosition().inSeconds(), true);
        if (auto mc = t->insertMIDIClip ("Conteneur MIDI", { te::TimePosition::fromSeconds (s), te::TimePosition::fromSeconds (ctx.addBars (s, 1.0)) }, nullptr))
            ctx.selectClip (mc.get());
    });

    // --- Transport
    ctx.addAction ("transport.play", U ("Démarrer / arrêter"), "Transport", KP (KP::spaceKey), [this, tr]
    {
        if (auto* t = tr())
        {
            onde::trace (t->isPlaying() ? "ACTION transport.play : arret" : "ACTION transport.play : demarrage");
            if (t->isPlaying()) { playWanted = false; t->stop (false, false); }
            else
            {
                playStartPosition = t->getPosition().inSeconds();
                t->play (false);
                playWanted = true;
                playRequestMs = juce::Time::getMillisecondCounter();
                playRetries = 0;
            }
        }
    });
    ctx.addAction ("transport.stop", "Stop", "Transport", KP ('0'), [this, tr]
    {
        if (auto* t = tr())
        {
            playWanted = false;
            if (t->isPlaying()) t->stop (false, false);
            else t->setPosition (te::TimePosition::fromSeconds (playStartPosition));   // second Stop : retour au point de départ
        }
    })->extraKeys.add (KP (KP::numberPad0));
    ctx.addAction ("transport.record", "Enregistrer", "Transport", KP ('r'), [this] { toggleRecord(); })->extraKeys.add (KP (KP::numberPadMultiply));
    auto* cyc = ctx.addAction ("transport.loop", U ("Cycle"), "Transport", KP ('l'), [this, tr]
    {
        if (auto* t = tr())
        {
            if (t->getLoopRange().getLength().inSeconds() < 0.01) t->setLoopRange ({ te::TimePosition(), te::TimePosition() + edit->getLength() });
            t->looping = ! t->looping.get();
        }
    });
    cyc->extraKeys.add (KP ('/')); cyc->extraKeys.add (KP (KP::numberPadDivide));
    cyc->isTicked = [tr] { auto* t = tr(); return t != nullptr && t->looping.get(); };
    auto* click = ctx.addAction ("transport.click", U ("Métronome"), "Transport", KP ('c'), [this]
    {
        if (! edit) return;
        edit->clickTrackEnabled = ! edit->clickTrackEnabled.get();
        setStatus (edit->clickTrackEnabled ? U ("Métronome activé") : U ("Métronome désactivé"));
    });
    click->isTicked = [this] { return edit && edit->clickTrackEnabled.get(); };
    ctx.addAction ("transport.home", U ("Aller au début du projet"), "Transport", KP (KP::homeKey), [tr] { if (auto* t = tr()) t->setPosition (te::TimePosition()); })->separatorBefore = true;
    ctx.addAction ("transport.end", U ("Aller à la fin du projet"), "Transport", KP (KP::endKey), [this, tr] { if (auto* t = tr()) t->setPosition (te::TimePosition() + edit->getLength()); });
    ctx.addAction ("transport.rewind", U ("Rembobiner d'une mesure"), "Transport", KP ('-'), [this, tr]
    {
        if (auto* t = tr())
        {
            const int bpb = ctx.beatsPerBar();
            const double bar = std::ceil (ctx.toBeats (t->getPosition().inSeconds()) / bpb - 1.0001);
            t->setPosition (te::TimePosition::fromSeconds (ctx.toSeconds (juce::jmax (0.0, bar) * bpb)));
        }
    })->extraKeys.add (KP (KP::numberPadSubtract));
    ctx.addAction ("transport.forward", U ("Avancer d'une mesure"), "Transport", KP ('+'), [this, tr]
    {
        if (auto* t = tr())
        {
            const int bpb = ctx.beatsPerBar();
            const double bar = std::floor (ctx.toBeats (t->getPosition().inSeconds()) / bpb + 1.0001);
            t->setPosition (te::TimePosition::fromSeconds (ctx.toSeconds (bar * bpb)));
        }
    })->extraKeys.add (KP (KP::numberPadAdd));
    ctx.findAction ("transport.forward")->extraKeys.add (KP ('='));
    ctx.addAction ("transport.locators", U ("Délimiteurs sur l'événement sélectionné"), "Transport", KP ('p'), [this, tr]
    {
        auto* c = ctx.getSelectedClip(); auto* t = tr();
        if (c == nullptr || t == nullptr) { setStatus (U ("Sélectionnez un événement")); return; }
        t->setLoopRange (c->getPosition().time);
        setStatus (U ("Délimiteurs placés sur « ") + c->getName() + U (" »"));
    })->separatorBefore = true;
    ctx.addAction ("transport.toLeft", U ("Aller au délimiteur gauche"), "Transport", KP ('1', ctrl, 0), [tr] { if (auto* t = tr()) t->setPosition (t->getLoopRange().getStart()); })->extraKeys.add (KP (KP::numberPad1));
    ctx.addAction ("transport.toRight", U ("Aller au délimiteur droit"), "Transport", KP ('2', ctrl, 0), [tr] { if (auto* t = tr()) t->setPosition (t->getLoopRange().getEnd()); })->extraKeys.add (KP (KP::numberPad2));
    ctx.addAction ("transport.addMarker", U ("Insérer un repère au curseur"), "Transport", KP (KP::insertKey), [this, tr]
    {
        auto* t = tr();
        if (t == nullptr) return;
        edit->getUndoManager().beginNewTransaction (U ("Insérer un repère"));
        edit->ensureMarkerTrack();
        auto& mm = edit->getMarkerManager();
        const int id = mm.getNextUniqueID();
        if (auto mk = mm.createMarker (id, t->getPosition(), te::TimeDuration::fromSeconds (ctx.barSeconds()), nullptr))
        {
            mk->setName (U ("Repère ") + juce::String (id));
            setStatus (U ("Repère ") + juce::String (id) + U (" inséré à ") + ctx.formatBars (t->getPosition().inSeconds(), false).trim()
                       + U ("  (double-clic dessus dans la règle pour le renommer)"));
        }
        ctx.changed();
    })->separatorBefore = true;
    ctx.addAction ("transport.nextMarker", U ("Aller au repère suivant"), "Transport", KP ('n', shift, 0), [this, tr]
    {
        if (auto* t = tr()) if (auto* mk = edit->getMarkerManager().getNextMarker (t->getPosition() + te::TimeDuration::fromSeconds (0.001))) t->setPosition (mk->getPosition().getStart());
    });
    ctx.addAction ("transport.prevMarker", U ("Aller au repère précédent"), "Transport", KP ('b', shift, 0), [this, tr]
    {
        if (auto* t = tr()) if (auto* mk = edit->getMarkerManager().getPrevMarker (t->getPosition() - te::TimeDuration::fromSeconds (0.001))) t->setPosition (mk->getPosition().getStart());
    });
    for (int n = 3; n <= 9; ++n)
    {
        auto* a = ctx.addAction ("transport.marker" + juce::String (n), U ("Aller au repère ") + juce::String (n), "Transport", KP (KP::numberPad0 + n), [this, tr, n]
        {
            if (auto* t = tr()) if (auto* mk = edit->getMarkerManager().getMarkerByID (n)) t->setPosition (mk->getPosition().getStart());
        });
        juce::ignoreUnused (a);
    }
    auto* punch = ctx.addAction ("transport.punch", U ("Punch (enregistrer entre les délimiteurs)"), "Transport", KP ('i'), [this]
    {
        if (edit) edit->recordingPunchInOut = ! edit->recordingPunchInOut.get();
    });
    punch->isTicked = [this] { return edit && edit->recordingPunchInOut.get(); };

    // --- Studio
    ctx.addAction ("view.mixer", "MixConsole", "Studio", KP (KP::F3Key), [this]
    {
        if (lowerVisible && lowerTab == 1) setZone (1, false); else showView (1);
    });
    ctx.addAction ("file.audio", U ("Configuration du studio (carte son)…"), "Studio", KP (',', ctrl, 0), [this] { showAudioSettings(); })->separatorBefore = true;
    ctx.addAction ("studio.remote", U ("Télécommande (téléphone)…"), "Studio", {}, [this]
    {
        if (remoteWindow != nullptr) { remoteWindow->toFront (true); return; }
        remoteWindow = RemotePanel::show (remote);
    });
    ctx.addAction ("plugins.scan", U ("Rechercher des plugins VST3 (analyse isolée)"), "Studio", {}, [this] { catalog.startScan(); setStatus (U ("Analyse des plugins lancée")); });
    ctx.addAction ("plugins.restart", U ("Relancer tous les plugins arrêtés"), "Studio", {}, [this]
    {
        if (! edit) return;
        int n = 0;
        for (auto* t : te::getAudioTracks (*edit)) for (auto* sp : ctx.getSandboxPlugins (*t)) if (sp->isFailed()) { sp->restartSandbox(); ++n; }
        setStatus (juce::String (n) + U (" plugin(s) relancé(s)"));
        ctx.changed();
    })->separatorBefore = true;
    ctx.addAction ("plugins.crash", U ("Simuler un plantage du plugin isolé (démo)"), "Studio", {}, [this] { simulateCrash(); });

    // --- Fenêtre
    auto zoneAction = [this] (const char* id, const char* name, juce::KeyPress k, int z)
    {
        auto* a = ctx.addAction (id, U (name), U ("Fenêtre"), k, [this, z] { setZone (z, ! isZoneVisible (z)); });
        a->isTicked = [this, z] { return isZoneVisible (z); };
        return a;
    };
    zoneAction ("view.inspector", "Zone gauche (inspecteur)", KP ('i', alt, 0), 0);
    zoneAction ("view.lowerzone", "Zone inférieure", KP ('e', ctrl | alt, 0), 1);
    zoneAction ("view.rightzone", "Zone droite (médias et plugins)", KP ('r', ctrl | alt, 0), 2);
    auto* il = ctx.addAction ("view.infoline", U ("Ligne d'infos"), U ("Fenêtre"), KP ('i', ctrl, 0), [this] { infoVisible = ! infoVisible; resized(); });
    il->isTicked = [this] { return infoVisible; };
    ctx.addAction ("view.piano", U ("Éditeur de notes (zone inférieure)"), U ("Fenêtre"), KP ('3', alt, 0), [this] { showView (2); })->separatorBefore = true;
    ctx.addAction ("view.mixer2", U ("MixConsole (zone inférieure)"), U ("Fenêtre"), KP ('2', alt, 0), [this] { showView (1); });
    ctx.addAction ("view.arrangement", U ("Fenêtre de projet seule"), U ("Fenêtre"), KP ('1', alt, 0), [this] { setZone (1, false); });
    ctx.addAction ("view.zoomIn", "Zoom avant", U ("Fenêtre"), KP ('h'), [this] { if (arrangement && edit) arrangement->zoom (1.25, edit->getTransport().getPosition().inSeconds()); })->separatorBefore = true;
    ctx.addAction ("view.zoomOut", U ("Zoom arrière"), U ("Fenêtre"), KP ('g'), [this] { if (arrangement && edit) arrangement->zoom (0.8, edit->getTransport().getPosition().inSeconds()); });
    ctx.addAction ("view.zoomFit", U ("Zoom sur tout le projet"), U ("Fenêtre"), KP ('f', shift, 0), [this] { if (arrangement) arrangement->zoomToFit(); });

    // --- Aide
    ctx.addAction ("palette.open", U ("Rechercher une commande…"), "Aide", KP ('k', ctrl, 0), [this] { openPalette(); });
    ctx.addAction ("help.shortcuts", "Raccourcis clavier", "Aide", KP (KP::F1Key), [this] { showShortcuts(); });
    ctx.addAction ("help.about", U ("À propos d'Onde"), "Aide", {}, [] { AboutPanel::show(); })->separatorBefore = true;

    // Une action par effet / instrument, pour les trouver en tapant leur nom (et dans le menu Studio).
    auto addPluginAction = [this] (const PluginEntry& e)
    {
        ctx.addAction ("plugin.add." + e.name, (e.isInstrument ? U ("Nouvelle piste instrument : ") : U ("Insérer l'effet : ")) + e.name + (e.isVST3() ? U (" (VST3 isolé)") : juce::String()),
                       e.isInstrument ? "Instrument" : "Effet", {}, [this, e]
        {
            if (edit == nullptr) return;
            edit->getUndoManager().beginNewTransaction();
            if (e.isInstrument) { ctx.addInstrumentTrack (&e); setStatus (U ("Piste instrument « ") + e.name + U (" » ajoutée")); return; }
            auto* t = ctx.getSelectedTrack();
            if (t == nullptr) return;
            PluginCatalog::addToTrack (*t, e);
            ctx.changed();
            setStatus (U ("« ") + e.name + U (" » inséré sur « ") + t->getName() + U (" »"));
        });
    };
    for (bool inst : { true, false })
    {
        for (auto& e : catalog.getBuiltIns (inst)) addPluginAction (e);
        for (auto& e : catalog.getVST3 (inst)) addPluginAction (e);
    }
    menuItemsChanged();
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Fichier", U ("Édition"), "Projet", "Audio", "MIDI", "Transport", "Studio", U ("Fenêtre"), "Aide" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int, const juce::String& menuName)
{
    juce::PopupMenu m;
    auto addItem = [this] (juce::PopupMenu& menu, int index)
    {
        auto* a = ctx.actions[index];
        juce::PopupMenu::Item it (a->name);
        it.itemID = index + 1;
        it.shortcutKeyDescription = a->keyText();
        it.isTicked = a->isTicked ? a->isTicked() : false;
        menu.addItem (std::move (it));
    };
    bool first = true;
    for (int i = 0; i < ctx.actions.size(); ++i)
    {
        auto* a = ctx.actions[i];
        if (a->category != menuName) continue;
        if (a->separatorBefore && ! first) m.addSeparator();
        addItem (m, i);
        first = false;
    }
    if (menuName == "Studio")
    {
        juce::PopupMenu inst, fx;
        for (int i = 0; i < ctx.actions.size(); ++i)
        {
            if (ctx.actions[i]->category == "Instrument") addItem (inst, i);
            if (ctx.actions[i]->category == "Effet") addItem (fx, i);
        }
        m.addSeparator();
        m.addSubMenu (U ("Instruments VST › nouvelle piste"), inst);
        m.addSubMenu (U ("Effets › insérer sur la piste sélectionnée"), fx);
    }
    if (menuName == "Fichier")
    {
        juce::PopupMenu recent;
        int n = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (projectsFolder(), false, "*.onde", juce::File::findFiles))
        {
            const auto f = entry.getFile();
            recent.addItem (U ("Ouvrir « ") + f.getFileNameWithoutExtension() + U (" »"), [this, f] { openProjectFile (f); });
            if (++n >= 10) break;
        }
        if (n > 0) m.addSubMenu (U ("Projets du dossier Onde"), recent, true, nullptr, false, 0);
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    if (auto* a = ctx.actions[id - 1])
    {
        auto fn = a->perform;
        juce::MessageManager::callAsync ([fn] { fn(); });
    }
}

//==============================================================================
void MainComponent::toggleRecord()
{
    if (! edit) return;
    auto& t = edit->getTransport();
    if (t.isRecording()) { t.stop (false, false); setStatus (U ("Enregistrement terminé")); ctx.changed(); return; }
    bool anyArmed = false;
    for (auto* tr : te::getAudioTracks (*edit)) anyArmed |= ctx.isTrackArmed (*tr);
    if (! anyArmed)
        if (auto* tr = ctx.getSelectedTrack()) { ctx.setTrackArmed (*tr, true); anyArmed = ctx.isTrackArmed (*tr); }
    if (! anyArmed) { setStatus (U ("Aucune entrée disponible : choisissez une carte son (Studio › Configuration du studio) ou branchez un clavier MIDI")); return; }
    playStartPosition = t.getPosition().inSeconds();
    t.record (false);
    setStatus (U ("Enregistrement en cours…"));
}

void MainComponent::splitSelectedClip()
{
    auto* c = ctx.getSelectedClip();
    if (c == nullptr || edit == nullptr) { setStatus (U ("Sélectionnez d'abord un événement")); return; }
    const auto t = edit->getTransport().getPosition();
    if (! c->getPosition().time.contains (t)) { setStatus (U ("Placez le curseur dans l'événement pour le couper")); return; }
    edit->getUndoManager().beginNewTransaction ("Couper");
    if (auto* ct = dynamic_cast<te::ClipTrack*> (c->getTrack())) ct->splitClip (*c, t);
    ctx.changed();
}

void MainComponent::glueSelectedClip()
{
    auto* c = ctx.getSelectedClip();
    if (c == nullptr || edit == nullptr) { setStatus (U ("Sélectionnez d'abord un événement")); return; }
    auto* ct = dynamic_cast<te::ClipTrack*> (c->getTrack());
    if (ct == nullptr) return;
    te::Clip* next = nullptr;
    for (auto* o : ct->getClips())
        if (o != c && o->getPosition().getStart() >= c->getPosition().getStart() && (next == nullptr || o->getPosition().getStart() < next->getPosition().getStart()))
            next = o;
    if (next == nullptr) { setStatus (U ("Aucun événement après celui-ci sur la piste")); return; }
    edit->getUndoManager().beginNewTransaction ("Coller");
    if (auto* a = dynamic_cast<te::MidiClip*> (c); a != nullptr && dynamic_cast<te::MidiClip*> (next) != nullptr)
    {
        juce::Array<te::MidiClip*> both { a, dynamic_cast<te::MidiClip*> (next) };
        auto r = te::mergeMidiClips (both);
        setStatus (r.wasOk() ? U ("Conteneurs MIDI réunis") : U ("Impossible de réunir : ") + r.getErrorMessage());
    }
    else if (auto* wa = dynamic_cast<te::WaveAudioClip*> (c); wa != nullptr)
    {
        auto* wb = dynamic_cast<te::WaveAudioClip*> (next);
        const bool contiguous = wb != nullptr && wa->getOriginalFile() == wb->getOriginalFile()
            && std::abs ((wb->getPosition().getStart() - wb->getPosition().getOffset()).inSeconds() - (wa->getPosition().getStart() - wa->getPosition().getOffset()).inSeconds()) < 0.001;
        if (! contiguous) { setStatus (U ("Seuls des morceaux contigus du même fichier audio peuvent être recollés")); return; }
        const auto end = next->getPosition().getEnd();
        next->removeFromParent();
        c->setLength (end - c->getPosition().getStart(), true);
        setStatus (U ("Événements recollés"));
    }
    ctx.selectClip (c);
    ctx.changed();
}

void MainComponent::deleteSelectedClip()
{
    // Éditeur de notes ouvert avec des notes sélectionnées : on supprime les notes, pas le conteneur.
    if (pianoRoll && pianoRoll->isVisible() && pianoRoll->deleteSelectedNotes()) return;
    auto sel = ctx.getSelectedClips();
    if (sel.isEmpty() || edit == nullptr) return;
    edit->getUndoManager().beginNewTransaction ("Supprimer");
    for (auto* c : sel) c->removeFromParent();
    ctx.selectClip (nullptr);
}

void MainComponent::copySelection()
{
    auto sel = ctx.getSelectedClips();
    if (sel.isEmpty()) return;
    double start = 1.0e9;
    for (auto* c : sel) start = juce::jmin (start, c->getPosition().getStart().inSeconds());
    clipboard.clearQuick();
    for (auto* c : sel)
        clipboard.add ({ c->state.createCopy(), c->getTrack() ? c->getTrack()->itemID : te::EditItemID(), c->getPosition().getStart().inSeconds() - start });
    setStatus (sel.size() == 1 ? U ("« ") + sel.getFirst()->getName() + U (" » copié") : juce::String (sel.size()) + U (" événements copiés"));
}

void MainComponent::pasteClip()
{
    if (clipboard.isEmpty() || edit == nullptr) { setStatus (U ("Presse-papiers vide : copiez d'abord un événement (Ctrl+C)")); return; }
    auto* selected = ctx.getSelectedTrack();
    if (selected == nullptr) return;
    // Un seul piste d'origine : on colle sur la piste sélectionnée. Plusieurs : chaque événement retrouve sa piste.
    bool multiTrack = false;
    for (auto& item : clipboard) multiTrack |= item.track != clipboard.getFirst().track;
    edit->getUndoManager().beginNewTransaction (U ("Coller"));
    const double cursor = edit->getTransport().getPosition().inSeconds();
    juce::Array<te::Clip*> pasted;
    for (auto& item : clipboard)
    {
        auto* t = selected;
        if (multiTrack)
            if (auto* orig = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, item.track))) t = orig;
        auto n = item.state.createCopy();
        te::EditItemID::remapIDs (n, nullptr, *edit);
        if (auto* c = te::insertClipCopy (*t, te::ClipCopy::fromClipboardState (n, false)))
        {
            c->setStart (te::TimePosition::fromSeconds (cursor + item.offset), false, true);
            pasted.add (c);
        }
    }
    ctx.setClipSelection (pasted);
    setStatus (juce::String (pasted.size()) + U (" événement(s) collé(s) au curseur"));
    ctx.changed();
}

void MainComponent::duplicateSelectedClip()
{
    auto sel = ctx.getSelectedClips();
    if (sel.isEmpty()) return;
    edit->getUndoManager().beginNewTransaction ("Dupliquer");
    // Le bloc sélectionné est recopié juste après sa fin.
    double start = 1.0e9, end = 0.0;
    for (auto* c : sel) { start = juce::jmin (start, c->getPosition().getStart().inSeconds()); end = juce::jmax (end, c->getPosition().getEnd().inSeconds()); }
    juce::Array<te::Clip*> copies;
    for (auto* c : sel)
        if (auto copy = te::duplicateClip (*c))
        {
            copy->setStart (te::TimePosition::fromSeconds (c->getPosition().getStart().inSeconds() + (end - start)), false, true);
            copies.add (copy.get());
        }
    ctx.setClipSelection (copies);
}

void MainComponent::quantiseSelectedClip()
{
    auto* mc = ctx.getSelectedMidiClip();
    if (mc == nullptr) { setStatus (U ("Sélectionnez un conteneur MIDI")); return; }
    auto* um = &edit->getUndoManager();
    um->beginNewTransaction ("Quantifier");
    const double q = ctx.quantizeBeats;
    for (auto* n : mc->getSequence().getNotes())
        n->setStartAndLength (te::BeatPosition::fromBeats (std::round (n->getStartBeat().inBeats() / q) * q),
                              te::BeatDuration::fromBeats (juce::jmax (q, std::round (n->getLengthBeats().inBeats() / q) * q)), um);
    setStatus (U ("Quantifié à 1/") + juce::String (juce::roundToInt (4.0 / q)));
    ctx.changed();
}

void MainComponent::deleteSelectedTrack()
{
    if (auto* f = ctx.getSelectedFolder())
    {
        edit->getUndoManager().beginNewTransaction (U ("Supprimer le dossier"));
        const auto name = f->getName();
        ctx.selectClip (nullptr);
        ctx.selectedFolderID = {};
        inspector.detachMeters();
        edit->deleteTrack (f);
        setStatus (U ("Dossier « ") + name + U (" » supprimé avec ses pistes (Ctrl+Z pour annuler)"));
        ctx.changed();
        return;
    }
    auto* t = ctx.getSelectedTrack();
    if (t == nullptr || edit == nullptr) return;
    edit->getUndoManager().beginNewTransaction ("Supprimer la piste");
    const auto name = t->getName();
    ctx.selectClip (nullptr);
    ctx.selectedTrackID = {};
    inspector.detachMeters();
    edit->deleteTrack (t);
    setStatus (U ("Piste « ") + name + U (" » supprimée (Ctrl+Z pour annuler)"));
    ctx.changed();
}

void MainComponent::changeTempo()
{
    if (! edit) return;
    const auto cursor = edit->getTransport().getPosition();
    auto* w = new juce::AlertWindow (U ("Tempo"), ctx.hasTempoChanges() ? U ("Tempo du segment sous le curseur (BPM) :") : U ("Nouveau tempo (BPM) :"),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("bpm", juce::String (edit->tempoSequence.getTempoAt (cursor).getBpm(), 2));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, cursor] (int r)
    {
        const double bpm = w->getTextEditorContents ("bpm").replaceCharacter (',', '.').getDoubleValue();
        if (r == 1 && bpm >= 20 && bpm <= 400 && edit)
        {
            edit->getUndoManager().beginNewTransaction ("Tempo");
            ctx.changeTempo ([&] { edit->tempoSequence.getTempoAt (cursor).setBpm (bpm); });
            setStatus ("Tempo : " + juce::String (bpm, 2) + " BPM");
            ctx.changed();
        }
    }), true);
}

void MainComponent::simulateCrash()
{
    if (! edit) return;
    SandboxPlugin* target = nullptr;
    if (auto* t = ctx.getSelectedTrack()) { auto l = ctx.getSandboxPlugins (*t); if (! l.isEmpty()) target = l.getFirst(); }
    if (target == nullptr)
        for (auto* t : te::getAudioTracks (*edit)) { auto l = ctx.getSandboxPlugins (*t); if (! l.isEmpty()) { target = l.getFirst(); break; } }
    if (target == nullptr) { setStatus (U ("Aucun plugin isolé dans le projet")); return; }
    target->simulateCrash();
    setStatus (U ("Plantage provoqué dans « ") + target->getName() + U (" » — il plantera au prochain bloc audio"));
}

void MainComponent::showShortcuts()
{
    juce::String text;
    juce::String cat;
    for (auto* a : ctx.actions)
    {
        if (! a->key.isValid() || a->category == "Instrument" || a->category == "Effet") continue;
        if (a->category != cat) { cat = a->category; text << "\n" << cat.toUpperCase() << "\n"; }
        text << "   " << a->keyText().paddedRight (' ', 14) << "  " << a->name << "\n";
    }
    auto* ed = new juce::TextEditor();
    ed->setMultiLine (true); ed->setReadOnly (true); ed->setFont (theme::mono (13.0f)); ed->setText (text.trim());
    ed->setSize (620, 640);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (ed);
    o.dialogTitle = "Raccourcis clavier";
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::openPluginEditor (te::Plugin& p, juce::Component* target)
{
    if (auto* sp = dynamic_cast<SandboxPlugin*> (&p))
    {
        if (sp->isFailed()) { setStatus (U ("Ce plugin est arrêté : relancez-le d'abord")); return; }
        if (sp->hasEditor()) { sp->showEditor(); setStatus (U ("Fenêtre de « ") + p.getName() + U (" » ouverte (dans son processus isolé)")); }
        else setStatus (U ("« ") + p.getName() + U (" » n'a pas de fenêtre de réglages"));
        return;
    }
    // Effets Onde : fenêtre dédiée (une seule par effet, ramenée au premier plan si déjà ouverte).
    if (auto editor = ondefx::createEditor (p))
    {
        auto& slot = effectWindows[p.itemID.toString()];
        if (slot != nullptr) { slot->toFront (true); return; }
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (editor.release());
        auto* track = p.getOwnerTrack();
        o.dialogTitle = p.getName() + (track != nullptr ? U (" — ") + track->getName() : juce::String());
        o.dialogBackgroundColour = juce::Colour (0xff17191d);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        slot = o.launchAsync();
        return;
    }
    auto ed = std::make_unique<ParamEditor> (p);
    juce::CallOutBox::launchAsynchronously (std::move (ed), target != nullptr ? target->getScreenBounds() : getScreenBounds().withSizeKeepingCentre (10, 10), nullptr);
}

//==============================================================================
void MainComponent::newProject()
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("onde-sans-titre-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)) + ".onde");
    auto e = te::createEmptyEdit (engine, file);
    e->tempoSequence.getTempos()[0]->setBpm (120.0);
    setEdit (std::move (e), file, true);
    ctx.addInstrumentTrack();
    ctx.addAudioTrack();
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    markCleanWhenSettled();
    setStatus ("Nouveau projet");
}

void MainComponent::newProjectFromTemplate (int kind)
{
    if (kind == 0) { newProject(); return; }
    // Projet sans piste, puis directement les pistes du modèle (rien à supprimer : aucune vue ne garde
    // de piste détruite).
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("onde-sans-titre-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)) + ".onde");
    auto e = te::createEmptyEdit (engine, file);
    e->tempoSequence.getTempos()[0]->setBpm (120.0);
    // Tracktion crée une piste par défaut : retirée avant que les vues ne s'attachent au projet.
    for (auto* t : te::getAudioTracks (*e)) e->deleteTrack (t);
    setEdit (std::move (e), file, true);
    if (! edit) return;
    auto named = [] (te::AudioTrack* t, const char* name) { if (t != nullptr) t->setName (U (name)); return t; };
    if (kind == 1)          // Enregistrement : voix et instruments acoustiques, avec une réverbération.
    {
        named (ctx.addAudioTrack(), "Voix");
        named (ctx.addAudioTrack(), "Guitare");
        named (ctx.addAudioTrack(), "Basse");
        named (ctx.addAudioTrack(), "Batterie");
        ctx.addFxTrack();
        if (auto* first = te::getAudioTracks (*edit).getFirst()) ctx.selectTrack (first);
        setStatus (U ("Nouveau projet « Enregistrement » : armez une piste (R) puis enregistrez (*)"));
    }
    else if (kind == 2)     // Production : instruments virtuels, une voix, une réverbération.
    {
        named (ctx.addInstrumentTrack(), "Basse");
        named (ctx.addInstrumentTrack(), "Accords");
        named (ctx.addInstrumentTrack(), "Lead");
        named (ctx.addAudioTrack(), "Voix");
        ctx.addFxTrack();
        if (auto* first = te::getAudioTracks (*edit).getFirst()) ctx.selectTrack (first);
        setStatus (U ("Nouveau projet « Production » : double-cliquez dans une piste pour créer un conteneur MIDI"));
    }
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    markCleanWhenSettled();
    ctx.changed();
}

static juce::File recentsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde").getChildFile (MainComponent::testMode ? U ("projets-récents-tests.txt") : U ("projets-récents.txt"));
}

juce::StringArray MainComponent::recentProjects()
{
    juce::StringArray r;
    for (auto& line : juce::StringArray::fromLines (recentsFile().loadFileAsString()))
        if (line.isNotEmpty() && juce::File (line).existsAsFile() && ! r.contains (line)) r.add (line);
    return r;
}

void MainComponent::rememberProject (const juce::File& f)
{
    if (f == recoveryFile() || ! f.existsAsFile()) return;
    auto list = recentProjects();
    list.removeString (f.getFullPathName());
    list.insert (0, f.getFullPathName());
    while (list.size() > 12) list.remove (list.size() - 1);
    recentsFile().getParentDirectory().createDirectory();
    recentsFile().replaceWithText (list.joinIntoString ("\n"));
}

void MainComponent::showHub()
{
    if (hubWindow != nullptr) { hubWindow->toFront (true); return; }
    hubWindow = HubPanel::show (*this);
}

void MainComponent::openProject()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Ouvrir un projet Onde"), projectsFolder(), "*.onde;*.tracktionedit");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) openProjectFile (fc.getResult());
    });
}

void MainComponent::openProjectFile (const juce::File& f)
{
    if (auto e = loadProject (engine, f))
    {
        setEdit (std::move (e), f, false);
        edit->resetChangedStatus();
        markCleanWhenSettled();
        rememberProject (f);
        setStatus (U ("Projet ouvert : ") + f.getFileName());
    }
    else setStatus (U ("Impossible d'ouvrir ") + f.getFileName());
}

int MainComponent::consolidateMedia (const juce::File& target)
{
    // Les prises d'un projet sans titre sont enregistrées dans le dossier temporaire de Windows :
    // à l'enregistrement, on les copie dans « <projet> - Audio » à côté du fichier du projet.
    if (! edit) return 0;
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto folder = target.getSiblingFile (target.getFileNameWithoutExtension() + " - Audio");
    int n = 0;
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* c : t->getClips())
            if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
            {
                const auto f = w->getSourceFileReference().getFile();
                if (! f.existsAsFile() || ! f.isAChildOf (temp)) continue;
                folder.createDirectory();
                auto dest = folder.getChildFile (f.getFileName());
                if (! dest.existsAsFile() && ! f.copyFileTo (dest)) continue;
                w->state.setProperty (te::IDs::source, dest.getFullPathName(), nullptr);   // hors historique : un « annuler » ne doit pas renvoyer vers le fichier temporaire
                ++n;
            }
    return n;
}

void MainComponent::saveProject (std::function<void (bool)> done)
{
    if (! edit) return;
    if (untitled) { saveProjectAs (done); return; }
    consolidateMedia (projectFile);
    const bool ok = te::EditFileOperations (*edit).writeToFile (projectFile, false);
    if (ok) edit->resetChangedStatus();
    setStatus (ok ? U ("Projet enregistré : ") + projectFile.getFileName() : U ("Échec de l'enregistrement"));
    recoveryFile().deleteFile();
    if (done) done (ok);
}

void MainComponent::saveProjectAs (std::function<void (bool)> done)
{
    projectsFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser> (U ("Enregistrer le projet"), projectsFolder().getChildFile (projectName() + ".onde"), "*.onde");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, done] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) { if (done) done (false); return; }
        const bool ok = saveProjectTo (f);
        if (done) done (ok);
    });
}

bool MainComponent::saveProjectTo (const juce::File& target)
{
    if (! edit) return false;
    const auto f = target.withFileExtension ("onde");
    f.getParentDirectory().createDirectory();
    // Les chemins relatifs dépendent de l'emplacement du projet : on les fige avant de le déplacer.
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* c : t->getClips())
            if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
                if (auto src = w->getSourceFileReference().getFile(); src.existsAsFile())
                    w->state.setProperty (te::IDs::source, src.getFullPathName(), nullptr);   // hors historique (l'enregistrement n'est pas une modification)
    edit->editFileRetriever = [f] { return f; };
    const int copied = consolidateMedia (f);
    const bool ok = te::EditFileOperations (*edit).writeToFile (f, false);
    if (ok) { projectFile = f; untitled = false; edit->resetChangedStatus(); rememberProject (f); if (onTitleChanged) onTitleChanged (projectName()); }
    else { const auto old = projectFile; edit->editFileRetriever = [old] { return old; }; }
    setStatus (! ok ? U ("Échec de l'enregistrement")
                    : copied > 0 ? U ("Projet enregistré, ") + juce::String (copied) + U (" fichier(s) audio copié(s) dans « ") + f.getFileNameWithoutExtension() + U (" - Audio »")
                                 : U ("Projet enregistré : ") + f.getFileName());
    return ok;
}

bool MainComponent::autosave()
{
    if (! edit || ! edit->hasChangedSinceSaved()) return false;
    // Projet nommé : enregistré en place. Sans titre : fichier de récupération.
    const auto target = untitled ? recoveryFile() : projectFile;
    target.getParentDirectory().createDirectory();
    const bool ok = te::EditFileOperations (*edit).writeToFile (target, false);
    if (ok && ! untitled) edit->resetChangedStatus();
    lastAutosave = juce::Time::getCurrentTime();
    return ok;
}

bool MainComponent::exportWavTo (const juce::File& f)
{
    if (! edit) return false;
    if (! EditContext::prepareAudioProxies (*edit))
        setStatus (U ("Certains événements inversés/transposés ne sont pas prêts : ils peuvent manquer dans l'export"));
    te::Renderer::Parameters p (*edit);
    p.destFile = f;
    p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 24;
    p.sampleRateForAudio = 48000.0;
    p.blockSizeForAudio = 512;
    auto& tr = edit->getTransport();
    // Comme une console de studio : si le cycle est actif, on exporte la zone entre les délimiteurs.
    p.time = tr.looping.get() && tr.getLoopRange().getLength().inSeconds() > 0.1
                 ? tr.getLoopRange()
                 : te::TimeRange (te::TimePosition(), te::TimePosition() + edit->getLength() + te::TimeDuration::fromSeconds (1.0));
    p.usePlugins = true;
    p.tracksToDo = te::toBitSet (te::getAllTracks (*edit));
    f.deleteFile();
    return te::Renderer::renderToFile (U ("Export"), p).existsAsFile();
}

void MainComponent::exportWav()
{
    if (! edit) return;
    exporter::showDialog (ctx, projectName(), [this] (const ExportSettings& s)
    {
        projectsFolder().createDirectory();
        const auto ext = s.extension();
        chooser = std::make_unique<juce::FileChooser> (U ("Exporter le mixage audio — ") + s.describe(), projectsFolder().getChildFile (projectName() + ext), "*" + ext);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, s] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File() || ! edit) return;
            setStatus (U ("Export en cours (") + s.describe() + U (")…"));
            repaint();
            juce::StringArray written;
            juce::String error;
            const bool ok = exporter::exportMix (*edit, f, s, written, error);
            setStatus (ok ? (written.size() == 1 ? U ("Exporté : ") + written[0] : juce::String (written.size()) + U (" fichiers exportés dans ") + f.getParentDirectory().getFullPathName())
                          : U ("Échec de l'export : ") + error);
            if (ok) juce::File (written[0]).revealToUser();
        });
    });
}

void MainComponent::importAudio()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Importer un fichier audio"), juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        if (! edit) return;
        auto files = fc.getResults();
        if (files.isEmpty()) return;
        edit->getUndoManager().beginNewTransaction (U ("Importer de l'audio"));
        const double start = edit->getTransport().getPosition().inSeconds();
        auto* target = ctx.getSelectedTrack();
        if (target != nullptr && ctx.getInstrument (*target) != nullptr) target = nullptr;
        for (auto& f : files)
        {
            auto* t = target != nullptr ? target : ctx.addAudioTrack();
            if (t == nullptr) continue;
            if (target == nullptr) t->setName (f.getFileNameWithoutExtension());
            const double len = te::AudioFile (engine, f).getLength();
            if (len > 0)
                if (auto c = t->insertWaveClip (f.getFileNameWithoutExtension(), f, { { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (start + len) }, te::TimeDuration() }, false))
                    ctx.selectClip (c.get());
            target = nullptr;
        }
        setStatus (juce::String (files.size()) + U (" fichier(s) importé(s) au curseur"));
        ctx.changed();
    });
}

void MainComponent::showAudioSettings()
{
    auto sel = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager().deviceManager, 0, 256, 0, 256, true, false, true, false);
    sel->setSize (600, 480);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (sel.release());
    o.dialogTitle = U ("Configuration du studio — carte son");
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

//==============================================================================
void MainComponent::timerCallback()
{
    if (! edit) return;
    // Garde-fou : si le moteur reconstruit son graphe audio au moment du démarrage, Tracktion peut arrêter
    // la lecture qu'on vient de lancer. Dans les 2 s qui suivent la demande, on la relance (3 essais au plus).
    if (playWanted)
    {
        auto& t = edit->getTransport();
        const auto elapsed = juce::Time::getMillisecondCounter() - playRequestMs;
        if (t.isPlaying() || t.isRecording()) { if (elapsed > 2000) playWanted = false; }
        else if (elapsed < 2000 && playRetries < 3)
        {
            ++playRetries;
            onde::trace ("garde-fou : lecture arrêtée par le moteur, relance");
            t.ensureContextAllocated();
            t.play (false);
        }
        else playWanted = false;
    }
    if ((juce::Time::getCurrentTime() - lastAutosave).inSeconds() >= 15.0) autosave();
    if (status.isNotEmpty() && (juce::Time::getCurrentTime() - statusTime).inSeconds() > 8.0) status = {};
    // Barre d'état : redessinée quand le message change, sinon deux fois par seconde (charge CPU).
    if (status != paintedStatus || ++statusTick % 12 == 0) { paintedStatus = status; repaint (0, getHeight() - statusH, getWidth(), statusH); }
}

juce::Rectangle<int> MainComponent::lowerHandle() const
{
    return lowerVisible ? juce::Rectangle<int> (lowerArea.getX(), lowerArea.getY(), lowerArea.getWidth(), 5) : juce::Rectangle<int>();
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (menuH));
    toolbar.setBounds (r.removeFromTop (toolbarH));
    infoLine.setVisible (infoVisible);
    if (infoVisible) infoLine.setBounds (r.removeFromTop (infoH));
    r.removeFromBottom (statusH);
    transport.setBounds (r.removeFromBottom (transportH));

    inspector.setVisible (inspectorVisible);
    if (inspectorVisible) inspector.setBounds (r.removeFromLeft (inspectorW));
    mediaRack.setVisible (rightVisible);
    if (rightVisible) mediaRack.setBounds (r.removeFromRight (rightW));

    lowerH = juce::jlimit (160, juce::jmax (160, r.getHeight() - 160), lowerH);
    lowerArea = lowerVisible ? r.removeFromBottom (lowerH) : juce::Rectangle<int>();
    centreArea = r;
    if (arrangement) arrangement->setBounds (centreArea);
    layoutLowerZone();
    if (palette.isVisible()) palette.setBounds (getLocalBounds());
}

void MainComponent::layoutLowerZone()
{
    for (auto* b : { &lowerMixTab, &lowerEditTab, &lowerAudioTab, &lowerClose }) b->setVisible (lowerVisible);
    if (mixer) mixer->setVisible (lowerVisible && lowerTab == 1);
    if (pianoRoll) pianoRoll->setVisible (lowerVisible && lowerTab == 2);
    if (audioEditor) audioEditor->setVisible (lowerVisible && lowerTab == 3);
    if (! lowerVisible) return;
    auto r = lowerArea.withTrimmedTop (5);
    auto tabs = r.removeFromTop (lowerTabsH).reduced (6, 2);
    lowerMixTab.setBounds (tabs.removeFromLeft (110)); tabs.removeFromLeft (4);
    lowerEditTab.setBounds (tabs.removeFromLeft (140)); tabs.removeFromLeft (4);
    lowerAudioTab.setBounds (tabs.removeFromLeft (120));
    lowerClose.setBounds (tabs.removeFromRight (26));
    if (mixer) mixer->setBounds (r);
    if (pianoRoll) pianoRoll->setBounds (r);
    if (audioEditor) audioEditor->setBounds (r);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    if (lowerVisible)
    {
        g.setColour (juce::Colour (0xff3a3e45)); g.fillRect (lowerHandle());
        g.setColour (theme::line); g.drawHorizontalLine (lowerArea.getY(), (float) lowerArea.getX(), (float) lowerArea.getRight());
        g.setColour (juce::Colour (0xff26292d)); g.fillRect (lowerArea.withTrimmedTop (5).withHeight (lowerTabsH));
    }

    // Ligne d'état.
    auto foot = getLocalBounds().removeFromBottom (statusH);
    g.setColour (juce::Colour (0xff222428)); g.fillRect (foot);
    g.setColour (theme::line); g.drawHorizontalLine (foot.getY(), 0.0f, (float) getWidth());
    auto& dm = engine.getDeviceManager();
    juce::String left = status.isNotEmpty() ? status
        : (dm.deviceManager.getCurrentAudioDevice() != nullptr
              ? dm.deviceManager.getCurrentAudioDevice()->getName() + U ("  ·  ") + juce::String (dm.getSampleRate() / 1000.0, 1) + " kHz  ·  "
                  + juce::String (dm.getBlockSize()) + U (" échantillons  ·  CPU ") + juce::String (juce::roundToInt (dm.getCpuUsage() * 100.0)) + " %"
              : U ("Aucune carte son active — Studio › Configuration du studio (Ctrl+,)"));
    g.setColour (status.isNotEmpty() ? theme::text : theme::muted); g.setFont (theme::ui (12.0f));
    g.drawText (left, foot.reduced (10, 0).withTrimmedRight (420), juce::Justification::centredLeft, true);
    g.setColour (catalog.isScanning() ? theme::warn : theme::muted);
    g.drawText (catalog.getStatus() + U ("   ·   Outil : ") + EditContext::toolName (ctx.tool) + (ctx.snapOn ? U ("   ·   Calage") : juce::String()),
                foot.reduced (10, 0).removeFromRight (520), juce::Justification::centredRight, true);
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (lowerHandle().expanded (0, 2).contains (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingLower = lowerHandle().expanded (0, 2).contains (e.getPosition());
    dragLowerStartH = lowerH;
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingLower) return;
    lowerH = dragLowerStartH - e.getDistanceFromDragStartY();
    resized();
}

void MainComponent::mouseUp (const juce::MouseEvent&) { draggingLower = false; }

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (onde::traceEnabled())
    {
        auto s = juce::String::formatted ("touche : code=%d car=%d mods=%d (%s)", k.getKeyCode(), (int) k.getTextCharacter(),
                                          k.getModifiers().getRawFlags(), k.getTextDescription().toRawUTF8());
        onde::trace (s.toRawUTF8());
    }
    if (palette.isVisible()) return false;
    return ctx.performShortcut (k);
}

void MainComponent::markCleanWhenSettled()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    te::Edit* e = edit.get();
    // Les plugins et le minuteur d'annulation de Tracktion marquent le projet « modifié » jusqu'à ~1 s après
    // sa création. Sans clic de l'utilisateur entre-temps, on remet l'état « non modifié » (à 0,6 puis 2 s).
    const auto clicks = juce::Desktop::getInstance().getMouseButtonClickCounter();
    for (int ms : { 600, 2000 })
        juce::Timer::callAfterDelay (ms, [safe, e, clicks]
        {
            if (safe == nullptr || safe->edit.get() != e || e == nullptr) return;
            if (juce::Desktop::getInstance().getMouseButtonClickCounter() != clicks) return;
            e->resetChangedStatus();
            if (safe->onTitleChanged) safe->onTitleChanged (safe->projectName());
        });
}
