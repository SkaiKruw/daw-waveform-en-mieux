// Fenêtre « Télécommande (téléphone) » : activation, QR code à scanner, adresses et code d'appairage.
#pragma once
#include "RemoteControl.h"
#include "QrCode.h"

class RemotePanel  : public juce::Component, private juce::Timer
{
public:
    explicit RemotePanel (RemoteControl&);
    ~RemotePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static juce::DialogWindow* show (RemoteControl&);   // la fenêtre (à fermer avant de détruire la télécommande)

private:
    void timerCallback() override { refresh(); }
    void refresh();

    RemoteControl& remote;
    juce::ToggleButton enable { U ("Activer la télécommande") };
    juce::ComboBox address;
    juce::TextEditor urlText;
    juce::Label hint;
    QrCode qr;
    juce::Rectangle<float> qrArea;
    juce::String shownUrl;
};
