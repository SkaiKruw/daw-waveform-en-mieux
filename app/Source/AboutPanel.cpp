#include "AboutPanel.h"
#include "EditContext.h"
#include "Updater.h"
#include "Preferences.h"

static juce::String buildDate()
{
    // __DATE__ (« Nov 24 2026 ») en français.
    const auto d = juce::StringArray::fromTokens (__DATE__, " ", "");
    const char* en[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const char* fr[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre" };
    for (int m = 0; m < 12 && d.size() == 3; ++m) if (d[0] == en[m]) return d[1] + " " + U (fr[m]) + " " + d[2];
    return __DATE__;
}

juce::StringArray AboutPanel::whatsNew()
{
    return { U ("Onde Ampli : ampli guitare et basse (clair, crunch, lead, moderne, basse), boost, porte, baffles"),
             U ("Guitares, basses, harpe et ukulélé en modélisation physique (6, 7, 8 et 12 cordes)"),
             U ("Fenêtre d'instrument redessinée : navigateur de sons, clavier et pads pour essayer"),
             U ("Une entrée par piste : micro sur 1, guitare sur 2, plusieurs pistes armées ensemble"),
             U ("Nouvel installateur, mises à jour signalées, tablature avec portée et accordage libre"),
             U ("Paramètres (Ctrl+,), réglages du métronome, modèles personnels"),
             U ("Onde Modulation : chorus, ensemble, flanger, phaser, vibrato"),
             U ("Analyse du mix (LUFS, crête vraie, corrélation), écoute mono, polarité") };
}

AboutPanel::AboutPanel()
{
    close.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0); };
    checkBtn.setButtonText (U ("Rechercher les mises à jour"));
    checkBtn.onClick = [this] { checkNow(); };
    downloadBtn.setButtonText (U ("Télécharger la nouvelle version"));
    downloadBtn.onClick = [this] { if (updateUrl.isNotEmpty()) juce::URL (updateUrl).launchInDefaultBrowser(); };
    for (auto* b : { &close, &checkBtn, &downloadBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, b == &downloadBtn ? theme::accent.darker (0.2f) : theme::panel2);
        b->setColour (juce::TextButton::textColourOffId, b == &downloadBtn ? juce::Colours::white : theme::text);
        addAndMakeVisible (b);
    }
    downloadBtn.setVisible (false);
    autoCheck.setButtonText (U ("Vérifier chaque jour au démarrage"));
    autoCheck.setColour (juce::ToggleButton::textColourId, theme::muted);
    autoCheck.setToggleState (prefs::file().getBoolValue ("checkUpdates", true), juce::dontSendNotification);
    autoCheck.onClick = [this] { prefs::file().setValue ("checkUpdates", autoCheck.getToggleState()); };
    addAndMakeVisible (autoCheck);
    updateText = U ("Version ") + version + U (" installée.");
    updateColour = theme::muted;
    setSize (680, 600);
}

void AboutPanel::checkNow()
{
    updateText = U ("Recherche en cours…");
    updateColour = theme::muted;
    repaint();
    juce::Component::SafePointer<AboutPanel> safe (this);
    updates::check ([safe] (const updates::Info& info)
    {
        if (safe == nullptr) return;
        if (info.newer)
        {
            safe->updateText = U ("La version ") + info.latest + U (" est disponible (vous avez la ") + version + ").";
            safe->updateColour = theme::ok;
            safe->updateUrl = info.pageUrl;
            safe->downloadBtn.setVisible (true);
        }
        else if (info.ok)
        {
            safe->updateText = U ("Onde est à jour (version ") + version + ").";
            safe->updateColour = theme::ok;
        }
        else
        {
            safe->updateText = U ("Vérification impossible : ") + info.error + ".";
            safe->updateColour = theme::warn;
        }
        safe->resized();
        safe->repaint();
    });
}

void AboutPanel::resized()
{
    close.setBounds (getWidth() - 24 - 100, getHeight() - 24 - 30, 100, 30);
    checkBtn.setBounds (28, 400, 230, 30);
    downloadBtn.setBounds (266, 400, 240, 30);
    autoCheck.setBounds (28, 436, 300, 24);
}

void AboutPanel::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272b33), 0, 0, juce::Colour (0xff15171b), 0, (float) getHeight(), false));
    g.fillAll();

    const auto logoArea = juce::Rectangle<float> (28.0f, 26.0f, 96.0f, 96.0f);
    theme::drawLogo (g, logoArea);
    const float x = logoArea.getRight() + 22.0f;
    g.setColour (theme::text); g.setFont (theme::ui (40.0f, true));
    g.drawText ("onde", juce::Rectangle<float> (x, 28.0f, 300.0f, 48.0f), juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::ui (14.0f));
    g.drawText (U ("Station de travail audio  ·  version ") + version + U ("  ·  compilée le ") + buildDate(),
                juce::Rectangle<float> (x + 2.0f, 76.0f, 460.0f, 20.0f), juce::Justification::centredLeft);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a6bff), x, 0, juce::Colour (0xff2de2c6), x + 200.0f, 0, false));
    g.fillRoundedRectangle (x + 2.0f, 104.0f, 120.0f, 3.0f, 1.5f);

    // Nouveautés.
    g.setColour (theme::text); g.setFont (theme::ui (14.0f, true));
    g.drawText (U ("Nouveautés de la version ") + version, 28, 140, 400, 20, juce::Justification::centredLeft);
    int y = 166;
    for (auto& line : whatsNew())
    {
        g.setColour (theme::accent); g.fillEllipse (32.0f, (float) y + 6.0f, 6.0f, 6.0f);
        g.setColour (theme::text.withAlpha (0.85f)); g.setFont (theme::ui (12.5f));
        g.drawText (line, 46, y, getWidth() - 74, 18, juce::Justification::centredLeft, true);
        y += 22;
    }

    // Mises à jour.
    g.setColour (theme::text); g.setFont (theme::ui (14.0f, true));
    g.drawText (U ("Mises à jour"), 28, 356, 300, 20, juce::Justification::centredLeft);
    g.setColour (updateColour); g.setFont (theme::ui (12.5f));
    g.drawText (updateText, 28, 376, getWidth() - 56, 20, juce::Justification::centredLeft, true);

    // Crédits.
    juce::AttributedString a;
    a.setJustification (juce::Justification::topLeft);
    a.setLineSpacing (2.0f);
    a.append (U ("Les plugins VST3 marqués ◆ tournent chacun dans leur propre processus : s'ils plantent, seule leur piste se tait.\n"),
              theme::ui (12.0f), theme::text.withAlpha (0.8f));
    a.append (U ("Construit avec JUCE et Tracktion Engine (GPL v3), SoundTouch (LGPL), séparation de sources Demucs v4 (demucs.cpp, MIT). "
                 "Instruments, effets et sons : créations originales d'Onde."),
              theme::ui (11.5f), theme::muted);
    a.draw (g, juce::Rectangle<float> (28.0f, 476.0f, (float) getWidth() - 56.0f, 60.0f));
}

void AboutPanel::show()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new AboutPanel());
    o.dialogTitle = U ("À propos d'Onde");
    o.dialogBackgroundColour = juce::Colour (0xff15171b);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
