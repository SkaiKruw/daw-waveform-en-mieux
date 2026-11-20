// Fenêtres de réglage des effets Onde : bouton rotatif lié à un paramètre automatisable, égaliseur avec
// analyseur et points de bande, compresseur avec courbe de transfert, limiteur avec réduction de gain.
#include "OndeEffects.h"
#include "Theme.h"

namespace ondefx
{
    namespace
    {
        const juce::Colour bandColours[EqPlugin::numBands] = {
            juce::Colour (0xff8b93a1), juce::Colour (0xffe5a04e), juce::Colour (0xffe8697a), juce::Colour (0xffb98cff),
            juce::Colour (0xff3fa4ff), juce::Colour (0xff2de2c6), juce::Colour (0xff8b93a1) };

        //==========================================================================
        // Bouton rotatif lié à un paramètre (automatisation, annulation, double-clic = valeur par défaut).
        class Knob  : public juce::Component, private juce::Timer
        {
        public:
            Knob (te::AutomatableParameter* p, juce::String label, juce::Colour c = theme::accent) : param (p), title (std::move (label)), colour (c)
            {
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
                slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff30343c));
                slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
                if (param != nullptr)
                {
                    slider.setNormalisableRange (juce::NormalisableRange<double> (param->valueRange.start, param->valueRange.end, param->valueRange.interval, param->valueRange.skew));
                    slider.setDoubleClickReturnValue (true, param->getDefaultValue().value_or (param->valueRange.start));
                    slider.setValue (param->getCurrentValue(), juce::dontSendNotification);
                    slider.onDragStart = [this] { param->getEdit().getUndoManager().beginNewTransaction (title); param->parameterChangeGestureBegin(); };
                    slider.onDragEnd = [this] { param->parameterChangeGestureEnd(); };
                    slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), juce::sendNotification); repaint(); };
                }
                addAndMakeVisible (slider);
                startTimerHz (15);
            }
            void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (16).withTrimmedBottom (18)); }
            void paint (juce::Graphics& g) override
            {
                g.setColour (theme::muted); g.setFont (theme::ui (11.0f));
                g.drawText (title, getLocalBounds().removeFromTop (16), juce::Justification::centred, true);
                g.setColour (theme::text); g.setFont (theme::mono (11.0f));
                g.drawText (param ? param->getCurrentValueAsString() : juce::String(), getLocalBounds().removeFromBottom (18), juce::Justification::centred, true);
            }
        private:
            void timerCallback() override
            {
                if (param == nullptr || slider.isMouseButtonDown()) return;
                const double v = param->getCurrentValue();
                if (std::abs (v - slider.getValue()) > 1.0e-6) { slider.setValue (v, juce::dontSendNotification); repaint(); }
            }
            te::AutomatableParameter::Ptr param;
            juce::String title;
            juce::Colour colour;
            juce::Slider slider;
        };

        void drawPanelBackground (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272e), 0, (float) r.getY(), juce::Colour (0xff17191d), 0, (float) r.getBottom(), false));
            g.fillRect (r);
        }

        void drawTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name, const juce::String& subtitle)
        {
            theme::drawLogo (g, r.removeFromLeft (r.getHeight()).toFloat().reduced (4.0f));
            r.removeFromLeft (8);
            g.setColour (theme::text); g.setFont (theme::ui (16.0f, true));
            g.drawText (name, r.removeFromLeft (150), juce::Justification::centredLeft);
            g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
            g.drawText (subtitle, r, juce::Justification::centredLeft, true);
        }

        //==========================================================================
        class EqEditor  : public juce::Component, private juce::Timer
        {
        public:
            explicit EqEditor (EqPlugin& p) : eq (p)
            {
                spectrum.assign (fftSize / 2, -100.0f);
                buildKnobs();
                setSize (860, 470);
                startTimerHz (30);
            }

            void paint (juce::Graphics& g) override
            {
                drawPanelBackground (g, getLocalBounds());
                drawTitle (g, getLocalBounds().removeFromTop (40).reduced (12, 6), "Onde EQ",
                           juce::String::fromUTF8 ("Glisser un point : fréquence et gain · molette : largeur (Q) · double-clic : activer/désactiver"));
                auto gr = graph().toFloat();
                g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (gr, 6.0f);
                g.saveState();
                g.reduceClipRegion (gr.toNearestInt());
                // Grille : décades et dB.
                g.setFont (theme::ui (10.0f));
                for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
                {
                    const float x = freqToX (f);
                    g.setColour (juce::Colour (0xff22262c)); g.drawVerticalLine ((int) x, gr.getY(), gr.getBottom());
                    g.setColour (theme::dim);
                    g.drawText (f >= 1000 ? juce::String (f / 1000.0, 0) + "k" : juce::String ((int) f), (int) x + 3, (int) gr.getBottom() - 15, 34, 12, juce::Justification::left);
                }
                for (int d = -24; d <= 24; d += 6)
                {
                    const float y = dbToY ((float) d);
                    g.setColour (d == 0 ? juce::Colour (0xff3a3f48) : juce::Colour (0xff1e2227)); g.drawHorizontalLine ((int) y, gr.getX(), gr.getRight());
                    if (d != 0) { g.setColour (theme::dim); g.drawText ((d > 0 ? "+" : "") + juce::String (d), (int) gr.getRight() - 30, (int) y - 7, 26, 12, juce::Justification::right); }
                }
                // Analyseur (après l'égaliseur).
                juce::Path spec;
                bool started = false;
                const double binHz = eq.getRate() / fftSize;
                for (int i = 1; i < fftSize / 2; ++i)
                {
                    const double f = i * binHz;
                    if (f < 20.0 || f > 20000.0) continue;
                    const float x = freqToX (f), y = juce::jlimit (gr.getY(), gr.getBottom(), specToY (spectrum[(size_t) i]));
                    if (! started) { spec.startNewSubPath (x, gr.getBottom()); spec.lineTo (x, y); started = true; } else spec.lineTo (x, y);
                }
                if (started)
                {
                    spec.lineTo (gr.getRight(), gr.getBottom()); spec.closeSubPath();
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0x553fa4ff), 0, gr.getY(), juce::Colour (0x103fa4ff), 0, gr.getBottom(), false));
                    g.fillPath (spec);
                }
                // Courbe de réponse.
                juce::Path curve;
                for (int x = (int) gr.getX(); x <= (int) gr.getRight(); x += 2)
                {
                    const float y = dbToY ((float) eq.responseDb (xToFreq ((float) x)));
                    if (x == (int) gr.getX()) curve.startNewSubPath ((float) x, y); else curve.lineTo ((float) x, y);
                }
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
                // Points des bandes.
                for (int b = 0; b < EqPlugin::numBands; ++b)
                {
                    const auto c = nodePos (b);
                    const bool on = eq.value (EqPlugin::bandId (b, "on")) > 0.5f;
                    const float r = b == selected ? 9.0f : 7.0f;
                    g.setColour (on ? bandColours[b] : juce::Colour (0xff4a4f58));
                    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
                    g.setColour (b == selected ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
                    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, b == selected ? 2.0f : 1.0f);
                    g.setColour (juce::Colours::black); g.setFont (theme::ui (9.5f, true));
                    g.drawText (juce::String (b + 1), juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r), juce::Justification::centred);
                }
                g.restoreState();
                // Titre de la bande sélectionnée.
                const char* names[] = { "Coupe-bas (24 dB/oct)", "Étagère grave", "Cloche", "Cloche", "Cloche", "Étagère aiguë", "Coupe-haut (24 dB/oct)" };
                g.setColour (bandColours[selected]); g.setFont (theme::ui (13.0f, true));
                g.drawText (U8 ("Bande ") + juce::String (selected + 1) + U8 (" · ") + juce::String::fromUTF8 (names[selected]),
                            12, graph().getBottom() + 8, 300, 20, juce::Justification::centredLeft);
            }

            void resized() override { layoutKnobs(); }

            void mouseDown (const juce::MouseEvent& e) override
            {
                dragging = -1;
                for (int b = EqPlugin::numBands; --b >= 0;)
                    if (nodePos (b).getDistanceFrom (e.position) < 12.0f) { dragging = b; break; }
                if (dragging >= 0)
                {
                    if (dragging != selected) { selected = dragging; buildKnobs(); }
                    eq.getUndoManager()->beginNewTransaction (U8 ("Bande d'égaliseur"));
                    if (auto* on = eq.param (EqPlugin::bandId (dragging, "on")); on && on->getCurrentValue() < 0.5f) on->setParameter (1.0f, juce::sendNotification);
                }
                repaint();
            }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragging < 0) return;
                auto gr = graph().toFloat();
                if (auto* f = eq.param (EqPlugin::bandId (dragging, "freq")))
                    f->setParameter ((float) juce::jlimit (20.0, 20000.0, xToFreq (juce::jlimit (gr.getX(), gr.getRight(), e.position.x))), juce::sendNotification);
                if (auto* gn = eq.param (EqPlugin::bandId (dragging, "gain")))
                    gn->setParameter (juce::jlimit (-24.0f, 24.0f, yToDb (juce::jlimit (gr.getY(), gr.getBottom(), e.position.y))), juce::sendNotification);
                repaint();
            }
            void mouseUp (const juce::MouseEvent&) override { dragging = -1; }
            void mouseDoubleClick (const juce::MouseEvent& e) override
            {
                for (int b = 0; b < EqPlugin::numBands; ++b)
                    if (nodePos (b).getDistanceFrom (e.position) < 12.0f)
                        if (auto* on = eq.param (EqPlugin::bandId (b, "on")))
                        {
                            eq.getUndoManager()->beginNewTransaction (U8 ("Bande d'égaliseur"));
                            on->setParameter (on->getCurrentValue() > 0.5f ? 0.0f : 1.0f, juce::sendNotification);
                        }
                repaint();
            }
            void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
            {
                for (int b = 0; b < EqPlugin::numBands; ++b)
                    if (nodePos (b).getDistanceFrom (e.position) < 16.0f)
                        if (auto* q = eq.param (EqPlugin::bandId (b, "q")))
                            q->setParameter (juce::jlimit (0.1f, 10.0f, q->getCurrentValue() * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f)), juce::sendNotification);
                repaint();
            }

            int selectedBand() const { return selected; }

        private:
            static juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }
            juce::Rectangle<int> graph() const { return getLocalBounds().withTrimmedTop (44).withTrimmedBottom (120).reduced (12, 0); }
            float freqToX (double f) const { auto gr = graph().toFloat(); return gr.getX() + gr.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); }
            double xToFreq (float x) const { auto gr = graph().toFloat(); return 20.0 * std::pow (1000.0, (double) ((x - gr.getX()) / gr.getWidth())); }
            float dbToY (float d) const    { auto gr = graph().toFloat(); return gr.getCentreY() - d / 24.0f * (gr.getHeight() * 0.5f - 8.0f); }
            float yToDb (float y) const    { auto gr = graph().toFloat(); return (gr.getCentreY() - y) / (gr.getHeight() * 0.5f - 8.0f) * 24.0f; }
            float specToY (float d) const  { auto gr = graph().toFloat(); return gr.getBottom() - (d + 90.0f) / 90.0f * gr.getHeight(); }
            juce::Point<float> nodePos (int b) const
            {
                const bool cut = b == 0 || b == EqPlugin::numBands - 1;
                return { freqToX (eq.value (EqPlugin::bandId (b, "freq"))), cut ? dbToY (0.0f) : dbToY (eq.value (EqPlugin::bandId (b, "gain"))) };
            }

            void buildKnobs()
            {
                knobs.clear();
                const auto c = bandColours[selected];
                knobs.add (new Knob (eq.param (EqPlugin::bandId (selected, "freq")), U8 ("Fréquence"), c));
                if (auto* p = eq.param (EqPlugin::bandId (selected, "gain"))) knobs.add (new Knob (p, "Gain", c));
                if (auto* p = eq.param (EqPlugin::bandId (selected, "q")))    knobs.add (new Knob (p, "Q", c));
                knobs.add (new Knob (eq.param ("out"), "Sortie"));
                for (auto* k : knobs) addAndMakeVisible (k);
                layoutKnobs();
            }
            void layoutKnobs()
            {
                auto r = getLocalBounds().removeFromBottom (92).reduced (12, 6);
                r.removeFromLeft (300);
                for (auto* k : knobs) { k->setBounds (r.removeFromLeft (100)); r.removeFromLeft (8); }
                if (! knobs.isEmpty()) knobs.getLast()->setBounds (getLocalBounds().removeFromBottom (92).reduced (12, 6).removeFromRight (100));
            }

            void timerCallback() override
            {
                // Analyseur : FFT de 4096 points (fenêtre de Hann), lissage, -90 à 0 dB.
                const int got = eq.readAnalyser (scratch.data(), (int) scratch.size());
                for (int i = 0; i < got; ++i) { history[(size_t) histPos] = scratch[(size_t) i]; histPos = (histPos + 1) % fftSize; }
                if (got > 0)
                {
                    std::fill (fftData.begin(), fftData.end(), 0.0f);
                    for (int i = 0; i < fftSize; ++i)
                    {
                        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
                        fftData[(size_t) i] = history[(size_t) ((histPos + i) % fftSize)] * w;
                    }
                    fft.performFrequencyOnlyForwardTransform (fftData.data());
                    for (int i = 0; i < fftSize / 2; ++i)
                    {
                        const float mag = juce::Decibels::gainToDecibels (fftData[(size_t) i] * 4.0f / (float) fftSize, -100.0f);
                        // +4,5 dB/oct de pente d'affichage, comme les analyseurs de mixage (bruit rose ≈ plat).
                        const float tilt = 4.5f * (float) std::log2 (juce::jmax (1.0, i * eq.getRate() / fftSize) / 1000.0);
                        auto& s = spectrum[(size_t) i];
                        s = std::max (mag <= -95.0f ? -100.0f : mag + tilt, s - 1.5f);   // silence : pas de pente
                    }
                }
                else for (auto& s : spectrum) s -= 1.5f;
                repaint (graph());
            }

            EqPlugin& eq;
            te::Plugin::Ptr keepAlive { &eq };
            juce::OwnedArray<Knob> knobs;
            int selected = 3, dragging = -1;
            static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
            juce::dsp::FFT fft { fftOrder };
            std::vector<float> fftData = std::vector<float> (2 * fftSize), history = std::vector<float> (fftSize), scratch = std::vector<float> (8192), spectrum;
            int histPos = 0;
        };

        //==========================================================================
        class CompEditor  : public juce::Component, private juce::Timer
        {
        public:
            explicit CompEditor (CompPlugin& p) : comp (p)
            {
                const char* ids[] = { "threshold", "ratio", "attack", "release", "knee", "makeup", "mix", "scHpf" };
                const char* labels[] = { "Seuil", "Taux", "Attaque", "Relâchement", "Genou", "Gain", "Mélange", "Filtre détection" };
                for (int i = 0; i < 8; ++i) addAndMakeVisible (knobs.add (new Knob (comp.param (ids[i]), juce::String::fromUTF8 (labels[i]))));
                setSize (720, 360);
                startTimerHz (30);
            }
            void paint (juce::Graphics& g) override
            {
                drawPanelBackground (g, getLocalBounds());
                drawTitle (g, getLocalBounds().removeFromTop (40).reduced (12, 6), "Onde Comp", juce::String::fromUTF8 ("Compresseur · genou progressif, détection filtrable, mélange parallèle"));
                // Courbe de transfert.
                auto r = curveArea().toFloat();
                g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (r, 6.0f);
                auto toX = [&] (float d) { return r.getX() + (d + 60.0f) / 60.0f * r.getWidth(); };
                auto toY = [&] (float d) { return r.getBottom() - (d + 60.0f) / 60.0f * r.getHeight(); };
                g.setColour (juce::Colour (0xff23272d));
                for (int d = -48; d < 0; d += 12) { g.drawVerticalLine ((int) toX ((float) d), r.getY(), r.getBottom()); g.drawHorizontalLine ((int) toY ((float) d), r.getX(), r.getRight()); }
                g.setColour (juce::Colour (0xff3a3f48)); g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);
                juce::Path p;
                for (int i = 0; i <= 120; ++i)
                {
                    const float in = -60.0f + i * 0.5f;
                    const float y = toY (juce::jlimit (-60.0f, 0.0f, comp.transferDb (in)));
                    if (i == 0) p.startNewSubPath (toX (in), y); else p.lineTo (toX (in), y);
                }
                g.setColour (theme::accent); g.strokePath (p, juce::PathStrokeType (2.2f));
                const float inDb = juce::jlimit (-60.0f, 0.0f, comp.inputDb.load());
                g.setColour (juce::Colours::white); g.fillEllipse (toX (inDb) - 4, toY (juce::jlimit (-60.0f, 0.0f, comp.transferDb (inDb))) - 4, 8, 8);
                // Réduction de gain.
                auto m = meterArea().toFloat();
                g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (m, 4.0f);
                const float gr = juce::jlimit (0.0f, 24.0f, shownGr);
                g.setColour (juce::Colour (0xffe5a04e)); g.fillRect (m.withHeight (m.getHeight() * gr / 24.0f).reduced (3.0f, 2.0f));
                g.setColour (theme::text); g.setFont (theme::mono (12.0f, true));
                g.drawText (gr < 0.05f ? juce::String ("0.0") : "-" + juce::String (gr, 1), m.translated (0, m.getHeight() + 4).withHeight (16), juce::Justification::centred);
                g.setColour (theme::muted); g.setFont (theme::ui (10.5f));
                g.drawText ("GR dB", m.translated (0, -16).withHeight (14), juce::Justification::centred);
            }
            void resized() override
            {
                auto r = getLocalBounds().withTrimmedTop (48).reduced (12, 8);
                r.removeFromLeft (curveArea().getWidth() + 70);
                const int w = r.getWidth() / 4, h = r.getHeight() / 2;
                for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (r.getX() + (i % 4) * w, r.getY() + (i / 4) * h, w, h);
            }
        private:
            juce::Rectangle<int> curveArea() const { return { 12, 52, 250, 250 }; }
            juce::Rectangle<int> meterArea() const { return { 278, 70, 30, 220 }; }
            void timerCallback() override
            {
                const float gr = comp.gainReductionDb.load();
                shownGr = gr > shownGr ? gr : shownGr * 0.9f;
                repaint (curveArea().getUnion (meterArea()).expanded (4, 24));
            }
            CompPlugin& comp;
            te::Plugin::Ptr keepAlive { &comp };
            juce::OwnedArray<Knob> knobs;
            float shownGr = 0;
        };

        //==========================================================================
        class LimiterEditor  : public juce::Component, private juce::Timer
        {
        public:
            explicit LimiterEditor (LimiterPlugin& p) : lim (p)
            {
                addAndMakeVisible (knobs.add (new Knob (lim.param ("input"), juce::String::fromUTF8 ("Gain d'entrée"))));
                addAndMakeVisible (knobs.add (new Knob (lim.param ("ceiling"), "Plafond", juce::Colour (0xffe8473f))));
                addAndMakeVisible (knobs.add (new Knob (lim.param ("release"), juce::String::fromUTF8 ("Relâchement"))));
                setSize (560, 300);
                startTimerHz (30);
            }
            void paint (juce::Graphics& g) override
            {
                drawPanelBackground (g, getLocalBounds());
                drawTitle (g, getLocalBounds().removeFromTop (40).reduced (12, 6), "Onde Limiteur",
                           juce::String::fromUTF8 ("Anticipation 1,5 ms (compensée) · plafond garanti"));
                auto bar = [&] (juce::Rectangle<float> r, float v, float range, juce::Colour c, const juce::String& label, const juce::String& text)
                {
                    g.setColour (juce::Colour (0xff111316)); g.fillRoundedRectangle (r, 4.0f);
                    g.setColour (c); g.fillRect (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, v / range)).reduced (2.0f, 3.0f));
                    g.setColour (theme::muted); g.setFont (theme::ui (11.0f));
                    g.drawText (label, r.translated (0, -16).withHeight (14), juce::Justification::left);
                    g.setColour (theme::text); g.setFont (theme::mono (12.0f, true));
                    g.drawText (text, r.translated (0, -16).withHeight (14), juce::Justification::right);
                };
                bar ({ 24.0f, 82.0f, 512.0f, 18.0f }, shownGr, 12.0f, juce::Colour (0xffe5a04e), U8 ("Réduction de gain"), (shownGr < 0.05f ? juce::String ("0.0") : "-" + juce::String (shownGr, 1)) + " dB");
                bar ({ 24.0f, 132.0f, 512.0f, 18.0f }, shownOut + 48.0f, 48.0f, theme::play, U8 ("Sortie (crête)"), juce::String (shownOut, 1) + " dBFS");
            }
            void resized() override
            {
                auto r = getLocalBounds().removeFromBottom (120).reduced (60, 8);
                const int w = r.getWidth() / 3;
                for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (r.getX() + i * w, r.getY(), w, r.getHeight());
            }
        private:
            static juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }
            void timerCallback() override
            {
                const float gr = lim.gainReductionDb.load(), out = juce::jmax (-48.0f, lim.outputDb.load());
                shownGr = gr > shownGr ? gr : shownGr * 0.9f;
                shownOut = out > shownOut ? out : shownOut - 0.8f;
                repaint (0, 60, getWidth(), 100);
            }
            LimiterPlugin& lim;
            te::Plugin::Ptr keepAlive { &lim };
            juce::OwnedArray<Knob> knobs;
            float shownGr = 0, shownOut = -48;
        };
    }

    std::unique_ptr<juce::Component> createEditor (te::Plugin& p)
    {
        if (auto* eq = dynamic_cast<EqPlugin*> (&p))          return std::make_unique<EqEditor> (*eq);
        if (auto* c = dynamic_cast<CompPlugin*> (&p))         return std::make_unique<CompEditor> (*c);
        if (auto* l = dynamic_cast<LimiterPlugin*> (&p))      return std::make_unique<LimiterEditor> (*l);
        return {};
    }
}
