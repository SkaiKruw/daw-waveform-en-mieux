// Projet de démonstration « Nuits du Nord » : 8 mesures à 92 BPM, Ré mineur.
#pragma once
#include <JuceHeader.h>
#include "SandboxPlugin.h"
#include "PluginCatalog.h"

namespace te = tracktion;

struct DemoProject
{
    std::unique_ptr<te::Edit> edit;
    SandboxPlugin* pad = nullptr;      // plugin isolé de la piste « Pad »
    te::AudioTrack* voice = nullptr;   // piste prête à enregistrer
};

namespace demo
{
    constexpr double bpm = 92.0;
    constexpr int bars = 8;
    inline double barSeconds()  { return 4.0 * 60.0 / bpm; }
    inline double songSeconds() { return bars * barSeconds(); }

    // padSpec : spec du worker, ex. "synth-pad" ou "synth-pad,crash-after:900".
    // vst3Synth : si fourni, une piste « Synthé VST3 » joue ce VST3 isolé.
    DemoProject create (te::Engine&, const juce::File& editFile, const juce::String& padSpec, const PluginEntry* vst3Synth = nullptr);
    void routeInputsToVoiceTrack (DemoProject&);
}
