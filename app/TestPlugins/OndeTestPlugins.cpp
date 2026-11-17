// Deux plugins VST3 de test, compilés avec Onde :
//   ONDE_TEST_SYNTH = 1 : « Onde Test Synth » — petit synthé en dents de scie (MIDI)
//   ONDE_TEST_SYNTH = 0 : « Onde Crash Test » — effet de gain avec un bouton « Faire planter »
#include <JuceHeader.h>
#include <cstdint>

namespace
{
    [[noreturn]] void crashNow()
    {
        static volatile std::uintptr_t address = 0;
        *reinterpret_cast<volatile int*> (address) = 42;   // vrai accès mémoire invalide
        std::abort();
    }
}

class TestProcessor  : public juce::AudioProcessor
{
public:
    TestProcessor()
        : AudioProcessor (ONDE_TEST_SYNTH ? BusesProperties().withOutput ("Sortie", juce::AudioChannelSet::stereo())
                                          : BusesProperties().withInput ("Entrée", juce::AudioChannelSet::stereo())
                                                             .withOutput ("Sortie", juce::AudioChannelSet::stereo()))
    {
        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Volume", 0.0f, 1.0f, 0.7f));
        addParameter (crash = new juce::AudioParameterBool (juce::ParameterID { "crash", 1 }, "Faire planter", false));
    }

    const juce::String getName() const override { return ONDE_TEST_SYNTH ? "Onde Test Synth" : "Onde Crash Test"; }
    void prepareToPlay (double sr, int) override { sampleRate = sr; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiIn) override
    {
        if (crash->get())
            crashNow();
        if (! ONDE_TEST_SYNTH && buffer.getNumSamples() > 0 && buffer.getSample (0, 0) == 12345.0f)
            crashNow();                                   // valeur audio « magique » = plantage (tests automatiques)
        for (const auto m : midiIn)                       // note MIDI 0 = plantage
            if (m.getMessage().isNoteOn() && m.getMessage().getNoteNumber() == 0)
                crashNow();

        if (ONDE_TEST_SYNTH)
        {
            buffer.clear();
            int pos = 0;
            for (const auto m : midiIn)
            {
                render (buffer, pos, m.samplePosition);
                pos = m.samplePosition;
                const auto msg = m.getMessage();
                if (msg.isNoteOn())       { freq = juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber()); note = msg.getNoteNumber(); level = msg.getFloatVelocity(); }
                else if (msg.isNoteOff() && msg.getNoteNumber() == note) level = 0.0f;
            }
            render (buffer, pos, buffer.getNumSamples());
        }
        else
        {
            buffer.applyGain (gain->get());
        }
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& d) override
    {
        juce::MemoryOutputStream out (d, false);
        out.writeFloat (gain->get());
    }
    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        if (size >= 4) *gain = in.readFloat();
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterBool* crash = nullptr;

private:
    void render (juce::AudioBuffer<float>& b, int from, int to)
    {
        for (int i = from; i < to; ++i)
        {
            smooth += 0.002f * (level - smooth);
            phase += freq / sampleRate; if (phase >= 1.0) phase -= 1.0;
            const float v = (float) (2.0 * phase - 1.0) * smooth * gain->get() * 0.25f;
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, v);
        }
    }
    double sampleRate = 48000.0, phase = 0.0, freq = 220.0;
    float level = 0.0f, smooth = 0.0f;
    int note = -1;
};

class TestEditor  : public juce::AudioProcessorEditor
{
public:
    explicit TestEditor (TestProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        addAndMakeVisible (title);
        title.setText (p.getName(), juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (0xffecedef));

        addAndMakeVisible (volume);
        volume.setRange (0.0, 1.0);
        volume.setValue (p.gain->get(), juce::dontSendNotification);
        volume.onValueChange = [this] { *proc.gain = (float) volume.getValue(); };

        addAndMakeVisible (crashButton);
        crashButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xfff2555a));
        crashButton.onClick = [this] { *proc.crash = true; };   // le plugin plantera au prochain bloc audio

        addAndMakeVisible (hint);
        hint.setText (juce::String::fromUTF8 ("Ce bouton provoque un vrai plantage du plugin. Dans Onde, seule sa piste doit s'arrêter."),
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, juce::Colour (0xffa1a6ae));
        setSize (420, 220);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1c1e22)); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        title.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        volume.setBounds (r.removeFromTop (32));
        r.removeFromTop (12);
        crashButton.setBounds (r.removeFromTop (40).removeFromLeft (180));
        r.removeFromTop (8);
        hint.setBounds (r);
    }
private:
    TestProcessor& proc;
    juce::Label title, hint;
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton crashButton { "Faire planter" };
};

juce::AudioProcessorEditor* TestProcessor::createEditor() { return new TestEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestProcessor(); }
