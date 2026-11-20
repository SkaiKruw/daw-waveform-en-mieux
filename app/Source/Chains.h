// Chaînes d'effets prêtes (« Voix pop », « Guitare saturée »…) et commandes rapides : quelques gros boutons
// par piste (« Présence », « Espace », « Punch »…) qui règlent plusieurs paramètres à la fois. Les commandes sont
// des macros du moteur : automatisables, enregistrées avec le projet, annulables.
#pragma once
#include <JuceHeader.h>

namespace te = tracktion;

namespace chains
{
    struct Step   { const char* type; std::vector<std::pair<const char*, float>> values; };
    struct Target { const char* type; const char* param; float depth; };     // profondeur en part de la course (±)
    struct Macro  { const char* name; std::vector<Target> targets; };
    struct Chain  { const char* category; const char* name; const char* description; std::vector<Step> steps; std::vector<Macro> macros; };

    const std::vector<Chain>& all();
    const Chain* find (const juce::String& name);
    juce::String currentChain (te::AudioTrack&);                 // nom de la chaîne posée, vide sinon

    // Remplace la chaîne précédente (seulement les effets qu'elle avait posés ; les effets ajoutés à la main
    // restent), puis refait les commandes rapides. Annulable (transaction de l'appelant). nullptr = retirer.
    void apply (te::AudioTrack&, const Chain*);

    // Commandes rapides de la piste : celles de sa chaîne et celles de son instrument Onde Synth (selon le moteur).
    void rebuildMacros (te::AudioTrack&);
    juce::ReferenceCountedArray<te::MacroParameter> quickControls (te::AudioTrack&);
}
