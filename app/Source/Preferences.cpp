#include "Preferences.h"
#include "MainComponent.h"
#include "HubPanel.h"
#include "PluginCatalog.h"

namespace prefs
{
    juce::PropertiesFile& file()
    {
        static std::unique_ptr<juce::PropertiesFile> f;
        if (f == nullptr)
        {
            auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde");
            juce::PropertiesFile::Options o;
            o.millisecondsBeforeSaving = 500;
            f = std::make_unique<juce::PropertiesFile> (dir.getChildFile (MainComponent::testMode ? "parametres-tests.xml" : "parametres.xml"), o);
        }
        return *f;
    }
    double newProjectBpm()       { return juce::jlimit (30.0, 300.0, file().getDoubleValue ("bpm", 120.0)); }
    int autosaveSeconds()        { return juce::jlimit (0, 3600, file().getIntValue ("autosave", 15)); }
    float uiScale()              { return (float) juce::jlimit (0.75, 2.0, file().getDoubleValue ("uiScale", 1.0)); }
    void applyUiScale()          { juce::Desktop::getInstance().setGlobalScaleFactor (uiScale()); }
    juce::File projectsFolder()
    {
        const juce::File f (file().getValue ("projectsFolder"));
        if (f.getFullPathName().isNotEmpty() && f.isDirectory()) return f;
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Onde");
    }
}

namespace
{
    // Ligne de réglage : intitulé et explication à gauche, commande à droite.
    struct Row { juce::String title, help; juce::Component* control; int controlW; };

    void styleBox (juce::ComboBox& b)
    {
        b.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff23272d));
        b.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff353a42));
        b.setColour (juce::ComboBox::textColourId, theme::text);
    }
    void styleButton (juce::TextButton& b, bool primary = false)
    {
        b.setColour (juce::TextButton::buttonColourId, primary ? theme::accent.darker (0.2f) : juce::Colour (0xff2a2e35));
        b.setColour (juce::TextButton::textColourOffId, primary ? juce::Colours::white : theme::text);
    }
}

//==============================================================================
class SettingsPanel::Page  : public juce::Component
{
public:
    Page (juce::String t, juce::String sub) : title (std::move (t)), subtitle (std::move (sub)) {}
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::text); g.setFont (theme::ui (20.0f, true));
        g.drawText (title, 0, 0, getWidth(), 30, juce::Justification::centredLeft);
        g.setColour (theme::muted); g.setFont (theme::ui (12.5f));
        g.drawText (subtitle, 0, 30, getWidth(), 18, juce::Justification::centredLeft);
        for (auto& r : rows)
        {
            auto b = r.bounds;
            g.setColour (juce::Colour (0xff1f2227)); g.fillRoundedRectangle (b.toFloat(), 7.0f);
            g.setColour (juce::Colour (0xff2b2f36)); g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 7.0f, 1.0f);
            auto t = b.reduced (16, 10).withTrimmedRight (r.row.controlW + 16);
            g.setColour (theme::text); g.setFont (theme::ui (13.5f, true));
            g.drawText (r.row.title, t.removeFromTop (20), juce::Justification::centredLeft);
            g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
            g.drawFittedText (r.row.help, t, juce::Justification::topLeft, 2);
        }
    }
    void resized() override
    {
        int y = 62;
        for (auto& r : rows)
        {
            r.bounds = { 0, y, getWidth(), 64 };
            auto c = r.bounds.reduced (16, 0);
            r.row.control->setBounds (c.removeFromRight (r.row.controlW).withSizeKeepingCentre (r.row.controlW, 28));
            y += 72;
        }
        if (body != nullptr) body->setBounds (0, y, getWidth(), getHeight() - y);
    }
    void addRow (juce::String t, juce::String help, juce::Component& c, int w)
    {
        rows.push_back ({ { std::move (t), std::move (help), &c, w }, {} });
        addAndMakeVisible (c);
    }
    void setBody (juce::Component* c) { body = c; if (c) addAndMakeVisible (c); }

private:
    struct Placed { Row row; juce::Rectangle<int> bounds; };
    std::vector<Placed> rows;
    juce::Component* body = nullptr;
    juce::String title, subtitle;
};

namespace
{
    class GeneralPage  : public SettingsPanel::Page
    {
    public:
        explicit GeneralPage (MainComponent& m) : Page (U ("Général"), U ("Démarrage, nouveaux projets, sauvegarde")), main (m)
        {
            hub.setToggleState (HubPanel::showAtStartup(), juce::dontSendNotification);
            hub.onClick = [this] { HubPanel::setShowAtStartup (hub.getToggleState()); };
            addRow (U ("Écran d'accueil au démarrage"), U ("Modèles de projet et projets récents à l'ouverture d'Onde."), hub, 40);

            bpm.setRange (30.0, 300.0, 1.0);
            bpm.setValue (prefs::newProjectBpm(), juce::dontSendNotification);
            bpm.setSliderStyle (juce::Slider::LinearHorizontal);
            bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
            bpm.setTextValueSuffix (" BPM");
            bpm.onValueChange = [this] { prefs::file().setValue ("bpm", bpm.getValue()); };
            addRow (U ("Tempo des nouveaux projets"), U ("Utilisé par « Nouveau projet » et les modèles d'Onde."), bpm, 260);

            autosave.addItemList ({ U ("Désactivée"), U ("Toutes les 15 s"), U ("Toutes les minutes"), U ("Toutes les 5 minutes") }, 1);
            const int secs = prefs::autosaveSeconds();
            autosave.setSelectedId (secs == 0 ? 1 : secs <= 15 ? 2 : secs <= 60 ? 3 : 4, juce::dontSendNotification);
            autosave.onChange = [this] { static const int v[] = { 0, 15, 60, 300 }; prefs::file().setValue ("autosave", v[autosave.getSelectedId() - 1]); };
            styleBox (autosave);
            addRow (U ("Sauvegarde automatique"), U ("Copie de secours du projet ouvert, retrouvée après un arrêt brutal."), autosave, 220);

            folder.setButtonText (prefs::projectsFolder().getFullPathName());
            folder.setTooltip (U ("Choisir le dossier des projets"));
            folder.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> (U ("Dossier des projets"), prefs::projectsFolder());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().isDirectory()) { prefs::file().setValue ("projectsFolder", fc.getResult().getFullPathName()); folder.setButtonText (fc.getResult().getFullPathName()); }
                });
            };
            styleButton (folder);
            addRow (U ("Dossier des projets"), U ("Proposé pour enregistrer, ouvrir et dans le menu Fichier."), folder, 320);
        }
    private:
        MainComponent& main;
        juce::ToggleButton hub;
        juce::Slider bpm;
        juce::ComboBox autosave;
        juce::TextButton folder;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    class AudioPage  : public SettingsPanel::Page
    {
    public:
        explicit AudioPage (MainComponent& m) : Page (U ("Audio et MIDI"), U ("Carte son, entrées et sorties, claviers MIDI branchés")),
            selector (m.getContext().engine.getDeviceManager().deviceManager, 0, 256, 0, 256, true, false, true, false)
        {
            setBody (&selector);
        }
        juce::AudioDeviceSelectorComponent selector;
    };

    class InterfacePage  : public SettingsPanel::Page
    {
    public:
        explicit InterfacePage (MainComponent& m) : Page (U ("Interface"), U ("Taille de l'affichage, raccourcis")), main (m)
        {
            const float scales[] = { 0.9f, 1.0f, 1.1f, 1.25f, 1.5f };
            for (int i = 0; i < 5; ++i) scale.addItem (juce::String (juce::roundToInt (scales[i] * 100)) + " %", i + 1);
            int sel = 2; for (int i = 0; i < 5; ++i) if (std::abs (scales[i] - prefs::uiScale()) < 0.01f) sel = i + 1;
            scale.setSelectedId (sel, juce::dontSendNotification);
            scale.onChange = [this, scales]
            {
                prefs::file().setValue ("uiScale", scales[scale.getSelectedId() - 1]);
                juce::MessageManager::callAsync ([] { prefs::applyUiScale(); });
            };
            styleBox (scale);
            addRow (U ("Taille de l'interface"), U ("Agrandit tout Onde (écrans haute résolution, confort de lecture)."), scale, 140);
            shortcuts.setButtonText (U ("Voir les raccourcis (F1)"));
            shortcuts.onClick = [this] { main.getContext().perform ("help.shortcuts"); };
            styleButton (shortcuts);
            addRow (U ("Raccourcis clavier"), U ("Toutes les commandes : Ctrl+K pour les chercher, F1 pour la liste."), shortcuts, 200);
        }
    private:
        MainComponent& main;
        juce::ComboBox scale;
        juce::TextButton shortcuts;
    };

    class PluginsPage  : public SettingsPanel::Page, private juce::ChangeListener
    {
    public:
        explicit PluginsPage (MainComponent& m) : Page (U ("Plugins"), U ("VST3 installés sur ce PC, chargés chacun dans un processus isolé")), main (m)
        {
            scan.setButtonText (U ("Rechercher les plugins"));
            scan.onClick = [this] { main.getContext().perform ("plugins.scan"); refresh(); };
            styleButton (scan, true);
            addRow (U ("Analyse des VST3"), U ("Chaque plugin est testé à part : un plugin défectueux ne peut pas faire planter Onde."), scan, 200);
            restart.setButtonText (U ("Relancer les plugins arrêtés"));
            restart.onClick = [this] { main.getContext().perform ("plugins.restart"); };
            styleButton (restart);
            addRow (U ("Plugins arrêtés"), U ("Un plugin qui a planté est mis à l'écart ; il peut être relancé ici."), restart, 220);
            list.setMultiLine (true); list.setReadOnly (true); list.setScrollbarsShown (true);
            list.setFont (theme::ui (12.5f));
            list.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff15171b));
            list.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2b2f36));
            list.setColour (juce::TextEditor::textColourId, theme::text);
            setBody (&list);
            main.getCatalog().addChangeListener (this);
            refresh();
        }
        ~PluginsPage() override { main.getCatalog().removeChangeListener (this); }
    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void refresh()
        {
            auto& cat = main.getCatalog();
            juce::String t;
            auto inst = cat.getVST3 (true), fx = cat.getVST3 (false);
            t << (cat.isScanning() ? U ("Analyse en cours… ") + cat.getStatus() + "\n\n" : juce::String());
            t << U ("Instruments (") << inst.size() << ")\n";
            for (auto& e : inst) t << U ("   ◆ ") << e.name << "  -  " << e.manufacturer << "\n";
            t << U ("\nEffets (") << fx.size() << ")\n";
            for (auto& e : fx) t << U ("   ◆ ") << e.name << "  -  " << e.manufacturer << "\n";
            const auto failed = cat.getFailures();
            if (! failed.isEmpty()) { t << U ("\nIgnorés (") << failed.size() << ")\n"; for (auto& f : failed) t << "   " << f << "\n"; }
            t << U ("\nDossiers analysés :\n");
            for (auto& f : PluginCatalog::getSearchFolders()) t << "   " << f.getFullPathName() << "\n";
            list.setText (t, false);
        }
        MainComponent& main;
        juce::TextButton scan, restart;
        juce::TextEditor list;
    };

    class TemplatesPage  : public SettingsPanel::Page, private juce::ListBoxModel
    {
    public:
        explicit TemplatesPage (MainComponent& m) : Page (U ("Modèles"), U ("Vos propres points de départ : pistes, sons, effets, routage, tempo")), main (m)
        {
            save.setButtonText (U ("Enregistrer le projet comme modèle…"));
            save.onClick = [this] { main.askSaveAsTemplate ([this] { refresh(); }); };
            styleButton (save, true);
            addRow (U ("Nouveau modèle"), U ("Le projet ouvert devient un modèle, proposé dans l'écran d'accueil et ici."), save, 260);
            for (auto* b : { &openBtn, &deleteBtn, &folderBtn }) { styleButton (*b); addAndMakeVisible (b); }
            openBtn.setButtonText (U ("Nouveau projet depuis ce modèle"));
            openBtn.onClick = [this] { if (auto f = selected(); f.existsAsFile()) main.newProjectFromUserTemplate (f); };
            deleteBtn.setButtonText (U ("Supprimer"));
            deleteBtn.onClick = [this]
            {
                auto f = selected();
                if (! f.existsAsFile()) return;
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, U ("Supprimer le modèle ?"),
                    U ("Le modèle « ") + f.getFileNameWithoutExtension() + U (" » sera mis à la corbeille."), U ("Supprimer"), U ("Annuler"), nullptr,
                    juce::ModalCallbackFunction::create ([this, f] (int r) { if (r == 1) { f.moveToTrash(); refresh(); } }));
            };
            folderBtn.setButtonText (U ("Ouvrir le dossier"));
            folderBtn.onClick = [] { MainComponent::templatesFolder().createDirectory(); MainComponent::templatesFolder().startAsProcess(); };
            list.setModel (this);
            list.setRowHeight (34);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff15171b));
            list.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff2b2f36));
            list.setOutlineThickness (1);
            setBody (&holder);
            holder.addAndMakeVisible (list);
            refresh();
        }
        ~TemplatesPage() override { list.setModel (nullptr); }
        void resized() override
        {
            Page::resized();
            auto r = holder.getLocalBounds();
            auto b = r.removeFromBottom (32);
            r.removeFromBottom (10);
            list.setBounds (r);
            openBtn.setBounds (holder.getX() + b.getX(), holder.getY() + b.getY(), 250, 30);
            deleteBtn.setBounds (openBtn.getRight() + 8, openBtn.getY(), 110, 30);
            folderBtn.setBounds (holder.getRight() - 150, openBtn.getY(), 150, 30);
        }
    private:
        void refresh() { files = MainComponent::userTemplates(); list.updateContent(); list.repaint(); }
        juce::File selected() const { const int r = list.getSelectedRow(); return juce::isPositiveAndBelow (r, files.size()) ? files[r] : juce::File(); }
        int getNumRows() override { return juce::jmax (1, files.size()); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
        {
            if (files.isEmpty())
            {
                if (row == 0) { g.setColour (theme::dim); g.setFont (theme::ui (12.5f)); g.drawText (U ("Aucun modèle pour l'instant."), 14, 0, w - 20, h, juce::Justification::centredLeft); }
                return;
            }
            if (sel) { g.setColour (theme::accent.withAlpha (0.2f)); g.fillRect (0, 0, w, h); }
            g.setColour (theme::text); g.setFont (theme::ui (13.0f, sel));
            g.drawText (files[row].getFileNameWithoutExtension(), 14, 0, w - 200, h, juce::Justification::centredLeft);
            g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
            g.drawText (files[row].getLastModificationTime().toString (true, false), w - 180, 0, 166, h, juce::Justification::centredRight);
        }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, files.size())) main.newProjectFromUserTemplate (files[row]); }
        MainComponent& main;
        juce::TextButton save, openBtn, deleteBtn, folderBtn;
        juce::Component holder;
        juce::ListBox list;
        juce::Array<juce::File> files;
    };
}

//==============================================================================
juce::StringArray SettingsPanel::tabNames() { return { U ("Général"), U ("Audio et MIDI"), U ("Interface"), U ("Plugins"), U ("Modèles") }; }

SettingsPanel::SettingsPanel (MainComponent& m) : main (m)
{
    pages.add (new GeneralPage (m));
    pages.add (new AudioPage (m));
    pages.add (new InterfacePage (m));
    pages.add (new PluginsPage (m));
    pages.add (new TemplatesPage (m));
    for (auto* p : pages) addChildComponent (p);
    showTab (0);
    setSize (900, 600);
}

SettingsPanel::~SettingsPanel() { prefs::file().saveIfNeeded(); }

void SettingsPanel::showTab (int t)
{
    tab = juce::jlimit (0, pages.size() - 1, t);
    for (int i = 0; i < pages.size(); ++i) pages[i]->setVisible (i == tab);
    repaint();
}

juce::Rectangle<int> SettingsPanel::navItem (int i) const { return { 12, 70 + i * 40, nav.getWidth() - 24, 34 }; }

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181a1f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272e), 0, 0, juce::Colour (0xff15171b), 0, (float) getHeight(), false));
    g.fillRect (nav);
    g.setColour (theme::line); g.drawVerticalLine (nav.getRight() - 1, 0.0f, (float) getHeight());
    theme::drawLogo (g, juce::Rectangle<float> (16.0f, 16.0f, 32.0f, 32.0f));
    g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
    g.drawText (U ("Paramètres"), 56, 16, nav.getWidth() - 60, 32, juce::Justification::centredLeft);
    const auto names = tabNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto r = navItem (i).toFloat();
        if (i == tab) { g.setColour (theme::accent.withAlpha (0.18f)); g.fillRoundedRectangle (r, 6.0f); g.setColour (theme::accent); g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f); }
        else if (i == hover) { g.setColour (juce::Colour (0xff262a31)); g.fillRoundedRectangle (r, 6.0f); }
        g.setColour (i == tab ? theme::text : theme::muted); g.setFont (theme::ui (13.5f, i == tab));
        g.drawText (names[i], r.toNearestInt().withTrimmedLeft (14), juce::Justification::centredLeft);
    }
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds();
    nav = r.removeFromLeft (200);
    for (auto* p : pages) p->setBounds (r.reduced (28, 22));
}

void SettingsPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < tabNames().size(); ++i) if (navItem (i).contains (e.getPosition())) { showTab (i); return; }
}

void SettingsPanel::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < tabNames().size(); ++i) if (navItem (i).contains (e.getPosition())) h = i;
    if (h != hover) { hover = h; repaint (nav); }
}

juce::DialogWindow* SettingsPanel::show (MainComponent& m, int tab)
{
    auto panel = std::make_unique<SettingsPanel> (m);
    panel->showTab (tab);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel.release());
    o.dialogTitle = U ("Paramètres d'Onde");
    o.dialogBackgroundColour = juce::Colour (0xff181a1f);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    return o.launchAsync();
}
