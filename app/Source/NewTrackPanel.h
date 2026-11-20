// « Nouvelle piste » (bouton + de la liste des pistes, Projet › Nouvelle piste…) : de grandes cartes pour dire ce
// qu'on veut faire — jouer d'un instrument, un batteur, chanter, brancher une guitare — et la piste arrive prête
// (son, chaîne d'effets, armement).
#pragma once
#include "EditContext.h"

class NewTrackPanel  : public juce::Component
{
public:
    explicit NewTrackPanel (EditContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override   { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    static juce::DialogWindow* show (EditContext&);

    enum Kind { instrument, drums, voice, guitar, vst3, audio, numKinds };
    // Crée la piste du type choisi (annulable). Renvoie la piste créée (nullptr si rien).
    static te::AudioTrack* create (EditContext&, Kind);

private:
    juce::Rectangle<int> cardBounds (int i) const;
    void choose (int i);
    EditContext& ctx;
    int hover = -1;
    juce::TextButton fxBtn { "Piste FX" }, folderBtn { "Dossier" }, groupBtn { "Groupe (sous-mixage)" };
};
