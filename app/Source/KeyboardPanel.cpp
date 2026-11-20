#include "KeyboardPanel.h"
#include "ParamControls.h"

namespace
{
    const juce::String deviceName ("Clavier Onde");

    // Rangée du milieu = touches blanches, rangée du haut = touches noires, à partir de Do.
    const std::pair<juce::juce_wchar, int> azertyKeys[] = {
        { 'q', 0 }, { 'z', 1 }, { 's', 2 }, { 'e', 3 }, { 'd', 4 }, { 'f', 5 }, { 't', 6 }, { 'g', 7 }, { 'y', 8 },
        { 'h', 9 }, { 'u', 10 }, { 'j', 11 }, { 'k', 12 }, { 'o', 13 }, { 'l', 14 }, { 'p', 15 }, { 'm', 16 } };
    const std::pair<juce::juce_wchar, int> qwertyKeys[] = {
        { 'a', 0 }, { 'w', 1 }, { 's', 2 }, { 'e', 3 }, { 'd', 4 }, { 'f', 5 }, { 't', 6 }, { 'g', 7 }, { 'y', 8 },
        { 'h', 9 }, { 'u', 10 }, { 'j', 11 }, { 'k', 12 }, { 'o', 13 }, { 'l', 14 }, { 'p', 15 }, { ';', 16 } };
}

te::MidiInputDevice* KeyboardPanel::getDevice (te::Engine& engine)
{
    // Le clavier d'Onde passe par l'entrée « toutes les entrées MIDI » du moteur, qui réunit déjà les claviers
    // physiques : une seule entrée par piste, donc une seule prise (deux entrées armées sur la même piste
    // produisaient une prise vide qui remplaçait l'autre).
    auto& dm = engine.getDeviceManager();
    te::MidiInputDevice* all = nullptr;
    std::vector<std::shared_ptr<te::MidiInputDevice>> legacy;
    for (int i = 0; i < dm.getNumMidiInDevices(); ++i)
        if (auto d = dm.getMidiInDevice (i))
        {
            if (d->getDeviceID() == "all_midi_in") all = d.get();
            else if (dynamic_cast<te::VirtualMidiInputDevice*> (d.get()) != nullptr && d->getName() == deviceName) legacy.push_back (d);
        }
    for (auto& d : legacy)                       // entrée séparée des versions précédentes : retirée
        if (auto* v = dynamic_cast<te::VirtualMidiInputDevice*> (d.get())) dm.deleteVirtualMidiDevice (*v);
    if (all != nullptr)
    {
        if (! all->isEnabled()) all->setEnabled (true);
        all->setMonitorMode (te::InputDevice::MonitorMode::on);
    }
    return all;
}

KeyboardPanel::KeyboardPanel (EditContext& c) : ctx (c)
{
    keyboard.setWantsKeyboardFocus (false);        // les touches de l'ordinateur sont gérées ici (AZERTY / QWERTY)
    keyboard.setKeyPressBaseOctave (99);           // pas de correspondance clavier interne de JUCE
    keyboard.setLowestVisibleKey (24);
    keyboard.setKeyWidth (22.0f);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent.withAlpha (0.75f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent.withAlpha (0.25f));
    keyboard.setVelocity (velocity / 127.0f, false);
    keyboard.letterFor = [this] (int note) -> juce::String
    {
        const int offset = note - getBaseNote();
        if (offset < 0) return {};
        if (azerty()) { for (auto& [k, o] : azertyKeys) if (o == offset) return juce::String::charToString (juce::CharacterFunctions::toUpperCase (k)); }
        else          { for (auto& [k, o] : qwertyKeys) if (o == offset) return juce::String::charToString (juce::CharacterFunctions::toUpperCase (k)); }
        return {};
    };
    state.addListener (this);
    addAndMakeVisible (keyboard);

    layout.addItem ("AZERTY", 1);
    layout.addItem ("QWERTY", 2);
    layout.setSelectedId (1, juce::dontSendNotification);
    layout.setWantsKeyboardFocus (false);
    layout.onChange = [this] { releaseAllKeys(); keyboard.repaint(); repaint(); };
    addAndMakeVisible (layout);

    for (auto* b : { &octDown, &octUp, &sustain }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    octDown.onClick = [this] { setOctave (octave - 1); };
    octUp.onClick   = [this] { setOctave (octave + 1); };
    sustain.setClickingTogglesState (true);
    sustain.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    sustain.onClick = [this] { send (juce::MidiMessage::controllerEvent (1, 64, sustain.getToggleState() ? 127 : 0)); };
    sustain.setTooltip (U ("Pédale de sustain (touche Tab)"));

    velocitySlider.setRange (1, 127, 1);
    velocitySlider.setValue (velocity, juce::dontSendNotification);
    velocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
    velocitySlider.setWantsKeyboardFocus (false);
    velocitySlider.onValueChange = [this] { setVelocity ((int) velocitySlider.getValue()); };
    addAndMakeVisible (velocitySlider);

    target.setFont (theme::ui (13.0f, true));
    target.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (target);

    setWantsKeyboardFocus (true);
    getDevice (ctx.engine);
    if (auto* t = ctx.getSelectedTrack()) ctx.routeInputsTo (t);
    setSize (980, 250);
    timerCallback();
    startTimerHz (4);
}

KeyboardPanel::~KeyboardPanel()
{
    releaseAllKeys();
    state.allNotesOff (1);
    state.removeListener (this);
}

void KeyboardPanel::visibilityChanged()
{
    if (isShowing()) juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<KeyboardPanel> (this)] { if (safe) safe->grabKeyboardFocus(); });
}

void KeyboardPanel::timerCallback()
{
    // Ce que le clavier joue : l'instrument de la piste sélectionnée.
    auto* t = ctx.edit != nullptr ? ctx.getSelectedTrack() : nullptr;
    juce::String text;
    if (t == nullptr) text = U ("Sélectionnez une piste instrument pour jouer");
    else if (auto* inst = ctx.getInstrument (*t)) text = U ("Joue « ") + t->getName() + U (" » · ") + inst->getName();
    else text = U ("« ") + t->getName() + U (" » n'a pas d'instrument : choisissez une piste instrument");
    if (target.getText() != text) target.setText (text, juce::dontSendNotification);
    if (t != nullptr && t->itemID != lastRouted) { lastRouted = t->itemID; ctx.routeInputsTo (t); getDevice (ctx.engine); }
}

void KeyboardPanel::send (const juce::MidiMessage& m)
{
    if (auto* d = getDevice (ctx.engine))
    {
        auto msg = m;
        msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        d->handleIncomingMidiMessage (msg, d->getMPESourceID());
    }
}

void KeyboardPanel::handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float v)
{
    send (juce::MidiMessage::noteOn (channel, note, v));
}

void KeyboardPanel::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float)
{
    send (juce::MidiMessage::noteOff (channel, note));
}

void KeyboardPanel::noteOn (int note, int v)  { state.noteOn (1, juce::jlimit (0, 127, note), juce::jlimit (1, 127, v) / 127.0f); }
void KeyboardPanel::noteOff (int note)        { state.noteOff (1, juce::jlimit (0, 127, note), 0.0f); }

void KeyboardPanel::setOctave (int o)
{
    releaseAllKeys();
    octave = juce::jlimit (0, 8, o);
    keyboard.setLowestVisibleKey (juce::jlimit (0, 84, getBaseNote() - 12));
    keyboard.repaint();
    repaint();
}

void KeyboardPanel::setVelocity (int v)
{
    velocity = juce::jlimit (1, 127, v);
    keyboard.setVelocity (velocity / 127.0f, false);
    velocitySlider.setValue (velocity, juce::dontSendNotification);
    repaint();
}

void KeyboardPanel::releaseAllKeys()
{
    for (auto& [key, note] : held) noteOff (note);
    held.clear();
}

int KeyboardPanel::offsetFor (juce::juce_wchar c) const
{
    c = juce::CharacterFunctions::toLowerCase (c);
    if (azerty()) { for (auto& [k, o] : azertyKeys) if (k == c) return o; }
    else          { for (auto& [k, o] : qwertyKeys) if (k == c) return o; }
    return -1;
}

bool KeyboardPanel::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown()) return false;   // raccourcis d'Onde
    auto c = key.getTextCharacter();
    if (c == 0) c = (juce::juce_wchar) key.getKeyCode();
    c = juce::CharacterFunctions::toLowerCase (c);
    if (key.getKeyCode() == juce::KeyPress::tabKey)
    {
        sustain.setToggleState (! sustain.getToggleState(), juce::sendNotificationSync);
        return true;
    }
    const juce::juce_wchar down = azerty() ? 'w' : 'z';
    if (c == down) { setOctave (octave - 1); return true; }
    if (c == 'x')  { setOctave (octave + 1); return true; }
    if (c == 'c')  { setVelocity (velocity - 10); return true; }
    if (c == 'v')  { setVelocity (velocity + 10); return true; }
    const int offset = offsetFor (c);
    if (offset < 0) return false;
    if (held.find (c) == held.end())
    {
        const int note = juce::jlimit (0, 127, getBaseNote() + offset);
        held[c] = note;
        noteOn (note, velocity);
    }
    return true;
}

bool KeyboardPanel::keyStateChanged (bool)
{
    bool any = false;
    for (auto it = held.begin(); it != held.end();)
    {
        const int code = (int) juce::CharacterFunctions::toUpperCase (it->first);
        if (! juce::KeyPress::isKeyCurrentlyDown (code) && ! juce::KeyPress::isKeyCurrentlyDown ((int) it->first))
        {
            noteOff (it->second);
            it = held.erase (it);
            any = true;
        }
        else ++it;
    }
    return any;
}

void KeyboardPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (28);
    juce::ignoreUnused (top);
    // Aide : touches de l'ordinateur (ou rappel de cliquer dans la fenêtre si elle n'a pas le clavier).
    auto bottom = getLocalBounds().reduced (12, 0).removeFromBottom (26);
    g.setColour (theme::text);
    g.setFont (theme::mono (12.0f, true));
    g.drawText ("Do" + juce::String (octave) + U (" · vélocité ") + juce::String (velocity), bottom, juce::Justification::centredRight, true);
    bottom.removeFromRight (150);
    if (! hasKeyboardFocus (true))
    {
        g.setColour (juce::Colour (0xffe5a04e));
        g.setFont (theme::ui (12.0f, true));
        g.drawText (U ("Cliquez dans cette fenêtre pour jouer avec le clavier de l'ordinateur"), bottom, juce::Justification::centredLeft, true);
        return;
    }
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    g.drawText (azerty() ? U ("Lettres sur les touches = clavier de l'ordinateur · W / X = octave · C / V = vélocité · Tab = sustain")
                         : U ("Lettres sur les touches = clavier de l'ordinateur · Z / X = octave · C / V = vélocité · Tab = sustain"),
                bottom, juce::Justification::centredLeft, true);
}

void KeyboardPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (28);
    layout.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (8);
    octDown.setBounds (top.removeFromLeft (84)); top.removeFromLeft (4);
    octUp.setBounds (top.removeFromLeft (84)); top.removeFromLeft (8);
    sustain.setBounds (top.removeFromLeft (80)); top.removeFromLeft (12);
    velocitySlider.setBounds (top.removeFromLeft (180)); top.removeFromLeft (12);
    target.setBounds (top);
    r.removeFromTop (8);
    r.removeFromBottom (26);
    keyboard.setBounds (r);
}

juce::DialogWindow* KeyboardPanel::show (EditContext& c)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new KeyboardPanel (c));
    o.dialogTitle = U ("Clavier");
    o.dialogBackgroundColour = juce::Colour (0xff1a1d22);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    // Fenêtre flottante : la fenêtre principale reste utilisable.
    return ondeui::showToolWindow (o.dialogTitle, o.content.release(), o.resizable, o.dialogBackgroundColour);
}
