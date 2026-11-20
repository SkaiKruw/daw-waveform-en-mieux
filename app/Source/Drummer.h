// Batteur Onde : une région de batterie (conteneur MIDI) qui se joue toute seule, d'après quelques réglages :
// style, simple / complexe, doux / fort, swing, jeu humain, remplissages. Les notes sont regénérées dès qu'un réglage
// change ; une même variation redonne toujours le même jeu. Notes General MIDI (kit d'Onde Synth).
#pragma once
#include "EditContext.h"

namespace drummer
{
    struct Settings
    {
        int style = 0;                 // voir styleNames
        float complexity = 0.4f;       // 0 simple … 1 complexe
        float intensity = 0.6f;        // 0 doux … 1 fort
        float swing = 0.0f;            // 0 … 1 (doubles croches décalées)
        float humanize = 0.5f;         // 0 machine … 1 très humain
        int fills = 1;                 // 0 aucun, 1 toutes les 4 mesures, 2 toutes les 8
        bool kick = true, snare = true, cymbals = true, percussion = true;
        int variation = 1;
    };
    constexpr int numStyles = 6;
    extern const char* const styleNames[numStyles];
    extern const char* const styleDescriptions[numStyles];

    bool isDrummerRegion (const te::Clip*);
    Settings read (const te::MidiClip&);
    // Écrit les réglages dans la région et regénère ses notes (annulable : transaction de l'appelant).
    void apply (te::MidiClip&, const Settings&);
    // Notes d'une région (sans la modifier) : utilisé par apply() et par les tests.
    struct Hit { double beat; int note; int velocity; double length; };
    std::vector<Hit> generate (const Settings&, int bars, int beatsPerBar);

    // Nouvelle piste « Batteur » (Onde Synth, kit acoustique) avec une région de 8 mesures au début du projet.
    te::MidiClip* addDrummerTrack (EditContext&, int bars = 8);
}
