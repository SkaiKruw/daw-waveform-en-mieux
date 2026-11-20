#include "RemotePanel.h"
#include "Theme.h"

RemotePanel::RemotePanel (RemoteControl& r) : remote (r)
{
    enable.setToggleState (remote.isRunning(), juce::dontSendNotification);
    enable.onClick = [this]
    {
        if (enable.getToggleState())
        {
            if (! remote.start())
            {
                enable.setToggleState (false, juce::dontSendNotification);
                hint.setText (U ("Le port ") + juce::String (RemoteControl::defaultPort) + U (" est déjà utilisé par un autre programme."), juce::dontSendNotification);
                return;
            }
        }
        else remote.stop();
        refresh();
        resized();
    };
    address.onChange = [this] { refresh(); };
    urlText.setReadOnly (true);
    urlText.setMultiLine (true, true);
    urlText.setFont (theme::mono (13.0f));
    urlText.setColour (juce::TextEditor::backgroundColourId, theme::display);
    urlText.setColour (juce::TextEditor::textColourId, theme::lcd);
    urlText.setColour (juce::TextEditor::outlineColourId, theme::panel3);
    hint.setColour (juce::Label::textColourId, theme::muted);
    hint.setFont (theme::ui (12.5f));
    hint.setJustificationType (juce::Justification::topLeft);
    for (auto* c : std::initializer_list<juce::Component*> { &enable, &address, &urlText, &hint }) addAndMakeVisible (*c);
    setSize (560, 400);
    refresh();
    startTimer (1000);
}

RemotePanel::~RemotePanel() { stopTimer(); }

void RemotePanel::refresh()
{
    const bool on = remote.isRunning();
    enable.setToggleState (on, juce::dontSendNotification);
    const auto addrs = on ? remote.getAddresses() : juce::Array<RemoteControl::Address>();
    const int keep = address.getSelectedItemIndex();
    juce::StringArray labels;
    for (auto& a : addrs) labels.add (a.label);
    juce::StringArray current;
    for (int i = 0; i < address.getNumItems(); ++i) current.add (address.getItemText (i));
    if (labels != current)
    {
        address.clear (juce::dontSendNotification);
        for (int i = 0; i < labels.size(); ++i) address.addItem (labels[i], i + 1);
        address.setSelectedItemIndex (juce::jlimit (0, juce::jmax (0, labels.size() - 1), keep < 0 ? 0 : keep), juce::dontSendNotification);
    }
    address.setVisible (on && ! addrs.isEmpty());

    juce::String url;
    if (on && ! addrs.isEmpty()) url = addrs[juce::jmax (0, address.getSelectedItemIndex())].url;
    if (url != shownUrl)
    {
        shownUrl = url;
        qr = QrCode::encode (url);
        urlText.setText (url, false);
    }
    urlText.setVisible (url.isNotEmpty());
    if (! on)
        hint.setText (U ("Pilotez Onde depuis votre téléphone comme un Stream Deck : lecture, enregistrement, repères, ")
                      + U ("annuler, pistes (muet, solo, armement) et vos propres boutons.\n\n")
                      + U ("Le téléphone doit être sur le même réseau Wi-Fi que ce PC (ou relié par Tailscale)."), juce::dontSendNotification);
    else if (addrs.isEmpty())
        hint.setText (U ("Aucun réseau local trouvé : connectez ce PC au Wi-Fi ou à la box."), juce::dontSendNotification);
    else
        hint.setText (U ("Scannez le QR code avec l'appareil photo du téléphone, ou tapez l'adresse. ")
                      + U ("Astuce : « Ajouter à l'écran d'accueil » pour l'ouvrir comme une application.\n\n")
                      + U ("Si Windows demande l'autorisation du pare-feu, acceptez pour les réseaux privés. ")
                      + U ("Le code change à chaque activation."), juce::dontSendNotification);
    repaint();
}

void RemotePanel::resized()
{
    auto r = getLocalBounds().reduced (18);
    enable.setBounds (r.removeFromTop (28));
    r.removeFromTop (10);
    auto body = r;
    auto left = body.removeFromLeft (230);
    qrArea = left.removeFromTop (230).toFloat();
    body.removeFromLeft (18);
    address.setBounds (body.removeFromTop (28));
    body.removeFromTop (14);
    body.removeFromTop (60);      // code d'appairage (dessiné)
    urlText.setBounds (body.removeFromTop (52));
    body.removeFromTop (12);
    if (remote.isRunning()) hint.setBounds (r.withTrimmedTop (244));
    else hint.setBounds (r.withTrimmedTop (4));
}

void RemotePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (! remote.isRunning()) return;
    if (qr.size() > 0) { qr.draw (g, qrArea); }
    else { g.setColour (theme::display); g.fillRoundedRectangle (qrArea, 6.0f); }
    // Code d'appairage en grand.
    auto codeArea = juce::Rectangle<float> (qrArea.getRight() + 18.0f, qrArea.getY() + 42.0f, (float) getWidth() - qrArea.getRight() - 36.0f, 60.0f);
    g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
    g.drawText (U ("Code d'appairage"), codeArea.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.setColour (theme::text); g.setFont (theme::mono (30.0f, true));
    const auto c = remote.getCode();
    g.drawText (c.substring (0, 3) + " " + c.substring (3), codeArea, juce::Justification::centredLeft);
}

juce::DialogWindow* RemotePanel::show (RemoteControl& r)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new RemotePanel (r));
    o.dialogTitle = U ("Télécommande (téléphone)");
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    return o.launchAsync();
}
