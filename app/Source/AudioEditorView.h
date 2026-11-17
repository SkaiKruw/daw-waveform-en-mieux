// Éditeur audio (zone inférieure) : forme d'onde de l'événement audio sélectionné, sélection de plage,
// gain, normalisation, fondus, inversion, transposition, rognage et suppression d'une plage.
#pragma once
#include "EditContext.h"

class AudioEditorView  : public juce::Component,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    explicit AudioEditorView (EditContext&);
    ~AudioEditorView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Traitements (aussi utilisés par les tests et les actions du menu Audio).
    static juce::File sourceFileOf (te::WaveAudioClip&);
    static float peakOf (te::WaveAudioClip&);                     // crête de la partie jouée (0..1+)
    static bool normalise (te::WaveAudioClip&, float targetDb = -0.1f);
    bool trimToSelection();
    bool deleteSelection();
    void setSelection (juce::Range<double> fileSeconds) { selection = fileSeconds; repaint(); }
    juce::Range<double> getSelection() const            { return selection; }

    static constexpr int toolbarH = 34, rulerH = 20;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    te::WaveAudioClip* clip() const;
    void frameClip();
    juce::Rectangle<int> waveArea() const;
    float fileTimeToX (double t) const;
    double xToFileTime (float x) const;
    double clipFileStart() const;                                 // début de la partie jouée, en temps du fichier
    double clipFileEnd() const;
    void syncControls();

    EditContext& ctx;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbCache { 8 };
    juce::AudioThumbnail thumb { 256, formats, thumbCache };
    juce::File shownFile;
    te::EditItemID framedClip;
    double viewStart = 0.0, viewLength = 10.0, fileLength = 0.0;
    juce::Range<double> selection;

    juce::Slider gain, pitch;
    juce::Label gainLabel, pitchLabel;
    juce::TextButton normaliseBtn { "Normaliser" }, reverseBtn { "Inverser" }, trimBtn { U ("Rogner à la sélection") },
                     deleteBtn { U ("Supprimer la sélection") }, fitBtn { U ("Tout voir") };

    enum class Drag { none, select, fadeIn, fadeOut } drag = Drag::none;
    double dragAnchor = 0.0;
};
