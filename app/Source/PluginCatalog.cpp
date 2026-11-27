#include "PluginCatalog.h"
#include "OndeEffects.h"
#include "OndeSynth.h"
#include "OndeFx2.h"
#include "OndeSampler.h"
#include "Pedalboard.h"
#include "SandboxPlugin.h"

PluginCatalog::PluginCatalog() : juce::Thread ("Analyse des plugins")
{
    load();
}

PluginCatalog::~PluginCatalog()
{
    stopThread (25000);
}

juce::File PluginCatalog::cacheFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde").getChildFile ("plugins.xml");
}

juce::Array<PluginEntry> PluginCatalog::getBuiltIns (bool instruments) const
{
    juce::Array<PluginEntry> r;
    auto add = [&] (const char* name, const char* type, bool inst) { PluginEntry e; e.name = juce::String::fromUTF8 (name); e.builtInType = type; e.isInstrument = inst; e.manufacturer = "Tracktion"; r.add (e); };
    if (instruments)
    {
        add ("Onde Synth (pianos, basses, nappes, cordes, batteries…)", ondefx::SynthPlugin::xmlTypeName, true); r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Échantillonneur (bibliothèques d'instruments enregistrés, SFZ)", ondefx::SamplerPlugin::xmlTypeName, true); r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("4OSC (synthé intégré)", te::FourOscPlugin::xmlTypeName, true);
    }
    else
    {
        // Effets Onde (fenêtres dédiées), puis effets de base de Tracktion sans équivalent Onde (égaliseur,
        // compresseur, réverbération et délai de base sont remplacés par Onde EQ, Comp, Espace, Écho ; les
        // anciens projets qui les utilisent s'ouvrent toujours).
        add ("Onde EQ (égaliseur paramétrique, analyseur)", ondefx::EqPlugin::xmlTypeName, false);       r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Comp (compresseur)", ondefx::CompPlugin::xmlTypeName, false);                         r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Limiteur (plafond garanti)", ondefx::LimiterPlugin::xmlTypeName, false);              r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Ampli (guitare, basse : clair, crunch, lead, moderne)", ondefx::AmpPlugin::xmlTypeName, false); r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Pédalier (16 pédales : overdrive, fuzz, chorus, délai, wah…, dans l'ordre voulu)", ondefx::PedalboardPlugin::xmlTypeName, false); r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Saturation (douce, lampe, bande, fuzz)", ondefx::SaturationPlugin::xmlTypeName, false); r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Écho (délai calé sur le tempo)", ondefx::EchoPlugin::xmlTypeName, false);              r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Espace (réverbération)", ondefx::SpacePlugin::xmlTypeName, false);                    r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Onde Modulation (chorus, ensemble, flanger, phaser, vibrato)", ondefx::ModulationPlugin::xmlTypeName, false); r.getReference (r.size() - 1).manufacturer = "Onde";
        add ("Passe-bas (Tracktion)", te::LowPassPlugin::xmlTypeName, false);
    }
    return r;
}

juce::Array<PluginEntry> PluginCatalog::getVST3 (bool instruments) const
{
    const juce::ScopedLock sl (lock);
    juce::Array<PluginEntry> r;
    for (auto& e : vst3)
        if (e.isInstrument == instruments)
            r.add (e);
    return r;
}

juce::String PluginCatalog::getStatus() const       { const juce::ScopedLock sl (lock); return status; }
juce::StringArray PluginCatalog::getFailures() const { const juce::ScopedLock sl (lock); return failures; }

juce::Array<juce::File> PluginCatalog::getSearchFolders()
{
    juce::Array<juce::File> dirs;
    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    dirs.add (exeDir.getChildFile ("plugins-de-test"));
   #if JUCE_WINDOWS
    dirs.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("VST3"));
    dirs.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getParentDirectory().getChildFile ("Local").getChildFile ("Programs").getChildFile ("Common").getChildFile ("VST3"));
   #elif JUCE_MAC
    dirs.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    dirs.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3"));
   #else
    dirs.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3"));
    dirs.add (juce::File ("/usr/lib/vst3"));
    dirs.add (juce::File ("/usr/local/lib/vst3"));
   #endif
    return dirs;
}

void PluginCatalog::startScan()
{
    if (! isThreadRunning())
        startThread();
}

void PluginCatalog::run()
{
    // 1. Liste des fichiers .vst3 (sans descendre dans les bundles).
    juce::Array<juce::File> candidates;
    for (auto& dir : getSearchFolders())
    {
        if (! dir.isDirectory()) continue;
        juce::Array<juce::File> found;
        dir.findChildFiles (found, juce::File::findFilesAndDirectories, true, "*.vst3");
        for (auto& f : found)
        {
            bool insideBundle = false;
            for (auto p = f.getParentDirectory(); p != dir && p.getFullPathName().length() > dir.getFullPathName().length(); p = p.getParentDirectory())
                if (p.hasFileExtension ("vst3")) { insideBundle = true; break; }
            if (! insideBundle) candidates.addIfNotAlreadyThere (f);
        }
    }

    // 2. Chaque fichier est analysé par onde-plugin-host --scan : un VST3 défectueux ne fait planter que l'analyse.
    juce::Array<PluginEntry> result;
    juce::StringArray failed;
    const auto host = SandboxPlugin::getWorkerFile();
    for (int i = 0; i < candidates.size() && ! threadShouldExit(); ++i)
    {
        const auto& f = candidates[i];
        {
            const juce::ScopedLock sl (lock);
            status = juce::String::fromUTF8 ("Analyse des plugins ") + juce::String (i + 1) + "/" + juce::String (candidates.size()) + " : " + f.getFileNameWithoutExtension();
        }
        sendChangeMessage();

        juce::ChildProcess proc;
        const auto answer = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("onde-analyse-" + juce::String (juce::Random::getSystemRandom().nextInt64()) + ".txt");
        if (! proc.start (juce::StringArray { host.getFullPathName(), "--scan", f.getFullPathName(), answer.getFullPathName() }, 0))
        {
            failed.add (f.getFileName() + juce::String::fromUTF8 (" (analyseur introuvable)"));
            continue;
        }
        // Attente limitée (20 s) ; la réponse est écrite dans un fichier, lu une fois l'analyseur terminé.
        bool finished = false;
        for (int waited = 0; waited < 20000 && ! threadShouldExit(); waited += 100)
            if (proc.waitForProcessToFinish (100)) { finished = true; break; }
        if (! finished) { proc.kill(); answer.deleteFile(); failed.add (f.getFileName() + juce::String::fromUTF8 (" (bloqué pendant l'analyse)")); continue; }
        const auto out = answer.loadFileAsString();
        answer.deleteFile();

        bool any = false;
        for (auto& line : juce::StringArray::fromLines (out))
        {
            auto t = juce::StringArray::fromTokens (line, "\t", {});
            if (t.size() >= 5 && t[0] == "PLUGIN")
            {
                PluginEntry e; e.name = t[1]; e.manufacturer = t[2]; e.isInstrument = t[3] == "1"; e.path = f.getFullPathName();
                result.add (e); any = true;
            }
        }
        if (! any)
            failed.add (f.getFileName() + juce::String::fromUTF8 (" (a planté ou n'est pas un VST3 valide)"));
    }

    {
        const juce::ScopedLock sl (lock);
        vst3 = result; failures = failed;
        status = juce::String (result.size()) + " plugin(s) VST3" + (failed.isEmpty() ? juce::String() : juce::String (", ") + juce::String (failed.size()) + juce::String::fromUTF8 (" ignoré(s)"));
    }
    save();
    sendChangeMessage();
}

void PluginCatalog::load()
{
    if (auto xml = juce::XmlDocument::parse (cacheFile()))
        for (auto* e : xml->getChildIterator())
        {
            PluginEntry p; p.name = e->getStringAttribute ("name"); p.manufacturer = e->getStringAttribute ("manufacturer");
            p.path = e->getStringAttribute ("path"); p.isInstrument = e->getBoolAttribute ("instrument");
            if (juce::File (p.path).exists()) vst3.add (p);
        }
    status = juce::String (vst3.size()) + " plugin(s) VST3";
}

void PluginCatalog::save() const
{
    juce::XmlElement root ("ONDE_PLUGINS");
    const juce::ScopedLock sl (lock);
    for (auto& p : vst3)
    {
        auto* e = root.createNewChildElement ("PLUGIN");
        e->setAttribute ("name", p.name); e->setAttribute ("manufacturer", p.manufacturer);
        e->setAttribute ("path", p.path); e->setAttribute ("instrument", p.isInstrument);
    }
    cacheFile().getParentDirectory().createDirectory();
    root.writeTo (cacheFile());
}

te::Plugin::Ptr PluginCatalog::addToTrack (te::AudioTrack& track, const PluginEntry& entry)
{
    auto& edit = track.edit;
    te::Plugin::Ptr p;
    if (entry.isVST3())
    {
        edit.engine.getPluginManager().createBuiltInType<SandboxPlugin>();
        p = edit.getPluginCache().createNewPlugin (SandboxPlugin::xmlTypeName, {});
        if (auto* sp = dynamic_cast<SandboxPlugin*> (p.get()))
        {
            sp->spec = SandboxPlugin::specForVST3 (entry.path, entry.name);
            sp->displayName = entry.name;
            sp->state.setProperty ("isInstrument", entry.isInstrument, nullptr);
        }
    }
    else
    {
        p = edit.getPluginCache().createNewPlugin (entry.builtInType, {});
    }
    if (p == nullptr) return {};

    auto& list = track.pluginList;
    int index = 0;
    if (entry.isInstrument)
    {
        for (auto existing : list.getPlugins())       // un seul instrument par piste
            if (existing->isSynth()) existing->deleteFromParent();
    }
    else
    {
        index = list.size();
        for (int i = 0; i < list.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) != nullptr) { index = i; break; }
    }
    list.insertPlugin (p, index, nullptr);
    return p;
}
