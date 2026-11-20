#include "NewTrackPanel.h"
#include "LibraryPanel.h"
#include "OndeSynth.h"
#include "Chains.h"
#include "Drummer.h"

namespace
{
    struct Card { const char* title; const char* text; juce::uint32 colour; };
    const Card cards[NewTrackPanel::numKinds] = {
        { "Instrument",            "Piano, synthés, basses, nappes… à jouer au clavier (écran, ordinateur ou MIDI)", 0xff3fa4ff },
        { "Batteur",               "Une batterie qui joue toute seule : style, simple ou complexe, doux ou fort", 0xffe08a3c },
        { "Voix ou micro",         "Chant, voix parlée, podcast : chaîne « Voix pop » et piste prête à enregistrer", 0xffe8697a },
        { "Guitare ou basse",      "Branchée sur la carte son : chaîne « Guitare claire », prête à enregistrer", 0xff9be35b },
        { "Instrument VST3",       "Un de vos plugins instruments, isolé dans son propre processus", 0xffb98cff },
        { "Piste audio vide",      "Pour glisser des fichiers audio ou enregistrer sans réglage", 0xff8b93a1 } };

    void drawIcon (juce::Graphics& g, int kind, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        juce::Path p;
        const auto cx = r.getCentreX(), cy = r.getCentreY(), s = juce::jmin (r.getWidth(), r.getHeight());
        switch (kind)
        {
            case 0:   // clavier
            {
                auto k = juce::Rectangle<float> (s * 0.9f, s * 0.5f).withCentre ({ cx, cy });
                g.drawRoundedRectangle (k, 3.0f, 2.0f);
                for (int i = 1; i < 7; ++i) g.drawVerticalLine ((int) (k.getX() + k.getWidth() * i / 7), k.getY(), k.getBottom());
                for (int i : { 1, 2, 4, 5, 6 }) g.fillRect (k.getX() + k.getWidth() * i / 7 - s * 0.04f, k.getY(), s * 0.08f, k.getHeight() * 0.6f);
                break;
            }
            case 1:   // caisse et baguettes
            {
                auto d = juce::Rectangle<float> (s * 0.8f, s * 0.42f).withCentre ({ cx, cy + s * 0.08f });
                g.drawEllipse (d.withHeight (d.getHeight() * 0.45f), 2.0f);
                g.drawLine (d.getX(), d.getY() + d.getHeight() * 0.22f, d.getX(), d.getBottom(), 2.0f);
                g.drawLine (d.getRight(), d.getY() + d.getHeight() * 0.22f, d.getRight(), d.getBottom(), 2.0f);
                g.drawLine (d.getX(), d.getBottom(), d.getRight(), d.getBottom(), 2.0f);
                g.drawLine (cx - s * 0.3f, cy - s * 0.42f, cx + s * 0.05f, cy - s * 0.05f, 2.5f);
                g.drawLine (cx + s * 0.3f, cy - s * 0.42f, cx - s * 0.05f, cy - s * 0.05f, 2.5f);
                break;
            }
            case 2:   // micro
            {
                auto m = juce::Rectangle<float> (s * 0.3f, s * 0.45f).withCentre ({ cx, cy - s * 0.12f });
                g.drawRoundedRectangle (m, s * 0.15f, 2.0f);
                p.addCentredArc (cx, cy - s * 0.05f, s * 0.25f, s * 0.25f, 0.0f, juce::MathConstants<float>::pi * 0.5f, juce::MathConstants<float>::pi * 1.5f, true);
                g.strokePath (p, juce::PathStrokeType (2.0f));
                g.drawLine (cx, cy + s * 0.2f, cx, cy + s * 0.38f, 2.0f);
                g.drawLine (cx - s * 0.15f, cy + s * 0.38f, cx + s * 0.15f, cy + s * 0.38f, 2.0f);
                break;
            }
            case 3:   // guitare
            {
                g.drawEllipse (cx - s * 0.32f, cy - s * 0.02f, s * 0.36f, s * 0.36f, 2.0f);
                g.drawEllipse (cx - s * 0.24f, cy - s * 0.2f, s * 0.26f, s * 0.26f, 2.0f);
                g.drawLine (cx - s * 0.05f, cy - s * 0.05f, cx + s * 0.38f, cy - s * 0.42f, 3.0f);
                g.fillEllipse (cx - s * 0.17f, cy + s * 0.1f, s * 0.08f, s * 0.08f);
                break;
            }
            case 4:   // prise (plugin)
            {
                auto b = juce::Rectangle<float> (s * 0.5f, s * 0.36f).withCentre ({ cx, cy + s * 0.05f });
                g.drawRoundedRectangle (b, 4.0f, 2.0f);
                g.drawLine (cx - s * 0.12f, b.getY(), cx - s * 0.12f, b.getY() - s * 0.18f, 2.5f);
                g.drawLine (cx + s * 0.12f, b.getY(), cx + s * 0.12f, b.getY() - s * 0.18f, 2.5f);
                g.drawLine (cx, b.getBottom(), cx, b.getBottom() + s * 0.2f, 2.0f);
                break;
            }
            default:  // forme d'onde
            {
                for (int i = 0; i <= 40; ++i)
                {
                    const float x = r.getX() + r.getWidth() * 0.08f + r.getWidth() * 0.84f * i / 40.0f;
                    const float y = cy + std::sin (i * 0.55f) * s * 0.25f * std::sin (i * juce::MathConstants<float>::pi / 40.0f);
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.strokePath (p, juce::PathStrokeType (2.0f));
                break;
            }
        }
    }
}

NewTrackPanel::NewTrackPanel (EditContext& c) : ctx (c)
{
    for (auto* b : { &fxBtn, &folderBtn, &groupBtn }) addAndMakeVisible (*b);
    auto closeThen = [this] (const char* action)
    {
        return [this, action]
        {
            auto& context = ctx;
            const juce::String a (action);
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
            juce::MessageManager::callAsync ([&context, a] { context.perform (a); });
        };
    };
    fxBtn.onClick = closeThen ("track.addFx");
    folderBtn.onClick = closeThen ("track.addFolder");
    groupBtn.onClick = closeThen ("track.addGroup");
    setSize (720, 470);
}

juce::Rectangle<int> NewTrackPanel::cardBounds (int i) const
{
    auto area = getLocalBounds().reduced (20).withTrimmedTop (50).withTrimmedBottom (46);
    const int cols = 3, w = area.getWidth() / cols, h = area.getHeight() / 2;
    return { area.getX() + (i % cols) * w, area.getY() + (i / cols) * h, w, h };
}

void NewTrackPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    g.setColour (theme::text); g.setFont (theme::ui (18.0f, true));
    g.drawText (U ("Que voulez-vous faire ?"), 20, 14, getWidth() - 40, 26, juce::Justification::centredLeft);
    for (int i = 0; i < numKinds; ++i)
    {
        auto r = cardBounds (i).reduced (6).toFloat();
        const auto colour = juce::Colour (cards[i].colour);
        const bool unavailable = i == vst3 && ctx.catalog.getVST3 (true).isEmpty();
        g.setColour (i == hover && ! unavailable ? juce::Colour (0xff2c3138) : juce::Colour (0xff22262c));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (i == hover && ! unavailable ? colour : juce::Colour (0xff30353d));
        g.drawRoundedRectangle (r, 10.0f, i == hover ? 2.0f : 1.0f);
        auto inner = r.reduced (14.0f);
        drawIcon (g, i, inner.removeFromTop (inner.getHeight() * 0.45f), unavailable ? theme::dim : colour);
        g.setColour (unavailable ? theme::dim : theme::text); g.setFont (theme::ui (15.0f, true));
        g.drawText (juce::String::fromUTF8 (cards[i].title), inner.removeFromTop (24), juce::Justification::centred);
        g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
        g.drawFittedText (unavailable ? U ("Aucun plugin instrument VST3 trouvé (Studio › Rechercher des plugins)") : juce::String::fromUTF8 (cards[i].text),
                          inner.toNearestInt(), juce::Justification::centredTop, 3);
    }
    g.setColour (theme::dim); g.setFont (theme::ui (11.5f));
    g.drawText (U ("Aussi :"), 20, getHeight() - 38, 60, 26, juce::Justification::centredLeft);
}

void NewTrackPanel::resized()
{
    auto row = juce::Rectangle<int> (80, getHeight() - 38, getWidth() - 100, 26);
    fxBtn.setBounds (row.removeFromLeft (110)); row.removeFromLeft (8);
    folderBtn.setBounds (row.removeFromLeft (110)); row.removeFromLeft (8);
    groupBtn.setBounds (row.removeFromLeft (170));
}

void NewTrackPanel::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < numKinds; ++i) if (cardBounds (i).reduced (6).contains (e.getPosition())) h = i;
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void NewTrackPanel::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < numKinds; ++i) if (cardBounds (i).reduced (6).contains (e.getPosition())) { choose (i); return; }
}

void NewTrackPanel::choose (int i)
{
    if (i == vst3)
    {
        // Choix du plugin dans un menu, puis la piste.
        auto vi = ctx.catalog.getVST3 (true);
        if (vi.isEmpty()) return;
        juce::PopupMenu m;
        for (int k = 0; k < vi.size(); ++k) m.addItem (k + 1, vi[k].name + "  -  " + vi[k].manufacturer);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cardBounds (i))), [this, vi] (int r)
        {
            if (r <= 0 || ctx.edit == nullptr) return;
            ctx.edit->getUndoManager().beginNewTransaction (U ("Ajouter une piste"));
            ctx.addInstrumentTrack (&vi.getReference (r - 1));
            ctx.changed();
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
        });
        return;
    }
    auto& context = ctx;
    const auto kind = (Kind) i;
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
    juce::MessageManager::callAsync ([&context, kind] { create (context, kind); });
}

te::AudioTrack* NewTrackPanel::create (EditContext& ctx, Kind kind)
{
    if (ctx.edit == nullptr) return nullptr;
    auto& um = ctx.edit->getUndoManager();
    te::AudioTrack* t = nullptr;
    switch (kind)
    {
        case instrument:
            um.beginNewTransaction (U ("Ajouter une piste instrument"));
            if (auto* p = ondefx::SynthPlugin::findPreset (U ("Piano de concert"))) t = LibraryPanel::applyToTrack (ctx, nullptr, *p);
            ctx.perform ("midi.library");                // choisir le son tout de suite
            break;
        case drums:
            ctx.perform ("track.addDrummer");
            return ctx.getSelectedTrack();
        case voice:
        case guitar:
        {
            um.beginNewTransaction (kind == voice ? U ("Ajouter une piste voix") : U ("Ajouter une piste guitare"));
            t = ctx.addAudioTrack();
            if (t == nullptr) return nullptr;
            t->setName (kind == voice ? "Voix" : "Guitare");
            t->setColour (juce::Colour (cards[kind].colour));
            chains::apply (*t, chains::find (kind == voice ? U ("Voix pop") : U ("Guitare claire")));
            ctx.selectTrack (t);
            if (ctx.engine.getDeviceManager().getNumWaveInDevices() > 0) ctx.setTrackArmed (*t, true);
            break;
        }
        case audio:
            um.beginNewTransaction (U ("Ajouter une piste audio"));
            t = ctx.addAudioTrack();
            break;
        default: break;
    }
    if (t != nullptr) ctx.selectTrack (t);
    ctx.changed();
    return t;
}

juce::DialogWindow* NewTrackPanel::show (EditContext& c)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new NewTrackPanel (c));
    o.dialogTitle = "Nouvelle piste";
    o.dialogBackgroundColour = juce::Colour (0xff1a1d22);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    return o.launchAsync();
}
