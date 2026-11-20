#include "ModulatorPanel.h"
#include "ParamControls.h"

namespace
{
    const juce::Colour palette[] = { juce::Colour (0xff3fa4ff), juce::Colour (0xffe5a04e), juce::Colour (0xff2de2c6),
                                     juce::Colour (0xffb98cff), juce::Colour (0xffe8697a), juce::Colour (0xff9be35b) };

    juce::StringArray rateTypes()
    {
        return { U ("Hertz"), U ("4 mesures"), U ("2 mesures"), U ("1 mesure"),
                 "1/2 T", "1/2", U ("1/2 pointée"), "1/4 T", "1/4", U ("1/4 pointée"), "1/8 T", "1/8", U ("1/8 pointée"),
                 "1/16 T", "1/16", U ("1/16 pointée"), "1/32 T", "1/32", U ("1/32 pointée"), "1/64 T", "1/64", U ("1/64 pointée") };
    }

    // Libellés français et listes des paramètres des modulateurs (identifiants du moteur).
    juce::StringArray itemsFor (const juce::String& id)
    {
        if (id == "wave")      return { "Sinus", "Triangle", "Dent de scie montante", "Dent de scie descendante", U ("Carré"),
                                        "4 marches montantes", "4 marches descendantes", "8 marches montantes", "8 marches descendantes",
                                        U ("Aléatoire"), "Bruit" };
        if (id == "syncType")  return { "Libre", "Sur le transport", U ("À chaque note") };
        if (id == "rateType")  return rateTypes();
        if (id == "biopolar")  return { "Unipolaire", "Bipolaire" };
        if (id == "type")      return { U ("Aléatoire par pas"), "Bruit" };
        if (id == "lowPassEnabled" || id == "highPassEnabled") return { "Non", "Oui" };
        return {};
    }

    juce::String labelFor (const juce::String& id, const juce::String& fallback)
    {
        static const std::pair<const char*, const char*> names[] = {
            { "wave", "Forme d'onde" }, { "syncType", "Synchro" }, { "rate", "Vitesse (Hz)" }, { "rateType", "Cadence" },
            { "depth", "Profondeur" }, { "biopolar", "Polarité" }, { "phase", "Phase" }, { "offset", "Décalage" },
            { "numSteps", "Pas" }, { "gainDb", "Gain d'entrée" }, { "attack", "Attaque" }, { "hold", "Maintien" },
            { "release", "Relâchement" }, { "lowPassEnabled", "Passe-bas" }, { "highPassEnabled", "Passe-haut" },
            { "lowPassFrequency", "Fréq. passe-bas" }, { "highPassFrequency", "Fréq. passe-haut" }, { "type", "Type" },
            { "shape", "Forme" }, { "stepDepth", "Écart des pas" }, { "smooth", "Lissage" } };
        for (auto& [k, v] : names) if (id == k) return U (v);
        return fallback;
    }

    ModulatorPanel::Kind kindOf (te::Modifier& m)
    {
        if (dynamic_cast<te::StepModifier*> (&m) != nullptr)              return ModulatorPanel::Kind::step;
        if (dynamic_cast<te::EnvelopeFollowerModifier*> (&m) != nullptr)  return ModulatorPanel::Kind::envelope;
        if (dynamic_cast<te::RandomModifier*> (&m) != nullptr)            return ModulatorPanel::Kind::random;
        return ModulatorPanel::Kind::lfo;
    }

    te::Track* trackOwning (te::Modifier& m)
    {
        for (auto* t : te::getAllTracks (m.edit))
            if (auto* list = t->getModifierList())
                if (list->getModifiers().contains (&m)) return t;
        return nullptr;
    }

    //==========================================================================
    // Pas à pas : barres de -1 à +1, à dessiner à la souris ; le pas en cours est éclairé.
    class StepGrid  : public juce::Component, private juce::Timer
    {
    public:
        StepGrid (te::StepModifier& s, juce::Colour c) : step (&s), colour (c) { startTimerHz (20); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (r, 4.0f);
            const int n = count();
            const float w = r.getWidth() / (float) n, mid = r.getCentreY();
            const int current = step->getCurrentStep();
            g.setColour (juce::Colour (0xff2a2e35)); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
            for (int i = 0; i < n; ++i)
            {
                const float v = step->getStep (i), h = v * (r.getHeight() * 0.5f - 3.0f);
                auto bar = juce::Rectangle<float> (r.getX() + i * w + 1.0f, h >= 0 ? mid - h : mid, w - 2.0f, std::abs (h)).withHeight (juce::jmax (1.5f, std::abs (h)));
                g.setColour (i == current ? juce::Colours::white : colour.withAlpha (0.85f));
                g.fillRect (bar);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            step->edit.getUndoManager().beginNewTransaction (U ("Pas du modulateur"));
            mouseDrag (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto r = getLocalBounds().toFloat();
            const int i = juce::jlimit (0, count() - 1, (int) ((e.position.x - r.getX()) / r.getWidth() * (float) count()));
            const float v = juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f - 3.0f));
            step->setStep (i, e.mods.isAltDown() ? 0.0f : v);
            repaint();
        }
    private:
        int count() const { return juce::jlimit (2, (int) te::StepModifier::maxNumSteps, juce::roundToInt (step->numStepsParam->getCurrentValue())); }
        void timerCallback() override { repaint(); }
        juce::ReferenceCountedObjectPtr<te::StepModifier> step;
        juce::Colour colour;
    };

    //==========================================================================
    // Une affectation : paramètre visé, profondeur (-100 à +100 % de sa course), retrait.
    class AssignmentRow  : public juce::Component
    {
    public:
        AssignmentRow (te::AutomatableParameter& p, te::AutomatableParameter::ModifierAssignment& a, juce::Colour c)
            : param (&p), assignment (&a)
        {
            auto* owner = p.getTrack();
            juce::String what = p.getParameterName();
            if (auto* plugin = p.getPlugin())
            {
                if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*> (plugin)) what = &p == vp->panParam.get() ? U ("Panoramique") : U ("Volume");
                else what = plugin->getName() + U (" · ") + what;
            }
            name.setText (U ("→ ") + (owner != nullptr ? owner->getName() + U (" · ") : juce::String()) + what, juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, theme::text);
            name.setFont (theme::ui (12.5f));
            depth.setSliderStyle (juce::Slider::LinearHorizontal);
            depth.setRange (-100.0, 100.0, 1.0);
            depth.setTextValueSuffix (" %");
            depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            depth.setColour (juce::Slider::trackColourId, c);
            depth.setDoubleClickReturnValue (true, 50.0);
            depth.setValue (assignment->value.get() * 100.0, juce::dontSendNotification);
            depth.onDragStart = [this] { param->getEdit().getUndoManager().beginNewTransaction (U ("Profondeur de modulation")); };
            depth.onValueChange = [this] { assignment->value = (float) (depth.getValue() / 100.0); };
            depth.setTooltip (U ("Profondeur : part de la course du paramètre parcourue par le modulateur (négative = sens inverse)"));
            remove.setTooltip (U ("Retirer cette affectation"));
            remove.onClick = [this]
            {
                param->getEdit().getUndoManager().beginNewTransaction (U ("Retirer une affectation"));
                param->removeModifier (*assignment);
            };
            addAndMakeVisible (name); addAndMakeVisible (depth); addAndMakeVisible (remove);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            remove.setBounds (r.removeFromRight (26).reduced (2));
            depth.setBounds (r.removeFromRight (230));
            name.setBounds (r);
        }
    private:
        te::AutomatableParameter::Ptr param;
        te::AutomatableParameter::ModifierAssignment::Ptr assignment;
        juce::Label name;
        juce::Slider depth;
        juce::TextButton remove { juce::String::fromUTF8 ("×") };
    };
}

//==============================================================================
class ModulatorPanel::Card  : public juce::Component, private juce::Timer
{
public:
    Card (te::Modifier& m) : mod (&m), colour (m.colour.get())
    {
        enable = std::make_unique<ondeui::ParamToggle> (m.enabledParam.get(), "Actif", colour);
        addAndMakeVisible (*enable);
        removeButton.setTooltip (U ("Supprimer ce modulateur et ses affectations"));
        removeButton.onClick = [this]
        {
            auto& edit = mod->edit;
            edit.getUndoManager().beginNewTransaction (U ("Supprimer un modulateur"));
            for (auto p : te::getAllParametersBeingModifiedBy (edit, *mod)) p->removeModifier (*mod);
            mod->remove();
        };
        addAndMakeVisible (removeButton);
        for (auto* p : m.getAutomatableParameters())
        {
            if (p == m.enabledParam.get()) continue;
            const auto id = p->paramID;
            const auto label = labelFor (id, p->getParameterName());
            if (itemsFor (id).size() > 0) controls.add (new ondeui::ParamChoice (p, label, itemsFor (id)));
            else                          controls.add (new ondeui::Knob (p, label, colour));
            if (id == "rate") rateControl = controls.getLast();
            if (id == "rateType") rateType = p;
            addAndMakeVisible (controls.getLast());
        }
        if (auto* s = dynamic_cast<te::StepModifier*> (&m))
        {
            steps = std::make_unique<StepGrid> (*s, colour);
            addAndMakeVisible (*steps);
        }
        assignButton.onClick = [this] { showAssignMenu(); };
        addAndMakeVisible (assignButton);
        refreshAssignments();
        startTimerHz (30);
    }

    int preferredHeight() const
    {
        const int controlRows = (controls.size() + perRow - 1) / perRow;
        return 40 + juce::jmax (scopeH, controlRows * 86) + (steps ? 76 : 0) + 8 + rows.size() * 28 + 34;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff20242a)); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colour.withAlpha (0.6f)); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (colour); g.fillEllipse (14.0f, 13.0f, 12.0f, 12.0f);
        g.setColour (theme::text); g.setFont (theme::ui (15.0f, true));
        g.drawText (describe (*mod), 34, 8, 260, 22, juce::Justification::centredLeft);

        // Oscilloscope : les 2 dernières secondes du modulateur (-1 en bas, +1 en haut).
        auto s = scope().toFloat();
        g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (s, 6.0f);
        g.setColour (juce::Colour (0xff2a2e35)); g.drawHorizontalLine ((int) s.getCentreY(), s.getX(), s.getRight());
        const auto values = mod->getValues (te::TimeDuration::fromSeconds (2.0));
        if (values.size() > 2)
        {
            juce::Path p;
            const int n = (int) values.size(), stride = juce::jmax (1, n / (int) s.getWidth());
            for (int i = 0; i < n; i += stride)
            {
                const float x = s.getRight() - (float) i / (float) n * s.getWidth();   // le plus récent à droite
                const float y = s.getCentreY() - juce::jlimit (-1.0f, 1.0f, values[(size_t) i]) * (s.getHeight() * 0.5f - 4.0f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (colour); g.strokePath (p, juce::PathStrokeType (1.8f));
        }
        else
        {
            g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
            g.drawText (U ("Lancez la lecture pour voir le mouvement"), s, juce::Justification::centred);
        }
        if (rows.isEmpty())
        {
            g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
            g.drawText (U ("Aucune affectation : choisissez le paramètre à faire bouger."), assignArea().toFloat(), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        enable->setBounds (getWidth() - 200, 8, 80, 24);
        removeButton.setBounds (getWidth() - 112, 8, 100, 24);
        auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (40);
        area.removeFromLeft (scopeW + 12);
        const int cellW = area.getWidth() / perRow;
        for (int i = 0; i < controls.size(); ++i)
        {
            juce::Rectangle<int> cell (area.getX() + (i % perRow) * cellW, area.getY() + (i / perRow) * 86, cellW - 6, 82);
            if (dynamic_cast<ondeui::ParamChoice*> (controls[i]) != nullptr) cell = cell.withSizeKeepingCentre (cell.getWidth(), 44);
            controls[i]->setBounds (cell);
        }
        int y = 40 + juce::jmax (scopeH, ((controls.size() + perRow - 1) / perRow) * 86);
        if (steps) { steps->setBounds (12, y + 4, getWidth() - 24, 66); y += 76; }
        y += 8;
        for (auto* row : rows) { row->setBounds (12, y, getWidth() - 24, 26); y += 28; }
        assignButton.setBounds (12, y + 2, 230, 26);
    }

    te::Modifier* modifier() const { return mod.get(); }

private:
    static constexpr int perRow = 6, scopeW = 200, scopeH = 110;
    juce::Rectangle<int> scope() const      { return { 12, 40, scopeW, scopeH }; }
    juce::Rectangle<int> assignArea() const { return assignButton.getBounds().translated (assignButton.getWidth() + 12, 0).withWidth (getWidth() - 280); }

    int countAssignments() const
    {
        int n = 0;
        mod->edit.visitAllAutomatableParams (true, [&] (te::AutomatableParameter& p)
        {
            for (auto* a : p.getAssignments()) if (a->isForModifierSource (*mod)) ++n;
        });
        return n;
    }

    void refreshAssignments()
    {
        rows.clear();
        mod->edit.visitAllAutomatableParams (true, [&] (te::AutomatableParameter& p)
        {
            for (auto* a : p.getAssignments())
                if (a->isForModifierSource (*mod)) addAndMakeVisible (rows.add (new AssignmentRow (p, *a, colour)));
        });
        shownAssignments = rows.size();
        if (auto* panel = findParentComponentOfClass<ModulatorPanel>()) panel->resized();
        resized();
        repaint();
    }

    void timerCallback() override
    {
        if (countAssignments() != shownAssignments) refreshAssignments();
        // La vitesse en hertz ne compte que si la cadence est « Hertz » (sinon calée sur le tempo).
        if (rateControl != nullptr && rateType != nullptr)
        {
            const bool free = juce::roundToInt (rateType->getCurrentValue()) == (int) te::ModifierCommon::hertz;
            if (rateControl->isEnabled() != free) { rateControl->setEnabled (free); rateControl->setAlpha (free ? 1.0f : 0.35f); }
        }
        repaint (scope());
    }

    void showAssignMenu()
    {
        // Paramètres de chaque piste : volume, panoramique, puis réglages de chaque effet. La piste du
        // modulateur d'abord ; les autres ensuite (utile au suiveur d'enveloppe : ducking).
        targets.clear();
        juce::PopupMenu menu, others;
        auto* home = trackOwning (*mod);
        auto addTrack = [this] (juce::PopupMenu& m, te::AudioTrack& t)
        {
            auto add = [this] (juce::PopupMenu& into, te::AutomatableParameter* p, const juce::String& label)
            {
                if (p == nullptr) return;
                bool already = false;
                for (auto* a : p->getAssignments()) already = already || a->isForModifierSource (*mod);
                targets.add (p);
                into.addItem (targets.size(), label, ! already, already);
            };
            if (auto* vp = t.getVolumePlugin()) { add (m, vp->volParam.get(), "Volume"); add (m, vp->panParam.get(), "Panoramique"); }
            for (auto* plugin : t.pluginList)
            {
                if (dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr) continue;
                juce::PopupMenu sub;
                for (auto* p : plugin->getAutomatableParameters()) add (sub, p, p->getParameterName());
                if (sub.getNumItems() > 0) m.addSubMenu (plugin->getName(), sub);
                else m.addItem (-1, plugin->getName() + U (" (réglages non exposés)"), false);
            }
        };
        for (auto* t : te::getAudioTracks (mod->edit))
        {
            juce::PopupMenu sub;
            addTrack (sub, *t);
            if (t == home) { menu.addSectionHeader (U ("Piste « ") + t->getName() + U (" »")); addTrack (menu, *t); }
            else others.addSubMenu (t->getName(), sub);
        }
        if (others.getNumItems() > 0) { menu.addSeparator(); menu.addSubMenu ("Autres pistes", others); }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&assignButton), [this] (int r)
        {
            if (r <= 0 || r > targets.size()) return;
            assign (*mod, *targets[r - 1], 0.5f);
            refreshAssignments();
        });
    }

    te::Modifier::Ptr mod;
    juce::Colour colour;
    std::unique_ptr<ondeui::ParamToggle> enable;
    juce::TextButton removeButton { "Supprimer" }, assignButton { U ("+ Affecter à un paramètre…") };
    juce::OwnedArray<juce::Component> controls, rows;
    std::unique_ptr<StepGrid> steps;
    juce::Array<te::AutomatableParameter*> targets;
    juce::Component* rateControl = nullptr;
    te::AutomatableParameter::Ptr rateType;
    int shownAssignments = 0;
};

//==============================================================================
const char* ModulatorPanel::kindName (Kind k)
{
    switch (k)
    {
        case Kind::step:     return "Pas à pas";
        case Kind::envelope: return "Suiveur d'enveloppe";
        case Kind::random:   return "Aléatoire";
        default:             return "LFO";
    }
}

te::Modifier::Ptr ModulatorPanel::addModulator (te::Track& track, Kind kind)
{
    auto* list = track.getModifierList();
    if (list == nullptr) return {};
    auto& edit = track.edit;
    edit.getUndoManager().beginNewTransaction (U ("Ajouter un modulateur (") + U (kindName (kind)) + ")");
    const juce::Identifier type = kind == Kind::step ? te::IDs::STEP : kind == Kind::envelope ? te::IDs::ENVELOPEFOLLOWER
                                : kind == Kind::random ? te::IDs::RANDOM : te::IDs::LFO;
    const int index = list->getModifiers().size();
    juce::ValueTree v (type);
    v.setProperty (te::IDs::colour, palette[index % juce::numElementsInArray (palette)].toString(), nullptr);
    auto m = list->insertModifier (v, -1, nullptr);
    if (m == nullptr) return {};
    // Réglages de départ utiles : le moteur part d'une profondeur nulle pour le pas à pas et le suiveur.
    if (auto* s = dynamic_cast<te::StepModifier*> (m.get()))
    {
        s->depthParam->setParameter (1.0f, juce::sendNotification);
        s->numStepsParam->setParameter (8.0f, juce::sendNotification);
        s->rateTypeParam->setParameter ((float) te::ModifierCommon::eighth, juce::sendNotification);
        s->syncTypeParam->setParameter ((float) te::ModifierCommon::transport, juce::sendNotification);
        for (int i = 0; i < 8; ++i) s->setStep (i, i % 2 == 0 ? 1.0f : -0.4f);
    }
    if (auto* e = dynamic_cast<te::EnvelopeFollowerModifier*> (m.get()))
    {
        e->depthParam->setParameter (1.0f, juce::sendNotification);
        e->attackParam->setParameter (10.0f, juce::sendNotification);
        e->releaseParam->setParameter (150.0f, juce::sendNotification);
    }
    if (auto* l = dynamic_cast<te::LFOModifier*> (m.get()))
    {
        l->depthParam->setParameter (1.0f, juce::sendNotification);
        l->bipolarParam->setParameter (1.0f, juce::sendNotification);
        l->rateTypeParam->setParameter ((float) te::ModifierCommon::hertz, juce::sendNotification);
    }
    return m;
}

te::AutomatableParameter::ModifierAssignment::Ptr ModulatorPanel::assign (te::Modifier& m, te::AutomatableParameter& p, float depth)
{
    m.edit.getUndoManager().beginNewTransaction (U ("Affecter un modulateur à « ") + p.getParameterName() + U (" »"));
    return p.addModifier (m, juce::jlimit (-1.0f, 1.0f, depth));
}

juce::String ModulatorPanel::describe (te::Modifier& m)
{
    const auto kind = kindOf (m);
    int n = 0;
    if (auto* t = trackOwning (m))
        for (auto* other : t->getModifierList()->getModifiers())
        {
            if (kindOf (*other) == kind) ++n;
            if (other == &m) break;
        }
    return U (kindName (kind)) + " " + juce::String (juce::jmax (1, n));
}

//==============================================================================
ModulatorPanel::ModulatorPanel (EditContext& c) : ctx (c)
{
    title.setFont (theme::ui (14.0f, true));
    title.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (title);
    for (int i = 0; i < 4; ++i)
    {
        auto& b = addButtons[i];
        b.setButtonText (U ("+ ") + U (kindName ((Kind) i)));
        b.onClick = [this, i] { if (auto* t = currentTrack()) addModulator (*t, (Kind) i); };
        addAndMakeVisible (b);
    }
    addButtons[0].setTooltip (U ("Oscillateur : sinus, triangle, carré, marches… libre ou calé sur le tempo"));
    addButtons[1].setTooltip (U ("Séquence de valeurs à dessiner, calée sur le tempo"));
    addButtons[2].setTooltip (U ("Suit le niveau de la piste (ducking, effets qui réagissent au jeu)"));
    addButtons[3].setTooltip (U ("Valeurs aléatoires lissées ou par pas"));
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setSize (980, 640);
    rebuild();
    startTimerHz (5);
}

ModulatorPanel::~ModulatorPanel() = default;

te::Track* ModulatorPanel::currentTrack() const { return ctx.edit != nullptr ? ctx.getSelectedTrack() : nullptr; }
int ModulatorPanel::numCards() const            { return cards.size(); }

juce::String ModulatorPanel::signature() const
{
    juce::String s = juce::String::toHexString ((juce::pointer_sized_int) ctx.edit);
    if (auto* t = currentTrack())
    {
        s << "/" << t->itemID.toString() << "/" << t->getName();
        if (auto* list = t->getModifierList())
            for (auto* m : list->getModifiers()) s << "/" << m->itemID.toString();
    }
    return s;
}

void ModulatorPanel::rebuild()
{
    lastSignature = signature();
    cards.clear();
    auto* t = currentTrack();
    title.setText (t != nullptr ? U ("Modulateurs de la piste « ") + t->getName() + U (" »")
                                : U ("Sélectionnez une piste pour lui ajouter des modulateurs"), juce::dontSendNotification);
    for (auto& b : addButtons) b.setEnabled (t != nullptr && t->getModifierList() != nullptr);
    if (t != nullptr && t->getModifierList() != nullptr)
        for (auto* m : t->getModifierList()->getModifiers())
            content.addAndMakeVisible (cards.add (new Card (*m)));
    resized();
    repaint();
}

void ModulatorPanel::timerCallback()
{
    if (signature() != lastSignature) rebuild();
}

void ModulatorPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    if (cards.isEmpty() && currentTrack() != nullptr)
    {
        g.setColour (theme::muted); g.setFont (theme::ui (13.0f));
        g.drawFittedText (U ("Un modulateur fait bouger un réglage tout seul : trémolo (LFO sur le volume), filtre rythmé (pas à pas),\n"
                             "ducking (suiveur d'enveloppe sur le volume d'une autre piste), variations (aléatoire).\n"
                             "Ajoutez-en un avec les boutons ci-dessus, puis affectez-le à un paramètre."),
                          getLocalBounds().withTrimmedTop (90).reduced (24, 0).removeFromTop (80), juce::Justification::centredTop, 4);
    }
}

void ModulatorPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (30);
    for (int i = 3; i >= 0; --i) { addButtons[i].setBounds (top.removeFromRight (i == 2 ? 170 : 120)); top.removeFromRight (6); }
    title.setBounds (top);
    r.removeFromTop (8);
    viewport.setBounds (r);
    const int w = r.getWidth() - viewport.getScrollBarThickness();
    int y = 0;
    for (auto* c : cards) { c->setBounds (0, y, w, c->preferredHeight()); y += c->getHeight() + 10; }
    content.setSize (w, juce::jmax (y, 10));
}

juce::DialogWindow* ModulatorPanel::show (EditContext& c)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ModulatorPanel (c));
    o.dialogTitle = U ("Modulateurs");
    o.dialogBackgroundColour = juce::Colour (0xff1a1d22);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    return o.launchAsync();
}
