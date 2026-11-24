// Fenêtre de projet : liste des pistes, règle avec cycle, événements éditables avec les outils,
// pistes d'automation, glisser-déposer de fichiers audio.
#pragma once
#include "EditContext.h"

class ArrangementView  : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer,
                         private juce::ChangeListener,
                         private juce::ScrollBar::Listener,
                         private juce::ValueTree::Listener
{
public:
    explicit ArrangementView (EditContext&);
    ~ArrangementView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    void zoom (double factor, double anchorSeconds);
    void zoomToFit();
    float timeToX (double seconds) const;
    double xToTime (float x) const;
    double snapTime (double seconds, const juce::ModifierKeys&) const;

    static constexpr int headerW = 250, rulerH = 48, cycleH = 13, markerTop = 14, markerH = 14, defaultRowH = 64, autoLaneH = 60, bannerH = 54, scrollH = 12, minRowH = 40;

    class TrackRow;
    class RowsHolder;
    class TempoRow;
    class FolderRow;
    static constexpr int tempoRowH = 76, folderRowH = 44, indentW = 14;

    // Glisser-déposer des en-têtes de piste (réordonner, ranger dans un dossier).
    void beginTrackDrag (te::Track&);
    void updateTrackDrag (juce::Point<int> holderPos);
    void endTrackDrag();

    // Sélection de plage (outil 2) : affichée sur toutes les pistes.
    juce::Range<double> rangeSelection;
    juce::Rectangle<int> rubber;                    // rectangle de sélection (coordonnées des pistes)

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    // Redessin ciblé : les pistes ne sont redessinées que si le projet change (arbre de l'edit, contexte)
    // ou pendant un enregistrement ; la tête de lecture ne redessine que deux bandes étroites.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override         { contentDirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override  { contentDirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override          { contentDirty = true; }
    void watchEdit();
    juce::ValueTree watchedState;
    te::Edit* watchedEdit = nullptr;
    bool contentDirty = true;
    float lastCursorX = -1.0f;
    double lastViewStart = -1.0, lastPxPerSecond = -1.0;
    void rebuildRows();
    void layoutRows();
    void updateScrollBar();
    void showAddTrackMenu();
    juce::String trackSignature() const;
    int rowHeightFor (const te::Track&) const;

    friend class TrackRow;
    EditContext& ctx;
    double viewStart = 0.0, pxPerSecond = 60.0, lastFollowPos = -1.0;
    std::unique_ptr<RowsHolder> holder;
    juce::Viewport viewport;
    juce::ScrollBar hbar { false };
    juce::OwnedArray<TrackRow> rows;
    juce::OwnedArray<FolderRow> folderRows;
    struct Entry { juce::Component* comp; te::Track* track; bool isFolder; };
    juce::Array<Entry> entries;                      // ordre d'affichage (dossiers dépliés, pistes)
    std::unique_ptr<TempoRow> tempoRow;
    te::EditItemID draggedTrack;
    std::optional<te::TrackInsertPoint> dropPoint;
    int dropY = -1;
    te::EditItemID dropIntoFolder;
    juce::TextButton addBtn { "+" };
    juce::String lastSignature;
    std::map<juce::String, int> rowHeights;
    int frame = 0;

    te::MarkerClip* markerAt (int x) const;
    void showMarkerMenu (te::MarkerClip&);
    enum class RulerDrag { none, cursor, newCycle, moveL, moveR, moveCycle, marker } rulerDrag = RulerDrag::none;
    te::EditItemID dragMarker;
    double rulerAnchor = 0.0;
    te::TimeRange rulerStartRange;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
