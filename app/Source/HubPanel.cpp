#include "HubPanel.h"
#include "MainComponent.h"
#include "AboutPanel.h"

namespace
{
    juce::File hiddenFlag()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Onde").getChildFile (U ("accueil-masqué"));
    }

    // Petites icônes des modèles (traits, couleur de la carte).
    void drawGlyph (juce::Graphics& g, int kind, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        juce::Path p;
        switch (kind)
        {
            case 0:   // projet vide : une feuille
                p.addRoundedRectangle (r.reduced (r.getWidth() * 0.22f, r.getHeight() * 0.12f), 3.0f);
                g.strokePath (p, juce::PathStrokeType (2.2f));
                break;
            case 1:   // enregistrement : un micro
            {
                auto m = r.withSizeKeepingCentre (r.getWidth() * 0.34f, r.getHeight() * 0.52f).translated (0, -r.getHeight() * 0.08f);
                g.fillRoundedRectangle (m, m.getWidth() * 0.5f);
                p.addCentredArc (r.getCentreX(), m.getCentreY(), m.getWidth() * 0.95f, m.getHeight() * 0.62f, 0, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f, true);
                p.startNewSubPath (r.getCentreX(), m.getCentreY() + m.getHeight() * 0.62f);
                p.lineTo (r.getCentreX(), r.getBottom() - r.getHeight() * 0.1f);
                g.strokePath (p, juce::PathStrokeType (2.2f));
                break;
            }
            case 2:   // production : un petit clavier
            {
                auto k = r.reduced (r.getWidth() * 0.12f, r.getHeight() * 0.24f);
                g.drawRoundedRectangle (k, 2.0f, 2.0f);
                for (int i = 1; i < 5; ++i) g.drawVerticalLine ((int) (k.getX() + k.getWidth() * i / 5.0f), k.getY(), k.getBottom());
                for (int i : { 1, 2, 4 }) g.fillRect (k.getX() + k.getWidth() * i / 5.0f - 3.0f, k.getY(), 6.0f, k.getHeight() * 0.58f);
                break;
            }
            default:  // démonstration : une onde
                for (int i = 0; i <= 40; ++i)
                {
                    const float t = (float) i / 40.0f;
                    const float y = r.getCentreY() - std::sin (t * juce::MathConstants<float>::twoPi * 2.0f) * std::sin (t * juce::MathConstants<float>::pi) * r.getHeight() * 0.32f;
                    const float x = r.getX() + r.getWidth() * (0.12f + 0.76f * t);
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;
        }
    }
}

//==============================================================================
class HubPanel::Card  : public juce::Component
{
public:
    Card (int k, juce::String t, juce::String d, juce::Colour c) : kind (k), title (std::move (t)), desc (std::move (d)), colour (c)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (title);
        setWantsKeyboardFocus (false);
    }
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool over = isMouseOver (true);
        g.setColour (over ? juce::Colour (0xff343944) : juce::Colour (0xff2a2e35));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (over ? colour.withAlpha (0.9f) : juce::Colour (0xff3a3f48));
        g.drawRoundedRectangle (r, 8.0f, over ? 1.6f : 1.0f);
        auto icon = r.removeFromLeft (64.0f).reduced (12.0f);
        g.setColour (colour.withAlpha (0.16f));
        g.fillRoundedRectangle (icon, 8.0f);
        drawGlyph (g, kind, icon.reduced (6.0f), colour);
        auto text = r.reduced (4.0f, 12.0f);
        g.setColour (theme::text); g.setFont (theme::ui (14.0f, true));
        g.drawText (title, text.removeFromTop (20.0f), juce::Justification::centredLeft, true);
        g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
        g.drawFittedText (desc, text.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (e.mouseWasClicked() && onClick) onClick(); }

private:
    int kind;
    juce::String title, desc;
    juce::Colour colour;
};

//==============================================================================
HubPanel::HubPanel (MainComponent& m) : main (m)
{
    struct Def { int kind; const char* title; const char* desc; juce::uint32 colour; };
    const Def defs[] = {
        { 0, "Projet vide",            "Une piste audio et une piste instrument, 120 BPM.",                         0xff8b93a1 },
        { 1, "Enregistrement",         "Voix, guitare, basse, batterie et une réverbération : prêt à enregistrer.", 0xffe8697a },
        { 2, "Production",             "Batterie, basse, accords, nappe et lead prêts à jouer, une voix, une réverbération.", 0xff3fa4ff },
        { 3, "Démonstration", "« Nuits du Nord » : 8 mesures avec batterie, basse, pad et synthé VST3.",   0xff2de2c6 },
    };
    for (auto& d : defs)
    {
        auto* c = cards.add (new Card (d.kind, U (d.title), U (d.desc), juce::Colour (d.colour)));
        const int kind = d.kind;
        c->onClick = [this, kind]
        {
            if (kind == 3) { if (main.onOpenDemo) main.onOpenDemo(); }
            else main.newProjectFromTemplate (kind);
            closeHub();
        };
        addAndMakeVisible (c);
    }

    recent = MainComponent::recentProjects();
    list.setModel (this);
    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1c1f24));
    list.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff2e323a));
    list.setOutlineThickness (1);
    addChildComponent (list);
    list.setVisible (! recent.isEmpty());      // sinon, message « aucun projet récent » dessiné à sa place

    openBtn.setButtonText (U ("Ouvrir un autre projet…"));
    openBtn.onClick = [this] { closeHub(); juce::MessageManager::callAsync ([&mc = main] { mc.getContext().perform ("file.open"); }); };
    closeBtn.setButtonText (U ("Continuer"));
    closeBtn.setTooltip (U ("Fermer l'accueil et garder le projet ouvert"));
    closeBtn.onClick = [this] { closeHub(); };
    for (auto* b : { &openBtn, &closeBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::panel2);
        b->setColour (juce::TextButton::textColourOffId, theme::text);
        addAndMakeVisible (*b);
    }
    startup.setButtonText (U ("Afficher au démarrage"));
    startup.setToggleState (showAtStartup(), juce::dontSendNotification);
    startup.setColour (juce::ToggleButton::textColourId, theme::muted);
    startup.onClick = [this] { setShowAtStartup (startup.getToggleState()); };
    addAndMakeVisible (startup);
    setSize (880, 520);
}

HubPanel::~HubPanel() { list.setModel (nullptr); }

bool HubPanel::showAtStartup()        { return ! hiddenFlag().existsAsFile(); }
void HubPanel::setShowAtStartup (bool on)
{
    if (on) hiddenFlag().deleteFile();
    else { hiddenFlag().getParentDirectory().createDirectory(); hiddenFlag().replaceWithText ("1"); }
}

void HubPanel::resized()
{
    auto r = getLocalBounds();
    side = r.removeFromLeft (250);
    startup.setBounds (side.getX() + 24, side.getBottom() - 44, side.getWidth() - 40, 24);

    r.reduce (28, 24);
    newTitle = r.removeFromTop (26);
    r.removeFromTop (8);
    auto grid = r.removeFromTop (2 * 84 + 10);
    const int cw = (grid.getWidth() - 12) / 2;
    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setBounds (grid.getX() + (i % 2) * (cw + 12), grid.getY() + (i / 2) * 94, cw, 84);
    r.removeFromTop (20);
    recentTitle = r.removeFromTop (26);
    r.removeFromTop (8);
    auto buttons = r.removeFromBottom (32);
    r.removeFromBottom (12);
    list.setBounds (r);
    closeBtn.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (10);
    openBtn.setBounds (buttons.removeFromRight (190));
}

void HubPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    // Colonne gauche : logo, nom, version.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272b33), 0, 0, juce::Colour (0xff131519), 0, (float) getHeight(), false));
    g.fillRect (side);
    g.setColour (juce::Colour (0xff2e323a)); g.drawVerticalLine (side.getRight() - 1, 0.0f, (float) getHeight());
    theme::drawLogo (g, juce::Rectangle<float> (24.0f, 34.0f, 112.0f, 112.0f));
    g.setColour (theme::text); g.setFont (theme::ui (40.0f, true));
    g.drawText ("onde", 26, 160, 200, 48, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::ui (13.0f));
    g.drawText (U ("Station de travail audio"), 28, 206, 210, 20, juce::Justification::centredLeft);
    g.drawText (U ("Version ") + AboutPanel::version, 28, 226, 210, 20, juce::Justification::centredLeft);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a6bff), 28, 0, juce::Colour (0xff2de2c6), 148, 0, false));
    g.fillRoundedRectangle (28.0f, 256.0f, 110.0f, 3.0f, 1.5f);
    g.setColour (theme::dim); g.setFont (theme::ui (11.5f));
    g.drawFittedText (U ("Astuce : Ctrl+K cherche n'importe quelle commande, F1 liste les raccourcis."),
                      juce::Rectangle<int> (28, 282, side.getWidth() - 48, 48), juce::Justification::topLeft, 3);

    g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
    g.drawText ("Nouveau projet", newTitle, juce::Justification::centredLeft);
    g.drawText (U ("Projets récents"), recentTitle, juce::Justification::centredLeft);
    if (recent.isEmpty())
    {
        g.setColour (juce::Colour (0xff1c1f24)); g.fillRect (list.getBounds());
        g.setColour (juce::Colour (0xff2e323a)); g.drawRect (list.getBounds());
        g.setColour (theme::dim); g.setFont (theme::ui (12.5f));
        g.drawText (U ("Aucun projet récent : les projets enregistrés ou ouverts apparaîtront ici."), list.getBounds(), juce::Justification::centred);
    }
}

void HubPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= recent.size()) return;
    const juce::File f (recent[row]);
    if (selected) { g.setColour (theme::accentBg); g.fillRect (0, 0, w, h); }
    theme::drawLogo (g, juce::Rectangle<float> (10.0f, (float) h * 0.5f - 12.0f, 24.0f, 24.0f));
    g.setColour (theme::text); g.setFont (theme::ui (13.5f, true));
    g.drawText (f.getFileNameWithoutExtension(), 44, 4, w - 200, 20, juce::Justification::centredLeft, true);
    g.setColour (theme::muted); g.setFont (theme::ui (11.0f));
    g.drawText (f.getParentDirectory().getFullPathName(), 44, 23, w - 200, 16, juce::Justification::centredLeft, true);
    g.drawText (f.getLastModificationTime().formatted ("%d/%m/%Y %H:%M"), w - 150, 0, 140, h, juce::Justification::centredRight);
    g.setColour (juce::Colour (0xff2a2e35)); g.drawHorizontalLine (h - 1, 0.0f, (float) w);
}

void HubPanel::openRecent (int row)
{
    if (row < 0 || row >= recent.size()) return;
    const juce::File f (recent[row]);
    auto& mc = main;          // la fenêtre (et ce composant) disparaît à la fermeture
    closeHub();
    mc.openProjectFile (f);
}

void HubPanel::closeHub()
{
    if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0);
}

juce::DialogWindow* HubPanel::show (MainComponent& m)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new HubPanel (m));
    o.dialogTitle = U ("Onde — Accueil");
    o.dialogBackgroundColour = juce::Colour (0xff1a1d22);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    return o.launchAsync();
}
