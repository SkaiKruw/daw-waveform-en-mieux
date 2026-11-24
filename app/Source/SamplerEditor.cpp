// Fenêtre d'Onde Échantillonneur : instruments installés (recherche), carte du clavier (zones jouables,
// articulations), chargement, réglages, clavier pour essayer.
#include "OndeSampler.h"
#include "ParamControls.h"
#include "Theme.h"

namespace ondefx
{
    namespace
    {
        juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }
        const juce::Colour colour (0xff5ad1a0), cardBg (0xff1b1e23), cardLine (0xff2b2f36);

        // Liste des instruments installés : un par fichier .sfz, regroupés par bibliothèque.
        class InstrumentList  : public juce::Component, private juce::ListBoxModel
        {
        public:
            std::function<void (const juce::File&)> onChoose;
            std::function<juce::File()> current;
            InstrumentList()
            {
                search.setTextToShowWhenEmpty (U8 ("Rechercher un instrument…"), theme::dim);
                search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111316));
                search.setColour (juce::TextEditor::outlineColourId, cardLine);
                search.onTextChange = [this] { refresh(); };
                addAndMakeVisible (search);
                list.setModel (this);
                list.setRowHeight (30);
                list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
                addAndMakeVisible (list);
                refresh();
            }
            ~InstrumentList() override { list.setModel (nullptr); }
            void refresh()
            {
                all.clear();
                for (auto& root : libraryRoots())
                    for (auto& f : findInstruments (root))
                        all.add ({ f, f.getRelativePathFrom (root).upToFirstOccurrenceOf ("\\", false, false).upToFirstOccurrenceOf ("/", false, false) });
                shown.clear();
                const auto q = search.getText().trim();
                for (auto& e : all) if (q.isEmpty() || e.file.getFileNameWithoutExtension().containsIgnoreCase (q) || e.library.containsIgnoreCase (q)) shown.add (e);
                list.updateContent(); list.repaint(); repaint();
            }
            int count() const { return all.size(); }
            void paint (juce::Graphics& g) override
            {
                g.setColour (juce::Colour (0xff15171b)); g.fillRect (getLocalBounds());
                g.setColour (theme::line); g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
                if (all.isEmpty())
                {
                    g.setColour (theme::muted); g.setFont (theme::ui (12.5f));
                    g.drawFittedText (U8 ("Aucune bibliothèque installée.\n\nCliquez sur « Bibliothèques… » pour télécharger des instruments enregistrés "
                                          "(piano à queue, orchestre, batterie, basses, guitares…), ou ouvrez un fichier SFZ."),
                                      getLocalBounds().reduced (16).withTrimmedTop (50), juce::Justification::topLeft, 10);
                }
            }
            void resized() override
            {
                auto r = getLocalBounds().reduced (10);
                search.setBounds (r.removeFromTop (30));
                r.removeFromTop (8);
                list.setBounds (r);
            }
        private:
            struct Entry { juce::File file; juce::String library; };
            int getNumRows() override { return shown.size(); }
            void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
            {
                if (! juce::isPositiveAndBelow (row, shown.size())) return;
                const auto& e = shown.getReference (row);
                const bool sel = current && current() == e.file;
                auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (4.0f, 2.0f);
                if (sel) { g.setColour (colour.withAlpha (0.2f)); g.fillRoundedRectangle (r, 5.0f); g.setColour (colour); g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f); }
                g.setColour (sel ? theme::text : juce::Colour (0xffc3c8cf)); g.setFont (theme::ui (12.5f, sel));
                g.drawFittedText (e.file.getFileNameWithoutExtension(), r.toNearestInt().withTrimmedLeft (12).withTrimmedRight (90), juce::Justification::centredLeft, 1, 0.8f);
                g.setColour (theme::dim); g.setFont (theme::ui (10.0f));
                g.drawText (e.library, r.toNearestInt().withTrimmedRight (8), juce::Justification::centredRight, true);
            }
            void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, shown.size()) && onChoose) onChoose (shown[row].file); }
            juce::TextEditor search;
            juce::ListBox list;
            juce::Array<Entry> all, shown;
        };

        class SamplerEditor  : public juce::Component, private juce::Timer, private juce::MidiKeyboardState::Listener
        {
        public:
            explicit SamplerEditor (SamplerPlugin& p) : sampler (p)
            {
                instruments.current = [this] { return sampler.getInstrumentFile(); };
                instruments.onChoose = [this] (const juce::File& f) { choose (f); };
                addAndMakeVisible (instruments);
                for (auto* b : { &librariesBtn, &openBtn })
                {
                    b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff23272d));
                    b->setColour (juce::TextButton::textColourOffId, theme::text);
                    addAndMakeVisible (b);
                }
                librariesBtn.onClick = [this] { if (onOpenLibraries) onOpenLibraries(); };
                openBtn.onClick = [this]
                {
                    chooser = std::make_unique<juce::FileChooser> (U8 ("Instrument SFZ"), librariesFolder(), "*.sfz");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                    { if (fc.getResult().existsAsFile()) choose (fc.getResult()); });
                };
                const char* ids[] = { "volume", "attack", "release", "tone", "velsens", "width", "tune" };
                const char* names[] = { "Volume", "Attaque", "Relâchement", "Brillance", "Toucher", "Largeur", "Accord" };
                for (int i = 0; i < 7; ++i)
                {
                    auto* k = knobs.add (new ondeui::Knob (sampler.param (ids[i]), U8 (names[i]), colour));
                    addAndMakeVisible (k);
                }
                keyboardState.addListener (this);
                keyboard.setAvailableRange (21, 108);
                keyboard.setWantsKeyboardFocus (false);
                keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, colour.withAlpha (0.75f));
                keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colour.withAlpha (0.2f));
                addAndMakeVisible (keyboard);
                setSize (1100, 640);
                startTimerHz (10);
            }
            ~SamplerEditor() override { keyboardState.removeListener (this); }
            std::function<void()> onOpenLibraries;

            void paint (juce::Graphics& g) override
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22262c), 0, 0, juce::Colour (0xff15171b), 0, (float) getHeight(), false));
                g.fillAll();
                auto h = header;
                theme::drawLogo (g, h.removeFromLeft (h.getHeight()).toFloat().reduced (8.0f));
                h.removeFromLeft (6);
                auto inst = sampler.getInstrument();
                g.setColour (theme::muted); g.setFont (theme::ui (11.0f, true));
                g.drawText (U8 ("ONDE ÉCHANTILLONNEUR"), h.removeFromTop (18).translated (0, 4), juce::Justification::centredLeft);
                g.setColour (theme::text); g.setFont (theme::ui (21.0f, true));
                const auto title = sampler.isLoading() ? U8 ("Chargement… ") + juce::String (juce::roundToInt (sampler.loadProgress() * 100)) + " %"
                                 : inst != nullptr ? inst->name : U8 ("Aucun instrument");
                g.drawText (title, h.withTrimmedRight (280), juce::Justification::centredLeft, true);

                // Carte du clavier : zones jouables, touches d'articulation, vélocités.
                auto map = mapArea.toFloat();
                g.setColour (cardBg); g.fillRoundedRectangle (map, 8.0f);
                g.setColour (cardLine); g.drawRoundedRectangle (map, 8.0f, 1.0f);
                auto in = map.reduced (14.0f, 12.0f);
                g.setColour (theme::dim); g.setFont (theme::ui (10.0f, true));
                g.drawText (U8 ("CARTE DU CLAVIER"), in.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft);
                in.removeFromTop (6.0f);
                const float kw = in.getWidth() / 88.0f;
                std::array<int, 128> layers {};
                if (inst != nullptr) for (auto& r : inst->regions) if (! r.releaseTrigger) for (int k = r.lokey; k <= r.hikey; ++k) layers[(size_t) k]++;
                int maxLayers = 1; for (int v : layers) maxLayers = std::max (maxLayers, v);
                const int sw = sampler.getCurrentKeyswitch();
                for (int k = 21; k <= 108; ++k)
                {
                    const float x = in.getX() + (k - 21) * kw;
                    const bool black = juce::MidiMessage::isMidiNoteBlack (k);
                    auto key = juce::Rectangle<float> (x + 0.5f, in.getY(), kw - 1.0f, in.getHeight() * 0.5f);
                    const bool isSwitch = inst != nullptr && (inst->keyswitchNames[(size_t) k].isNotEmpty() || (inst->swLo >= 0 && k >= inst->swLo && k <= inst->swHi));
                    juce::Colour c = black ? juce::Colour (0xff2a2d33) : juce::Colour (0xff3a3e45);
                    if (layers[(size_t) k] > 0) c = colour.withAlpha (0.35f + 0.55f * layers[(size_t) k] / maxLayers);
                    if (isSwitch) c = k == sw ? juce::Colour (0xffe5a04e) : juce::Colour (0xffe5a04e).withAlpha (0.45f);
                    g.setColour (c); g.fillRect (key);
                    if (k % 12 == 0) { g.setColour (theme::muted); g.setFont (theme::ui (9.5f)); g.drawText ("Do" + juce::String (k / 12 - 1), (int) x - 2, (int) key.getBottom() + 2, 34, 12, juce::Justification::centredLeft); }
                }
                auto info = in.withTrimmedTop (in.getHeight() * 0.5f + 18.0f);
                g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
                if (inst != nullptr)
                {
                    int lo = 127, hi = 0; for (auto& r : inst->regions) if (! r.releaseTrigger) { lo = std::min (lo, r.lokey); hi = std::max (hi, r.hikey); }
                    int releases = 0, rr = 1; for (auto& r : inst->regions) { releases += r.releaseTrigger ? 1 : 0; rr = std::max (rr, r.seqLength); }
                    juce::String t;
                    t << juce::String ((int) inst->regions.size()) << U8 (" zones · ") << juce::String ((int) inst->samples.size()) << U8 (" échantillons · tessiture ")
                      << juce::MidiMessage::getMidiNoteName (lo, true, true, 4) << " - " << juce::MidiMessage::getMidiNoteName (hi, true, true, 4)
                      << (rr > 1 ? U8 (" · ") + juce::String (rr) + U8 (" variations par note") : juce::String())
                      << (releases > 0 ? U8 (" · bruits de relâchement") : juce::String());
                    g.drawFittedText (t, info.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 1);
                    if (! inst->articulations.isEmpty())
                        g.drawFittedText (U8 ("Articulations (touches orange) : ") + inst->articulations.joinIntoString (", ")
                                          + (sw >= 0 && inst->keyswitchNames[(size_t) sw].isNotEmpty() ? U8 (" — en cours : ") + inst->keyswitchNames[(size_t) sw] : juce::String()),
                                          info.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 1);
                    if (inst->error.isNotEmpty()) { g.setColour (theme::warn); g.drawText (inst->error, info.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, true); }
                }
                else g.drawText (U8 ("Choisissez un instrument dans la liste (ou téléchargez une bibliothèque)."), info.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft);
                g.setColour (theme::dim); g.setFont (theme::ui (10.5f, true));
                g.drawText (U8 ("ESSAYER L'INSTRUMENT · cliquez sur le clavier"), keyboard.getX(), keyboard.getY() - 18, 400, 16, juce::Justification::centredLeft);
            }

            void resized() override
            {
                auto r = getLocalBounds();
                instruments.setBounds (r.removeFromLeft (270));
                r.reduce (14, 10);
                header = r.removeFromTop (48);
                openBtn.setBounds (header.getRight() - 140, header.getCentreY() - 14, 140, 28);
                librariesBtn.setBounds (openBtn.getX() - 138, header.getCentreY() - 14, 130, 28);
                r.removeFromTop (12);
                keyboard.setBounds (r.removeFromBottom (80));
                keyboard.setKeyWidth (juce::jmax (12.0f, r.getWidth() / 52.0f));
                keyboard.setLowestVisibleKey (21);
                r.removeFromBottom (22);
                auto knobRow = r.removeFromBottom (110);
                const int w = knobRow.getWidth() / knobs.size();
                for (auto* k : knobs) k->setBounds (knobRow.removeFromLeft (w).reduced (4));
                r.removeFromBottom (10);
                mapArea = r;
            }

        private:
            void choose (const juce::File& f)
            {
                sampler.edit.getUndoManager().beginNewTransaction (U8 ("Instrument « ") + f.getFileNameWithoutExtension() + U8 (" »"));
                sampler.loadInstrument (f);
                if (auto* t = sampler.getOwnerTrack()) if (t->getName().startsWith ("Audio") || t->getName().startsWith (U8 ("Onde Échantillonneur")) || t->getName() == shownName)
                    t->setName (f.getFileNameWithoutExtension());
                shownName = f.getFileNameWithoutExtension();
                instruments.refresh();
                repaint();
            }
            void timerCallback() override
            {
                const auto key = juce::String ((int) sampler.isLoading()) + juce::String (sampler.loadProgress()) + juce::String (sampler.getCurrentKeyswitch())
                               + (sampler.getInstrument() ? sampler.getInstrument()->name : juce::String());
                if (key != lastKey) { lastKey = key; repaint(); instruments.repaint(); }
            }
            void handleNoteOn (juce::MidiKeyboardState*, int, int note, float v) override  { sampler.auditionNote (note, juce::jmax (0.05f, v)); }
            void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override    { sampler.auditionNote (note, 0.0f); }

            SamplerPlugin& sampler;
            te::Plugin::Ptr keepAlive { &sampler };
            InstrumentList instruments;
            juce::TextButton librariesBtn { U8 ("Bibliothèques…") }, openBtn { U8 ("Ouvrir un fichier SFZ…") };
            juce::OwnedArray<ondeui::Knob> knobs;
            juce::MidiKeyboardState keyboardState;
            juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
            std::unique_ptr<juce::FileChooser> chooser;
            juce::Rectangle<int> header, mapArea;
            juce::String lastKey, shownName;
        };
    }

    std::function<void()>& openLibrariesHook() { static std::function<void()> f; return f; }

    std::unique_ptr<juce::Component> createSamplerEditor (SamplerPlugin& s)
    {
        auto e = std::make_unique<SamplerEditor> (s);
        e->onOpenLibraries = [] { if (openLibrariesHook()) openLibrariesHook()(); };
        return e;
    }
}
