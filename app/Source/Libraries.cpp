#include "Libraries.h"
#include "OndeSampler.h"
#include "Preferences.h"
#include "ParamControls.h"
#include <set>

namespace libraries
{
    const std::vector<Info>& catalog()
    {
        // Bibliothèques libres (licence CC0 : utilisables sans condition, sauf mention) ; tailles indiquées par GitHub.
        static const std::vector<Info> list = {
            { "vsco2", "Orchestre", "Orchestre VSCO 2", "Cordes (violons, altos, violoncelles, contrebasses : tenues, pizzicati, spiccati), cuivres, bois, percussions, claviers.", "CC0 (Versilian Studios)", 2280, "sgossner/VSCO-2-CE" },
            { "salamander", "Pianos", "Piano à queue Salamander", "Yamaha C5 enregistré note par note, 16 couches de vélocité, bruits d'étouffoirs.", "CC-BY 3.0 (Alexander Holm) : citer l'auteur", 714, "sfzinstruments/SalamanderGrandPiano" },
            { "scarypiano", "Pianos", "Piano droit Scary", "Piano droit au caractère marqué, plusieurs vélocités.", "CC0 (Karoryfer Samples)", 341, "sfzinstruments/karoryfer.scarypiano" },
            { "virtuosity", "Batteries", "Batterie Virtuosity", "Batterie acoustique complète, multi-micros, nombreuses variations par coup.", "CC0 (Versilian Studios, Karoryfer)", 1170, "sfzinstruments/virtuosity_drums" },
            { "meatbass", "Basses", "Basse électrique Meatbass", "Basse électrique aux doigts, notes étouffées, glissés.", "CC0 (Karoryfer Samples)", 244, "sfzinstruments/karoryfer.meatbass" },
            { "growlybass", "Basses", "Basse électrique Growly", "Basse au médiator, grain prononcé.", "CC0 (Karoryfer Samples)", 160, "sfzinstruments/karoryfer.growlybass" },
            { "doublebass", "Cordes", "Contrebasse", "Contrebasse jouée à l'archet et en pizzicato.", "CC0 (D. Smolken)", 253, "sfzinstruments/dsmolken.double-bass" },
            { "cello", "Cordes", "Violoncelle Bigcat", "Violoncelle solo, plusieurs articulations.", "CC0 (Karoryfer, Bigcat)", 126, "sfzinstruments/karoryfer-bigcat.cello" },
            { "shinyguitar", "Guitares", "Guitare électrique Shiny", "Guitare électrique claire, plusieurs articulations.", "CC0 (Karoryfer Samples)", 351, "sfzinstruments/karoryfer.shinyguitar" },
            { "emilyguitar", "Guitares", "Guitare Emily", "Guitare acoustique.", "CC0 (Karoryfer Samples)", 99, "sfzinstruments/karoryfer.emilyguitar" },
            { "weresax", "Bois", "Saxophone Weresax", "Saxophone ténor expressif.", "CC0 (Karoryfer Samples)", 188, "sfzinstruments/karoryfer.weresax" },
            { "wartuba", "Cuivres", "Tuba War Tuba", "Tuba, attaques et tenues.", "CC0 (Karoryfer Samples)", 104, "sfzinstruments/karoryfer.war-tuba" },
        };
        return list;
    }

    juce::File folderFor (const Info& i) { return ondefx::librariesFolder().getChildFile (i.id); }
    bool isInstalled (const Info& i)     { return ! ondefx::findInstruments (folderFor (i)).isEmpty(); }
    juce::String downloadUrl (const Info& i) { return "https://github.com/" + juce::String (i.repository) + "/archive/HEAD.zip"; }


    //==========================================================================
    // Instruments créés automatiquement depuis un dossier d'échantillons nommés (« spic_A2_v1_RR1.wav ») :
    // note, couche de vélocité (v1, v2… ou p, mf, f…), variation (rr1, rr2…) ; l'octave est vérifiée en mesurant
    // la hauteur réelle d'un échantillon (les bibliothèques ne numérotent pas toutes les octaves de la même façon).
    namespace
    {
        struct Parsed { juce::File file; int note = -1; int layer = 0; int rr = 1; juce::String prefix; };

        int dynamicRank (const juce::String& t)
        {
            static const char* order[] = { "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff" };
            for (int i = 0; i < 8; ++i) if (t == order[i]) return i + 1;
            return 0;
        }

        Parsed parseName (const juce::File& f)
        {
            Parsed p; p.file = f;
            const auto tokens = juce::StringArray::fromTokens (f.getFileNameWithoutExtension(), "_ -.", "");
            juce::StringArray prefix;
            for (auto& raw : tokens)
            {
                const auto t = raw.toLowerCase();
                if (p.note < 0 && t.length() >= 2 && t.length() <= 4 && juce::String ("abcdefg").containsChar (t[0]))
                {
                    int i = 1, n = juce::String ("c d ef g a b").indexOfChar (t[0]);
                    if (t[i] == '#' || t[i] == 's') { ++n; ++i; } else if (t[i] == 'b' && t.length() > 2) { --n; ++i; }
                    const auto oct = t.substring (i);
                    if (oct.isNotEmpty() && oct.containsOnly ("-0123456789")) { p.note = (oct.getIntValue() + 1) * 12 + n; continue; }
                }
                if (t.startsWith ("rr") && t.substring (2).containsOnly ("0123456789") && t.length() > 2) { p.rr = juce::jmax (1, t.substring (2).getIntValue()); continue; }
                if ((t.startsWith ("v") || t.startsWith ("vl")) && t.trimCharactersAtStart ("vl").containsOnly ("0123456789") && t.length() > 1) { p.layer = t.trimCharactersAtStart ("vl").getIntValue(); continue; }
                if (const int d = dynamicRank (t); d > 0) { p.layer = d; continue; }
                if (p.note < 0) prefix.add (raw);
            }
            p.prefix = prefix.joinIntoString (" ");
            return p;
        }

        // Fréquence fondamentale par autocorrélation (0,2 s à 0,6 s du début).
        double measurePitch (const juce::File& f)
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return 0;
            const int start = (int) (r->sampleRate * 0.15), len = (int) juce::jmin<juce::int64> ((juce::int64) (r->sampleRate * 0.4), r->lengthInSamples - start);
            if (len < 2048) return 0;
            juce::AudioBuffer<float> b (1, len);
            r->read (&b, 0, len, start, true, false);
            const float* x = b.getReadPointer (0);
            const int minLag = (int) (r->sampleRate / 2000.0), maxLag = juce::jmin (len / 2, (int) (r->sampleRate / 25.0));
            double energy = 0; for (int i = 0; i < len - maxLag; ++i) energy += x[i] * x[i];
            if (energy <= 1.0e-9) return 0;
            std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
            for (int lag = minLag; lag <= maxLag; ++lag)
            {
                double acf = 0, m = 0;
                for (int i = 0; i < len - maxLag; ++i) { acf += x[i] * x[i + lag]; m += x[i] * x[i] + x[i + lag] * x[i + lag]; }
                nsdf[(size_t) lag] = m > 0 ? 2.0 * acf / m : 0.0;
            }
            // Premier maximum important (évite les sous-octaves).
            double best = 0; int bestLag = 0;
            for (int lag = minLag + 1; lag < maxLag; ++lag) best = std::max (best, nsdf[(size_t) lag]);
            for (int lag = minLag + 1; lag < maxLag; ++lag)
                if (nsdf[(size_t) lag] > 0.9 * best && nsdf[(size_t) lag] >= nsdf[(size_t) lag - 1] && nsdf[(size_t) lag] >= nsdf[(size_t) lag + 1]) { bestLag = lag; break; }
            return bestLag > 0 && best > 0.5 ? r->sampleRate / bestLag : 0.0;
        }
    }

    int buildInstrumentsFromSamples (const juce::File& root, const juce::File& outDir, std::function<void (float)> progress)
    {
        // Échantillons regroupés par dossier et par préfixe (articulation).
        std::map<juce::String, std::vector<Parsed>> groups;
        std::map<juce::String, juce::String> names;
        juce::Array<juce::File> wavs;
        for (const auto& e : juce::RangedDirectoryIterator (root, true, "*.wav;*.flac;*.ogg;*.aif;*.aiff", juce::File::findFiles)) wavs.add (e.getFile());
        for (auto& f : wavs)
        {
            auto p = parseName (f);
            if (p.note < 0) continue;
            const auto key = f.getParentDirectory().getFullPathName() + "|" + p.prefix.toLowerCase();
            groups[key].push_back (p);
            auto rel = f.getParentDirectory().getRelativePathFrom (root).replaceCharacter ('\\', '/');
            auto parts = juce::StringArray::fromTokens (rel, "/", "");
            parts.removeEmptyStrings();
            while (parts.size() > 2) parts.remove (0);       // « Cello Section/spic »
            names[key] = parts.joinIntoString (" - ");
        }
        outDir.createDirectory();
        int made = 0, index = 0;
        for (auto& [key, samples] : groups)
        {
            if (progress) progress ((float) index++ / (float) juce::jmax<size_t> (1, groups.size()));
            std::set<int> notes;
            for (auto& s : samples) notes.insert (s.note);
            if (notes.size() < 3) continue;                                   // pas un instrument joué sur une tessiture
            // Octave : hauteur mesurée d'un échantillon du milieu de la tessiture.
            int offset = 0;
            {
                const int mid = *std::next (notes.begin(), (long) notes.size() / 2);
                for (auto& s : samples)
                    if (s.note == mid)
                    {
                        const double hz = measurePitch (s.file);
                        if (hz > 0)
                        {
                            const double measured = 69.0 + 12.0 * std::log2 (hz / 440.0);
                            offset = juce::jlimit (-24, 24, juce::roundToInt ((measured - mid) / 12.0) * 12);
                        }
                        break;
                    }
            }
            std::vector<int> layers;
            int maxRr = 1;
            for (auto& s : samples) { if (std::find (layers.begin(), layers.end(), s.layer) == layers.end()) layers.push_back (s.layer); maxRr = std::max (maxRr, s.rr); }
            std::sort (layers.begin(), layers.end());
            const std::vector<int> noteList (notes.begin(), notes.end());
            const auto artName = names[key] + (samples.front().prefix.isNotEmpty() && ! names[key].containsIgnoreCase (samples.front().prefix) ? " " + samples.front().prefix : juce::String());
            const auto lower = artName.toLowerCase();
            const bool shortArt = lower.contains ("spic") || lower.contains ("pizz") || lower.contains ("stac") || lower.contains ("hit") || lower.contains ("short");
            juce::String sfzText;
            sfzText << "// Instrument créé par Onde à partir des échantillons de « " << names[key] << " »\n";
            sfzText << "<global> ampeg_release=" << (shortArt ? "0.12" : "0.35") << (shortArt ? " loop_mode=one_shot" : "") << "\n";
            for (auto& s : samples)
            {
                const auto it = std::find (noteList.begin(), noteList.end(), s.note);
                const size_t i = (size_t) std::distance (noteList.begin(), it);
                const int lo = i == 0 ? s.note - 4 : (noteList[i - 1] + s.note) / 2 + 1;
                const int hi = i + 1 == noteList.size() ? s.note + 4 : (s.note + noteList[i + 1]) / 2;
                const size_t li = (size_t) std::distance (layers.begin(), std::find (layers.begin(), layers.end(), s.layer));
                const int lovel = 1 + (int) (127 * li / layers.size()), hivel = (int) (127 * (li + 1) / layers.size());
                sfzText << "<region> sample=" << s.file.getRelativePathFrom (outDir).replaceCharacter ('\\', '/')
                        << " pitch_keycenter=" << (s.note + offset) << " lokey=" << juce::jlimit (0, 127, lo + offset) << " hikey=" << juce::jlimit (0, 127, hi + offset)
                        << " lovel=" << lovel << " hivel=" << hivel;
                if (maxRr > 1) sfzText << " seq_length=" << maxRr << " seq_position=" << juce::jlimit (1, maxRr, s.rr);
                sfzText << "\n";
            }
            const auto file = outDir.getChildFile (juce::File::createLegalFileName (artName.trim()) + ".sfz");
            if (file.replaceWithText (sfzText)) ++made;
        }
        if (progress) progress (1.0f);
        return made;
    }

    juce::Result installFromZip (const Info& info, const juce::File& zipFile, std::function<void (float)> progress)
    {
        juce::ZipFile zip (zipFile);
        if (zip.getNumEntries() == 0) return juce::Result::fail (juce::String::fromUTF8 ("archive illisible"));
        const auto dest = folderFor (info);
        dest.deleteRecursively();
        if (! dest.createDirectory()) return juce::Result::fail (juce::String::fromUTF8 ("dossier impossible à créer : ") + dest.getFullPathName());
        // Les archives GitHub ont un dossier racine (« depot-master/ ») : il est retiré.
        juce::String prefix;
        if (auto* first = zip.getEntry (0)) if (first->filename.containsChar ('/')) prefix = first->filename.upToFirstOccurrenceOf ("/", true, false);
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry (i);
            if (progress) progress ((float) i / (float) zip.getNumEntries());
            auto name = e->filename;
            if (prefix.isNotEmpty() && name.startsWith (prefix)) name = name.substring (prefix.length());
            if (name.isEmpty() || name.endsWithChar ('/')) continue;
            const auto target = dest.getChildFile (name);
            target.getParentDirectory().createDirectory();
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
            if (in == nullptr) continue;
            target.deleteFile();
            juce::FileOutputStream out (target);
            if (! out.openedOk()) return juce::Result::fail (juce::String::fromUTF8 ("écriture impossible : ") + target.getFullPathName());
            out.writeFromInputStream (*in, -1);
        }
        // Bibliothèque sans fichiers SFZ (échantillons seuls, comme VSCO 2) : instruments créés à partir des noms.
        if (ondefx::findInstruments (dest).isEmpty()) buildInstrumentsFromSamples (dest, dest.getChildFile ("Instruments Onde"), nullptr);
        if (progress) progress (1.0f);
        return ondefx::findInstruments (dest).isEmpty() ? juce::Result::fail (juce::String::fromUTF8 ("aucun instrument SFZ dans cette archive")) : juce::Result::ok();
    }

    const Info* find (const juce::String& id)
    {
        for (auto& i : catalog()) if (id == i.id) return &i;
        return nullptr;
    }

    juce::Result downloadAndInstall (const Info& info, std::function<void (int, float, juce::int64)> progress, std::function<bool()> shouldStop)
    {
        const auto zip = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String ("onde-bibliotheque-") + info.id + ".zip");
        zip.deleteFile();
        int status = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("User-Agent: Onde")
                           .withConnectionTimeoutMs (20000)
                           .withNumRedirectsToFollow (8)
                           .withStatusCode (&status);
        auto in = juce::URL (downloadUrl (info)).createInputStream (options);
        if (in == nullptr) return juce::Result::fail (juce::String::fromUTF8 ("connexion impossible"));
        if (status >= 400) return juce::Result::fail (juce::String::fromUTF8 ("le serveur a répondu ") + juce::String (status));
        const juce::int64 expected = in->getTotalLength() > 0 ? in->getTotalLength() : (juce::int64) info.sizeMb * 1024 * 1024;
        {
            juce::FileOutputStream out (zip);
            if (! out.openedOk()) return juce::Result::fail (juce::String::fromUTF8 ("écriture impossible : ") + zip.getFullPathName());
            juce::HeapBlock<char> buffer (1 << 20);
            juce::int64 total = 0;
            for (;;)
            {
                if (shouldStop && shouldStop()) { out.flush(); zip.deleteFile(); return juce::Result::fail (juce::String::fromUTF8 ("annulé")); }
                const int n = in->read (buffer, 1 << 20);
                if (n <= 0) break;
                if (! out.write (buffer, (size_t) n)) return juce::Result::fail (juce::String::fromUTF8 ("disque plein ?"));
                total += n;
                if (progress) progress (0, juce::jlimit (0.0f, 1.0f, (float) total / (float) juce::jmax<juce::int64> (1, expected)), total);
            }
            out.flush();
            if (total < 1024) return juce::Result::fail (juce::String::fromUTF8 ("archive vide"));
        }
        const auto r = installFromZip (info, zip, [&] (float f) { if (progress) progress (1, f, 0); });
        zip.deleteFile();
        return r;
    }
}

//==============================================================================
// Téléchargement puis installation d'une bibliothèque, en arrière-plan (annulé si la fenêtre se ferme).
struct LibrariesPanel::Job
{
    enum State { idle, downloading, extracting, done, failed };
    const libraries::Info& info;
    std::atomic<int> state { idle };
    std::atomic<float> fraction { 0.0f };
    std::atomic<juce::int64> received { 0 }, total { 0 };
    std::atomic<bool> cancel { false };
    juce::String error;
    std::thread worker;

    explicit Job (const libraries::Info& i) : info (i) { if (libraries::isInstalled (i)) state = done; }
    ~Job() { cancel = true; if (worker.joinable()) worker.join(); }

    void start()
    {
        if (worker.joinable()) worker.join();
        error.clear(); cancel = false;
        state = downloading; fraction = 0; received = 0; total = (juce::int64) info.sizeMb * 1024 * 1024;
        worker = std::thread ([this]
        {
            const auto r = libraries::downloadAndInstall (info, [this] (int stage, float f, juce::int64 bytes)
            {
                state = stage == 0 ? downloading : extracting;
                fraction = f;
                if (stage == 0) received = bytes;
            }, [this] { return cancel.load(); });
            if (r.failed()) { error = r.getErrorMessage(); state = failed; } else state = done;
        });
    }
};

LibrariesPanel::LibrariesPanel (EditContext& c) : ctx (c)
{
    for (auto& i : libraries::catalog()) jobs.add (new Job (i));
    openFolder.setButtonText (U ("Ouvrir le dossier"));
    openFolder.onClick = [] { ondefx::librariesFolder().createDirectory(); ondefx::librariesFolder().startAsProcess(); };
    addFolder.setButtonText (U ("Ajouter mes bibliothèques SFZ…"));
    addFolder.setTooltip (U ("Un dossier de votre ordinateur contenant des instruments au format SFZ"));
    addFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (U ("Dossier de bibliothèques SFZ"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (! fc.getResult().isDirectory()) return;
            auto list = juce::StringArray::fromLines (prefs::file().getValue ("libraryFolders"));
            list.removeEmptyStrings();
            list.addIfNotAlreadyThere (fc.getResult().getFullPathName());
            prefs::file().setValue ("libraryFolders", list.joinIntoString ("\n"));
            if (ctx.setStatus) ctx.setStatus (U ("Dossier ajouté : ") + juce::String (ondefx::findInstruments (fc.getResult()).size()) + U (" instrument(s) SFZ"));
            repaint();
        });
    };
    for (auto* b : { &openFolder, &addFolder })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2e35));
        b->setColour (juce::TextButton::textColourOffId, theme::text);
        addAndMakeVisible (b);
    }
    setSize (820, 640);
    startTimerHz (8);
}

LibrariesPanel::~LibrariesPanel() { stopTimer(); jobs.clear(); }

juce::Rectangle<int> LibrariesPanel::rowBounds (int i) const { return { 20, top + i * rowH - scroll, getWidth() - 40, rowH - 8 }; }
juce::Rectangle<int> LibrariesPanel::buttonBounds (int i) const { auto r = rowBounds (i); return r.removeFromRight (170).reduced (12, 20); }

void LibrariesPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181a1f));
    g.setColour (theme::text); g.setFont (theme::ui (20.0f, true));
    g.drawText (U ("Bibliothèques d'instruments"), 20, 14, 500, 28, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::ui (12.5f));
    g.drawFittedText (U ("Instruments enregistrés (vrais pianos, cordes, cuivres, batteries…), libres de droits, joués par Onde Échantillonneur. "
                         "Un clic télécharge la bibliothèque (rien n'est téléchargé sans vous). Licence de chaque bibliothèque indiquée."),
                      juce::Rectangle<int> (20, 44, getWidth() - 40, 36), juce::Justification::topLeft, 2);
    g.saveState();
    g.reduceClipRegion (0, top - 6, getWidth(), getHeight() - top - 50);
    juce::String lastFamily;
    for (int i = 0; i < jobs.size(); ++i)
    {
        auto r = rowBounds (i);
        if (r.getBottom() < top - 6 || r.getY() > getHeight()) continue;
        auto& job = *jobs[i];
        const auto& info = job.info;
        g.setColour (juce::Colour (0xff1f2227)); g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (juce::Colour (0xff2b2f36)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        auto text = r.reduced (16, 8).withTrimmedRight (170);
        g.setColour (theme::accent); g.setFont (theme::ui (10.5f, true));
        g.drawText (juce::String::fromUTF8 (info.family).toUpperCase() + "  ·  " + juce::String (info.sizeMb >= 1000 ? juce::String (info.sizeMb / 1000.0, 1) + " Go" : juce::String (info.sizeMb) + " Mo"),
                    text.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (theme::text); g.setFont (theme::ui (14.0f, true));
        g.drawText (juce::String::fromUTF8 (info.name), text.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
        g.drawFittedText (juce::String::fromUTF8 (info.description) + U ("  Licence : ") + juce::String::fromUTF8 (info.licence), text, juce::Justification::topLeft, 2);
        // Bouton ou progression.
        auto b = buttonBounds (i).toFloat();
        const int st = job.state.load();
        if (st == Job::downloading || st == Job::extracting)
        {
            g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (b.withHeight (8.0f).withY (b.getCentreY() + 4.0f), 4.0f);
            g.setColour (theme::accent); g.fillRoundedRectangle (b.withHeight (8.0f).withY (b.getCentreY() + 4.0f).withWidth (b.getWidth() * job.fraction.load()), 4.0f);
            g.setColour (theme::text); g.setFont (theme::ui (11.5f));
            const juce::String what = st == Job::downloading ? juce::String (job.received.load() / (1024 * 1024)) + " / " + juce::String (job.total.load() / (1024 * 1024)) + " Mo"
                                                             : U ("Installation… ") + juce::String (juce::roundToInt (job.fraction.load() * 100)) + " %";
            g.drawText (what, b.withHeight (16.0f).withY (b.getCentreY() - 14.0f).toNearestInt(), juce::Justification::centred);
        }
        else
        {
            const bool installed = st == Job::done;
            g.setColour (installed ? juce::Colour (0xff23332a) : theme::accent.darker (0.2f)); g.fillRoundedRectangle (b, 6.0f);
            g.setColour (installed ? theme::ok : juce::Colours::white); g.setFont (theme::ui (12.5f, true));
            g.drawText (installed ? U ("Installée · retirer") : st == Job::failed ? U ("Réessayer") : U ("Télécharger"), b.toNearestInt(), juce::Justification::centred);
            if (st == Job::failed) { g.setColour (theme::rec); g.setFont (theme::ui (10.5f)); g.drawText (job.error, r.reduced (16, 4).removeFromBottom (14), juce::Justification::centredLeft, true); }
        }
    }
    g.restoreState();
}

void LibrariesPanel::resized()
{
    auto b = getLocalBounds().reduced (20, 12).removeFromBottom (30);
    openFolder.setBounds (b.removeFromLeft (150));
    b.removeFromLeft (8);
    addFolder.setBounds (b.removeFromLeft (240));
}

void LibrariesPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int maxScroll = juce::jmax (0, jobs.size() * rowH - (getHeight() - top - 50));
    scroll = juce::jlimit (0, maxScroll, scroll - juce::roundToInt (w.deltaY * 200.0f));
    repaint();
}

void LibrariesPanel::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < jobs.size(); ++i)
        if (buttonBounds (i).contains (e.getPosition()) && e.y > top - 6)
        {
            auto& job = *jobs[i];
            const int st = job.state.load();
            if (st == Job::done)
            {
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, U ("Retirer la bibliothèque ?"),
                    U ("« ") + juce::String::fromUTF8 (job.info.name) + U (" » sera supprimée de cet ordinateur (elle pourra être téléchargée à nouveau)."),
                    U ("Retirer"), U ("Annuler"), nullptr,
                    juce::ModalCallbackFunction::create ([this, i] (int r) { if (r == 1) { libraries::folderFor (jobs[i]->info).deleteRecursively(); jobs[i]->state = Job::idle; repaint(); } }));
            }
            else if (st == Job::idle || st == Job::failed) start (i);
            return;
        }
}

void LibrariesPanel::start (int i) { jobs[i]->start(); repaint(); }

void LibrariesPanel::timerCallback()
{
    for (auto* j : jobs) if (j->state == Job::downloading || j->state == Job::extracting) { repaint(); return; }
}

juce::DialogWindow* LibrariesPanel::show (EditContext& c)
{
    return ondeui::showToolWindow (U ("Bibliothèques d'instruments"), new LibrariesPanel (c), false, juce::Colour (0xff181a1f));
}
