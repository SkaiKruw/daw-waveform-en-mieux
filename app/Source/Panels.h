// Panneaux de la fenêtre de projet : barre d'outils, ligne d'infos, inspecteur, rack média, barre de transport.
#pragma once
#include "EditContext.h"

//==============================================================================
// Bouton à icône dessinée (outils, transport, zones).
class IconButton  : public juce::Button
{
public:
    using Painter = std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;
    IconButton (const juce::String& tip, Painter p, juce::Colour onColour = theme::accent)
        : juce::Button (tip), painter (std::move (p)), on (onColour)
    {
        setTooltip (tip); setTitle (tip); setWantsKeyboardFocus (false);
    }
    bool active = false;                    // allumé (indépendamment du clic)
    bool flat = false;                      // sans fond quand éteint
    void setActive (bool a)                 { if (a != active) { active = a; repaint(); } }   // redessin seulement si l'état change
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    Painter painter;
    juce::Colour on;
};

namespace icons
{
    IconButton::Painter tool (Tool);
    IconButton::Painter toStart();
    IconButton::Painter toEnd();
    IconButton::Painter rewind();
    IconButton::Painter forward();
    IconButton::Painter cycle();
    IconButton::Painter stop();
    IconButton::Painter play();
    IconButton::Painter record();
    IconButton::Painter zone (int which);  // 0 gauche, 1 bas, 2 droite
    IconButton::Painter magnet();
    IconButton::Painter metronome();
    IconButton::Painter punch (bool in);
}

//==============================================================================
// Lecture sûre du niveau de la sortie master : le contexte de lecture de Tracktion est détruit puis recréé
// (export, changement de carte son) ; on suit ces changements pour ne jamais toucher un vumètre disparu.
class MasterLevelTap  : private te::TransportControl::Listener
{
public:
    explicit MasterLevelTap (EditContext& c) : ctx (c) {}
    ~MasterLevelTap() override { detach(); }
    void detach();
    std::pair<float, float> read();                 // dB gauche / droite depuis la dernière lecture
private:
    void playbackContextChanged() override;
    EditContext& ctx;
    te::Edit* listened = nullptr;
    te::EditPlaybackContext* context = nullptr;
    te::LevelMeasurer::Client client;
};

//==============================================================================
// Vumètre stéréo (vertical ou horizontal) avec maintien de crête.
class StereoMeter  : public juce::Component
{
public:
    explicit StereoMeter (bool vertical = true) : isVertical (vertical) {}
    void setLevels (float leftDb, float rightDb);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { peak[0] = peak[1] = -100.0f; repaint(); }
    float getPeak() const { return juce::jmax (peak[0], peak[1]); }
private:
    bool isVertical;
    float levels[2] { -100.0f, -100.0f }, peak[2] { -100.0f, -100.0f };
    int drawn[4] { -1, -1, -1, -1 };          // dernières hauteurs dessinées (pixels)
};

//==============================================================================
class ToolbarPanel  : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit ToolbarPanel (EditContext&);
    ~ToolbarPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<bool (int)> isZoneVisible;
    std::function<juce::String()> getProjectTitle;
    std::function<int()> remoteState;                // 0 coupée, 1 active, 2 téléphone connecté
    void paintOverChildren (juce::Graphics&) override;
private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncTools(); }
    void syncTools();
    EditContext& ctx;
    juce::OwnedArray<IconButton> zoneBtns, transportBtns, toolBtns;
    std::unique_ptr<IconButton> snapBtn;
    std::unique_ptr<IconButton> remoteBtn;
    int shownRemoteState = -1;
    juce::ComboBox gridBox, quantBox;
    juce::TextButton searchBtn { U ("Rechercher  Ctrl+K") };
    int toolsX = 0, snapX = 0, titleX = 0;
    int tick = 0;
};

//==============================================================================
// Ligne d'infos : l'événement sélectionné, éditable.
class InfoLine  : public juce::Component, private juce::Timer
{
public:
    explicit InfoLine (EditContext&);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    void setupField (juce::Label&);
    double parseBars (const juce::String&, bool isLength) const;
    EditContext& ctx;
    juce::Label name, start, end, length, offset;
    juce::ToggleButton muteToggle { U ("Muet") };
    te::EditItemID shown;
};

//==============================================================================
class Inspector  : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit Inspector (EditContext&);
    ~Inspector() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void rebuild();
    void showSlotMenu (int slot, juce::Component* target);
    void showInstrumentMenu();
    void syncValues();
    EditContext& ctx;
    te::EditItemID trackID;
    juce::String signature;
    juce::Label name;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" }, armBtn { U ("●") }, monBtn { U ("◉") }, readBtn { "R" }, writeBtn { "W" }, autoBtn { U ("Automation") };
    juce::TextButton instrumentBtn;
    juce::ComboBox chainBox;                         // chaîne d'effets prête
    juce::ComboBox inputBox;                         // entrée audio de la piste (prise de la carte son)
    juce::StringArray shownInputs;
    int inputY = -1;
    juce::OwnedArray<juce::Component> quickKnobs;    // commandes rapides (macros)
    int quickY = -1, ticks = 0;
    juce::Slider volume, pan;
    juce::Label volLabel, panLabel;
    juce::OwnedArray<juce::TextButton> slots;
    juce::OwnedArray<juce::TextButton> bypass;
    juce::OwnedArray<juce::Slider> sendSliders;
    juce::OwnedArray<juce::Label> sendLabels;
    juce::Array<int> sendBuses;
    juce::TextButton addFxBtn { U ("+ Piste FX") };
    int sendsY = 0;
    StereoMeter meter { false };
    te::LevelMeasurer* measurer = nullptr;
    te::Plugin::Ptr meterPlugin;                     // garde le vumètre en vie tant qu'on y est abonné
    te::LevelMeasurer::Client client;
    juce::Rectangle<int> colourSwatch;
public:
    void detachMeters();                             // avant de fermer le projet
private:
    int insertsY = 0, instrumentY = 0, faderY = 0;
    te::AudioTrack* track() const;
};

//==============================================================================
// Zone droite : instruments, effets, fichiers audio (double-clic pour utiliser).
class MediaRack  : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit MediaRack (EditContext&);
    ~MediaRack() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    int getNumRows() override { return items.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refill(); }
    void refill();
    void useItem (int row);
    struct Item { juce::String title, sub; PluginEntry entry; juce::File file; bool isHeader = false; };
    EditContext& ctx;
    juce::TextButton tabInst { "Instruments" }, tabFx { "Effets" }, tabFiles { U ("Médias") };
    int tab = 0;
    juce::TextEditor filter;
    juce::ListBox list;
    juce::TextButton useBtn { U ("Utiliser") };
    juce::Array<Item> items;
};

//==============================================================================
class TransportPanel  : public juce::Component, private juce::Timer
{
public:
    explicit TransportPanel (EditContext&);
    ~TransportPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void detachMeters()  { master.detach(); }
private:
    void timerCallback() override;
    EditContext& ctx;
    juce::OwnedArray<IconButton> buttons;
    std::unique_ptr<IconButton> clickBtn, punchInBtn, punchOutBtn;
    StereoMeter outMeter { false };
    MasterLevelTap master { ctx };
    juce::Rectangle<int> perfArea, locL, locR, display, tempoArea, sigArea;
    int tick = 0;
    double shownPosition = -1.0;
    double dragTempo = 0.0;
    bool draggingTempo = false;
};
