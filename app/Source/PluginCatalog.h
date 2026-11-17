// Catalogue des plugins : effets intégrés de Tracktion + VST3 trouvés par une analyse isolée.
#pragma once
#include <JuceHeader.h>

namespace te = tracktion;

struct PluginEntry
{
    juce::String name, manufacturer, path, builtInType;   // builtInType non vide = plugin intégré
    bool isInstrument = false;
    bool isVST3() const { return builtInType.isEmpty(); }
};

class PluginCatalog  : public juce::ChangeBroadcaster,
                       private juce::Thread
{
public:
    PluginCatalog();
    ~PluginCatalog() override;

    juce::Array<PluginEntry> getBuiltIns (bool instruments) const;
    juce::Array<PluginEntry> getVST3 (bool instruments) const;

    void startScan();                                 // en arrière-plan ; ChangeBroadcaster à chaque avancée
    bool isScanning() const                           { return isThreadRunning(); }
    juce::String getStatus() const;
    juce::StringArray getFailures() const;

    static juce::Array<juce::File> getSearchFolders();

    // Crée le plugin dans la liste de la piste (instrument en tête, effets avant le volume).
    static te::Plugin::Ptr addToTrack (te::AudioTrack&, const PluginEntry&);

private:
    void run() override;
    void load();
    void save() const;
    juce::File cacheFile() const;

    mutable juce::CriticalSection lock;
    juce::Array<PluginEntry> vst3;
    juce::StringArray failures;
    juce::String status;
};
