#include "AboutPanel.h"
#include "EditContext.h"

AboutPanel::AboutPanel()
{
    close.onClick = [this]
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0);
    };
    close.setColour (juce::TextButton::buttonColourId, theme::panel2);
    close.setColour (juce::TextButton::textColourOffId, theme::text);
    addAndMakeVisible (close);
    setSize (560, 330);
}

void AboutPanel::resized()
{
    close.setBounds (getWidth() - 24 - 100, getHeight() - 24 - 30, 100, 30);
}

void AboutPanel::paint (juce::Graphics& g)
{
    // Fond : léger dégradé, et un rappel de l'onde du logo en filigrane.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272b33), 0, 0, juce::Colour (0xff15171b), 0, (float) getHeight(), false));
    g.fillAll();

    const auto logoArea = juce::Rectangle<float> (28.0f, 30.0f, 128.0f, 128.0f);
    theme::drawLogo (g, logoArea);

    const float x = logoArea.getRight() + 26.0f;
    g.setColour (theme::text);
    g.setFont (theme::ui (44.0f, true));
    g.drawText ("onde", juce::Rectangle<float> (x, 40.0f, 300.0f, 52.0f), juce::Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (theme::ui (14.0f));
    g.drawText (U ("Station de travail audio  ·  version ") + version, juce::Rectangle<float> (x + 2.0f, 92.0f, 360.0f, 20.0f), juce::Justification::centredLeft);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a6bff), x, 0, juce::Colour (0xff2de2c6), x + 200.0f, 0, false));
    g.fillRoundedRectangle (x + 2.0f, 122.0f, 120.0f, 3.0f, 1.5f);

    g.setColour (theme::text.withAlpha (0.85f));
    g.setFont (theme::ui (13.0f));
    juce::AttributedString a;
    a.setJustification (juce::Justification::topLeft);
    a.setLineSpacing (3.0f);
    a.append (U ("Chaque plugin VST3 marqué ◆ tourne dans son propre processus : s'il plante, seule sa piste se tait, ")
              + U ("et « Relancer » le redémarre avec son dernier réglage.\n\n"), theme::ui (13.0f), theme::text.withAlpha (0.85f));
    a.append (U ("Construit avec JUCE et Tracktion Engine. Séparation de sources : Demucs v4 (demucs.cpp, licence MIT)."),
              theme::ui (12.0f), theme::muted);
    a.draw (g, juce::Rectangle<float> (28.0f, 182.0f, (float) getWidth() - 56.0f, 90.0f));
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
