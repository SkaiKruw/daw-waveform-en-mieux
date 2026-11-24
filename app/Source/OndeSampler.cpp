#include "OndeSampler.h"
#include "Preferences.h"

//==============================================================================
// Lecture des fichiers SFZ.
namespace sfz
{
    int noteFromText (const juce::String& raw)
    {
        const auto t = raw.trim().toLowerCase();
        if (t.isEmpty()) return -1;
        if (t.containsOnly ("-0123456789")) return t.getIntValue();
        static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };      // a b c d e f g
        const auto c = t[0];
        if (c < 'a' || c > 'g') return -1;
        int n = base[c - 'a'], i = 1;
        if (t[i] == '#') { ++n; ++i; } else if (t[i] == 'b' && t.length() > 2) { --n; ++i; }
        const int octave = t.substring (i).getIntValue();
        return juce::jlimit (0, 127, (octave + 1) * 12 + n);          // c4 = 60
    }

    bool Instrument::hasNote (int note) const
    {
        for (auto& r : regions) if (! r.releaseTrigger && note >= r.lokey && note <= r.hikey) return true;
        return false;
    }

    namespace
    {
        using Opcodes = std::map<juce::String, juce::String>;

        juce::String stripComments (const juce::String& text)
        {
            juce::String out;
            out.preallocateBytes (text.getNumBytesAsUTF8());
            auto p = text.getCharPointer();
            while (! p.isEmpty())
            {
                if (*p == '/' && p[1] == '/') { while (! p.isEmpty() && *p != '\n') ++p; continue; }
                if (*p == '/' && p[1] == '*') { p += 2; while (! p.isEmpty() && ! (*p == '*' && p[1] == '/')) ++p; if (! p.isEmpty()) p += 2; continue; }
                out << juce::String::charToString (*p);
                ++p;
            }
            return out;
        }

        // Texte complet : #include remplacés, #define appliqués.
        juce::String preprocess (const juce::File& file, std::map<juce::String, juce::String>& defines, int depth = 0)
        {
            if (depth > 16 || ! file.existsAsFile()) return {};
            juce::String result;
            for (auto line : juce::StringArray::fromLines (stripComments (file.loadFileAsString())))
            {
                const auto trimmed = line.trim();
                if (trimmed.startsWith ("#define"))
                {
                    const auto rest = trimmed.substring (7).trim();
                    defines[rest.upToFirstOccurrenceOf (" ", false, false).trim()] = rest.fromFirstOccurrenceOf (" ", false, false).trim();
                    continue;
                }
                if (trimmed.startsWith ("#include"))
                {
                    const auto name = trimmed.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false);
                    result << preprocess (file.getParentDirectory().getChildFile (name.replaceCharacter ('\\', '/')), defines, depth + 1) << "\n";
                    continue;
                }
                // Variables $NOM : les plus longues d'abord ($VEL10 avant $VEL1).
                if (line.containsChar ('$'))
                {
                    std::vector<std::pair<juce::String, juce::String>> list (defines.begin(), defines.end());
                    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.first.length() > b.first.length(); });
                    for (auto& [k, v] : list) line = line.replace (k, v);
                }
                result << line << "\n";
            }
            return result;
        }

        float num (const Opcodes& o, const char* key, float def)
        {
            auto it = o.find (key);
            return it != o.end() ? it->second.getFloatValue() : def;
        }
        int key (const Opcodes& o, const char* k, int def)
        {
            auto it = o.find (k);
            if (it == o.end()) return def;
            const int n = noteFromText (it->second);
            return n >= 0 ? n : def;
        }
        bool has (const Opcodes& o, const char* k) { return o.find (k) != o.end(); }
    }

    std::shared_ptr<Instrument> parse (const juce::File& file)
    {
        auto inst = std::make_shared<Instrument>();
        inst->file = file;
        inst->name = file.getFileNameWithoutExtension();
        if (! file.existsAsFile()) { inst->error = juce::String::fromUTF8 ("fichier introuvable : ") + file.getFullPathName(); return inst; }
        std::map<juce::String, juce::String> defines;
        const auto text = preprocess (file, defines);

        Opcodes control, global, master, group, region;
        enum class Level { none, control, global, master, group, region } level = Level::none;
        juce::File defaultPath = file.getParentDirectory();
        int noteOffset = 0;

        auto flushRegion = [&]
        {
            if (level != Level::region) return;
            Opcodes o = global;
            for (auto& [k, v] : master) o[k] = v;
            for (auto& [k, v] : group) o[k] = v;
            for (auto& [k, v] : region) o[k] = v;
            auto it = o.find ("sample");
            if (it == o.end() || it->second.isEmpty() || it->second.startsWithChar ('*')) { region.clear(); return; }   // *sine… : non géré
            Region r;
            r.sample = defaultPath.getChildFile (it->second.trim().replaceCharacter ('\\', '/'));
            if (has (o, "key")) { r.lokey = r.hikey = r.keycenter = key (o, "key", 60); }
            r.lokey = key (o, "lokey", r.lokey); r.hikey = key (o, "hikey", r.hikey);
            r.keycenter = key (o, "pitch_keycenter", has (o, "key") ? r.keycenter : 60);
            r.lokey = juce::jlimit (0, 127, r.lokey + noteOffset); r.hikey = juce::jlimit (0, 127, r.hikey + noteOffset);
            r.keycenter += noteOffset;
            r.lovel = (int) num (o, "lovel", 1); r.hivel = (int) num (o, "hivel", 127);
            r.transpose = (int) num (o, "transpose", 0);
            r.tune = num (o, "tune", 0) + num (o, "pitch", 0);
            r.volume = num (o, "volume", 0) + num (o, "gain", 0);
            r.pan = num (o, "pan", 0);
            r.ampVeltrack = num (o, "amp_veltrack", 100);
            r.pitchKeytrack = num (o, "pitch_keytrack", 100);
            r.delay = juce::jmax (0.0f, num (o, "delay", 0));
            for (auto& [k, v] : o) if (k.startsWith ("amp_velcurve_")) r.velCurve.push_back ({ k.substring (13).getIntValue(), v.getFloatValue() });
            std::sort (r.velCurve.begin(), r.velCurve.end());
            r.seqLength = juce::jmax (1, (int) num (o, "seq_length", 1));
            r.seqPosition = juce::jmax (1, (int) num (o, "seq_position", 1));
            r.lorand = num (o, "lorand", 0); r.hirand = num (o, "hirand", 1);
            r.group = (int) num (o, "group", 0); r.offBy = (int) num (o, "off_by", 0);
            r.attack = juce::jmax (0.0005f, num (o, "ampeg_attack", 0.001f));
            r.hold = num (o, "ampeg_hold", 0);
            r.decay = num (o, "ampeg_decay", 0);
            r.sustain = num (o, "ampeg_sustain", 100);
            r.release = juce::jmax (0.005f, num (o, "ampeg_release", 0.05f));
            const auto trigger = o.count ("trigger") ? o["trigger"].trim().toLowerCase() : juce::String ("attack");
            r.releaseTrigger = trigger == "release";
            const auto loopMode = o.count ("loop_mode") ? o["loop_mode"] : (o.count ("loopmode") ? o["loopmode"] : juce::String());
            r.oneShot = loopMode == "one_shot";
            r.loop = loopMode == "loop_continuous" || loopMode == "loop_sustain";
            r.offset = (juce::int64) num (o, "offset", 0);
            r.end = o.count ("end") ? (juce::int64) num (o, "end", -1) : -1;
            r.loopStart = (juce::int64) (o.count ("loop_start") ? num (o, "loop_start", 0) : num (o, "loopstart", 0));
            r.loopEnd = (juce::int64) (o.count ("loop_end") ? num (o, "loop_end", -1) : num (o, "loopend", -1));
            if (has (o, "sw_last")) r.swLast = key (o, "sw_last", -1);
            if (has (o, "sw_lokey")) r.swLokey = key (o, "sw_lokey", -1);
            if (has (o, "sw_hikey")) r.swHikey = key (o, "sw_hikey", -1);
            if (r.swLast >= 0 && o.count ("sw_label")) inst->keyswitchNames[(size_t) juce::jlimit (0, 127, r.swLast)] = o["sw_label"].trim();
            if (r.swLokey >= 0) { inst->swLo = inst->swLo < 0 ? r.swLokey : juce::jmin (inst->swLo, r.swLokey); inst->swHi = juce::jmax (inst->swHi, r.swHikey); }
            if (has (o, "sw_default")) inst->swDefault = key (o, "sw_default", -1);
            if (r.lokey <= r.hikey) inst->regions.push_back (r);
            region.clear();
        };

        // Découpage : en-têtes <x> et paires clé=valeur (une valeur peut contenir des espaces : « sample=Violin A4.wav »).
        int i = 0;
        const int len = text.length();
        while (i < len)
        {
            const auto c = text[i];
            if (c == '<')
            {
                const int close = text.indexOfChar (i, '>');
                if (close < 0) break;
                const auto header = text.substring (i + 1, close).trim().toLowerCase();
                flushRegion();
                if (header == "control") { level = Level::control; control.clear(); }
                else if (header == "global") { level = Level::global; global.clear(); master.clear(); group.clear(); }
                else if (header == "master") { level = Level::master; master.clear(); group.clear(); }
                else if (header == "group") { level = Level::group; group.clear(); }
                else if (header == "region") { level = Level::region; region.clear(); }
                else level = Level::none;
                i = close + 1;
                continue;
            }
            if (juce::CharacterFunctions::isWhitespace (c)) { ++i; continue; }
            const int eq = text.indexOfChar (i, '=');
            if (eq < 0) break;
            const auto name = text.substring (i, eq).trim();
            // Fin de la valeur : avant le prochain « mot= » ou « < ».
            int j = eq + 1, valueEnd = len;
            while (j < len)
            {
                if (text[j] == '<') { valueEnd = j; break; }
                if (text[j] == '\n' || text[j] == '\r') { valueEnd = j; break; }
                if (juce::CharacterFunctions::isWhitespace (text[j]))
                {
                    int k = j; while (k < len && juce::CharacterFunctions::isWhitespace (text[k]) && text[k] != '\n') ++k;
                    int m = k; while (m < len && (juce::CharacterFunctions::isLetterOrDigit (text[m]) || text[m] == '_')) ++m;
                    if (m > k && m < len && text[m] == '=') { valueEnd = j; break; }
                }
                ++j;
            }
            const auto value = text.substring (eq + 1, valueEnd).trim();
            Opcodes* target = level == Level::control ? &control : level == Level::global ? &global : level == Level::master ? &master
                            : level == Level::group ? &group : level == Level::region ? &region : nullptr;
            if (target != nullptr) (*target)[name.toLowerCase()] = value;
            if (level == Level::control && name == "default_path") defaultPath = file.getParentDirectory().getChildFile (value.replaceCharacter ('\\', '/'));
            if (level == Level::control && name == "note_offset") noteOffset = value.getIntValue();
            i = valueEnd;
        }
        flushRegion();
        if (inst->regions.empty()) inst->error = juce::String::fromUTF8 ("aucune zone jouable dans ce fichier");
        for (int k = 0; k < 128; ++k) if (inst->keyswitchNames[(size_t) k].isNotEmpty()) inst->articulations.add (inst->keyswitchNames[(size_t) k]);
        if (inst->swDefault < 0)
            for (auto& r : inst->regions) if (r.swLast >= 0) { inst->swDefault = r.swLast; break; }
        return inst;
    }

    void loadSamples (Instrument& inst, std::function<bool()> shouldStop)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::map<juce::String, std::shared_ptr<Sample>> cache;
        int missing = 0;
        for (auto& r : inst.regions)
        {
            if (shouldStop && shouldStop()) return;
            const auto path = r.sample.getFullPathName();
            if (cache.count (path)) continue;
            auto s = std::make_shared<Sample>();
            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (r.sample)))
            {
                const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 48000 * 60 * 3);
                s->data.setSize ((int) juce::jmin (2u, reader->numChannels), n);
                reader->read (&s->data, 0, n, 0, true, s->data.getNumChannels() > 1);
                s->rate = reader->sampleRate;
                // Boucle inscrite dans le fichier (WAV « smpl ») quand le SFZ n'en donne pas.
                (void) reader->metadataValues;
            }
            else ++missing;
            cache[path] = s;
        }
        inst.samples = std::move (cache);
        if (missing > 0 && inst.error.isEmpty()) inst.error = juce::String (missing) + juce::String::fromUTF8 (" échantillon(s) introuvable(s)");
    }
}

//==============================================================================
namespace ondefx
{
    bool& librariesTestMode() { static bool b = false; return b; }

    juce::File librariesFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde")
                   .getChildFile (juce::String::fromUTF8 (librariesTestMode() ? "Bibliothèques-tests" : "Bibliothèques"));
    }

    juce::Array<juce::File> libraryRoots()
    {
        juce::Array<juce::File> roots { librariesFolder() };
        for (auto& p : juce::StringArray::fromLines (prefs::file().getValue ("libraryFolders")))
            if (p.isNotEmpty() && juce::File (p).isDirectory()) roots.addIfNotAlreadyThere (juce::File (p));
        return roots;
    }

    juce::Array<juce::File> findInstruments (const juce::File& root)
    {
        juce::Array<juce::File> list;
        if (root.isDirectory())
            for (const auto& e : juce::RangedDirectoryIterator (root, true, "*.sfz", juce::File::findFiles))
                list.add (e.getFile());
        std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; });
        return list;
    }

    //==========================================================================
    struct SamplerPlugin::Voice
    {
        const sfz::Region* region = nullptr;
        std::shared_ptr<sfz::Sample> sample;
        int note = -1;
        bool active = false, held = false, releasing = false;
        double pos = 0, inc = 1;
        float gainL = 1, gainR = 1, env = 0, envTarget = 1;
        int stage = 0;                                   // 0 attaque, 1 maintien, 2 décroissance, 3 tenue, 4 relâchement
        float attackInc = 1, decayCoef = 0, sustain = 1, releaseCoef = 0.99f, holdLeft = 0;
        juce::uint32 age = 0, event = 0;
        int delayLeft = 0;
    };

    const char* SamplerPlugin::xmlTypeName = "ondeSampler";

    SamplerPlugin::SamplerPlugin (te::PluginCreationInfo info) : OndeEffect (info)
    {
        sfzPath.referTo (state, "sfz", getUndoManager(), {});
        declare ("volume", "Volume", { -30.0f, 12.0f }, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
        declare ("release", juce::String::fromUTF8 ("Relâchement"), { 0.1f, 4.0f }, 1.0f, [] (float v) { return juce::String (v, 2) + "x"; });
        declare ("attack", "Attaque", { 0.0f, 1.0f }, 0.0f, [] (float v) { return v < 0.005f ? juce::String::fromUTF8 ("naturelle") : juce::String (juce::roundToInt (v * 1000)) + " ms"; });
        declare ("tone", juce::String::fromUTF8 ("Brillance"), { 0.0f, 1.0f }, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        declare ("velsens", juce::String::fromUTF8 ("Sensibilité au toucher"), { 0.0f, 1.0f }, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        declare ("tune", "Accord", { -100.0f, 100.0f }, 0.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " ct"; });
        declare ("width", juce::String::fromUTF8 ("Largeur stéréo"), { 0.0f, 1.5f }, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        for (int i = 0; i < 96; ++i) voices.push_back (std::make_unique<Voice>());
        startLoading();
    }

    SamplerPlugin::~SamplerPlugin()
    {
        cancelPendingUpdate();
        stopLoading = true;
        if (loader.joinable()) loader.join();
    }

    juce::File SamplerPlugin::getInstrumentFile() const
    {
        const auto p = sfzPath.get();
        if (p.isEmpty()) return {};
        if (juce::File::isAbsolutePath (p)) return juce::File (p);
        return librariesFolder().getChildFile (p);            // chemin relatif au dossier des bibliothèques (projet portable)
    }

    std::shared_ptr<const sfz::Instrument> SamplerPlugin::getInstrument() const
    {
        const juce::ScopedLock sl (instrumentLock);
        return instrument;
    }

    void SamplerPlugin::loadInstrument (const juce::File& f)
    {
        const auto root = librariesFolder();
        sfzPath = f.isAChildOf (root) ? f.getRelativePathFrom (root) : f.getFullPathName();
    }

    void SamplerPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
    {
        OndeEffect::valueTreePropertyChanged (v, i);
        if (v == state && i.toString() == "sfz") triggerAsyncUpdate();
    }

    void SamplerPlugin::handleAsyncUpdate() { startLoading(); }

    void SamplerPlugin::startLoading()
    {
        const auto file = getInstrumentFile();
        if (file.getFullPathName() == loadedPath && (instrument != nullptr || loading)) return;
        loadedPath = file.getFullPathName();
        stopLoading = true;
        if (loader.joinable()) loader.join();
        stopLoading = false;
        if (file == juce::File()) { const juce::ScopedLock sl (instrumentLock); instrument.reset(); return; }
        loading = true; progress = 0.0f;
        loader = std::thread ([this, file]
        {
            auto inst = sfz::parse (file);
            progress = 0.1f;
            if (inst->error.isEmpty() || ! inst->regions.empty()) sfz::loadSamples (*inst, [this] { return stopLoading.load(); });
            if (stopLoading) { loading = false; return; }
            { const juce::ScopedLock sl (instrumentLock); instrument = inst; }
            currentSwitch = inst->swDefault;
            progress = 1.0f;
            loading = false;
        });
    }

    bool SamplerPlugin::waitUntilLoaded (int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (loading && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (10);
        return ! loading;
    }

    void SamplerPlugin::initialise (const te::PluginInitialisationInfo& info) { rate = info.sampleRate; reset(); }

    void SamplerPlugin::reset()
    {
        for (auto& v : voices) v->active = false;
        sustainDown = false; pitchBend = 0; lpState[0] = lpState[1] = 0;
        noteHeld.fill (false);
    }

    void SamplerPlugin::auditionNote (int note, float velocity)
    {
        const auto w = auditionFifo.write (1);
        if (w.blockSize1 > 0) auditionQueue[(size_t) w.startIndex1] = { note, velocity };
    }

    void SamplerPlugin::startRegion (const sfz::Region& r, int note, int velocity, float gainScale)
    {
        std::shared_ptr<sfz::Instrument> inst;
        { const juce::ScopedTryLock sl (instrumentLock); if (! sl.isLocked()) return; inst = instrument; }
        if (inst == nullptr) return;
        auto it = inst->samples.find (r.sample.getFullPathName());
        if (it == inst->samples.end() || it->second->data.getNumSamples() == 0) return;
        // Groupes exclusifs (charleston fermée qui coupe l'ouverte…).
        if (r.group != 0)
            for (auto& v : voices) if (v->active && v->region != nullptr && v->region->offBy == r.group && v->event != currentEvent) { v->releasing = true; v->stage = 4; v->releaseCoef = std::exp (-1.0f / (0.01f * (float) rate)); }
        Voice* v = nullptr;
        for (auto& x : voices) if (! x->active) { v = x.get(); break; }
        if (v == nullptr) { v = voices[0].get(); for (auto& x : voices) if ((x->releasing && ! v->releasing) || (x->releasing == v->releasing && x->age < v->age)) v = x.get(); }
        const float vel = velocity / 127.0f, velsens = value ("velsens");
        const float vt = r.ampVeltrack / 100.0f * velsens;
        float velGain = (1.0f - vt) + vt * vel * vel;
        if (! r.velCurve.empty())
        {
            // Courbe donnée par points (amp_velcurve_N), interpolée ; 0 -> 0 et 127 -> 1 si absents.
            std::vector<std::pair<int, float>> pts = r.velCurve;
            if (pts.front().first > 0) pts.insert (pts.begin(), { 0, 0.0f });
            if (pts.back().first < 127) pts.push_back ({ 127, 1.0f });
            for (size_t k = 1; k < pts.size(); ++k)
                if (velocity <= pts[k].first) { const float f = (float) (velocity - pts[k - 1].first) / (float) juce::jmax (1, pts[k].first - pts[k - 1].first); velGain = juce::jmap (f, pts[k - 1].second, pts[k].second); break; }
            velGain = (1.0f - velsens) + velsens * velGain;
        }
        const float gain = juce::Decibels::decibelsToGain (r.volume) * velGain * gainScale;
        const float pan = juce::jlimit (-1.0f, 1.0f, r.pan / 100.0f);
        v->region = &r; v->sample = it->second; v->note = note;
        v->active = true; v->held = true; v->releasing = false; v->age = ++voiceCounter;
        v->event = currentEvent;
        v->delayLeft = (int) (r.delay * rate);
        v->pos = (double) juce::jlimit<juce::int64> (0, (juce::int64) v->sample->data.getNumSamples() - 1, r.offset);
        const double semis = (note - r.keycenter) * (r.pitchKeytrack / 100.0) + r.transpose + (r.tune + value ("tune")) / 100.0;
        v->inc = std::pow (2.0, semis / 12.0) * v->sample->rate / rate;
        v->gainL = gain * std::sqrt (0.5f * (1.0f - pan)) * 1.414f;
        v->gainR = gain * std::sqrt (0.5f * (1.0f + pan)) * 1.414f;
        const float atk = juce::jmax (r.attack, value ("attack"));
        v->env = 0; v->stage = 0;
        v->attackInc = 1.0f / (float) juce::jmax (1.0, atk * rate);
        v->holdLeft = r.hold * (float) rate;
        v->sustain = juce::jlimit (0.0f, 1.0f, r.sustain / 100.0f);
        v->decayCoef = r.decay > 0 ? std::exp (-6.9f / (r.decay * (float) rate)) : 0.0f;
        v->releaseCoef = std::exp (-6.9f / (juce::jmax (0.005f, r.release * value ("release")) * (float) rate));
    }

    void SamplerPlugin::noteOn (int note, int velocity)
    {
        std::shared_ptr<sfz::Instrument> inst;
        { const juce::ScopedTryLock sl (instrumentLock); if (! sl.isLocked()) return; inst = instrument; }
        if (inst == nullptr) return;
        // Touches de commutation : changent l'articulation, ne jouent pas.
        bool isSwitch = (inst->swLo >= 0 && note >= inst->swLo && note <= inst->swHi) || inst->keyswitchNames[(size_t) note].isNotEmpty();
        if (! isSwitch) for (auto& r : inst->regions) if (r.swLast == note) { isSwitch = true; break; }
        if (isSwitch) { currentSwitch = note; return; }
        noteVelocity[(size_t) note] = velocity;
        noteHeld[(size_t) note] = true;
        const int counter = ++seqCounter[(size_t) note];
        ++currentEvent;                                    // zones d'une même touche : jamais coupées entre elles
        const float rnd = random.nextFloat();
        const int sw = currentSwitch.load();
        for (auto& r : inst->regions)
        {
            if (r.releaseTrigger || note < r.lokey || note > r.hikey || velocity < r.lovel || velocity > r.hivel) continue;
            if (r.swLast >= 0 && r.swLast != sw) continue;
            if (r.seqLength > 1 && ((counter - 1) % r.seqLength) + 1 != r.seqPosition) continue;
            if (rnd < r.lorand || rnd >= r.hirand) continue;
            startRegion (r, note, velocity, 1.0f);
        }
    }

    void SamplerPlugin::noteOff (int note)
    {
        noteHeld[(size_t) note] = false;
        for (auto& v : voices)
            if (v->active && v->held && v->note == note)
            {
                v->held = false;
                if (! sustainDown && ! v->region->oneShot) { v->releasing = true; v->stage = 4; }
            }
        // Échantillons de relâchement (bruit des étouffoirs, fin de coup d'archet…).
        std::shared_ptr<sfz::Instrument> inst;
        { const juce::ScopedTryLock sl (instrumentLock); if (! sl.isLocked()) return; inst = instrument; }
        if (inst == nullptr) return;
        const int velocity = juce::jmax (1, noteVelocity[(size_t) note]);
        const int sw = currentSwitch.load();
        for (auto& r : inst->regions)
            if (r.releaseTrigger && note >= r.lokey && note <= r.hikey && velocity >= r.lovel && velocity <= r.hivel && (r.swLast < 0 || r.swLast == sw))
                startRegion (r, note, velocity, 1.0f);
    }

    void SamplerPlugin::renderVoices (float* L, float* R, int start, int n)
    {
        for (auto& vp : voices)
        {
            auto& v = *vp;
            if (! v.active) continue;
            int skip = 0;
            if (v.delayLeft > 0) { skip = juce::jmin (n, v.delayLeft); v.delayLeft -= skip; if (skip >= n) continue; }
            const auto& data = v.sample->data;
            const int len = data.getNumSamples();
            const float* d0 = data.getReadPointer (0);
            const float* d1 = data.getNumChannels() > 1 ? data.getReadPointer (1) : d0;
            const auto& r = *v.region;
            const juce::int64 endPos = r.end > 0 ? juce::jmin<juce::int64> (r.end, len - 1) : len - 1;
            const bool looping = r.loop && r.loopEnd > r.loopStart && r.loopEnd < len;
            const double bend = std::pow (2.0, pitchBend * 2.0 / 12.0);
            for (int i = skip; i < n; ++i)
            {
                // Enveloppe.
                switch (v.stage)
                {
                    case 0: v.env += v.attackInc; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = v.holdLeft > 0 ? 1 : 2; } break;
                    case 1: if (--v.holdLeft <= 0) v.stage = 2; break;
                    case 2: if (v.decayCoef > 0) { v.env = v.sustain + (v.env - v.sustain) * v.decayCoef; if (std::abs (v.env - v.sustain) < 1.0e-4f) v.stage = 3; } else { v.env = v.sustain > 0 ? v.sustain : v.env; v.stage = 3; } break;
                    case 3: break;
                    default: v.env *= v.releaseCoef; if (v.env < 1.0e-4f) { v.active = false; } break;
                }
                if (! v.active) break;
                // Lecture interpolée (Hermite).
                const int p = (int) v.pos;
                const float t = (float) (v.pos - p);
                auto at = [&] (const float* d, int k) { return d[juce::jlimit (0, len - 1, k)]; };
                auto herm = [&] (const float* d)
                {
                    const float y0 = at (d, p - 1), y1 = at (d, p), y2 = at (d, p + 1), y3 = at (d, p + 2);
                    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    return ((c3 * t + c2) * t + c1) * t + y1;
                };
                const float sl = herm (d0), sr = herm (d1);
                L[start + i] += sl * v.gainL * v.env;
                R[start + i] += sr * v.gainR * v.env;
                v.pos += v.inc * bend;
                if (looping && v.pos >= (double) r.loopEnd && ! (v.releasing && r.loop && false)) v.pos -= (double) (r.loopEnd - r.loopStart);
                if (v.pos >= (double) endPos) { v.active = false; break; }
            }
        }
    }

    void SamplerPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr) return;
        auto& buf = *fc.destBuffer;
        const int n = fc.bufferNumSamples, start = fc.bufferStartSample;
        buf.clear (start, n);
        if (buf.getNumChannels() == 0) return;
        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : buf.getWritePointer (0);
        int pos = 0;
        auto renderTo = [&] (int end) { if (end > pos) { renderVoices (L, R, start + pos, end - pos); pos = end; } };
        if (fc.bufferForMidiMessages != nullptr)
        {
            fc.bufferForMidiMessages->sortByTimestamp();
            for (auto& msg : *fc.bufferForMidiMessages)
            {
                renderTo (juce::jlimit (0, n, juce::roundToInt (msg.getTimeStamp() * rate)));
                if (msg.isNoteOn()) noteOn (msg.getNoteNumber(), msg.getVelocity());
                else if (msg.isNoteOff()) noteOff (msg.getNoteNumber());
                else if (msg.isSustainPedalOn()) sustainDown = true;
                else if (msg.isSustainPedalOff())
                {
                    sustainDown = false;
                    for (auto& v : voices) if (v->active && ! v->held && ! v->releasing && ! v->region->oneShot) { v->releasing = true; v->stage = 4; }
                }
                else if (msg.isPitchWheel()) pitchBend = (msg.getPitchWheelValue() - 8192) / 8192.0f;
                else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) v->active = false;
            }
        }
        {
            const auto rd = auditionFifo.read (auditionFifo.getNumReady());
            for (int k = 0; k < rd.blockSize1 + rd.blockSize2; ++k)
            {
                const auto& [note, vel] = auditionQueue[(size_t) (k < rd.blockSize1 ? rd.startIndex1 + k : rd.startIndex2 + k - rd.blockSize1)];
                if (vel > 0.0f) noteOn (note, juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f))); else noteOff (note);
            }
        }
        renderTo (n);
        // Sortie : brillance (passe-bas doux), largeur stéréo, volume.
        const float gain = juce::Decibels::decibelsToGain (value ("volume")), width = value ("width");
        const float tone = value ("tone");
        const float cutoff = 800.0f * std::pow (25.0f, tone);
        const float a = tone >= 0.999f ? 1.0f : 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) rate);
        for (int i = start; i < start + n; ++i)
        {
            float l = L[i], r = R[i];
            if (a < 1.0f) { lpState[0] += a * (l - lpState[0]); lpState[1] += a * (r - lpState[1]); l = lpState[0]; r = lpState[1]; }
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
            L[i] = (mid + side) * gain;
            if (R != L) R[i] = (mid - side) * gain;
        }
    }
}
