#include "Drummer.h"
#include "OndeSynth.h"
#include "LibraryPanel.h"

namespace drummer
{
    const char* const styleNames[numStyles] = { "Pop", "Rock", "Hip-hop", "Électro", "Funk", "Ballade" };
    const char* const styleDescriptions[numStyles] = {
        "Carré et entraînant, charleston en croches",
        "Grosse caisse appuyée, caisse claire sur 2 et 4",
        "Groove posé, grosse caisse syncopée",
        "Grosse caisse à chaque temps, claps, charleston ouverte",
        "Doubles croches, notes fantômes, très syncopé",
        "Demi-tempo, cerclage et cymbale douce" };

    namespace
    {
        const juce::Identifier regionID ("ondeDrummer");
        enum Note { kickN = 36, snareN = 38, rimN = 37, clapN = 39, hatN = 42, openHatN = 46, rideN = 51, crashN = 49, cowbellN = 56,
                    tomHighN = 50, tomMidHN = 48, tomMidN = 47, tomLowN = 45, tomFloorN = 43, tomFloor2N = 41 };

        // Probabilités par double croche (16 pas par mesure de 4 temps) : jeu simple et jeu complexe.
        struct Pattern { float kickS[16], kickC[16], snareS[16], snareC[16]; };
        const Pattern patterns[numStyles] = {
            // Pop
            { { 1,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0 }, { 1,0,0,0, 0,0,.6f,0, 1,0,.5f,0, 0,0,0,.3f },
              { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0 }, { 0,0,0,0, 1,0,0,.25f, 0,0,0,0, 1,0,0,.3f } },
            // Rock
            { { 1,0,0,0, 0,0,0,0, 1,0,1,0, 0,0,0,0 }, { 1,0,0,.5f, 0,0,0,0, 1,0,1,0, 0,0,.4f,0 },
              { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0 }, { 0,0,0,0, 1,0,0,0, 0,0,0,.3f, 1,0,0,.4f } },
            // Hip-hop
            { { 1,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0 }, { 1,0,0,.5f, 0,0,0,.3f, 0,.4f,1,0, 0,0,.3f,0 },
              { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0 }, { 0,0,0,0, 1,0,0,.3f, 0,.2f,0,0, 1,0,0,.3f } },
            // Électro (grosse caisse à chaque temps)
            { { 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0 }, { 1,0,0,0, 1,0,0,.3f, 1,0,0,0, 1,0,.4f,0 },
              { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0 }, { 0,0,0,0, 1,0,0,0, 0,0,0,.3f, 1,0,0,0 } },
            // Funk
            { { 1,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0 }, { 1,0,0,.6f, 0,0,.4f,0, 0,.5f,1,0, 0,0,.4f,0 },
              { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0 }, { 0,.3f,0,.4f, 1,0,.3f,.4f, 0,.4f,0,.3f, 1,0,.4f,.3f } },
            // Ballade (demi-tempo : caisse claire sur le 3e temps)
            { { 1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 }, { 1,0,0,0, 0,0,0,0, 0,0,.5f,0, 0,0,0,.3f },
              { 0,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0 }, { 0,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,.2f,0 } },
        };

        juce::String encode (const Settings& s)
        {
            juce::StringArray a;
            a.add (juce::String (s.style)); a.add (juce::String (s.complexity, 3)); a.add (juce::String (s.intensity, 3));
            a.add (juce::String (s.swing, 3)); a.add (juce::String (s.humanize, 3)); a.add (juce::String (s.fills));
            a.add (s.kick ? "1" : "0"); a.add (s.snare ? "1" : "0"); a.add (s.cymbals ? "1" : "0"); a.add (s.percussion ? "1" : "0");
            a.add (juce::String (s.variation));
            return a.joinIntoString (";");
        }

        Settings decode (const juce::String& text)
        {
            Settings s;
            const auto a = juce::StringArray::fromTokens (text, ";", {});
            if (a.size() < 11) return s;
            s.style = juce::jlimit (0, numStyles - 1, a[0].getIntValue());
            s.complexity = a[1].getFloatValue(); s.intensity = a[2].getFloatValue(); s.swing = a[3].getFloatValue();
            s.humanize = a[4].getFloatValue(); s.fills = juce::jlimit (0, 2, a[5].getIntValue());
            s.kick = a[6] == "1"; s.snare = a[7] == "1"; s.cymbals = a[8] == "1"; s.percussion = a[9] == "1";
            s.variation = a[10].getIntValue();
            return s;
        }
    }

    bool isDrummerRegion (const te::Clip* c)
    {
        return dynamic_cast<const te::MidiClip*> (c) != nullptr && c->state.hasProperty (regionID);
    }

    Settings read (const te::MidiClip& c) { return decode (c.state[regionID].toString()); }

    std::vector<Hit> generate (const Settings& s, int bars, int beatsPerBar)
    {
        std::vector<Hit> hits;
        const auto& pat = patterns[juce::jlimit (0, numStyles - 1, s.style)];
        const int stepsPerBar = juce::jmax (1, beatsPerBar) * 4;
        const float cx = juce::jlimit (0.0f, 1.0f, s.complexity), in = juce::jlimit (0.0f, 1.0f, s.intensity);
        const int fillEvery = s.fills == 1 ? 4 : s.fills == 2 ? 8 : 0;
        auto vel = [&] (juce::Random& r, float base)
        {
            // Intensité : de pianissimo à fortissimo ; jeu humain : petites variations.
            const float v = base * (0.45f + 0.55f * in) * 127.0f + (r.nextFloat() * 2.0f - 1.0f) * 12.0f * s.humanize;
            return juce::jlimit (1, 127, juce::roundToInt (v));
        };
        for (int bar = 0; bar < bars; ++bar)
        {
            // Même variation, même mesure : même jeu (régénération stable).
            juce::Random r ((juce::int64) s.variation * 7919 + bar * 104729 + s.style * 31);
            const bool fillBar = fillEvery > 0 && (bar % fillEvery) == fillEvery - 1 && bar < bars;
            // Remplissage : de la moitié d'un temps (simple) à deux temps (complexe), en fin de mesure.
            const int fillSteps = fillBar ? juce::jlimit (2, 8, juce::roundToInt (2.0f + 6.0f * cx)) : 0;
            const int fillStart = stepsPerBar - fillSteps;
            auto add = [&] (int step, int note, int v, double length = 0.2)
            {
                double beat = bar * beatsPerBar + step * 0.25;
                if (step % 2 == 1) beat += s.swing * 0.33 * 0.25;                       // swing des doubles croches
                beat += (r.nextFloat() * 2.0f - 1.0f) * 0.02 * s.humanize;               // placement humain (± 10 ms à 120)
                hits.push_back ({ juce::jmax (0.0, beat), note, v, length });
            };
            for (int step = 0; step < stepsPerBar; ++step)
            {
                const int k = step % 16;
                const bool inFill = step >= fillStart && fillBar;
                // Grosse caisse.
                if (s.kick && ! inFill)
                {
                    const float p = pat.kickS[k] + (pat.kickC[k] - pat.kickS[k]) * cx;
                    if (p > 0 && r.nextFloat() < p) add (step, kickN, vel (r, step % 4 == 0 ? 1.0f : 0.85f));
                }
                // Caisse claire (cerclage en ballade douce, clap en électro), notes fantômes.
                if (s.snare && ! inFill)
                {
                    const float p = pat.snareS[k] + (pat.snareC[k] - pat.snareS[k]) * cx;
                    if (p > 0 && r.nextFloat() < p)
                    {
                        const bool ghost = pat.snareS[k] < 0.5f;
                        int note = snareN;
                        if (s.style == 5 && in < 0.45f) note = rimN;
                        if (s.style == 3 && ! ghost) note = clapN;
                        add (step, note, vel (r, ghost ? 0.3f : 0.95f));
                    }
                }
                // Charleston / ride : croches (simple) ou doubles croches (complexe), ouverte à forte intensité.
                if (s.cymbals && ! inFill)
                {
                    const bool eighth = step % 2 == 0;
                    const bool play = eighth || r.nextFloat() < cx * 0.85f;
                    if (play)
                    {
                        int note = in > 0.8f && s.style == 1 ? rideN : hatN;
                        if (s.style == 3 && k % 4 == 2) note = openHatN;                           // électro : ouverte en contretemps
                        else if (in > 0.65f && k == 14 && r.nextFloat() < 0.5f) note = openHatN;    // ouverte avant le temps fort
                        if (s.style == 5 && in > 0.55f) note = rideN;
                        add (step, note, vel (r, step % 4 == 0 ? 0.8f : eighth ? 0.65f : 0.45f), note == openHatN ? 0.4 : 0.1);
                    }
                }
                // Percussions : cloche à vache sur les temps (funk, électro) quand c'est complexe.
                if (s.percussion && ! inFill && (s.style == 4 || s.style == 3) && cx > 0.6f && step % 4 == 0 && r.nextFloat() < cx - 0.4f)
                    add (step, cowbellN, vel (r, 0.5f));
                // Remplissage : toms du plus aigu au plus grave, crescendo, caisse claire au début.
                if (inFill)
                {
                    static const int toms[] = { snareN, tomHighN, tomMidHN, tomMidN, tomLowN, tomFloorN, tomFloor2N, tomFloor2N };
                    const int idx = (step - fillStart) * 7 / juce::jmax (1, fillSteps);
                    const float crescendo = 0.7f + 0.3f * (float) (step - fillStart) / (float) juce::jmax (1, fillSteps);
                    add (step, toms[juce::jlimit (0, 7, idx)], vel (r, crescendo));
                    if (s.kick && (step - fillStart) % 4 == 0) add (step, kickN, vel (r, 0.8f));
                }
            }
            // Cymbale crash au début de la phrase qui suit un remplissage (et au tout début si c'est fort).
            const bool phraseStart = fillEvery > 0 ? bar % fillEvery == 0 && bar > 0 : false;
            if (s.cymbals && (phraseStart || (bar == 0 && in > 0.6f))) hits.push_back ({ (double) bar * beatsPerBar, crashN, vel (r, 0.9f), 1.0 });
        }
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
        return hits;
    }

    void apply (te::MidiClip& clip, const Settings& s)
    {
        auto* um = &clip.edit.getUndoManager();
        clip.state.setProperty (regionID, encode (s), um);
        // Longueur de la région en mesures (au moins une), signature au début de la région.
        auto& ts = clip.edit.tempoSequence;
        const auto pos = clip.getPosition();
        const double startBeat = ts.toBeats (pos.getStart()).inBeats(), endBeat = ts.toBeats (pos.getEnd()).inBeats();
        const int beatsPerBar = juce::jmax (1, ts.getTimeSigAt (pos.getStart()).numerator.get());
        const int bars = juce::jmax (1, juce::roundToInt ((endBeat - startBeat) / beatsPerBar));
        auto& seq = clip.getSequence();
        seq.clear (um);
        for (auto& h : generate (s, bars, beatsPerBar))
            if (h.beat < endBeat - startBeat)
                seq.addNote (h.note, te::BeatPosition::fromBeats (h.beat), te::BeatDuration::fromBeats (h.length), h.velocity, 0, um);
    }

    te::MidiClip* addDrummerTrack (EditContext& ctx, int bars)
    {
        if (ctx.edit == nullptr) return nullptr;
        auto* kit = ondefx::SynthPlugin::findPreset ("Batterie acoustique");
        auto* t = LibraryPanel::applyToTrack (ctx, nullptr, *kit);
        if (t == nullptr) return nullptr;
        t->setName ("Batteur");
        t->setColour (juce::Colour (0xffe08a3c));
        auto& ts = ctx.edit->tempoSequence;
        const int beatsPerBar = juce::jmax (1, ts.getTimeSigAt (te::TimePosition()).numerator.get());
        const auto end = ts.toTime (te::BeatPosition::fromBeats (bars * beatsPerBar));
        auto clip = t->insertMIDIClip (juce::String::fromUTF8 ("Batteur"), { te::TimePosition(), end }, nullptr);
        if (clip == nullptr) return nullptr;
        clip->setColour (juce::Colour (0xffe08a3c));
        apply (*clip, Settings());
        ctx.selectTrack (t);
        ctx.changed();
        return clip.get();
    }
}
