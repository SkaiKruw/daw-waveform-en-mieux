// État partagé par toutes les vues : projet ouvert, sélection, outils, grille, catalogue, actions.
#pragma once
#include <JuceHeader.h>
#include "PluginCatalog.h"
#include "SandboxPlugin.h"
#include "Theme.h"
#include "onde/Trace.h"

namespace te = tracktion;

inline juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

struct Action
{
    juce::String id, name, category;
    juce::KeyPress key;
    juce::Array<juce::KeyPress> extraKeys;          // raccourcis secondaires (pavé numérique…)
    bool separatorBefore = false;                   // séparateur avant l'entrée dans les menus
    std::function<void()> perform;
    std::function<bool()> isTicked;                 // coche dans les menus (facultatif)
    juce::String keyText() const;
};

// Outils de la fenêtre de projet (touches 1 à 8).
enum class Tool { select = 0, range, split, glue, erase, zoom, mute, draw };

class EditContext  : public juce::ChangeBroadcaster
{
public:
    EditContext (te::Engine& e, PluginCatalog& c) : engine (e), catalog (c) {}

    te::Engine& engine;
    PluginCatalog& catalog;
    te::Edit* edit = nullptr;

    // Sélection (par identifiant : survit aux annulations).
    te::EditItemID selectedTrackID, selectedClipID;
    te::AudioTrack* getSelectedTrack() const;
    te::Clip* getSelectedClip() const;
    te::MidiClip* getSelectedMidiClip() const { return dynamic_cast<te::MidiClip*> (getSelectedClip()); }
    void selectTrack (te::AudioTrack*);
    void selectClip (te::Clip*);                    // sélection simple (remplace la sélection multiple)

    // Sélection multiple d'événements (Maj/Ctrl + clic, rectangle, Ctrl+A).
    juce::Array<te::EditItemID> selectedClips;
    bool isClipSelected (const te::Clip& c) const   { return selectedClips.contains (c.itemID); }
    void toggleClipSelection (te::Clip*);
    void setClipSelection (const juce::Array<te::Clip*>&);
    juce::Array<te::Clip*> getSelectedClips() const;
    int trackNumber (const te::Track&) const;

    // Outils, aimant et grille.
    Tool tool = Tool::select;
    bool snapOn = true;
    double gridBeats = 1.0;                         // 4 = mesure, 1 = noire, 0.5 = croche, 0.25 = double croche
    double quantizeBeats = 0.25;
    void setTool (Tool t)                           { tool = t; changed(); }
    static juce::String toolName (Tool);

    // Mesures et temps (tiennent compte de la piste de tempo).
    double toBeats (double seconds) const;
    double toSeconds (double beats) const;
    double bpmAt (double seconds) const;
    double barStart (double seconds) const;         // début de la mesure contenant ce temps
    double addBars (double seconds, double bars) const;
    bool hasTempoChanges() const;
    bool tempoTrackShown = false;
    double beatSeconds() const;                     // durée d'un temps au tempo initial
    int beatsPerBar() const;
    double barSeconds() const   { return beatSeconds() * beatsPerBar(); }
    double bpm() const;
    double snap (double seconds, double beatsPerStep = 1.0) const;
    double snapToGrid (double seconds, bool force = false) const;   // respecte l'aimant
    juce::String formatBars (double seconds, bool withTicks = true) const;   // « 5. 2. 3. 0 »
    static juce::String formatTime (double seconds);                         // « 0:00:05.217 »

    // Pistes : plugins, entrées, enregistrement, moniteur.
    juce::Array<SandboxPlugin*> getSandboxPlugins (te::AudioTrack&) const;
    te::Plugin* getInstrument (te::AudioTrack&) const;
    static bool isUserPlugin (te::Plugin*);
    juce::Array<te::Plugin*> getInserts (te::AudioTrack&) const;     // effets (sans l'instrument, le volume, le vumètre)
    void routeInputsTo (te::AudioTrack*);           // entrées MIDI + audio vers la piste sélectionnée
    void setTrackArmed (te::AudioTrack&, bool);
    bool isTrackArmed (te::AudioTrack&) const;
    void setTrackMonitoring (te::AudioTrack&, bool);
    bool isTrackMonitoring (te::AudioTrack&) const;

    // Voies d'automation affichées (volume, panoramique).
    std::set<juce::String> automationShown;
    bool isAutomationShown (const te::Track& t) const { return automationShown.count (t.itemID.toString()) > 0; }
    void toggleAutomation (const te::Track& t);

    // Ajout de pistes.
    te::AudioTrack* addAudioTrack();
    te::AudioTrack* addInstrumentTrack (const PluginEntry* instrument = nullptr);

    // Dossiers de pistes (et pistes de groupe = dossiers de sous-mixage).
    te::EditItemID selectedFolderID;
    te::FolderTrack* getSelectedFolder() const;
    void selectFolder (te::FolderTrack*);
    te::FolderTrack* addFolderTrack (bool submix, bool moveSelectedTrackInside);
    void moveTrackIntoFolder (te::Track&, te::FolderTrack&);
    void moveTrackOutOfFolder (te::Track&);
    static int depthOf (const te::Track&);
    // Repli d'un dossier : propriété de la piste, donc enregistré avec le projet (hors annulation, comme Cubase).
    static bool isCollapsed (const te::Track& f)    { return f.state.getProperty ("ondeCollapsed", false); }
    static void expand (te::Track& f)               { f.state.removeProperty ("ondeCollapsed", nullptr); }
    void toggleCollapsed (const te::Track& f);
    te::TrackInsertPoint insertPointForNewTrack() const;

    // Pistes FX (bus auxiliaires) : un retour d'effet par piste FX, des envois post-fader depuis les autres pistes.
    te::AudioTrack* addFxTrack (const PluginEntry* effect = nullptr);
    static int fxBusOf (te::AudioTrack&);           // -1 : pas une piste FX
    juce::Array<te::AudioTrack*> getFxTracks() const;
    te::AuxSendPlugin* getSend (te::AudioTrack&, int bus) const;
    te::AuxSendPlugin* ensureSend (te::AudioTrack&, int bus);

    // Avant un export : prépare les fichiers intermédiaires des événements inversés / transposés.
    // Renvoie false si l'attente a dépassé timeoutMs.
    static bool prepareAudioProxies (te::Edit&, int timeoutMs = 60000);
    // Ferme un projet sans risque de blocage : le callback audio est détaché (ce qui attend la fin du bloc
    // en cours) avant que Tracktion arrête ses threads de calcul, puis rebranché.
    static void destroyEdit (std::unique_ptr<te::Edit>&);
    // Modifie le tempo en gardant tout calé sur les mesures (comme Cubase) : conteneurs MIDI, événements audio
    // (début ; en mode musical, aussi la durée, étirée), délimiteurs et automation sont recalés.
    void changeTempo (const std::function<void()>& change);
    // Mode musical : l'événement audio suit le tempo (étirement temporel). Le tempo d'origine est celui du
    // projet au début de l'événement au moment de l'activation.
    static void setMusicalMode (te::WaveAudioClip&, bool);
    // Vumètre d'une piste de groupe (après son volume) : ajouté à la création et à l'ouverture des anciens projets.
    static te::LevelMeterPlugin* groupMeter (te::FolderTrack&, bool createIfMissing);

    // Actions (menus, palette Ctrl+K, raccourcis).
    juce::OwnedArray<Action> actions;
    Action* addAction (const juce::String& id, const juce::String& name, const juce::String& category,
                       juce::KeyPress key, std::function<void()> fn);
    Action* findAction (const juce::String& id) const;
    bool perform (const juce::String& id) const     { if (auto* a = findAction (id)) { a->perform(); return true; } return false; }
    bool performShortcut (const juce::KeyPress&);
    juce::KeyPress lastShortcut;
    juce::uint32 lastShortcutMs = 0;

    std::function<void (int)> showView;             // 0 projet, 1 MixConsole, 2 éditeur de notes (zone inférieure)
    std::function<void (const juce::String&)> setStatus;
    std::function<void (te::Plugin&, juce::Component*)> openPluginEditor;

    void changed() { sendChangeMessage(); }
};

// Bouton plat au style du thème.
inline void styleButton (juce::TextButton& b, bool accent = false)
{
    b.setColour (juce::TextButton::buttonColourId, accent ? theme::accentBg : theme::panel2);
    b.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    b.setColour (juce::TextButton::textColourOffId, accent ? theme::accent : theme::text);
    b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0c1620));
    b.setColour (juce::ComboBox::outlineColourId, theme::line);
    b.setWantsKeyboardFocus (false);
}

// Petit bouton d'état de piste (M, S, R, moniteur, R/W d'automation).
inline void styleStateButton (juce::TextButton& b, juce::Colour on, juce::Colour textOn = juce::Colour (0xff111214))
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonColourId, theme::panel2);
    b.setColour (juce::TextButton::buttonOnColourId, on);
    b.setColour (juce::TextButton::textColourOffId, theme::muted);
    b.setColour (juce::TextButton::textColourOnId, textOn);
    b.setWantsKeyboardFocus (false);
}
