// Installateur d'Onde pour Windows (programme à part, aux couleurs d'Onde). Le contenu à installer est une archive
// zip ajoutée à la fin de l'exécutable, suivie de son emplacement et de la signature « ONDEPAK1 ».
//   Onde-Setup.exe                          étapes : bienvenue, dossier et options, installation, terminé
//   Onde-Setup.exe --silencieux [--dossier D] installation sans fenêtre (mises à jour automatiques, tests)
//   « Désinstaller Onde.exe » --desinstaller   (copie de l'installateur sans l'archive, dans le dossier d'Onde)
// Installation pour l'utilisateur courant (sans droits administrateur) : %LOCALAPPDATA%\Programs\Onde par défaut.
// Les projets et réglages ne sont jamais touchés (ils sont ailleurs), sauf si on le demande à la désinstallation.
#include <JuceHeader.h>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

namespace
{
    juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
    const juce::Colour bg (0xff16181c), panel (0xff1f2227), line (0xff2b2f36), txt (0xffdfe2e6), muted (0xff8e949c), accent (0xff3fa4ff), ok (0xff4fc06b), bad (0xffe8473f);
    const char* uninstallKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Onde\\";

    juce::Font ui (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }

    // Archive embarquée : [exécutable][zip][position du zip sur 8 octets][« ONDEPAK1 »].
    struct Payload
    {
        juce::int64 offset = -1, length = 0;
        juce::String version;
        bool valid() const { return offset > 0 && length > 0; }
        static Payload find()
        {
            Payload p;
            const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            juce::FileInputStream in (self);
            if (! in.openedOk() || in.getTotalLength() < 32) return p;
            in.setPosition (in.getTotalLength() - 16);
            const auto off = in.readInt64();
            char magic[9] {}; in.read (magic, 8);
            if (juce::String (magic) != "ONDEPAK1" || off <= 0 || off >= in.getTotalLength() - 16) return p;
            p.offset = off; p.length = in.getTotalLength() - 16 - off;
            // Version : fichier version.txt de l'archive.
            if (auto zip = p.open())
                if (auto* e = zip->getEntry ("version.txt"))
                    if (auto s = std::unique_ptr<juce::InputStream> (zip->createStreamForEntry (*e))) p.version = s->readEntireStreamAsString().trim();
            return p;
        }
        std::unique_ptr<juce::ZipFile> open() const
        {
            auto* file = new juce::FileInputStream (juce::File::getSpecialLocation (juce::File::currentExecutableFile));
            return std::make_unique<juce::ZipFile> (new juce::SubregionStream (file, offset, length, true), true);
        }
    };

    juce::File defaultFolder() { return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs").getChildFile ("Onde"); }

    juce::File installedFolder()
    {
        const auto loc = juce::WindowsRegistry::getValue (juce::String (uninstallKey) + "InstallLocation");
        return loc.isNotEmpty() ? juce::File (loc) : juce::File();
    }

    // Onde (ou ses processus isolés) en cours d'exécution depuis ce dossier.
    juce::StringArray runningFrom (const juce::File& folder)
    {
        juce::StringArray found;
        HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return found;
        PROCESSENTRY32W pe { sizeof (pe) };
        for (BOOL more = Process32FirstW (snap, &pe); more; more = Process32NextW (snap, &pe))
        {
            if (HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID))
            {
                wchar_t path[MAX_PATH * 2]; DWORD n = (DWORD) std::size (path);
                if (QueryFullProcessImageNameW (h, 0, path, &n) && juce::File (juce::String (path)).isAChildOf (folder))
                    found.addIfNotAlreadyThere (juce::File (juce::String (path)).getFileName());
                CloseHandle (h);
            }
        }
        CloseHandle (snap);
        return found;
    }

    bool makeShortcut (const juce::File& lnk, const juce::File& target, const juce::String& description)
    {
        IShellLinkW* link = nullptr;
        if (FAILED (CoCreateInstance (CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**) &link))) return false;
        link->SetPath (target.getFullPathName().toWideCharPointer());
        link->SetWorkingDirectory (target.getParentDirectory().getFullPathName().toWideCharPointer());
        link->SetIconLocation (target.getFullPathName().toWideCharPointer(), 0);
        link->SetDescription (description.toWideCharPointer());
        IPersistFile* file = nullptr;
        bool ok = false;
        if (SUCCEEDED (link->QueryInterface (IID_IPersistFile, (void**) &file)))
        {
            lnk.getParentDirectory().createDirectory();
            ok = SUCCEEDED (file->Save (lnk.getFullPathName().toWideCharPointer(), TRUE));
            file->Release();
        }
        link->Release();
        return ok;
    }

    juce::File startMenuLink() { return juce::File (juce::SystemStats::getEnvironmentVariable ("APPDATA", {})).getChildFile ("Microsoft").getChildFile ("Windows").getChildFile ("Start Menu").getChildFile ("Programs").getChildFile ("Onde.lnk"); }
    juce::File desktopLink()   { return juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("Onde.lnk"); }

    struct Options { juce::File folder; bool desktop = true, startMenu = true, associate = true, registerApp = true; };

    // Installation (fil de travail) : extraction, désinstalleur, raccourcis, « Applications installées », fichiers .onde.
    juce::Result install (const Payload& payload, const Options& o, std::function<void (double, const juce::String&)> progress)
    {
        auto zip = payload.open();
        if (zip == nullptr || zip->getNumEntries() == 0) return juce::Result::fail (U ("archive d'installation illisible"));
        if (! o.folder.createDirectory()) return juce::Result::fail (U ("impossible de créer ") + o.folder.getFullPathName());
        juce::int64 total = 0, done = 0;
        for (int i = 0; i < zip->getNumEntries(); ++i) total += zip->getEntry (i)->uncompressedSize;
        for (int i = 0; i < zip->getNumEntries(); ++i)
        {
            const auto* e = zip->getEntry (i);
            progress (total > 0 ? (double) done / (double) total : 0.0, U ("Copie de ") + e->filename);
            const auto r = zip->uncompressEntry (i, o.folder, true);
            if (r.failed()) return juce::Result::fail (e->filename + " : " + r.getErrorMessage());
            done += e->uncompressedSize;
        }
        progress (0.97, U ("Désinstalleur, raccourcis…"));
        // Désinstalleur : cet exécutable sans l'archive.
        {
            const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            juce::FileInputStream in (self);
            const auto un = o.folder.getChildFile (U ("Désinstaller Onde.exe"));
            un.deleteFile();
            juce::FileOutputStream out (un);
            if (in.openedOk() && out.openedOk()) out.writeFromInputStream (in, payload.offset);
        }
        o.folder.getChildFile ("version.txt").replaceWithText (payload.version);
        const auto exe = o.folder.getChildFile ("Onde.exe");
        CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        startMenuLink().deleteFile(); desktopLink().deleteFile();
        if (o.startMenu) makeShortcut (startMenuLink(), exe, U ("Onde — station de travail audio"));
        if (o.desktop)   makeShortcut (desktopLink(), exe, U ("Onde — station de travail audio"));
        CoUninitialize();
        if (! o.registerApp) { progress (1.0, U ("Terminé")); return juce::Result::ok(); }
        const juce::String k (uninstallKey);
        juce::WindowsRegistry::setValue (k + "DisplayName", juce::String ("Onde"));
        juce::WindowsRegistry::setValue (k + "DisplayVersion", payload.version);
        juce::WindowsRegistry::setValue (k + "Publisher", juce::String ("Onde"));
        juce::WindowsRegistry::setValue (k + "DisplayIcon", exe.getFullPathName() + ",0");
        juce::WindowsRegistry::setValue (k + "InstallLocation", o.folder.getFullPathName());
        juce::WindowsRegistry::setValue (k + "UninstallString", "\"" + o.folder.getChildFile (U ("Désinstaller Onde.exe")).getFullPathName() + "\" --desinstaller");
        juce::WindowsRegistry::setValue (k + "NoModify", (juce::uint32) 1);
        juce::WindowsRegistry::setValue (k + "NoRepair", (juce::uint32) 1);
        juce::WindowsRegistry::setValue (k + "EstimatedSize", (juce::uint32) (total / 1024));
        if (o.associate)
        {
            // Double-clic sur un projet .onde : il s'ouvre dans Onde.
            juce::WindowsRegistry::setValue ("HKEY_CURRENT_USER\\Software\\Classes\\.onde\\", juce::String ("Onde.Projet"));
            juce::WindowsRegistry::setValue ("HKEY_CURRENT_USER\\Software\\Classes\\Onde.Projet\\", U ("Projet Onde"));
            juce::WindowsRegistry::setValue ("HKEY_CURRENT_USER\\Software\\Classes\\Onde.Projet\\DefaultIcon\\", exe.getFullPathName() + ",0");
            juce::WindowsRegistry::setValue ("HKEY_CURRENT_USER\\Software\\Classes\\Onde.Projet\\shell\\open\\command\\", "\"" + exe.getFullPathName() + "\" \"%1\"");
            SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        }
        progress (1.0, U ("Terminé"));
        return juce::Result::ok();
    }

    juce::Result uninstall (const juce::File& folder, bool removeSettings)
    {
        if (! folder.isDirectory() || ! folder.getChildFile ("Onde.exe").existsAsFile()) return juce::Result::fail (U ("Onde n'est pas installé dans ") + folder.getFullPathName());
        // Raccourcis, inscription et fichiers .onde : seulement s'ils désignent ce dossier (une copie de test ou un
        // autre dossier ne touche jamais l'installation enregistrée).
        if (installedFolder() == folder)
        {
            startMenuLink().deleteFile(); desktopLink().deleteFile();
            if (juce::WindowsRegistry::getValue ("HKEY_CURRENT_USER\\Software\\Classes\\.onde\\") == "Onde.Projet")
            {
                juce::WindowsRegistry::deleteKey ("HKEY_CURRENT_USER\\Software\\Classes\\.onde");
                juce::WindowsRegistry::deleteKey ("HKEY_CURRENT_USER\\Software\\Classes\\Onde.Projet");
                SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            }
            juce::WindowsRegistry::deleteKey (juce::String (uninstallKey).dropLastCharacters (1));
        }
        if (removeSettings) juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde").deleteRecursively();
        if (! folder.deleteRecursively()) return juce::Result::fail (U ("certains fichiers n'ont pas pu être supprimés (Onde est-il ouvert ?)"));
        return juce::Result::ok();
    }

    //==============================================================================
    class Button  : public juce::TextButton
    {
    public:
        Button (const juce::String& t, bool primary = false) : juce::TextButton (t)
        {
            setColour (juce::TextButton::buttonColourId, primary ? accent.darker (0.15f) : juce::Colour (0xff2a2e35));
            setColour (juce::TextButton::textColourOffId, primary ? juce::Colours::white : txt);
        }
    };

    class SetupComponent  : public juce::Component, private juce::Timer
    {
    public:
        SetupComponent (Payload p, bool uninstallMode, juce::File uninstallFolder)
            : payload (std::move (p)), uninstalling (uninstallMode), target (uninstallFolder)
        {
            logo = juce::ImageCache::getFromMemory (nullptr, 0);
            const auto previous = installedFolder();
            options.folder = previous.isDirectory() ? previous : defaultFolder();
            update = previous.getChildFile ("Onde.exe").existsAsFile();
            previousVersion = previous.getChildFile ("version.txt").loadFileAsString().trim();

            folderEdit.setText (options.folder.getFullPathName(), false);
            folderEdit.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111316));
            folderEdit.setColour (juce::TextEditor::outlineColourId, line);
            folderEdit.setColour (juce::TextEditor::textColourId, txt);
            browse.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> (U ("Dossier d'installation"), juce::File (folderEdit.getText()));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult() != juce::File()) folderEdit.setText (fc.getResult().getChildFile (fc.getResult().getFileName() == "Onde" ? "" : "Onde").getFullPathName(), false);
                });
            };
            for (auto* t : { &desktop, &startMenu, &associate, &launch, &removeSettings })
            {
                t->setColour (juce::ToggleButton::textColourId, txt);
                t->setColour (juce::ToggleButton::tickColourId, accent);
            }
            desktop.setToggleState (true, juce::dontSendNotification);
            startMenu.setToggleState (true, juce::dontSendNotification);
            associate.setToggleState (true, juce::dontSendNotification);
            launch.setToggleState (true, juce::dontSendNotification);
            next.onClick = [this] { goNext(); };
            back.onClick = [this] { if (page > 0 && page < 2) { page--; refresh(); } };
            cancel.onClick = [] { juce::JUCEApplication::quit(); };
            for (auto* c : std::initializer_list<juce::Component*> { &folderEdit, &browse, &desktop, &startMenu, &associate, &launch, &removeSettings, &next, &back, &cancel })
                addChildComponent (c);
            addChildComponent (bar);
            bar.setColour (juce::ProgressBar::foregroundColourId, accent);
            bar.setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xff111316));
            setSize (720, 460);
            refresh();
        }
        ~SetupComponent() override { if (worker.joinable()) worker.join(); }
        void showStep (int p) { page = p; refresh(); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (bg);
            auto side = getLocalBounds().removeFromLeft (220);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272b33), 0, 0, juce::Colour (0xff131519), 0, (float) getHeight(), false));
            g.fillRect (side);
            drawWave (g, juce::Rectangle<float> (30.0f, 40.0f, 160.0f, 90.0f));
            g.setColour (txt); g.setFont (ui (36.0f, true));
            g.drawText ("onde", 30, 140, 180, 44, juce::Justification::centredLeft);
            g.setColour (muted); g.setFont (ui (12.5f));
            g.drawText (U ("Station de travail audio"), 32, 182, 180, 18, juce::Justification::centredLeft);
            g.drawText (U ("Version ") + (payload.version.isNotEmpty() ? payload.version : juce::String ("?")), 32, 200, 180, 18, juce::Justification::centredLeft);
            // Étapes.
            const char* steps[] = { "Bienvenue", "Dossier et options", "Installation", "Terminé" };
            if (! uninstalling)
                for (int i = 0; i < 4; ++i)
                {
                    g.setColour (i == page ? accent : i < page ? ok : muted);
                    g.fillEllipse (32.0f, 260.0f + i * 30.0f, 10.0f, 10.0f);
                    g.setColour (i == page ? txt : muted); g.setFont (ui (13.0f, i == page));
                    g.drawText (U (steps[i]), 52, 254 + i * 30, 160, 22, juce::Justification::centredLeft);
                }
            auto r = getLocalBounds().withTrimmedLeft (220).reduced (34, 30);
            g.setColour (txt); g.setFont (ui (22.0f, true));
            g.drawText (title, r.removeFromTop (34), juce::Justification::centredLeft);
            r.removeFromTop (8);
            g.setColour (status == Status::failed ? bad : juce::Colour (0xffc3c8cf)); g.setFont (ui (13.5f));
            g.drawFittedText (body, r.removeFromTop (110), juce::Justification::topLeft, 7);
            if (! uninstalling && page == 0)
            {
                const char* features[] = { "Instruments prêts à jouer : pianos, guitares 6 à 12 cordes, basses, synthés, batteries",
                                           "Batteur automatique, amplis guitare et basse, effets de studio",
                                           "Mixage : analyse LUFS, écoute mono, VCA, gel de pistes",
                                           "Plugins VST3 isolés : un plugin qui plante ne fait pas tomber Onde" };
                auto list = r.removeFromTop (130);
                for (auto* f : features)
                {
                    auto row = list.removeFromTop (28);
                    g.setColour (accent); g.fillEllipse ((float) row.getX() + 2.0f, (float) row.getCentreY() - 3.0f, 6.0f, 6.0f);
                    g.setColour (txt.withAlpha (0.85f)); g.setFont (ui (12.5f));
                    g.drawText (U (f), row.withTrimmedLeft (16), juce::Justification::centredLeft, true);
                }
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().withTrimmedLeft (220).reduced (34, 30);
            auto buttons = r.removeFromBottom (34);
            cancel.setBounds (buttons.removeFromLeft (110));
            next.setBounds (buttons.removeFromRight (150)); buttons.removeFromRight (8);
            back.setBounds (buttons.removeFromRight (110));
            r.removeFromTop (160);
            auto row = r.removeFromTop (30);
            browse.setBounds (row.removeFromRight (110)); row.removeFromRight (8);
            folderEdit.setBounds (row);
            r.removeFromTop (14);
            for (auto* t : { &desktop, &startMenu, &associate }) { t->setBounds (r.removeFromTop (26)); r.removeFromTop (2); }
            removeSettings.setBounds (getLocalBounds().withTrimmedLeft (254).withY (200).withHeight (26).withWidth (420));
            bar.setBounds (getLocalBounds().withTrimmedLeft (254).withY (260).withHeight (14).withWidth (430));
            launch.setBounds (getLocalBounds().withTrimmedLeft (254).withY (230).withHeight (26).withWidth (300));
        }

    private:
        enum class Status { idle, running, done, failed };

        static void drawWave (juce::Graphics& g, juce::Rectangle<float> r)
        {
            juce::Path p;
            for (int i = 0; i <= 80; ++i)
            {
                const float t = i / 80.0f, env = std::sin (t * juce::MathConstants<float>::pi);
                const float x = r.getX() + r.getWidth() * t, y = r.getCentreY() - std::sin (t * juce::MathConstants<float>::twoPi * 2.5f) * env * r.getHeight() * 0.42f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a6bff), r.getX(), 0, juce::Colour (0xff2de2c6), r.getRight(), 0, false));
            g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void refresh()
        {
            for (auto* c : getChildren()) c->setVisible (false);
            cancel.setVisible (status != Status::running);
            if (uninstalling)
            {
                title = U ("Désinstaller Onde");
                if (status == Status::idle)
                {
                    body = U ("Onde va être retiré de ") + target.getFullPathName() + U (".\n\nVos projets ne sont jamais supprimés.");
                    removeSettings.setButtonText (U ("Supprimer aussi mes réglages, modèles et sons de métronome"));
                    removeSettings.setVisible (true);
                    next.setButtonText (U ("Désinstaller")); next.setVisible (true);
                }
                else if (status == Status::done) { body = U ("Onde a été désinstallé."); cancel.setButtonText ("Fermer"); }
                repaint(); return;
            }
            switch (page)
            {
                case 0:
                    title = update ? U ("Mettre à jour Onde") : U ("Installer Onde");
                    body = update ? U ("Onde ") + previousVersion + U (" est installé : il sera remplacé par la version ") + payload.version
                                    + U (".\nVos projets, modèles et réglages sont conservés.")
                                  : U ("Bienvenue ! Onde est une station de travail audio : instruments prêts à jouer, batteur, amplis, "
                                       "mixage, séparation de sources, plugins VST3 isolés.\n\nL'installation ne demande pas de droits "
                                       "d'administrateur et prend environ une minute.");
                    next.setButtonText ("Suivant"); next.setVisible (true);
                    break;
                case 1:
                    title = U ("Dossier et options");
                    body = U ("Onde sera installé pour vous seul, dans ce dossier :");
                    for (auto* c : std::initializer_list<juce::Component*> { &folderEdit, &browse, &desktop, &startMenu, &associate, &back }) c->setVisible (true);
                    next.setButtonText (update ? U ("Mettre à jour") : U ("Installer")); next.setVisible (true);
                    break;
                case 2:
                    title = U ("Installation…");
                    bar.setVisible (true);
                    break;
                default:
                    title = status == Status::failed ? U ("L'installation a échoué") : U ("Onde est installé");
                    if (status != Status::failed)
                    {
                        body = U ("Onde est prêt. Retrouvez-le dans le menu Démarrer") + (options.desktop ? U (" et sur le Bureau") : juce::String())
                             + U (".\nPour le retirer : Paramètres de Windows › Applications.");
                        launch.setVisible (true);
                    }
                    next.setButtonText (U ("Fermer")); next.setVisible (true);
                    break;
            }
            resized();
            repaint();
        }

        void goNext()
        {
            if (uninstalling)
            {
                if (status == Status::done) { juce::JUCEApplication::quit(); return; }
                const auto running = runningFrom (target);
                if (! running.isEmpty()) { body = U ("Onde est ouvert (") + running.joinIntoString (", ") + U (") : fermez-le puis cliquez à nouveau."); repaint(); return; }
                const auto r = uninstall (target, removeSettings.getToggleState());
                status = r.wasOk() ? Status::done : Status::failed;
                if (r.failed()) body = r.getErrorMessage();
                next.setVisible (false);
                refresh();
                return;
            }
            if (page == 0) { page = 1; refresh(); return; }
            if (page == 1)
            {
                options.folder = juce::File (folderEdit.getText().trim());
                options.desktop = desktop.getToggleState(); options.startMenu = startMenu.getToggleState(); options.associate = associate.getToggleState();
                const auto running = runningFrom (options.folder);
                if (! running.isEmpty()) { body = U ("Onde est ouvert (") + running.joinIntoString (", ") + U (") : fermez-le puis cliquez à nouveau sur « Installer »."); repaint(); return; }
                page = 2; status = Status::running; refresh();
                worker = std::thread ([this]
                {
                    const auto r = install (payload, options, [this] (double p, const juce::String& what)
                    {
                        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SetupComponent> (this), p, what]
                        { if (safe) { safe->progressValue = p; safe->body = what; safe->repaint(); } });
                    });
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SetupComponent> (this), r]
                    {
                        if (safe == nullptr) return;
                        safe->status = r.wasOk() ? Status::done : Status::failed;
                        if (r.failed()) safe->body = r.getErrorMessage();
                        safe->page = 3;
                        safe->refresh();
                    });
                });
                return;
            }
            if (page == 3)
            {
                if (status == Status::done && launch.getToggleState())
                    options.folder.getChildFile ("Onde.exe").startAsProcess();
                juce::JUCEApplication::quit();
            }
        }

        void timerCallback() override {}

        Payload payload;
        bool uninstalling = false, update = false;
        juce::File target;
        juce::String previousVersion, title, body;
        Options options;
        int page = 0;
        Status status = Status::idle;
        double progressValue = 0.0;
        juce::Image logo;
        juce::TextEditor folderEdit;
        Button browse { U ("Parcourir…") }, next { "Suivant", true }, back { U ("Retour") }, cancel { "Annuler" };
        juce::ToggleButton desktop { U ("Raccourci sur le Bureau") }, startMenu { U ("Raccourci dans le menu Démarrer") },
                           associate { U ("Ouvrir les projets .onde avec Onde (double-clic)") }, launch { U ("Lancer Onde maintenant") },
                           removeSettings;
        juce::ProgressBar bar { progressValue };
        std::unique_ptr<juce::FileChooser> chooser;
        std::thread worker;
    };

    class SetupWindow  : public juce::DocumentWindow
    {
    public:
        SetupWindow (const juce::String& name, juce::Component* content) : juce::DocumentWindow (name, bg, juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::quit(); }
    };
}

//==============================================================================
class OndeSetupApp  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return U ("Installation d'Onde"); }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        auto payload = Payload::find();
        // Désinstallation : relancée depuis une copie temporaire (le désinstalleur est dans le dossier à supprimer).
        if (args.contains ("--desinstaller"))
        {
            const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            const int at = args.indexOf ("--depuis");
            juce::File folder = at >= 0 ? juce::File (args[at + 1].unquoted()) : self.getParentDirectory();
            const bool silent = args.contains ("--silencieux");
            // Copie temporaire : attend que le désinstalleur du dossier soit fermé (sinon son fichier reste verrouillé).
            if (const int w = args.indexOf ("--attendre"); w >= 0)
                if (HANDLE h = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) args[w + 1].getIntValue())) { WaitForSingleObject (h, 15000); CloseHandle (h); }
            if (self.isAChildOf (folder))
            {
                // Un programme ne peut pas supprimer le dossier d'où il tourne : copie temporaire, qui finit le travail.
                const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (U ("onde-desinstaller.exe"));
                juce::ChildProcess child;
                juce::StringArray cmd { temp.getFullPathName(), "--desinstaller", "--depuis", folder.getFullPathName(),
                                        "--attendre", juce::String ((int) GetCurrentProcessId()) };
                if (silent) cmd.add ("--silencieux");
                if (self.copyFileTo (temp) && child.start (cmd, 0)) { quit(); return; }
            }
            else if (self.getFileName() == U ("onde-desinstaller.exe") && self.isAChildOf (juce::File::getSpecialLocation (juce::File::tempDirectory)))
            {
                // Copie temporaire : supprimée au prochain redémarrage.
                MoveFileExW (self.getFullPathName().toWideCharPointer(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            }
            if (silent) { const auto r = uninstall (folder, false); setApplicationReturnValue (r.wasOk() ? 0 : 1); quit(); return; }
            window = std::make_unique<SetupWindow> (U ("Désinstaller Onde"), new SetupComponent (payload, true, folder));
            return;
        }
        if (const int cap = args.indexOf ("--capture"); cap >= 0)
        {
            // Vérification de la mise en page : images des étapes (bienvenue, options) et de la désinstallation.
            const juce::File dir (args[cap + 1].unquoted());
            dir.createDirectory();
            SetupComponent setup (payload, false, {});
            setup.getChildComponent (0);
            for (int step = 0; step < 2; ++step)
            {
                if (step == 1) setup.showStep (1);
                juce::PNGImageFormat png;
                juce::FileOutputStream os (dir.getChildFile ("installateur-" + juce::String (step + 1) + ".png"));
                os.setPosition (0); os.truncate();
                png.writeImageToStream (setup.createComponentSnapshot (setup.getLocalBounds(), true, 1.0f), os);
            }
            SetupComponent un (payload, true, defaultFolder());
            juce::PNGImageFormat png;
            juce::FileOutputStream os (dir.getChildFile ("installateur-desinstaller.png"));
            os.setPosition (0); os.truncate();
            png.writeImageToStream (un.createComponentSnapshot (un.getLocalBounds(), true, 1.0f), os);
            quit();
            return;
        }
        if (! payload.valid())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, U ("Installation d'Onde"), U ("Cet installateur est incomplet (archive absente)."));
            setApplicationReturnValue (1);
            juce::Timer::callAfterDelay (100, [] { quit(); });
            return;
        }
        if (args.contains ("--silencieux"))
        {
            Options o;
            const int at = args.indexOf ("--dossier");
            const auto previous = installedFolder();
            o.folder = at >= 0 ? juce::File (args[at + 1].unquoted()) : previous.isDirectory() ? previous : defaultFolder();
            o.desktop = ! args.contains ("--sans-raccourcis") && desktopLink().existsAsFile();
            o.startMenu = ! args.contains ("--sans-raccourcis");
            o.associate = ! args.contains ("--sans-raccourcis");
            o.registerApp = ! args.contains ("--sans-raccourcis");      // tests : ni raccourcis ni « Applications installées »
            const auto r = install (payload, o, [] (double, const juce::String&) {});
            std::printf ("%s\n", r.wasOk() ? ("Onde " + payload.version + " installé dans " + o.folder.getFullPathName()).toRawUTF8() : r.getErrorMessage().toRawUTF8());
            setApplicationReturnValue (r.wasOk() ? 0 : 1);
            quit();
            return;
        }
        window = std::make_unique<SetupWindow> (U ("Installation d'Onde"), new SetupComponent (payload, false, {}));
    }
    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<SetupWindow> window;
};

START_JUCE_APPLICATION (OndeSetupApp)
