// Vue Mixeur : une tranche par piste + le master.
#pragma once
#include "EditContext.h"

class MixerView  : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit MixerView (EditContext&);
    ~MixerView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    class Strip;
private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void rebuild();
    juce::String signature() const;
    EditContext& ctx;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Strip> strips;
    juce::String lastSig;
};
