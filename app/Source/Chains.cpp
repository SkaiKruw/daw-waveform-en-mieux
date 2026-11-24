#include "Chains.h"
#include "OndeEffects.h"
#include "OndeSynth.h"
#include "OndeFx2.h"

namespace chains
{
    namespace
    {
        const juce::Identifier chainProp ("ondeChain"), macroProp ("ondeMacro");
        // Raccourcis des types d'effets.
        constexpr const char* EQ = "ondeEQ";
        constexpr const char* COMP = "ondeComp";
        constexpr const char* LIM = "ondeLimiter";
        constexpr const char* SAT = "ondeSaturation";
        constexpr const char* ECHO = "ondeEcho";
        constexpr const char* SPACE = "ondeSpace";
        constexpr const char* SYNTH = "ondeSynth";

        te::Plugin* findPlugin (te::AudioTrack& t, const char* type)
        {
            for (auto* p : t.pluginList) if (p->getPluginType() == type) return p;
            return nullptr;
        }

        te::AutomatableParameter* findParam (te::AudioTrack& t, const char* type, const char* id)
        {
            if (auto* p = findPlugin (t, type))
                for (auto* prm : p->getAutomatableParameters()) if (prm->paramID == id) return prm;
            return nullptr;
        }

        // Commandes rapides d'Onde Synth, selon son moteur.
        std::vector<Macro> synthMacros (int engine)
        {
            switch (engine)
            {
                case 0:  return { { "Brillance", { { SYNTH, "cutoff", 0.6f } } }, { "Résonance", { { SYNTH, "reso", 0.6f } } },
                                  { "Attaque", { { SYNTH, "a", 0.5f }, { SYNTH, "fa", 0.4f } } }, { "Relâchement", { { SYNTH, "r", 0.5f } } } };
                case 1:  return { { "Brillance", { { SYNTH, "index", 0.4f }, { SYNTH, "bright", 0.6f } } }, { "Attaque", { { SYNTH, "a", 0.5f } } },
                                  { "Tenue", { { SYNTH, "d", 0.5f } } }, { "Relâchement", { { SYNTH, "r", 0.5f } } } };
                case 2:  return { { "Brillance", { { SYNTH, "bright", 0.8f } } }, { "Dureté", { { SYNTH, "hard", 0.8f } } },
                                  { "Stéréo", { { SYNTH, "width", 0.8f } } } };
                case 3:  return { { "Accord", { { SYNTH, "dtune", 0.5f } } }, { "Résonance", { { SYNTH, "ddecay", 0.5f } } } };
                default: return { { "Brillance", { { SYNTH, "bright", 0.6f } } }, { "Attaque", { { SYNTH, "pick", 0.6f } } },
                                  { "Résonance", { { SYNTH, "ring", 0.4f } } }, { "Étouffement", { { SYNTH, "mute", 0.8f } } } };
            }
        }

    }

    const std::vector<Chain>& all()
    {
        // Gains d'égaliseur en dB, seuils en dBFS ; les profondeurs des commandes sont des parts de course (±).
        static const std::vector<Chain> list = {
            { "Voix", "Voix pop", "Claire, présente et aérée, un peu d'écho et d'espace",
              { { EQ, { { "b0on", 1 }, { "b0freq", 90 }, { "b2freq", 300 }, { "b2gain", -2 }, { "b4freq", 3500 }, { "b4gain", 2.5f }, { "b5gain", 3 } } },
                { COMP, { { "threshold", -20 }, { "ratio", 3 }, { "attack", 5 }, { "release", 80 }, { "makeup", 4 }, { "autorel", 1 } } },
                { ECHO, { { "division", 4 }, { "feedback", 20 }, { "mix", 10 } } },
                { SPACE, { { "size", 0.45f }, { "decay", 1.6f }, { "mix", 15 } } } },
              { { "Présence", { { EQ, "b4gain", 0.25f } } }, { "Air", { { EQ, "b5gain", 0.25f } } }, { "Compression", { { COMP, "threshold", -0.3f } } },
                { "Écho", { { ECHO, "mix", 0.4f } } }, { "Espace", { { SPACE, "mix", 0.5f } } } } },
            { "Voix", "Voix rap", "Serrée et en avant, légère saturation",
              { { EQ, { { "b0on", 1 }, { "b0freq", 100 }, { "b3gain", -1.5f }, { "b4freq", 4000 }, { "b4gain", 3 }, { "b5gain", 2 } } },
                { COMP, { { "threshold", -22 }, { "ratio", 4 }, { "attack", 3 }, { "release", 60 }, { "makeup", 6 }, { "style", 2 } } },
                { SAT, { { "character", 0 }, { "drive", 6 }, { "mix", 30 } } },
                { SPACE, { { "size", 0.3f }, { "decay", 0.9f }, { "mix", 8 } } } },
              { { "Présence", { { EQ, "b4gain", 0.25f } } }, { "Saturation", { { SAT, "drive", 0.5f } } }, { "Compression", { { COMP, "threshold", -0.3f } } },
                { "Espace", { { SPACE, "mix", 0.4f } } } } },
            { "Voix", "Podcast", "Voix parlée intelligible, niveau régulier, prête à publier",
              { { EQ, { { "b0on", 1 }, { "b0freq", 80 }, { "b1gain", -2 }, { "b1freq", 150 }, { "b4freq", 3000 }, { "b4gain", 2 } } },
                { COMP, { { "threshold", -24 }, { "ratio", 3 }, { "autorel", 1 }, { "makeup", 5 } } },
                { LIM, { { "input", 4 }, { "ceiling", -1 }, { "target", 1 } } } },
              { { "Présence", { { EQ, "b4gain", 0.25f } } }, { "Compression", { { COMP, "threshold", -0.3f } } }, { "Volume", { { LIM, "input", 0.5f } } } } },
            { "Guitares", "Guitare claire", "Nette et brillante, écho pointé et espace",
              { { EQ, { { "b0on", 1 }, { "b0freq", 80 }, { "b4freq", 3000 }, { "b4gain", 2 } } },
                { COMP, { { "threshold", -18 }, { "ratio", 2.5f }, { "style", 1 } } },
                { ECHO, { { "division", 5 }, { "feedback", 25 }, { "mix", 15 } } },
                { SPACE, { { "size", 0.6f }, { "decay", 2 }, { "mix", 18 } } } },
              { { "Brillance", { { EQ, "b5gain", 0.25f } } }, { "Écho", { { ECHO, "mix", 0.4f } } }, { "Espace", { { SPACE, "mix", 0.5f } } },
                { "Compression", { { COMP, "threshold", -0.3f } } } } },
            { "Guitares", "Guitare saturée", "Saturation à lampe, médiums creusés, aigus adoucis",
              { { SAT, { { "character", 1 }, { "drive", 24 }, { "tone", 0.1f }, { "mix", 100 } } },
                { EQ, { { "b0on", 1 }, { "b0freq", 90 }, { "b2freq", 400 }, { "b2gain", -3 }, { "b4freq", 2500 }, { "b4gain", 2 }, { "b6on", 1 }, { "b6freq", 9000 } } },
                { SPACE, { { "size", 0.4f }, { "decay", 1.2f }, { "mix", 10 } } } },
              { { "Saturation", { { SAT, "drive", 0.4f } } }, { "Tonalité", { { SAT, "tone", 0.6f } } }, { "Brillance", { { EQ, "b4gain", 0.25f } } },
                { "Espace", { { SPACE, "mix", 0.4f } } } } },
            { "Guitares", "Guitare acoustique", "Naturelle, moins de boue, plus d'air",
              { { EQ, { { "b0on", 1 }, { "b0freq", 70 }, { "b2freq", 250 }, { "b2gain", -2 }, { "b5gain", 2.5f } } },
                { COMP, { { "threshold", -20 }, { "ratio", 2 }, { "style", 1 } } },
                { SPACE, { { "size", 0.5f }, { "decay", 1.8f }, { "mix", 15 } } } },
              { { "Air", { { EQ, "b5gain", 0.25f } } }, { "Compression", { { COMP, "threshold", -0.3f } } }, { "Espace", { { SPACE, "mix", 0.5f } } } } },
            { "Basses", "Basse ronde", "Grave plein, régulier, un peu de grain de bande",
              { { EQ, { { "b0on", 1 }, { "b0freq", 35 }, { "b1freq", 80 }, { "b1gain", 2 }, { "b3freq", 800 }, { "b3gain", -2 } } },
                { COMP, { { "threshold", -22 }, { "ratio", 4 }, { "attack", 10 }, { "release", 120 }, { "style", 1 }, { "makeup", 4 } } },
                { SAT, { { "character", 2 }, { "drive", 6 }, { "mix", 40 } } } },
              { { "Rondeur", { { EQ, "b1gain", 0.25f } } }, { "Grain", { { SAT, "drive", 0.5f } } }, { "Compression", { { COMP, "threshold", -0.3f } } } } },
            { "Basses", "Basse punchy", "Attaque nette, présente dans le mix",
              { { EQ, { { "b0on", 1 }, { "b0freq", 40 }, { "b2freq", 120 }, { "b2gain", 2 }, { "b4freq", 2000 }, { "b4gain", 2 } } },
                { COMP, { { "threshold", -24 }, { "ratio", 5 }, { "attack", 2 }, { "makeup", 5 } } },
                { SAT, { { "character", 0 }, { "drive", 10 }, { "mix", 50 } } } },
              { { "Punch", { { COMP, "threshold", -0.3f } } }, { "Grain", { { SAT, "drive", 0.5f } } }, { "Claquant", { { EQ, "b4gain", 0.25f } } } } },
            { "Batterie", "Batterie punchy", "Compression parallèle, grain de bande, aigus ouverts",
              { { EQ, { { "b1freq", 70 }, { "b1gain", 2 }, { "b2freq", 400 }, { "b2gain", -2 }, { "b5gain", 2 } } },
                { COMP, { { "threshold", -18 }, { "ratio", 4 }, { "attack", 15 }, { "release", 80 }, { "mix", 60 }, { "style", 2 } } },
                { SAT, { { "character", 2 }, { "drive", 8 }, { "mix", 30 } } },
                { SPACE, { { "size", 0.3f }, { "decay", 0.8f }, { "mix", 10 } } } },
              { { "Punch", { { COMP, "mix", 0.8f } } }, { "Grain", { { SAT, "drive", 0.5f } } }, { "Brillance", { { EQ, "b5gain", 0.25f } } },
                { "Espace", { { SPACE, "mix", 0.4f } } } } },
            { "Batterie", "Batterie lo-fi", "Sale, sombre, compressée",
              { { SAT, { { "character", 3 }, { "drive", 10 }, { "tone", -0.5f }, { "mix", 40 } } },
                { EQ, { { "b0on", 1 }, { "b0freq", 120 }, { "b6on", 1 }, { "b6freq", 7000 } } },
                { COMP, { { "threshold", -20 }, { "ratio", 6 }, { "style", 2 }, { "makeup", 5 } } } },
              { { "Saleté", { { SAT, "drive", 0.5f }, { SAT, "mix", 0.4f } } }, { "Compression", { { COMP, "threshold", -0.3f } } } } },
            { "Claviers et synthés", "Nappe large", "Écho ping-pong et grand espace",
              { { EQ, { { "b0on", 1 }, { "b0freq", 150 } } },
                { ECHO, { { "division", 1 }, { "feedback", 40 }, { "pingpong", 1 }, { "mix", 20 } } },
                { SPACE, { { "size", 0.8f }, { "decay", 4 }, { "mix", 35 } } } },
              { { "Écho", { { ECHO, "mix", 0.4f } } }, { "Espace", { { SPACE, "mix", 0.6f } } }, { "Brillance", { { EQ, "b5gain", 0.25f } } } } },
            { "Claviers et synthés", "Lead brillant", "Légère saturation, écho pointé en ping-pong",
              { { SAT, { { "character", 0 }, { "drive", 8 }, { "mix", 50 } } },
                { ECHO, { { "division", 5 }, { "feedback", 30 }, { "pingpong", 1 }, { "mix", 18 } } },
                { SPACE, { { "size", 0.5f }, { "decay", 1.5f }, { "mix", 15 } } } },
              { { "Saturation", { { SAT, "drive", 0.5f } } }, { "Écho", { { ECHO, "mix", 0.4f } } }, { "Espace", { { SPACE, "mix", 0.4f } } } } },
            { "Claviers et synthés", "Piano chaleureux", "Grave arrondi, compression douce, salle",
              { { EQ, { { "b1gain", 1.5f }, { "b5gain", -1 } } },
                { COMP, { { "threshold", -20 }, { "ratio", 2 }, { "style", 1 } } },
                { SPACE, { { "size", 0.6f }, { "decay", 2.2f }, { "mix", 20 } } } },
              { { "Chaleur", { { EQ, "b1gain", 0.25f }, { EQ, "b5gain", -0.15f } } }, { "Espace", { { SPACE, "mix", 0.5f } } },
                { "Compression", { { COMP, "threshold", -0.3f } } } } },
            { "Master", "Mastering streaming", "Égalisation légère, compression douce, -14 LUFS, plafond -1 dBTP",
              { { EQ, { { "b0on", 1 }, { "b0freq", 25 }, { "b1gain", 1 }, { "b5gain", 1.5f } } },
                { COMP, { { "threshold", -14 }, { "ratio", 2 }, { "attack", 30 }, { "release", 200 }, { "autorel", 1 }, { "style", 1 }, { "knee", 12 } } },
                { LIM, { { "input", 4 }, { "ceiling", -1 }, { "truepeak", 1 }, { "target", 0 } } } },
              { { "Volume", { { LIM, "input", 0.5f } } }, { "Compression", { { COMP, "threshold", -0.3f } } }, { "Air", { { EQ, "b5gain", 0.25f } } } } },
        };
        return list;
    }

    const Chain* find (const juce::String& name)
    {
        for (auto& c : all()) if (juce::String::fromUTF8 (c.name) == name) return &c;
        return nullptr;
    }

    juce::String currentChain (te::AudioTrack& t) { return t.state[chainProp].toString(); }

    void apply (te::AudioTrack& t, const Chain* chain)
    {
        auto* um = &t.edit.getUndoManager();
        // Les effets posés par la chaîne précédente s'en vont ; ceux ajoutés à la main restent.
        for (auto* p : t.pluginList.getPlugins())
            if (p->state.hasProperty (chainProp)) p->deleteFromParent();
        if (chain != nullptr)
        {
            int index = t.pluginList.size();
            if (auto* v = t.getVolumePlugin()) index = t.pluginList.indexOf (v);
            for (auto& step : chain->steps)
            {
                auto p = t.edit.getPluginCache().createNewPlugin (step.type, {});
                if (p == nullptr) continue;
                p->state.setProperty (chainProp, juce::String::fromUTF8 (chain->name), um);
                t.pluginList.insertPlugin (p, index++, nullptr);
                if (auto* fx = dynamic_cast<ondefx::OndeEffect*> (p.get()))
                    for (auto& [id, v] : step.values) fx->setStored (id, v);
            }
            t.state.setProperty (chainProp, juce::String::fromUTF8 (chain->name), um);
        }
        else t.state.removeProperty (chainProp, um);
        rebuildMacros (t);
    }

    void rebuildMacros (te::AudioTrack& t)
    {
        auto& list = t.getMacroParameterListForWriting();
        for (auto m : list.getMacroParameters())
            if (m->state.hasProperty (macroProp))
            {
                for (auto p : te::getAllParametersBeingModifiedBy (t.edit, *m)) p->removeModifier (*m);
                list.removeMacroParameter (*m);
            }
        auto add = [&] (const Macro& def, const char* kind)
        {
            juce::Array<te::AutomatableParameter*> targets;
            for (auto& tg : def.targets)
                if (auto* p = findParam (t, tg.type, tg.param)) targets.add (p);
            if (targets.isEmpty()) return;
            auto* m = list.createMacroParameter();
            m->macroName = juce::String::fromUTF8 (def.name);
            m->state.setProperty (macroProp, kind, nullptr);
            m->setParameter (0.5f, juce::sendNotification);
            // Au milieu (0,5), la commande laisse le réglage de la chaîne tel quel ; elle le fait varier de part et d'autre.
            int k = 0;
            for (auto& tg : def.targets)
                if (auto* p = findParam (t, tg.type, tg.param))
                    p->addModifier (*m, tg.depth, -0.5f * tg.depth, 0.5f), ++k;
        };
        // D'abord l'instrument, puis la chaîne.
        for (auto* p : t.pluginList)
            if (auto* s = dynamic_cast<ondefx::SynthPlugin*> (p))
            {
                for (auto& m : synthMacros (juce::roundToInt (s->value ("engine")))) add (m, "synth");
                break;
            }
        if (auto* c = find (currentChain (t)))
            for (auto& m : c->macros) add (m, "chain");
    }

    juce::ReferenceCountedArray<te::MacroParameter> quickControls (te::AudioTrack& t)
    {
        juce::ReferenceCountedArray<te::MacroParameter> r;
        for (auto m : t.getMacroParameters()) if (m->state.hasProperty (macroProp)) r.add (m);
        return r;
    }
}
