// Fenêtre d'Onde Synth : navigateur de sons (recherche, familles, liste), affichage propre à chaque moteur,
// réglages rangés par sections (avec courbes d'enveloppe) et clavier pour essayer le son sans quitter la fenêtre.
#include "OndeSynth.h"
#include "ParamControls.h"
#include "Chains.h"
#include "Theme.h"

namespace ondefx
{
    namespace
    {
        juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }
        const juce::Colour engineColours[] = { theme::accent, juce::Colour (0xffe5a04e), juce::Colour (0xff2de2c6), juce::Colour (0xffe8697a), juce::Colour (0xffc58cff) };
        const juce::Colour familyColours[] = { juce::Colour (0xff3fa4ff), juce::Colour (0xffb98cff), juce::Colour (0xffe5a04e), juce::Colour (0xff2de2c6),
                                               juce::Colour (0xffe8697a), juce::Colour (0xff9be35b), juce::Colour (0xfff2d34a), juce::Colour (0xffe8473f),
                                               juce::Colour (0xff5ad1e6), juce::Colour (0xffff8f5a) };
        const juce::Colour cardBg { 0xff1b1e23 }, cardLine { 0xff2b2f36 };

        juce::Colour familyColour (const juce::String& family)
        {
            const int i = SynthPlugin::categories().indexOf (family);
            return familyColours[juce::jmax (0, i) % (int) std::size (familyColours)];
        }

        //==============================================================================
        // Navigateur de sons : recherche, familles, liste. Un clic pose le son (annulable).
        class SoundBrowser  : public juce::Component, private juce::ListBoxModel
        {
        public:
            std::function<void (const SynthPreset&)> onChoose;
            std::function<juce::String()> currentName;

            SoundBrowser()
            {
                search.setTextToShowWhenEmpty (U8 ("Rechercher un son…"), theme::dim);
                search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111316));
                search.setColour (juce::TextEditor::outlineColourId, cardLine);
                search.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
                search.setFont (theme::ui (13.0f));
                search.setIndents (26, 6);
                search.onTextChange = [this] { refilter(); };
                search.onEscapeKey = [this] { search.clear(); refilter(); };
                addAndMakeVisible (search);
                families.add (U8 ("Tous les sons"));
                families.addArray (SynthPlugin::categories());
                list.setModel (this);
                list.setRowHeight (30);
                list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
                list.setOutlineThickness (0);
                addAndMakeVisible (list);
                refilter();
            }
            ~SoundBrowser() override { list.setModel (nullptr); }

            void paint (juce::Graphics& g) override
            {
                g.setColour (juce::Colour (0xff15171b)); g.fillRect (getLocalBounds());
                g.setColour (theme::line); g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
                // Loupe dans le champ de recherche.
                auto s = search.getBounds().toFloat().withWidth (24.0f).reduced (6.0f, 8.0f);
                g.setColour (theme::dim);
                g.drawEllipse (s.withSize (s.getHeight() * 0.75f, s.getHeight() * 0.75f), 1.4f);
                g.drawLine (s.getX() + s.getHeight() * 0.65f, s.getY() + s.getHeight() * 0.65f, s.getX() + s.getHeight(), s.getBottom(), 1.6f);
                // Familles : pastilles en deux colonnes.
                g.setFont (theme::ui (12.0f));
                for (int i = 0; i < families.size(); ++i)
                {
                    auto r = familyBounds (i).toFloat();
                    const bool on = i == family, over = i == hoverFamily;
                    const auto c = i == 0 ? theme::accent : familyColour (families[i]);
                    g.setColour (on ? c.withAlpha (0.22f) : over ? juce::Colour (0xff23272d) : juce::Colour (0xff1b1e23));
                    g.fillRoundedRectangle (r, 5.0f);
                    if (on) { g.setColour (c.withAlpha (0.7f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f); }
                    g.setColour (c); g.fillEllipse (r.getX() + 7, r.getCentreY() - 3, 6, 6);
                    g.setColour (on ? theme::text : theme::muted);
                    g.drawFittedText (families[i], r.toNearestInt().withTrimmedLeft (18).withTrimmedRight (4), juce::Justification::centredLeft, 1, 0.8f);
                }
                g.setColour (theme::dim); g.setFont (theme::ui (10.5f, true));
                g.drawText (juce::String (shown.size()) + (shown.size() > 1 ? " SONS" : " SON"), list.getX() + 10, list.getY() - 18, 200, 16, juce::Justification::centredLeft);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (10);
                search.setBounds (r.removeFromTop (30));
                familiesTop = r.getY() + 10;
                const int rows = (families.size() + 1) / 2;
                r.removeFromTop (10 + rows * 28 + 22);
                list.setBounds (r);
            }

            void mouseMove (const juce::MouseEvent& e) override  { const int f = familyAt (e.getPosition()); if (f != hoverFamily) { hoverFamily = f; repaint(); } }
            void mouseExit (const juce::MouseEvent&) override    { hoverFamily = -1; repaint(); }
            void mouseDown (const juce::MouseEvent& e) override  { const int f = familyAt (e.getPosition()); if (f >= 0) { family = f; refilter(); } }

            void step (int delta)
            {
                if (shown.isEmpty()) return;
                int i = indexOfCurrent();
                i = i < 0 ? 0 : (i + delta + shown.size()) % shown.size();
                if (onChoose) onChoose (*shown[i]);
                list.selectRow (i); list.scrollToEnsureRowIsOnscreen (i);
            }
            void syncSelection()
            {
                const int i = indexOfCurrent();
                if (i != list.getSelectedRow()) { if (i >= 0) { list.selectRow (i); list.scrollToEnsureRowIsOnscreen (i); } else list.deselectAllRows(); }
                list.repaint();
            }
            int numShown() const { return shown.size(); }
            void setFamily (int f) { family = juce::jlimit (0, families.size() - 1, f); refilter(); }

        private:
            juce::Rectangle<int> familyBounds (int i) const
            {
                const int w = (getWidth() - 20 - 6) / 2;
                return { 10 + (i % 2) * (w + 6), familiesTop + (i / 2) * 28, w, 24 };
            }
            int familyAt (juce::Point<int> p) const { for (int i = 0; i < families.size(); ++i) if (familyBounds (i).contains (p)) return i; return -1; }
            int indexOfCurrent() const
            {
                const auto cur = currentName ? currentName() : juce::String();
                for (int i = 0; i < shown.size(); ++i) if (U8 (shown[i]->name) == cur) return i;
                return -1;
            }
            void refilter()
            {
                shown.clear();
                const auto q = search.getText().trim();
                for (auto& p : SynthPlugin::presets())
                {
                    const auto cat = U8 (p.category), name = U8 (p.name);
                    if (family > 0 && cat != families[family]) continue;
                    if (q.isNotEmpty() && ! name.containsIgnoreCase (q) && ! cat.containsIgnoreCase (q)
                        && ! U8 (SynthPlugin::engineNames[SynthPlugin::engineOf (p)]).containsIgnoreCase (q)) continue;
                    shown.add (&p);
                }
                list.updateContent();
                syncSelection();
                repaint();
            }
            int getNumRows() override { return shown.size(); }
            void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
            {
                if (! juce::isPositiveAndBelow (row, shown.size())) return;
                const auto& p = *shown[row];
                const auto c = familyColour (U8 (p.category));
                auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (4.0f, 2.0f);
                if (selected) { g.setColour (c.withAlpha (0.2f)); g.fillRoundedRectangle (r, 5.0f); g.setColour (c); g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f); }
                g.setColour (selected ? theme::text : juce::Colour (0xffc3c8cf)); g.setFont (theme::ui (13.0f, selected));
                g.drawFittedText (U8 (p.name), r.toNearestInt().withTrimmedLeft (12).withTrimmedRight (70), juce::Justification::centredLeft, 1, 0.8f);
                const int e = SynthPlugin::engineOf (p);
                g.setColour (engineColours[e].withAlpha (selected ? 0.95f : 0.6f)); g.setFont (theme::ui (10.0f));
                g.drawText (U8 (SynthPlugin::engineNames[e]), r.toNearestInt().withTrimmedRight (8), juce::Justification::centredRight);
            }
            void listBoxItemClicked (int row, const juce::MouseEvent&) override
            {
                if (juce::isPositiveAndBelow (row, shown.size()) && onChoose) onChoose (*shown[row]);
            }
            void returnKeyPressed (int row) override { if (juce::isPositiveAndBelow (row, shown.size()) && onChoose) onChoose (*shown[row]); }

            juce::TextEditor search;
            juce::ListBox list;
            juce::StringArray families;
            juce::Array<const SynthPreset*> shown;
            int family = 0, hoverFamily = -1, familiesTop = 0;
        };

        //==============================================================================
        // Section de réglages : titre, courbe d'enveloppe éventuelle, boutons.
        class Section  : public juce::Component
        {
        public:
            Section (SynthPlugin& s, juce::String t, juce::Colour c, std::vector<const char*> ids, std::array<const char*, 4> env = {})
                : synth (s), title (std::move (t)), colour (c), envIds (env)
            {
                for (auto* id : ids)
                {
                    auto* p = synth.param (id);
                    if (p == nullptr) continue;
                    const juce::String sid (id);
                    const bool choice = sid.startsWith ("wave") || sid == "kit" || sid == "body" || sid == "double";
                    juce::Component* comp = choice ? (juce::Component*) new ondeui::ParamChoice (p, p->getParameterName())
                                                   : (juce::Component*) new ondeui::Knob (p, p->getParameterName(), colour);
                    controls.add (comp); isChoice.add (choice);
                    addAndMakeVisible (comp);
                }
            }
            int preferredWidth() const
            {
                int w = 16 + (envIds[0] != nullptr ? 170 : 0);
                for (bool c : isChoice) w += c ? 150 : 94;
                return w;
            }
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (3.0f);
                g.setColour (cardBg); g.fillRoundedRectangle (r, 8.0f);
                g.setColour (cardLine); g.drawRoundedRectangle (r, 8.0f, 1.0f);
                g.setColour (colour); g.fillRoundedRectangle (r.getX() + 12, r.getY() + 9, 3, 12, 1.5f);
                g.setColour (theme::text.withAlpha (0.9f)); g.setFont (theme::ui (11.5f, true));
                g.drawText (title.toUpperCase(), (int) r.getX() + 21, (int) r.getY() + 5, (int) r.getWidth() - 30, 20, juce::Justification::centredLeft);
                if (envIds[0] != nullptr) paintEnvelope (g, envArea.toFloat());
            }
            void resized() override
            {
                auto r = getLocalBounds().reduced (10, 6);
                r.removeFromTop (22);
                if (envIds[0] != nullptr) { envArea = r.removeFromLeft (160).reduced (2, 6); r.removeFromLeft (8); }
                // Largeur répartie : un menu compte pour 1,6 bouton, plafonnée pour ne pas étirer les boutons.
                float units = 0; for (bool c : isChoice) units += c ? 1.6f : 1.0f;
                const float unit = juce::jmin (110.0f, r.getWidth() / juce::jmax (1.0f, units));
                for (int i = 0; i < controls.size(); ++i)
                {
                    auto cell = r.removeFromLeft (juce::roundToInt (unit * (isChoice[i] ? 1.6f : 1.0f)));
                    controls[i]->setBounds (isChoice[i] ? cell.withSizeKeepingCentre (cell.getWidth() - 8, 46) : cell.reduced (2, 0));
                }
            }
            juce::String envSignature() const
            {
                if (envIds[0] == nullptr) return {};
                juce::String s; for (auto* id : envIds) s << synth.value (id) << ";"; return s;
            }
        private:
            void paintEnvelope (juce::Graphics& g, juce::Rectangle<float> a)
            {
                g.setColour (juce::Colour (0xff121417)); g.fillRoundedRectangle (a, 5.0f);
                const float at = synth.value (envIds[0]), dt = synth.value (envIds[1]), sl = synth.value (envIds[2]), rt = synth.value (envIds[3]);
                // Largeurs à l'échelle de la racine du temps : les temps courts restent visibles.
                const float wa = std::sqrt (at), wd = std::sqrt (dt), ws = 0.6f, wr = std::sqrt (rt), total = wa + wd + ws + wr;
                auto in = a.reduced (8.0f, 8.0f);
                auto X = [&] (float w) { return in.getX() + in.getWidth() * w / total; };
                auto Y = [&] (float lvl) { return in.getBottom() - in.getHeight() * lvl; };
                juce::Path p;
                p.startNewSubPath (in.getX(), in.getBottom());
                p.lineTo (X (wa), Y (1.0f));
                for (int k = 1; k <= 12; ++k) { const float t = k / 12.0f; p.lineTo (X (wa + wd * t), Y (sl + (1.0f - sl) * std::pow (0.001f, t))); }
                p.lineTo (X (wa + wd + ws), Y (sl));
                for (int k = 1; k <= 12; ++k) { const float t = k / 12.0f; p.lineTo (X (wa + wd + ws + wr * t), Y (sl * std::pow (0.001f, t))); }
                juce::Path fill (p); fill.lineTo (in.getRight(), in.getBottom()); fill.closeSubPath();
                g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.35f), 0, in.getY(), colour.withAlpha (0.02f), 0, in.getBottom(), false));
                g.fillPath (fill);
                g.setColour (colour); g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
                g.setColour (theme::dim); g.setFont (theme::ui (9.5f, true));
                const char* labels[] = { "A", "D", "S", "R" };
                const float xs[] = { X (wa * 0.5f), X (wa + wd * 0.5f), X (wa + wd + ws * 0.5f), X (wa + wd + ws + wr * 0.5f) };
                for (int k = 0; k < 4; ++k) g.drawText (labels[k], (int) xs[k] - 6, (int) a.getY() + 2, 12, 10, juce::Justification::centred);
            }
            SynthPlugin& synth;
            juce::String title;
            juce::Colour colour;
            std::array<const char*, 4> envIds;
            juce::OwnedArray<juce::Component> controls;
            juce::Array<bool> isChoice;
            juce::Rectangle<int> envArea;
        };

        //==============================================================================
        // Affichage du moteur : forme d'onde et filtre, spectre FM, partiels du piano, pads de batterie, corde pincée.
        class EngineDisplay  : public juce::Component
        {
        public:
            explicit EngineDisplay (SynthPlugin& s) : synth (s) {}
            int engine = 0;

            juce::String signature() const
            {
                juce::String sig; sig << engine << ";" << pressedPad << ";";
                for (auto* id : { "wave1", "wave2", "semi2", "mix2", "sub", "cutoff", "reso", "ratio", "index", "feedback", "bright", "hard",
                                  "kit", "body", "pickpos", "pick", "ring", "mute", "double", "dtune", "ddecay" })
                    sig << synth.value (id) << ";";
                return sig;
            }

            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (3.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff101317), 0, r.getY(), juce::Colour (0xff181c22), 0, r.getBottom(), false));
                g.fillRoundedRectangle (r, 8.0f);
                g.setColour (cardLine); g.drawRoundedRectangle (r, 8.0f, 1.0f);
                g.saveState(); g.reduceClipRegion (r.toNearestInt());
                auto in = r.reduced (14.0f, 12.0f);
                switch (engine)
                {
                    case 0:  paintAnalog (g, in); break;
                    case 1:  paintFm (g, in); break;
                    case 2:  paintPiano (g, in); break;
                    case 3:  paintDrums (g, in); break;
                    default: paintStrings (g, in); break;
                }
                g.restoreState();
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (engine != 3) return;
                const int p = padAt (e.position);
                if (p < 0) return;
                pressedPad = p;
                synth.auditionNote (pads()[(size_t) p].note, 0.65f + 0.35f * (1.0f - (e.position.y - padRect (p).getY()) / padRect (p).getHeight()));
                repaint();
            }
            void mouseUp (const juce::MouseEvent&) override
            {
                if (pressedPad >= 0) synth.auditionNote (pads()[(size_t) pressedPad].note, 0.0f);
                pressedPad = -1; repaint();
            }

        private:
            struct Pad { int note; const char* name; };
            static const std::array<Pad, 12>& pads()
            {
                static const std::array<Pad, 12> list { { { 49, "Crash" }, { 51, "Ride" }, { 46, "Charleston ouverte" }, { 42, "Charleston" },
                                                         { 48, "Tom aigu" }, { 45, "Tom médium" }, { 41, "Tom basse" }, { 56, "Cloche" },
                                                         { 36, "Grosse caisse" }, { 38, "Caisse claire" }, { 39, "Clap" }, { 37, "Rimshot" } } };
                return list;
            }
            juce::Rectangle<float> padRect (int i) const
            {
                auto in = getLocalBounds().toFloat().reduced (17.0f, 15.0f);
                const float w = (in.getWidth() - 3 * 10.0f) / 4.0f, h = (in.getHeight() - 2 * 10.0f) / 3.0f;
                return { in.getX() + (i % 4) * (w + 10.0f), in.getY() + (i / 4) * (h + 10.0f), w, h };
            }
            int padAt (juce::Point<float> p) const { for (int i = 0; i < 12; ++i) if (padRect (i).contains (p)) return i; return -1; }

            void label (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& t)
            {
                g.setColour (theme::dim); g.setFont (theme::ui (10.0f, true));
                g.drawText (t, a.toNearestInt().removeFromTop (14), juce::Justification::centredLeft);
            }

            static float rawWave (int w, double t)
            {
                switch (w)
                {
                    case 0:  return (float) std::sin (juce::MathConstants<double>::twoPi * t);
                    case 1:  return (float) (4.0 * std::abs (t - 0.5) - 1.0);
                    case 2:  return (float) (2.0 * t - 1.0);
                    case 3:  return t < 0.5 ? 1.0f : -1.0f;
                    default: return t < 0.25 ? 1.0f : -1.0f;
                }
            }

            void paintAnalog (juce::Graphics& g, juce::Rectangle<float> in)
            {
                const auto c = engineColours[0];
                auto left = in.removeFromLeft (in.getWidth() * 0.45f); in.removeFromLeft (16.0f);
                label (g, left, U8 ("FORME D'ONDE"));
                left.removeFromTop (16.0f);
                const int w1 = juce::roundToInt (synth.value ("wave1")), w2 = juce::roundToInt (synth.value ("wave2"));
                const float mix2 = synth.value ("mix2"), sub = synth.value ("sub"), ratio = std::pow (2.0f, synth.value ("semi2") / 12.0f);
                juce::Path p;
                for (int i = 0; i <= 400; ++i)
                {
                    const double t = i / 400.0 * 2.0;
                    float v = rawWave (w1, std::fmod (t, 1.0)) * (1.0f - mix2) + rawWave (w2, std::fmod (t * ratio, 1.0)) * mix2
                            + sub * 0.7f * (float) std::sin (juce::MathConstants<double>::pi * t);
                    v /= 1.0f + sub * 0.7f;
                    const float x = left.getX() + left.getWidth() * i / 400.0f, y = left.getCentreY() - v * left.getHeight() * 0.42f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (theme::lineBar); g.drawHorizontalLine ((int) left.getCentreY(), left.getX(), left.getRight());
                g.setColour (c.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (5.0f));
                g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.8f));

                label (g, in, U8 ("FILTRE PASSE-BAS 24 dB"));
                in.removeFromTop (16.0f);
                const float fc = synth.value ("cutoff"), q = 0.5f + synth.value ("reso") * 9.0f;
                auto xOf = [&] (float f) { return in.getX() + in.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
                g.setColour (theme::lineBar);
                for (float f : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine ((int) xOf (f), in.getY(), in.getBottom());
                juce::Path fp;
                for (int i = 0; i <= 200; ++i)
                {
                    const float f = 20.0f * std::pow (1000.0f, i / 200.0f), x = f / fc;
                    const float mag2 = 1.0f / ((1.0f - x * x) * (1.0f - x * x) + x * x / (q * q));
                    const float db = juce::jlimit (-48.0f, 24.0f, 10.0f * std::log10 (mag2) + 10.0f * std::log10 (1.0f / ((1.0f - x * x) * (1.0f - x * x) + 2.0f * x * x)));
                    const float y = in.getY() + in.getHeight() * (1.0f - (db + 48.0f) / 72.0f);
                    if (i == 0) fp.startNewSubPath (xOf (f), y); else fp.lineTo (xOf (f), y);
                }
                juce::Path fill (fp); fill.lineTo (in.getRight(), in.getBottom()); fill.lineTo (in.getX(), in.getBottom()); fill.closeSubPath();
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.3f), 0, in.getY(), c.withAlpha (0.0f), 0, in.getBottom(), false));
                g.fillPath (fill);
                g.setColour (c); g.strokePath (fp, juce::PathStrokeType (1.8f));
                g.setColour (theme::text); g.setFont (theme::mono (11.0f));
                g.drawText (fc >= 1000 ? juce::String (fc / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (fc)) + " Hz",
                            (int) xOf (fc) - 40, (int) in.getY(), 80, 14, juce::Justification::centred);
            }

            void paintBars (juce::Graphics& g, juce::Rectangle<float> a, const std::vector<float>& amps, juce::Colour c, const juce::String& title)
            {
                label (g, a, title);
                a.removeFromTop (16.0f);
                const int n = (int) amps.size();
                const float bw = a.getWidth() / n;
                for (int i = 0; i < n; ++i)
                {
                    const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (amps[(size_t) i], -60.0f));
                    const float h = a.getHeight() * (db + 60.0f) / 60.0f;
                    auto bar = juce::Rectangle<float> (a.getX() + i * bw + bw * 0.18f, a.getBottom() - h, bw * 0.64f, h);
                    g.setGradientFill (juce::ColourGradient (c, 0, bar.getY(), c.withAlpha (0.35f), 0, a.getBottom(), false));
                    g.fillRoundedRectangle (bar, 2.0f);
                }
            }

            void paintFm (juce::Graphics& g, juce::Rectangle<float> in)
            {
                const auto c = engineColours[1];
                const float ratio = synth.value ("ratio"), index = synth.value ("index"), fb = synth.value ("feedback");
                std::vector<float> wave (512);
                float last = 0;
                for (int i = 0; i < 512; ++i)
                {
                    const double t = i / 512.0;
                    const float mod = (float) std::sin (juce::MathConstants<double>::twoPi * ratio * t + fb * 2.0f * last); last = mod;
                    wave[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * t + index * mod);
                }
                auto left = in.removeFromLeft (in.getWidth() * 0.45f); in.removeFromLeft (16.0f);
                label (g, left, U8 ("FORME D'ONDE (ATTAQUE)"));
                left.removeFromTop (16.0f);
                juce::Path p;
                for (int i = 0; i < 1024; ++i)
                {
                    const float x = left.getX() + left.getWidth() * i / 1023.0f, y = left.getCentreY() - wave[(size_t) (i % 512)] * left.getHeight() * 0.42f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (c.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (5.0f));
                g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.6f));
                std::vector<float> amps (24);
                for (int h = 1; h <= 24; ++h)
                {
                    double re = 0, im = 0;
                    for (int i = 0; i < 512; ++i) { const double ph = juce::MathConstants<double>::twoPi * h * i / 512.0; re += wave[(size_t) i] * std::cos (ph); im += wave[(size_t) i] * std::sin (ph); }
                    amps[(size_t) h - 1] = (float) (2.0 * std::sqrt (re * re + im * im) / 512.0);
                }
                paintBars (g, in, amps, c, U8 ("HARMONIQUES"));
            }

            void paintPiano (juce::Graphics& g, juce::Rectangle<float> in)
            {
                const auto c = engineColours[2];
                const float bright = synth.value ("bright") * 0.8f, hard = synth.value ("hard");
                std::vector<float> amps (20);
                float maxA = 0;
                for (int n = 1; n <= 20; ++n)
                {
                    const float comb = std::abs (std::sin (juce::MathConstants<float>::pi * n * 0.118f)) + 0.12f;
                    amps[(size_t) n - 1] = std::pow ((float) n, -(1.9f - 1.3f * bright)) * comb * (1.0f + hard * 0.3f * n / 20.0f);
                    maxA = std::max (maxA, amps[(size_t) n - 1]);
                }
                for (auto& a : amps) a /= maxA;
                auto left = in.removeFromLeft (in.getWidth() * 0.55f); in.removeFromLeft (16.0f);
                paintBars (g, left, amps, c, U8 ("PARTIELS DE LA CORDE (DO 3)"));
                // Décroissance en deux temps : attaque brillante puis son prolongé.
                label (g, in, U8 ("DÉCROISSANCE"));
                in.removeFromTop (16.0f);
                for (int k = 0; k < 3; ++k)
                {
                    const float T = 10.0f * std::pow (0.5f, (36 + k * 24 - 36) / 16.0f);
                    juce::Path p;
                    for (int i = 0; i <= 100; ++i)
                    {
                        const float t = i / 100.0f * 6.0f, v = 0.65f * std::exp (-6.9f * t / (T * 0.18f)) + 0.35f * std::exp (-6.9f * t / T);
                        const float db = juce::jmax (-60.0f, juce::Decibels::gainToDecibels (v));
                        const float x = in.getX() + in.getWidth() * i / 100.0f, y = in.getY() + in.getHeight() * (-db / 60.0f);
                        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    }
                    g.setColour (c.withAlpha (1.0f - k * 0.3f)); g.strokePath (p, juce::PathStrokeType (1.6f));
                    g.setFont (theme::ui (10.0f));
                    g.drawText (k == 0 ? "grave" : k == 1 ? "medium" : "aigu", (int) in.getRight() - 50, (int) in.getY() + 4 + k * 13, 50, 12, juce::Justification::centredRight);
                }
            }

            void paintDrums (juce::Graphics& g, juce::Rectangle<float>)
            {
                const auto c = engineColours[3];
                for (int i = 0; i < 12; ++i)
                {
                    auto r = padRect (i);
                    const bool on = i == pressedPad;
                    g.setGradientFill (juce::ColourGradient (on ? c.withAlpha (0.8f) : juce::Colour (0xff262a31), 0, r.getY(),
                                                             on ? c.withAlpha (0.45f) : juce::Colour (0xff1d2026), 0, r.getBottom(), false));
                    g.fillRoundedRectangle (r, 7.0f);
                    g.setColour (on ? c : c.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.2f);
                    g.setColour (on ? juce::Colours::white : theme::text); g.setFont (theme::ui (12.5f, true));
                    g.drawFittedText (U8 (pads()[(size_t) i].name), r.toNearestInt().reduced (8, 4), juce::Justification::centred, 2, 0.8f);
                    g.setColour (on ? juce::Colours::white.withAlpha (0.7f) : theme::dim); g.setFont (theme::ui (9.5f));
                    g.drawText (juce::String (pads()[(size_t) i].note), r.toNearestInt().reduced (7, 5), juce::Justification::bottomRight);
                }
            }

            void paintStrings (juce::Graphics& g, juce::Rectangle<float> in)
            {
                const auto c = engineColours[4];
                const int type = juce::roundToInt (synth.value ("body"));
                const float pick = synth.value ("pickpos"), bright = synth.value ("bright"), mute = synth.value ("mute");
                const bool dbl = synth.value ("double") > 0.5f, acoustic = type == 1 || type == 2 || type == 5;
                auto left = in.removeFromLeft (in.getWidth() * 0.58f); in.removeFromLeft (16.0f);
                label (g, left, U8 ("CORDE ET POSITION DU MÉDIATOR"));
                left.removeFromTop (18.0f);
                // Instrument schématique : caisse (acoustique) ou micros (électrique), cordes, chevalet à droite.
                auto inst = left.reduced (4.0f, 6.0f);
                const float bridgeX = inst.getRight() - 20.0f, nutX = inst.getX() + 10.0f;
                if (acoustic)
                {
                    auto bodyR = juce::Rectangle<float> (inst.getRight() - inst.getWidth() * 0.55f, inst.getY(), inst.getWidth() * 0.55f, inst.getHeight());
                    g.setColour (juce::Colour (0xff2a2219)); g.fillEllipse (bodyR);
                    g.setColour (juce::Colour (0xff4a3a28)); g.drawEllipse (bodyR, 1.5f);
                    g.setColour (juce::Colour (0xff0d0b09)); g.fillEllipse (bodyR.withSizeKeepingCentre (bodyR.getHeight() * 0.5f, bodyR.getHeight() * 0.5f).translated (-bodyR.getWidth() * 0.12f, 0));
                }
                else if (type == 0 || type == 3)
                {
                    for (float pos : { 0.12f, 0.24f })
                    {
                        const float x = bridgeX - (bridgeX - nutX) * pos;
                        g.setColour (juce::Colour (0xff2b2f36)); g.fillRoundedRectangle (x - 9, inst.getY() + 4, 18, inst.getHeight() - 8, 3.0f);
                        g.setColour (juce::Colour (0xff50555e));
                        for (int k = 0; k < 6; ++k) g.fillEllipse (x - 2, inst.getY() + 10 + k * (inst.getHeight() - 24) / 5.0f, 4, 4);
                    }
                }
                const int strings = type == 3 ? 4 : type == 5 ? 4 : 6;
                const float px = bridgeX - (bridgeX - nutX) * pick;
                for (int s = 0; s < strings; ++s)
                {
                    const float y = inst.getY() + 10 + s * (inst.getHeight() - 20) / (strings - 1);
                    const float thick = 0.8f + (type == 2 ? 1.0f : 0.5f) + s * (type == 3 ? 0.6f : 0.3f);
                    if (s == strings / 2)       // la corde pincée : forme triangulaire au point d'attaque
                    {
                        juce::Path p; p.startNewSubPath (nutX, y); p.lineTo (px, y - 12.0f * (1.0f - mute * 0.6f)); p.lineTo (bridgeX, y);
                        g.setColour (c.withAlpha (0.3f)); g.strokePath (p, juce::PathStrokeType (thick + 4.0f));
                        g.setColour (c); g.strokePath (p, juce::PathStrokeType (thick));
                    }
                    else
                    {
                        g.setColour (juce::Colour (0xffb8bcc4).withAlpha (0.75f)); g.drawLine (nutX, y, bridgeX, y, thick);
                        if (dbl && s < 4) g.drawLine (nutX, y + 3.0f, bridgeX, y + 3.0f, thick * 0.6f);
                    }
                }
                g.setColour (juce::Colour (0xffe8e2d6)); g.fillRect (bridgeX, inst.getY() + 4, 4.0f, inst.getHeight() - 8);
                g.setColour (juce::Colour (0xff8d8a83)); g.fillRect (nutX - 4, inst.getY() + 4, 4.0f, inst.getHeight() - 8);
                if (mute > 0.05f) { g.setColour (juce::Colour (0xffd9a46a).withAlpha (0.25f + 0.5f * mute)); g.fillRoundedRectangle (bridgeX - 26, inst.getY(), 22, inst.getHeight(), 8.0f); }
                g.setColour (theme::text); g.fillEllipse (px - 5, inst.getY() - 2, 10, 10);
                // Spectre au point d'attaque : les harmoniques ayant un nœud au médiator disparaissent.
                std::vector<float> amps (24);
                for (int n = 1; n <= 24; ++n)
                    amps[(size_t) n - 1] = std::abs (std::sin (juce::MathConstants<float>::pi * n * pick)) / n * std::pow ((float) n, 0.9f * bright - 0.2f) * (1.0f - mute * 0.03f * n);
                float mx = 0; for (auto a : amps) mx = std::max (mx, a);
                for (auto& a : amps) a = juce::jmax (0.0f, a / juce::jmax (1.0e-6f, mx));
                paintBars (g, in, amps, c, U8 ("HARMONIQUES AU POINT D'ATTAQUE"));
            }

            SynthPlugin& synth;
            int pressedPad = -1;
        };
    }

    //==============================================================================
    class SynthEditorWindow  : public juce::Component, private juce::Timer, private juce::MidiKeyboardState::Listener
    {
    public:
        explicit SynthEditorWindow (SynthPlugin& p) : synth (p), display (p)
        {
            browser.currentName = [this] { return synth.getPresetName(); };
            browser.onChoose = [this] (const SynthPreset& pr) { choose (pr); };
            addAndMakeVisible (browser);
            addAndMakeVisible (display);
            for (auto* b : { &prevBtn, &nextBtn })
            {
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff23272d));
                b->setColour (juce::TextButton::textColourOffId, theme::text);
                addAndMakeVisible (b);
            }
            prevBtn.setTooltip (U8 ("Son précédent")); nextBtn.setTooltip (U8 ("Son suivant"));
            prevBtn.onClick = [this] { browser.step (-1); };
            nextBtn.onClick = [this] { browser.step (+1); };
            keyboardState.addListener (this);
            keyboard.setAvailableRange (21, 108);
            keyboard.setWantsKeyboardFocus (false);
            keyboard.setScrollButtonsVisible (true);
            keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9ebee));
            keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1a1c20));
            keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff9aa0a8));
            keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent.withAlpha (0.75f));
            keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent.withAlpha (0.2f));
            keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.4f));
            keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour (0xff23272d));
            keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, theme::text);
            addAndMakeVisible (keyboard);
            rebuild();
            setSize (1280, 760);
            startTimerHz (15);
        }
        ~SynthEditorWindow() override { keyboardState.removeListener (this); keyboardState.allNotesOff (0); }

        void paint (juce::Graphics& g) override
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22262c), 0, 0, juce::Colour (0xff15171b), 0, (float) getHeight(), false));
            g.fillAll();
            auto h = header;
            theme::drawLogo (g, h.removeFromLeft (h.getHeight()).toFloat().reduced (8.0f));
            h.removeFromLeft (6);
            const auto* pr = SynthPlugin::findPreset (synth.getPresetName());
            const auto fam = pr != nullptr ? U8 (pr->category) : juce::String();
            g.setColour (theme::muted); g.setFont (theme::ui (11.0f, true));
            g.drawText ("ONDE SYNTH" + (fam.isNotEmpty() ? U8 ("  ·  ") + fam.toUpperCase() : juce::String()), h.removeFromTop (18).translated (0, 4), juce::Justification::centredLeft);
            g.setColour (theme::text); g.setFont (theme::ui (21.0f, true));
            g.drawText (synth.getPresetName(), h.withTrimmedRight (330), juce::Justification::centredLeft, true);
            // Pastille du moteur.
            const auto ec = engineColours[engine];
            auto pill = juce::Rectangle<float> ((float) prevBtn.getX() - 150.0f, (float) header.getCentreY() - 11.0f, 140.0f, 22.0f);
            g.setColour (ec.withAlpha (0.15f)); g.fillRoundedRectangle (pill, 11.0f);
            g.setColour (ec.withAlpha (0.6f)); g.drawRoundedRectangle (pill, 11.0f, 1.0f);
            g.setColour (ec); g.setFont (theme::ui (11.5f, true));
            g.drawText (U8 ("Moteur ") + U8 (SynthPlugin::engineNames[engine]).toLowerCase(), pill.toNearestInt(), juce::Justification::centred);
            g.setColour (theme::line); g.drawHorizontalLine (header.getBottom() + 6, (float) browser.getRight(), (float) getWidth());
            g.setColour (theme::dim); g.setFont (theme::ui (10.5f, true));
            g.drawText (U8 ("ESSAYER LE SON  ·  cliquez sur le clavier") + (engine == 3 ? U8 (" ou sur les pads") : juce::String()),
                        keyboard.getX(), keyboard.getY() - 18, 400, 16, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            browser.setBounds (r.removeFromLeft (250));
            r.reduce (14, 10);
            header = r.removeFromTop (48);
            nextBtn.setBounds (header.getRight() - 34, header.getCentreY() - 14, 34, 28);
            prevBtn.setBounds (header.getRight() - 72, header.getCentreY() - 14, 34, 28);
            r.removeFromTop (14);
            auto kb = r.removeFromBottom (76);
            keyboard.setBounds (kb);
            keyboard.setKeyWidth (juce::jmax (14.0f, kb.getWidth() / 36.0f));
            keyboard.setLowestVisibleKey (engine == 3 ? 35 : engine == 4 ? 28 : 36);
            r.removeFromBottom (22);
            const int nRows = juce::jmax (1, (int) rows.size());
            const int rowH = juce::jlimit (108, 132, (r.getHeight() - 170) / nRows);
            const int displayH = juce::jmax (150, r.getHeight() - (int) rows.size() * rowH - 6);
            display.setBounds (r.removeFromTop (displayH));
            r.removeFromTop (6);
            for (auto& row : rows)
            {
                auto rr = r.removeFromTop (rowH);
                int total = 0;
                for (auto* s : row) total += s->preferredWidth();
                const float k = total > 0 ? (float) rr.getWidth() / total : 1.0f;
                for (int i = 0; i < (int) row.size(); ++i)
                {
                    const int w = i == (int) row.size() - 1 ? rr.getWidth() : juce::roundToInt (row[(size_t) i]->preferredWidth() * k);
                    row[(size_t) i]->setBounds (rr.removeFromLeft (w));
                }
            }
        }

    private:
        void choose (const SynthPreset& pr)
        {
            const auto old = synth.getPresetName();
            synth.edit.getUndoManager().beginNewTransaction (U8 ("Son « ") + U8 (pr.name) + U8 (" »"));
            synth.applyPreset (pr);
            if (auto* t = dynamic_cast<te::AudioTrack*> (synth.getOwnerTrack()))
            {
                if (t->getName() == old) t->setName (U8 (pr.name));       // piste nommée d'après son son : elle suit
                chains::rebuildMacros (*t);
            }
            rebuild();
            browser.syncSelection();
            // On entend tout de suite le nouveau son (accord court, ou grosse caisse et caisse claire).
            const bool drums = SynthPlugin::engineOf (pr) == 3;
            for (int n : drums ? std::vector<int> { 36, 42 } : std::vector<int> { 48, 55, 64 }) synth.auditionNote (n, 0.75f);
            juce::Timer::callAfterDelay (450, [ptr = te::Plugin::Ptr (&synth), drums]
            {
                if (auto* s = dynamic_cast<SynthPlugin*> (ptr.get()))
                    for (int n : drums ? std::vector<int> { 36, 42 } : std::vector<int> { 48, 55, 64 }) s->auditionNote (n, 0.0f);
            });
        }

        void rebuild()
        {
            engine = juce::jlimit (0, SynthPlugin::numEngines - 1, juce::roundToInt (synth.value ("engine")));
            display.engine = engine;
            rows.clear(); sections.clear();
            const auto c = engineColours[engine];
            auto add = [&] (std::vector<Section*> row) { for (auto* s : row) { sections.add (s); addAndMakeVisible (s); } rows.push_back (row); };
            auto S = [&] (const char* t, std::vector<const char*> ids, std::array<const char*, 4> env = {}) { return new Section (synth, U8 (t), c, std::move (ids), env); };
            switch (engine)
            {
                case 0:
                    add ({ S ("Oscillateurs", { "wave1", "wave2", "semi2", "detune", "mix2", "sub", "noise" }), S ("Unisson", { "unison", "spread", "width" }) });
                    add ({ S ("Filtre", { "cutoff", "reso", "keytrack", "fenv" }), S ("Enveloppe du filtre", { "fa", "fd", "fs", "fr" }, { "fa", "fd", "fs", "fr" }) });
                    add ({ S ("Amplitude", { "a", "d", "s", "r" }, { "a", "d", "s", "r" }), S ("Jeu", { "vibrato", "velsens", "drive", "volume" }) });
                    break;
                case 1:
                    add ({ S ("Opérateurs FM", { "ratio", "index", "idecay", "feedback", "bright" }), S ("Jeu", { "vibrato", "velsens", "drive" }) });
                    add ({ S ("Amplitude", { "a", "d", "s", "r" }, { "a", "d", "s", "r" }), S ("Sortie", { "width", "volume" }) });
                    break;
                case 2:
                    add ({ S ("Piano", { "bright", "hard" }), S ("Jeu", { "velsens", "vibrato" }), S ("Sortie", { "drive", "width", "volume" }) });
                    break;
                case 3:
                    add ({ S ("Kit", { "kit", "dtune", "ddecay" }), S ("Jeu", { "velsens" }), S ("Sortie", { "drive", "volume" }) });
                    break;
                default:
                    add ({ S ("Instrument", { "body", "double" }), S ("Corde", { "pickpos", "pick", "bright", "ring", "mute" }) });
                    add ({ S ("Jeu", { "r", "velsens", "vibrato" }), S ("Sortie", { "drive", "width", "volume" }) });
                    break;
            }
            lastEnvSig.clear();
            resized();
            repaint();
        }

        void timerCallback() override
        {
            if (juce::roundToInt (synth.value ("engine")) != engine) rebuild();
            const auto sig = display.signature();
            if (sig != lastDisplaySig) { lastDisplaySig = sig; display.repaint(); }
            juce::String env; for (auto* s : sections) env << s->envSignature() << "|";
            if (env != lastEnvSig) { lastEnvSig = env; for (auto* s : sections) if (s->envSignature().isNotEmpty()) s->repaint(); }
            if (synth.getPresetName() != shownName) { shownName = synth.getPresetName(); browser.syncSelection(); repaint (header.withLeft (0)); }
        }

        void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override  { synth.auditionNote (note, juce::jmax (0.05f, velocity)); }
        void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override           { synth.auditionNote (note, 0.0f); }

        SynthPlugin& synth;
        te::Plugin::Ptr keepAlive { &synth };
        SoundBrowser browser;
        EngineDisplay display;
        juce::TextButton prevBtn { U8 ("◀") }, nextBtn { U8 ("▶") };
        juce::MidiKeyboardState keyboardState;
        juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
        juce::OwnedArray<Section> sections;
        std::vector<std::vector<Section*>> rows;
        juce::Rectangle<int> header;
        juce::String lastDisplaySig, lastEnvSig, shownName;
        int engine = -1;
    };

    std::unique_ptr<juce::Component> createSynthEditor (SynthPlugin& s) { return std::make_unique<SynthEditorWindow> (s); }
}
