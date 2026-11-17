// Plugin Tracktion qui délègue le traitement audio à un processus isolé (PluginSandbox).
// spec = "vst3:<chemin>#<nom>" pour un vrai VST3, ou une spec de démonstration ("synth-pad"…).
#pragma once
#include <JuceHeader.h>
#include "onde/PluginSandbox.h"

namespace te = tracktion;

class SandboxPlugin  : public te::Plugin
{
public:
    static const char* getPluginName()   { return "Plugin isole"; }
    static const char* xmlTypeName;

    explicit SandboxPlugin (te::PluginCreationInfo);
    ~SandboxPlugin() override;

    juce::String getName() const override                    { return displayName.get(); }
    juce::String getPluginType() override                    { return xmlTypeName; }
    juce::String getSelectableDescription() override         { return getName(); }
    bool isSynth() override;
    bool takesMidiInput() override                           { return true; }
    bool takesAudioInput() override                          { return ! isSynth(); }
    bool producesAudioWhenNoAudioInput() override            { return true; }
    BusLayout getBusses() const override                     { return BusLayout::singlePassThrough(); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;
    void flushPluginStateToValueTree() override;              // préréglage du VST3 -> fichier du projet

    onde::PluginSandbox* getSandbox()                        { return sandbox.get(); }
    bool isFailed() const;
    juce::String describeFailure() const;
    void restartSandbox();                                   // relance + recharge le dernier préréglage connu
    void showEditor();
    void hideEditor();
    bool isEditorVisible() const;
    bool hasEditor() const;
    double getLatencySeconds() override;                     // compensation de latence (PDC)
    void simulateCrash();                                    // démo uniquement

    static juce::File getWorkerFile();
    static juce::String specForVST3 (const juce::String& path, const juce::String& name) { return "vst3:" + path + "#" + name; }

    juce::CachedValue<juce::String> spec, displayName, savedState;

private:
    void ensureSandbox (double sampleRate, int blockSize);
    std::unique_ptr<onde::PluginSandbox> sandbox;
    double preparedRate = 0;
    std::vector<onde::MidiEvent> midiScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandboxPlugin)
};
