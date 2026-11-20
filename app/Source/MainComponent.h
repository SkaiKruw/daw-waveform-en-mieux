// Fenêtre de projet d'Onde : menus, barre d'outils, ligne d'infos, inspecteur, pistes, zone inférieure
// (MixConsole / éditeur de notes), zone droite (médias), barre de transport.
#pragma once
#include "EditContext.h"
#include "ArrangementView.h"
#include "MixerView.h"
#include "PianoRollView.h"
#include "AudioEditorView.h"
#include "Exporter.h"
#include "StemSeparator.h"
#include "RemoteControl.h"
#include "RemotePanel.h"
#include "AboutPanel.h"
#include "HubPanel.h"
#include "HistoryPanel.h"
#include "ModulatorPanel.h"
#include "OndeEffects.h"
#include "CommandPalette.h"
#include "Panels.h"

class MainComponent  : public juce::Component,
                       public juce::MenuBarModel,
                       private juce::Timer,
                       private juce::ChangeListener,
                       private juce::FocusChangeListener
{
public:
    MainComponent (te::Engine&, PluginCatalog&);
    ~MainComponent() override;

    void setEdit (std::unique_ptr<te::Edit>, const juce::File& projectFile, bool untitled);
    te::Edit* getEdit() { return edit.get(); }
    EditContext& getContext() { return ctx; }
    void showView (int);                            // 0 projet, 1 MixConsole, 2 éditeur de notes, 3 éditeur audio
    AudioEditorView* getAudioEditor() { return audioEditor.get(); }
    RemoteControl& getRemote() { return remote; }
    void openPalette (const juce::String& query = {});
    void closePalette() { palette.close(); }

    void newProject();
    // Modèles de projet de l'écran d'accueil : 0 vide, 1 enregistrement, 2 production.
    void newProjectFromTemplate (int kind);
    std::function<void()> onOpenDemo;                 // projet de démonstration (fourni par l'application)
    static juce::StringArray recentProjects();        // projets récents existants, du plus récent au plus ancien
    static void rememberProject (const juce::File&);
    static inline bool testMode = false;              // modes --xxx-test : liste de projets récents séparée
    void showHub();
    // Après création ou ouverture : les plugins écrivent leur état juste après ; sans action de l'utilisateur
    // (historique vide), le projet doit rester « non modifié » (pas de question à la fermeture).
    void markCleanWhenSettled();
    void openProject();
    void openProjectFile (const juce::File&);
    void saveProject (std::function<void (bool)> done = nullptr);
    void saveProjectAs (std::function<void (bool)> done = nullptr);
    bool saveProjectTo (const juce::File&);
    void exportWav();
    bool exportWavTo (const juce::File&);
    void importAudio();
    void showAudioSettings();
    bool autosave();                                // renvoie true si quelque chose a été écrit

    static juce::File recoveryFile();
    static void installPathResolver (te::Edit&);
    static std::unique_ptr<te::Edit> loadProject (te::Engine&, const juce::File&);
    static juce::File projectsFolder();
    std::function<void (const juce::String&)> onTitleChanged;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void globalFocusChanged (juce::Component*) override;
    void registerActions();
    void setStatus (const juce::String&);
    void openPluginEditor (te::Plugin&, juce::Component*);
    void splitSelectedClip();
    void deleteSelectedClip();
    void duplicateSelectedClip();
    void quantiseSelectedClip();
    void glueSelectedClip();
    void pasteClip();
    int consolidateMedia (const juce::File& projectTarget);
    void copySelection();
    struct ClipboardItem { juce::ValueTree state; te::EditItemID track; double offset = 0; };
    juce::Array<ClipboardItem> clipboard;
    void deleteSelectedTrack();
    void changeTempo();
    void simulateCrash();
    void toggleRecord();
    void showShortcuts();
    void setZone (int which, bool visible);
    bool isZoneVisible (int which) const;
    void layoutLowerZone();
    juce::String projectName() const;
    juce::Rectangle<int> lowerHandle() const;

    te::Engine& engine;
    PluginCatalog& catalog;
    EditContext ctx;
    std::unique_ptr<te::Edit> edit;
    juce::File projectFile;
    bool untitled = true;
    double playStartPosition = 0.0;
    bool playWanted = false;
    juce::uint32 playRequestMs = 0;
    int playRetries = 0;

    juce::MenuBarComponent menuBar { this };
    ToolbarPanel toolbar { ctx };
    InfoLine infoLine { ctx };
    Inspector inspector { ctx };
    MediaRack mediaRack { ctx };
    TransportPanel transport { ctx };

    std::unique_ptr<ArrangementView> arrangement;
    std::unique_ptr<MixerView> mixer;
    std::unique_ptr<PianoRollView> pianoRoll;
    std::unique_ptr<AudioEditorView> audioEditor;
    int lowerTab = 1;                               // 1 MixConsole, 2 éditeur de notes, 3 éditeur audio
    bool inspectorVisible = true, lowerVisible = false, rightVisible = false, infoVisible = true;
    int lowerH = 420, dragLowerStartH = 0;
    bool draggingLower = false;
    juce::TextButton lowerMixTab { "MixConsole" }, lowerEditTab { U ("Éditeur de notes") }, lowerAudioTab { U ("Éditeur audio") }, lowerClose { U ("×") };
    juce::Rectangle<int> lowerArea, centreArea;

    CommandPalette palette { ctx };
    RemoteControl remote { ctx };                       // télécommande depuis un téléphone
    juce::Component::SafePointer<juce::DialogWindow> remoteWindow;
    juce::Component::SafePointer<juce::DialogWindow> hubWindow, historyWindow, modulatorWindow;
    std::map<juce::String, juce::Component::SafePointer<juce::DialogWindow>> effectWindows;   // fenêtres des effets Onde
    juce::String status;
    juce::String paintedStatus;
    int statusTick = 0;
    juce::Time statusTime, lastAutosave;
    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int menuH = 24, toolbarH = 40, infoH = 26, transportH = 54, statusH = 22, lowerTabsH = 26,
                         inspectorW = 240, rightW = 270;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
