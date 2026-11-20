#include "ArrangementView.h"

namespace
{
    struct Peaks { std::vector<float> values; double seconds = 0; };

    // Crêtes des fichiers audio, calculées une fois (nouvel essai tant que la lecture échoue).
    const Peaks* peaksFor (const juce::File& f)
    {
        static std::map<juce::String, Peaks> cache;
        auto it = cache.find (f.getFullPathName());
        if (it != cache.end() && ! it->second.values.empty()) return &it->second;
        double seconds = 0;

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::vector<float> pk;
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (f)))
        {
            const int buckets = 4000;
            const auto len = reader->lengthInSamples;
            seconds = (double) len / reader->sampleRate;
            const int per = (int) std::max<int64_t> (1, len / buckets);
            juce::AudioBuffer<float> buf ((int) reader->numChannels, per);
            for (int i = 0; i < buckets && (int64_t) i * per < len; ++i)
            {
                reader->read (&buf, 0, per, (juce::int64) i * per, true, true);
                pk.push_back (buf.getMagnitude (0, per));
            }
        }
        auto& slot = cache[f.getFullPathName()];
        slot.values = std::move (pk);
        slot.seconds = seconds;
        return slot.values.empty() ? nullptr : &slot;
    }

    juce::File sourceFile (te::WaveAudioClip& c)
    {
        // Résolution mise en cache : interroger le disque (3 tests d'existence) à chaque dessin de chaque
        // événement coûtait ~15 % du fil de l'interface en lecture. Revérifiée toutes les 3 s.
        struct Resolved { juce::File file; juce::uint32 checkedAt = 0; };
        static std::unordered_map<juce::String, Resolved> resolved;
        const auto original = c.getOriginalFile();
        const auto key = original.getFullPathName();
        const auto now = juce::Time::getMillisecondCounter();
        auto it = resolved.find (key);
        if (it != resolved.end() && now - it->second.checkedAt < 3000) return it->second.file;
        auto f = original;
        if (! f.existsAsFile()) f = c.getAudioFile().getFile();
        if (! f.existsAsFile() && c.edit.editFileRetriever)     // repli : à côté du fichier du projet
            f = c.edit.editFileRetriever().getSiblingFile (original.getFileName());
        resolved[key] = { f, now };
        return f;
    }

    // Ouvre le champ de nom après la fin du clic : sinon JUCE rend le clavier à la fenêtre et les lettres
    // tapées partent en raccourcis (R = enregistrer, M = muet…).
    void editNameLater (juce::Label& l)
    {
        juce::Component::SafePointer<juce::Label> safe (&l);
        juce::Timer::callAfterDelay (60, [safe]
        {
            if (safe == nullptr) return;
            safe->showEditor();
            if (auto* ed = safe->getCurrentTextEditor()) ed->grabKeyboardFocus();
        });
    }

    void renameMarker (EditContext& ctx, te::EditItemID id)
    {
        auto* mk = ctx.edit ? te::findClipForID (*ctx.edit, id) : nullptr;
        if (mk == nullptr) return;
        auto* w = new juce::AlertWindow (U ("Renommer le repère"), U ("Nom du repère :"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", mk->getName());
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([&ctx, id, w] (int r)
        {
            if (r != 1 || ctx.edit == nullptr) return;
            if (auto* c = te::findClipForID (*ctx.edit, id))
            {
                ctx.edit->getUndoManager().beginNewTransaction (U ("Renommer le repère"));
                c->setName (w->getTextEditorContents ("name"));
                ctx.changed();
            }
        }), true);
    }

    juce::Colour trackColour (const te::Track& t)
    {
        auto c = t.getColour();
        return c.isTransparent() ? juce::Colour (0xff6a8fb0) : c;
    }

    // Grille : mesures, temps et subdivisions selon le zoom.
    void paintGrid (juce::Graphics& g, const EditContext& ctx, const ArrangementView& v, juce::Rectangle<int> r, int width)
    {
        // On avance en temps musicaux (la piste de tempo peut faire varier leur durée).
        const int bpb = ctx.beatsPerBar();
        const double t0 = juce::jmax (0.0, v.xToTime ((float) ArrangementView::headerW));
        const double pxBeat = ctx.beatSeconds() * (v.timeToX (1.0) - v.timeToX (0.0)) * ctx.bpm() / juce::jmax (1.0, ctx.bpmAt (t0));
        const double step = pxBeat > 60 ? juce::jmin (1.0, ctx.gridBeats) : (pxBeat > 14 ? 1.0 : (double) bpb);
        for (double b = std::floor (ctx.toBeats (t0) / step) * step; ; b += step)
        {
            const float x = v.timeToX (ctx.toSeconds (b));
            if (x > (float) width) break;
            if (x < ArrangementView::headerW) continue;
            const bool isBar = std::abs (std::fmod (b + 1e-6, (double) bpb)) < 1e-4;
            const bool isBeat = std::abs (std::fmod (b + 1e-6, 1.0)) < 1e-4;
            g.setColour (isBar ? theme::lineBar : isBeat ? theme::lineSoft : theme::lineSoft.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, (float) r.getY(), (float) r.getBottom());
        }
    }
}

//==============================================================================
class ArrangementView::RowsHolder  : public juce::Component
{
public:
    explicit RowsHolder (ArrangementView& o) : owner (o) {}
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg);
        g.setColour (theme::panel); g.fillRect (0, 0, headerW, getHeight());
        g.setColour (theme::line); g.drawVerticalLine (headerW - 1, 0.0f, (float) getHeight());
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.ctx.selectClip (nullptr);
        if (e.mods.isPopupMenu() || e.getNumberOfClicks() == 2) owner.showAddTrackMenu();
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        // Indicateur de dépôt pendant le glisser d'une piste.
        if (owner.dropY < 0) return;
        g.setColour (theme::accent);
        if (owner.dropIntoFolder.isValid())
        {
            for (auto& e : owner.entries)
                if (e.track->itemID == owner.dropIntoFolder) g.drawRect (e.comp->getBounds().withWidth (getWidth()), 2);
        }
        else g.fillRect (0, owner.dropY - 1, getWidth(), 3);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown() || e.mods.isShiftDown() || e.mods.isAltDown()) owner.mouseWheelMove (e.getEventRelativeTo (&owner), w);
        else Component::mouseWheelMove (e, w);
    }
    ArrangementView& owner;
};

//==============================================================================
class ArrangementView::TrackRow  : public juce::Component
{
public:
    TrackRow (ArrangementView& o, te::AudioTrack& t) : owner (o), ctx (o.ctx), track (t)
    {
        addAndMakeVisible (nameLabel);
        nameLabel.setFont (theme::ui (13.0f, true));
        nameLabel.setColour (juce::Label::textColourId, theme::text);
        nameLabel.setEditable (false, true, false);
        nameLabel.setTooltip (U ("Double-clic pour renommer"));
        nameLabel.onTextChange = [this] { ctx.edit->getUndoManager().beginNewTransaction ("Renommer"); track.setName (nameLabel.getText()); ctx.changed(); };
        nameLabel.setInterceptsMouseClicks (false, false);

        styleStateButton (muteBtn, theme::mute); styleStateButton (soloBtn, theme::solo);
        styleStateButton (armBtn, theme::rec, juce::Colours::white); styleStateButton (monBtn, theme::monitor);
        styleStateButton (autoBtn, theme::readA);
        muteBtn.setTooltip ("Rendre muet (M)"); soloBtn.setTooltip ("Solo (Maj+S)"); armBtn.setTooltip ("Activer l'enregistrement (Maj+R)");
        monBtn.setTooltip ("Monitoring"); autoBtn.setTooltip (U ("Afficher l'automation (A)"));
        muteBtn.onClick = [this] { track.setMute (muteBtn.getToggleState()); ctx.changed(); };
        soloBtn.onClick = [this] { track.setSolo (soloBtn.getToggleState()); ctx.changed(); };
        armBtn.onClick  = [this] { ctx.setTrackArmed (track, armBtn.getToggleState()); };
        monBtn.onClick  = [this] { ctx.setTrackMonitoring (track, monBtn.getToggleState()); };
        autoBtn.onClick = [this] { ctx.toggleAutomation (track); };
        for (auto* b : { &muteBtn, &soloBtn, &armBtn, &monBtn, &autoBtn }) addAndMakeVisible (*b);

        styleButton (paramBtn);
        paramBtn.setColour (juce::TextButton::buttonColourId, theme::display);
        paramBtn.onClick = [this] { usePan = ! usePan; sync(); repaint(); };
        paramBtn.setTooltip (U ("Paramètre automatisé (cliquer pour changer)"));
        addChildComponent (paramBtn);

        addChildComponent (restartBtn);
        styleButton (restartBtn, true);
        restartBtn.onClick = [this]
        {
            for (auto* sp : ctx.getSandboxPlugins (track))
                if (sp->isFailed()) sp->restartSandbox();
            ctx.changed();
        };
        if (auto* lm = track.getLevelMeterPlugin()) { meterPlugin = lm; lm->measurer.addClient (client); }
        sync();
    }

    ~TrackRow() override
    {
        if (auto* lm = dynamic_cast<te::LevelMeterPlugin*> (meterPlugin.get())) lm->measurer.removeClient (client);
    }

    te::AudioTrack& track;
    const te::EditItemID trackID { track.itemID };
    bool usePan = false;
    int depth = 0;                                   // niveau d'imbrication dans les dossiers

    bool hasFailure() const
    {
        for (auto* sp : ctx.getSandboxPlugins (track))
            if (sp->isFailed()) return true;
        return false;
    }

    int trackH() const { return owner.rowHeightFor (track); }
    bool autoShown() const { return ctx.isAutomationShown (track); }
    int preferredHeight() const { return trackH() + (autoShown() ? autoLaneH : 0) + (hasFailure() ? bannerH : 0); }
    juce::Rectangle<int> autoArea() const { return autoShown() ? juce::Rectangle<int> (0, trackH(), getWidth(), autoLaneH) : juce::Rectangle<int>(); }

    te::AutomatableParameter* param() const
    {
        auto* v = track.getVolumePlugin();
        if (v == nullptr) return nullptr;
        return usePan ? v->panParam.get() : v->volParam.get();
    }

    void sync()
    {
        if (! trackAlive()) return;
        muteBtn.setToggleState (track.isMuted (false), juce::dontSendNotification);
        soloBtn.setToggleState (track.isSolo (false), juce::dontSendNotification);
        armBtn.setToggleState (ctx.isTrackArmed (track), juce::dontSendNotification);
        monBtn.setToggleState (ctx.isTrackMonitoring (track), juce::dontSendNotification);
        autoBtn.setToggleState (autoShown(), juce::dontSendNotification);
        if (! nameLabel.isBeingEdited()) nameLabel.setText (track.getName(), juce::dontSendNotification);
        paramBtn.setVisible (autoShown());
        paramBtn.setButtonText (usePan ? U ("Panoramique ▾") : U ("Volume ▾"));
        const bool failed = hasFailure();
        restartBtn.setVisible (failed);
        if (failed != wasFailed || autoShown() != wasAuto) { wasFailed = failed; wasAuto = autoShown(); owner.layoutRows(); }
    }

    void updateMeter()
    {
        const float l = client.getAndClearAudioLevel (0).dB, r = client.getAndClearAudioLevel (1).dB;
        const float v = juce::jmax (l, r);
        level = v > level ? v : juce::jmax (-100.0f, level - 2.0f);
        // Seul le vumètre est redessiné, et seulement s'il a bougé d'au moins un pixel.
        const int px = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 66.0f) * (float) getHeight());
        if (px != drawnMeterPx) { drawnMeterPx = px; repaint (ArrangementView::headerW - 11, 0, 7, getHeight()); }
    }

    void resized() override
    {
        const int h = trackH();
        auto head = juce::Rectangle<int> (0, 0, headerW, h).reduced (0, 4);
        auto line1 = head.removeFromTop (20);
        line1.removeFromLeft (34 + depth * indentW);
        nameLabel.setBounds (line1.withTrimmedRight (14));
        auto line2 = head.removeFromTop (22).withTrimmedTop (2);
        line2.removeFromLeft (34 + juce::jmin (depth * indentW, 40));
        for (auto* b : { &muteBtn, &soloBtn, &armBtn, &monBtn, &autoBtn }) { b->setBounds (line2.removeFromLeft (22).withHeight (18)); line2.removeFromLeft (3); }
        if (autoShown()) paramBtn.setBounds (juce::Rectangle<int> (34, h + 8, headerW - 50, 20));
        restartBtn.setBounds (getLocalBounds().removeFromBottom (bannerH).removeFromRight (180).reduced (10, 12));
    }

    // La piste existe-t-elle encore ? (supprimée juste avant que la liste soit reconstruite au minuteur)
    bool trackAlive() const
    {
        return ctx.edit != nullptr && te::findTrackForID (*ctx.edit, trackID) == &track;
    }

    void paint (juce::Graphics& g) override
    {
        if (! trackAlive()) { g.fillAll (theme::bg); return; }
        const auto col = trackColour (track);
        const int h = trackH();
        const bool selected = ctx.selectedTrackID == track.itemID;
        auto header = juce::Rectangle<int> (0, 0, headerW, h);

        // En-tête de piste (sauté si la zone à redessiner est entièrement à droite : tête de lecture).
        if (g.getClipBounds().getX() < headerW)
        {
        g.setColour (selected ? juce::Colour (0xff4a5058) : theme::header); g.fillRect (header);
        if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (header.withWidth (headerW).withHeight (1)); }
        // Retrait des pistes rangées dans un dossier (un filet par niveau).
        const int ind = depth * indentW;
        for (int d = 0; d < depth; ++d) { g.setColour (theme::lineBar); g.fillRect (d * indentW + 6, 0, 2, h); }
        g.setColour (col); g.fillRect (ind, 0, 5, h);
        g.setColour (selected ? theme::text : theme::muted); g.setFont (theme::ui (11.0f));
        g.drawText (juce::String (ctx.trackNumber (track)), ind + 8, 4, 22, 20, juce::Justification::centred);
        // Icône du type de piste.
        {
            auto ic = juce::Rectangle<float> ((float) juce::jmin (ind, 40) + 12.0f, 31.0f, 12.0f, 12.0f);
            g.setColour (theme::muted);
            if (EditContext::fxBusOf (track) >= 0)
            {
                g.setFont (theme::ui (9.5f, true));
                g.drawText ("FX", ic.expanded (3.0f), juce::Justification::centred);
            }
            else if (ctx.getInstrument (track) != nullptr)
            {
                g.drawRect (ic, 1.0f);
                for (int k = 1; k < 3; ++k) g.fillRect (ic.getX() + k * 4.0f - 0.5f, ic.getY(), 1.5f, ic.getHeight() * 0.6f);
            }
            else
            {
                juce::Path w; w.startNewSubPath (ic.getX(), ic.getCentreY());
                for (int k = 0; k <= 12; ++k) w.lineTo (ic.getX() + k, ic.getCentreY() + std::sin (k * 1.1f) * (k % 2 ? 5.0f : 2.0f));
                g.strokePath (w, juce::PathStrokeType (1.2f));
            }
        }
        if (h >= 60)
        {
            juce::String sub;
            if (auto* inst = ctx.getInstrument (track)) sub = inst->getName();
            const int n = ctx.getInserts (track).size();
            if (n > 0) sub << (sub.isEmpty() ? "" : U ("  ·  ")) << n << (n > 1 ? " inserts" : " insert");
            g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
            g.drawText (sub, 34 + juce::jmin (ind, 40), 46, headerW - 54 - juce::jmin (ind, 40), 14, juce::Justification::centredLeft, true);
        }
        // Vumètre de piste (bord droit de l'en-tête).
        {
            auto m = juce::Rectangle<float> ((float) headerW - 10, 4.0f, 5.0f, (float) h - 8);
            g.setColour (theme::display); g.fillRect (m);
            const float n = juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 66.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2fb85a), 0, m.getBottom(), theme::rec, 0, m.getY(), false));
            g.fillRect (m.withTop (m.getBottom() - n * m.getHeight()));
        }
        g.setColour (theme::line); g.drawVerticalLine (headerW - 1, 0.0f, (float) getHeight());
        }

        // Zone des événements.
        auto lane = juce::Rectangle<int> (headerW, 0, getWidth() - headerW, h);
        g.setColour (selected ? juce::Colour (0xff25282c) : theme::bg); g.fillRect (lane);
        paintGrid (g, ctx, owner, lane, getWidth());

        g.saveState();
        g.reduceClipRegion (lane);
        const bool dim = hasFailure();
        // Seuls les événements qui touchent la zone à redessiner sont dessinés.
        const auto dirty = g.getClipBounds();
        for (auto* clip : track.getClips())
        {
            const float cx0 = owner.timeToX (clip->getPosition().getStart().inSeconds()), cx1 = owner.timeToX (clip->getPosition().getEnd().inSeconds());
            if (cx1 < (float) dirty.getX() - 2.0f || cx0 > (float) dirty.getRight() + 2.0f) continue;
            paintClip (g, *clip, lane, col, dim);
        }
        if (drawPreview.getLength() > 0)
        {
            const float x0 = owner.timeToX (drawPreview.getStart()), x1 = owner.timeToX (drawPreview.getEnd());
            g.setColour (col.withAlpha (0.5f)); g.fillRect (x0, (float) lane.getY() + 2, x1 - x0, (float) lane.getHeight() - 4);
        }
        if (track.getClips().isEmpty() && ! dim)
        {
            g.setColour (theme::dim); g.setFont (theme::ui (11.5f));
            g.drawText (ctx.getInstrument (track) != nullptr ? U ("Outil Crayon (8) ou double-clic : créer un conteneur MIDI")
                                                              : U ("Glissez un fichier audio ici, ou armez la piste (●) et enregistrez"),
                        lane.reduced (12, 0), juce::Justification::centredLeft);
        }
        g.restoreState();

        g.setColour (theme::line);
        g.drawHorizontalLine (h - 1, 0.0f, (float) getWidth());

        if (autoShown()) paintAutomation (g);
        if (dim) paintBanner (g);
        // Poignée de hauteur de piste.
        g.setColour (juce::Colours::white.withAlpha (0.04f)); g.fillRect (0, h - 3, headerW, 2);
    }

    void paintClip (juce::Graphics& g, te::Clip& clip, juce::Rectangle<int> lane, juce::Colour col, bool dim)
    {
        const auto pos = clip.getPosition().time;
        const float x0 = owner.timeToX (pos.getStart().inSeconds()), x1 = owner.timeToX (pos.getEnd().inSeconds());
        if (x1 < headerW || x0 > (float) getWidth()) return;
        juce::Rectangle<float> c (x0, (float) lane.getY() + 1, juce::jmax (3.0f, x1 - x0), (float) lane.getHeight() - 2);
        const bool sel = ctx.isClipSelected (clip);
        const bool muted = clip.isMuted();
        auto base = muted ? juce::Colour (0xff5a5d62) : col;
        float a = dim ? 0.35f : 1.0f;
        auto body = base.withMultipliedSaturation (0.75f).withMultipliedBrightness (sel ? 0.95f : 0.62f).withAlpha (a);
        g.setColour (body); g.fillRect (c);
        auto title = c.removeFromTop (juce::jmin (15.0f, c.getHeight() * 0.3f));
        g.setColour (base.withMultipliedBrightness (sel ? 1.15f : 0.9f).withAlpha (a)); g.fillRect (title);
        g.setColour (juce::Colour (0xff0e0f11).withAlpha (a)); g.setFont (theme::ui (11.0f, true));
        g.drawText ((muted ? U ("[muet] ") : juce::String()) + clip.getName(), title.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);

        auto inner = c.reduced (2.0f, 2.0f);
        const auto ink = juce::Colour (0xff0b0c0e).withAlpha ((muted ? 0.35f : 0.72f) * a);
        g.setColour (ink);
        if (auto* wac = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            if (auto* peaks = peaksFor (sourceFile (*wac)))
            {
                const auto* pk = &peaks->values;
                const double fileLen = juce::jmax (0.001, peaks->seconds);
                const double off = wac->getPosition().getOffset().inSeconds();
                const float mid = inner.getCentreY(), half = inner.getHeight() * 0.5f;
                const int ix0 = (int) juce::jmax ((float) headerW, inner.getX()), ix1 = (int) juce::jmin ((float) getWidth(), inner.getRight());
                juce::Path w;
                auto amp = [&] (int px)
                {
                    const double t = off + owner.xToTime ((float) px) - pos.getStart().inSeconds();
                    const size_t idx = (size_t) juce::jlimit (0.0, (double) pk->size() - 1, t / fileLen * (double) pk->size());
                    return juce::jmin (1.0f, (*pk)[idx]) * half;
                };
                if (ix1 > ix0)
                {
                    w.startNewSubPath ((float) ix0, mid);
                    for (int i = ix0; i <= ix1; ++i) w.lineTo ((float) i, mid - amp (i));
                    for (int i = ix1; i >= ix0; --i) w.lineTo ((float) i, mid + amp (i));
                    w.closeSubPath();
                    g.fillPath (w);
                }
                g.setColour (ink.withAlpha (0.3f)); g.drawHorizontalLine ((int) mid, inner.getX(), inner.getRight());
            }
        }
        else if (auto* mc = dynamic_cast<te::MidiClip*> (&clip))
        {
            auto& seq = mc->getSequence();
            auto range = seq.getNoteNumberRange();
            const int lo = range.getStart() - 1, span = juce::jmax (8, range.getLength() + 2);
            const float nh = juce::jlimit (1.5f, 5.0f, inner.getHeight() / span);
            for (auto* n : seq.getNotes())
            {
                const auto tr = n->getEditTimeRange (*mc);
                const float nx0 = owner.timeToX (tr.getStart().inSeconds()), nx1 = owner.timeToX (tr.getEnd().inSeconds());
                const float ny = inner.getBottom() - (n->getNoteNumber() - lo) / (float) span * inner.getHeight();
                g.fillRect (nx0, ny - nh * 0.5f, juce::jmax (2.0f, nx1 - nx0 - 1.0f), nh);
            }
        }
        g.setColour (sel ? juce::Colours::white : juce::Colour (0xff0e0f11).withAlpha (0.8f));
        g.drawRect (juce::Rectangle<float> (x0, (float) lane.getY() + 1, juce::jmax (3.0f, x1 - x0), (float) lane.getHeight() - 2), sel ? 1.5f : 1.0f);
        if (sel)
        {
            // Poignées de redimensionnement.
            g.setColour (juce::Colours::white);
            g.fillRect (x0 + 1, (float) lane.getBottom() - 8, 4.0f, 6.0f);
            g.fillRect (x1 - 5, (float) lane.getBottom() - 8, 4.0f, 6.0f);
        }
        // Fondus et gain des événements audio (poignées en haut, comme dans une station pro).
        if (auto* w = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            const float top = (float) lane.getY() + 1, bottom = (float) lane.getBottom() - 1;
            const float fi = (float) (w->getFadeIn().inSeconds() * owner.pxPerSecond), fo = (float) (w->getFadeOut().inSeconds() * owner.pxPerSecond);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            if (fi > 1.0f) { juce::Path p; p.addTriangle (x0, top, x0 + fi, top, x0, bottom); g.fillPath (p); }
            if (fo > 1.0f) { juce::Path p; p.addTriangle (x1, top, x1 - fo, top, x1, bottom); g.fillPath (p); }
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            if (fi > 1.0f) g.drawLine (x0, bottom, x0 + fi, top, 1.2f);
            if (fo > 1.0f) g.drawLine (x1 - fo, top, x1, bottom, 1.2f);
            if (sel || fi > 1.0f || fo > 1.0f)
            {
                g.fillRect (x0 + fi - 3.0f, top, 6.0f, 6.0f);
                g.fillRect (x1 - fo - 3.0f, top, 6.0f, 6.0f);
            }
            if (sel) g.fillRect ((x0 + x1) * 0.5f - 3.0f, top, 6.0f, 6.0f);   // poignée de gain
            if (std::abs (w->getGainDB()) > 0.05f || w->getIsReversed() || std::abs (w->getPitchChange()) > 0.01f || w->getAutoTempo())
            {
                juce::String tag;
                if (std::abs (w->getGainDB()) > 0.05f) tag << (w->getGainDB() > 0 ? "+" : "") << juce::String (w->getGainDB(), 1) << " dB  ";
                if (std::abs (w->getPitchChange()) > 0.01f) tag << (w->getPitchChange() > 0 ? "+" : "") << juce::roundToInt (w->getPitchChange()) << " dt  ";
                if (w->getIsReversed()) tag << U ("inversé");
                if (w->getAutoTempo()) tag << U ("  ♪ musical");
                g.setColour (juce::Colour (0xff0e0f11)); g.setFont (theme::ui (10.0f, true));
                g.drawText (tag.trim(), juce::Rectangle<float> (x0, top, juce::jmax (0.0f, x1 - x0 - 6.0f), 14.0f), juce::Justification::centredRight, false);
            }
        }
    }

    // Poignée de fondu ou de gain sous la souris ? (0 aucune, 1 fondu entrant, 2 fondu sortant, 3 gain)
    int audioHandleAt (te::Clip* c, juce::Point<int> p) const
    {
        auto* w = dynamic_cast<te::WaveAudioClip*> (c);
        if (w == nullptr || p.y > 12) return 0;
        const float x0 = owner.timeToX (w->getPosition().getStart().inSeconds()), x1 = owner.timeToX (w->getPosition().getEnd().inSeconds());
        const float fi = x0 + (float) (w->getFadeIn().inSeconds() * owner.pxPerSecond), fo = x1 - (float) (w->getFadeOut().inSeconds() * owner.pxPerSecond);
        if (std::abs ((float) p.x - fi) <= 6.0f) return 1;
        if (std::abs ((float) p.x - fo) <= 6.0f) return 2;
        if (ctx.isClipSelected (*w) && std::abs ((float) p.x - (x0 + x1) * 0.5f) <= 6.0f) return 3;
        return 0;
    }

    // --- Automation -------------------------------------------------------------
    float valueToY (float v, juce::Rectangle<int> a, te::AutomatableParameter& p) const
    {
        const auto vr = p.getValueRange();
        const float n = (v - vr.getStart()) / juce::jmax (0.0001f, vr.getLength());
        return (float) a.getBottom() - 4.0f - n * (float) (a.getHeight() - 8);
    }
    float yToValue (float y, juce::Rectangle<int> a, te::AutomatableParameter& p) const
    {
        const float n = juce::jlimit (0.0f, 1.0f, ((float) a.getBottom() - 4.0f - y) / (float) (a.getHeight() - 8));
        const auto vr = p.getValueRange();
        return vr.getStart() + n * vr.getLength();
    }
    juce::String valueText (float v) const
    {
        if (usePan) { const int p = juce::roundToInt (v * 100.0f); return p == 0 ? juce::String ("C") : (p < 0 ? "G " + juce::String (-p) : "D " + juce::String (p)); }
        const float db = te::volumeFaderPositionToDB (v);
        return db < -90.0f ? U ("-∞ dB") : juce::String (db, 1) + " dB";
    }

    void paintAutomation (juce::Graphics& g)
    {
        auto a = autoArea();
        auto* p = param();
        g.setColour (juce::Colour (0xff2f3237)); g.fillRect (a.withWidth (headerW));
        g.setColour (theme::readA.withAlpha (0.8f)); g.fillRect (0, a.getY(), 5, a.getHeight());
        auto lane = a.withTrimmedLeft (headerW);
        g.setColour (juce::Colour (0xff1b1d21)); g.fillRect (lane);
        paintGrid (g, ctx, owner, lane, getWidth());
        g.setColour (theme::line); g.drawHorizontalLine (a.getBottom() - 1, 0.0f, (float) getWidth());
        g.drawVerticalLine (headerW - 1, (float) a.getY(), (float) a.getBottom());
        if (p == nullptr) return;
        auto& curve = p->getCurve();
        g.setColour (theme::muted); g.setFont (theme::ui (11.0f));
        g.drawText (valueText (p->getCurrentValue()), 34, a.getY() + 32, headerW - 50, 16, juce::Justification::centredLeft);

        const auto line = juce::Colour (0xffe8d34a);
        juce::Path path;
        const int n = curve.getNumPoints();
        const int x0 = headerW, x1 = getWidth();
        for (int x = x0; x <= x1; x += 2)
        {
            const float v = n > 0 ? curve.getValueAt (te::TimePosition::fromSeconds (juce::jmax (0.0, owner.xToTime ((float) x))), p->getCurrentValue()) : p->getCurrentValue();
            const float y = valueToY (v, a, *p);
            if (x == x0) path.startNewSubPath ((float) x, y); else path.lineTo ((float) x, y);
        }
        juce::Path fill (path); fill.lineTo ((float) x1, (float) a.getBottom()); fill.lineTo ((float) x0, (float) a.getBottom()); fill.closeSubPath();
        g.setColour (line.withAlpha (0.10f)); g.fillPath (fill);
        g.setColour (n > 0 ? line : line.withAlpha (0.4f)); g.strokePath (path, juce::PathStrokeType (1.5f));
        for (int i = 0; i < n; ++i)
        {
            const float x = owner.timeToX (curve.getPointTime (i).inSeconds());
            if (x < headerW - 4 || x > getWidth() + 4) continue;
            const float y = valueToY (curve.getPointValue (i), a, *p);
            g.setColour (i == dragPoint ? juce::Colours::white : line); g.fillRect (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
            g.setColour (juce::Colours::black); g.drawRect (x - 3.0f, y - 3.0f, 6.0f, 6.0f, 1.0f);
        }
        if (n == 0)
        {
            g.setColour (theme::dim); g.setFont (theme::ui (11.0f));
            g.drawText (U ("Cliquez pour ajouter des points d'automation · double-clic / clic droit sur un point : supprimer"),
                        lane.reduced (10, 0).withTrimmedTop (a.getHeight() / 2), juce::Justification::centredLeft, true);
        }
    }

    int pointAt (juce::Point<int> pt) const
    {
        auto* p = param();
        if (p == nullptr) return -1;
        auto& curve = p->getCurve();
        for (int i = curve.getNumPoints(); --i >= 0;)
        {
            const float x = owner.timeToX (curve.getPointTime (i).inSeconds()), y = valueToY (curve.getPointValue (i), autoArea(), *p);
            if (std::abs ((float) pt.x - x) <= 6.0f && std::abs ((float) pt.y - y) <= 6.0f) return i;
        }
        return -1;
    }

    void paintBanner (juce::Graphics& g)
    {
        auto r = getLocalBounds().withTrimmedTop (trackH() + (autoShown() ? autoLaneH : 0));
        g.setColour (juce::Colour (0xff3a2a14)); g.fillRect (r);
        g.setColour (theme::warn); g.fillRect (r.withWidth (5));
        juce::String who, why;
        for (auto* sp : ctx.getSandboxPlugins (track))
            if (sp->isFailed()) { who = sp->getName(); why = sp->describeFailure(); break; }
        juce::AttributedString s;
        s.append (U ("« ") + who + U (" » ") + why + ". ", theme::ui (13.0f, true), theme::text);
        s.append (U ("Il tournait isolé : seule la piste « ") + track.getName() + U (" » se tait, la lecture et vos autres pistes continuent."),
                  theme::ui (13.0f), theme::text);
        s.setJustification (juce::Justification::centredLeft);
        s.setWordWrap (juce::AttributedString::byWord);
        s.draw (g, r.withTrimmedLeft (headerW + 14).withTrimmedRight (190).reduced (0, 6).toFloat());
        g.setColour (theme::warn); g.setFont (theme::ui (12.0f, true));
        g.drawText (U ("Plugin arrêté"), r.withWidth (headerW).reduced (16, 0), juce::Justification::centredLeft);
    }

    // --- Souris -------------------------------------------------------------------
    te::Clip* clipAt (float x) const
    {
        const double t = owner.xToTime (x);
        te::Clip* found = nullptr;
        for (auto* c : track.getClips())
            if (c->getPosition().time.contains (te::TimePosition::fromSeconds (t))) found = c;    // le dernier (dessus) gagne
        return found;
    }

    enum class Edge { none, left, right };
    Edge edgeAt (te::Clip* c, float x) const
    {
        if (c == nullptr) return Edge::none;
        const float x0 = owner.timeToX (c->getPosition().getStart().inSeconds()), x1 = owner.timeToX (c->getPosition().getEnd().inSeconds());
        if (x1 - x0 < 18) return x1 - x < 6 ? Edge::right : Edge::none;
        if (x - x0 < 7) return Edge::left;
        if (x1 - x < 7) return Edge::right;
        return Edge::none;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto cursor = juce::MouseCursor::NormalCursor;
        if (e.x < headerW && std::abs (e.y - (trackH() - 1)) <= 3) cursor = juce::MouseCursor::UpDownResizeCursor;
        else if (e.x >= headerW && e.y < trackH())
        {
            auto* c = clipAt ((float) e.x);
            switch (ctx.tool)
            {
                case Tool::select: if (edgeAt (c, (float) e.x) != Edge::none) cursor = juce::MouseCursor::LeftRightResizeCursor;
                                   else if (c) cursor = juce::MouseCursor::DraggingHandCursor; break;
                case Tool::split:  cursor = juce::MouseCursor::IBeamCursor; break;
                case Tool::draw:   cursor = juce::MouseCursor::CrosshairCursor; break;
                case Tool::zoom:   cursor = juce::MouseCursor::UpDownLeftRightResizeCursor; break;
                case Tool::erase:  case Tool::mute: case Tool::glue: cursor = c ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor; break;
                case Tool::range:  cursor = juce::MouseCursor::IBeamCursor; break;
            }
        }
        else if (autoShown() && autoArea().contains (e.getPosition()) && e.x >= headerW)
            cursor = pointAt (e.getPosition()) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor;
        setMouseCursor (cursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! trackAlive()) return;
        mode = Mode::none;
        auto& um = ctx.edit->getUndoManager();
        const int h = trackH();

        // En-tête : hauteur, sélection, menu.
        if (e.x < headerW)
        {
            if (std::abs (e.y - (h - 1)) <= 3) { mode = Mode::resizeRow; dragValue = h; return; }
            ctx.selectTrack (&track);
            if (e.mods.isPopupMenu()) { showHeaderMenu(); return; }
            if (e.getNumberOfClicks() == 2 && nameLabel.getBounds().contains (e.getPosition())) { editNameLater (nameLabel); return; }
            mode = Mode::headerPress;            // glisser l'en-tête : déplacer la piste
            return;
        }
        // Piste d'automation.
        if (autoShown() && autoArea().contains (e.getPosition()))
        {
            auto* p = param();
            if (p == nullptr) return;
            auto& curve = p->getCurve();
            const int i = pointAt (e.getPosition());
            if (i >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() == 2))
            {
                um.beginNewTransaction (U ("Supprimer un point d'automation"));
                curve.removePoint (i, &um);
                dragPoint = -1; repaint(); return;
            }
            if (i < 0 && e.mods.isPopupMenu()) { showAutomationMenu(); return; }
            um.beginNewTransaction (U ("Automation"));
            if (i >= 0) dragPoint = i;
            else
            {
                const double t = owner.snapTime (owner.xToTime ((float) e.x), e.mods);
                dragPoint = curve.addPoint (te::TimePosition::fromSeconds (t), yToValue ((float) e.y, autoArea(), *p), 0.0f, &um);
            }
            mode = Mode::automation;
            repaint();
            return;
        }
        if (e.y >= h) return;     // bandeau

        auto* clip = clipAt ((float) e.x);
        ctx.selectTrack (&track);
        const double t = owner.xToTime ((float) e.x);
        if (e.mods.isPopupMenu()) { if (clip) { ctx.selectClip (clip); showClipMenu (*clip); } else showLaneMenu (t); return; }

        switch (ctx.tool)
        {
            case Tool::zoom:
                owner.zoom (e.mods.isAltDown() ? 0.6 : 1.6, t);
                return;
            case Tool::erase:
                if (clip) { um.beginNewTransaction ("Gomme"); ctx.selectClip (nullptr); clip->removeFromParent(); ctx.changed(); }
                return;
            case Tool::mute:
                if (clip) { um.beginNewTransaction ("Muet"); clip->setMuted (! clip->isMuted()); ctx.selectClip (clip); }
                return;
            case Tool::split:
                if (clip)
                {
                    const double st = owner.snapTime (t, e.mods);
                    if (clip->getPosition().time.contains (te::TimePosition::fromSeconds (st)) && st > clip->getPosition().getStart().inSeconds() + 0.001)
                    {
                        um.beginNewTransaction ("Ciseaux");
                        track.splitClip (*clip, te::TimePosition::fromSeconds (st));
                        ctx.changed();
                    }
                }
                return;
            case Tool::glue:
                if (clip) { ctx.selectClip (clip); ctx.perform ("clip.glue"); }
                return;
            case Tool::range:
                ctx.selectClip (nullptr);
                mode = Mode::range;
                dragValue = owner.snapTime (t, e.mods);
                owner.rangeSelection = { dragValue, dragValue };
                return;
            case Tool::draw:
                if (clip == nullptr && ctx.getInstrument (track) != nullptr)
                {
                    mode = Mode::draw;
                    dragValue = ctx.snapToGrid (t, true);
                    drawPreview = { dragValue, ctx.toSeconds (ctx.toBeats (dragValue) + juce::jmax (1.0, ctx.gridBeats)) };
                    repaint();
                    return;
                }
                break;
            case Tool::select:
                break;
        }

        if (clip == nullptr)
        {
            owner.rangeSelection = {};
            if (e.getNumberOfClicks() == 2 && ctx.getInstrument (track) != nullptr)
            {
                um.beginNewTransaction (U ("Nouveau conteneur MIDI"));
                const double start = ctx.barStart (t);
                if (auto mc = track.insertMIDIClip ("Conteneur MIDI", { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (ctx.addBars (start, 1.0)) }, nullptr))
                    ctx.selectClip (mc.get());
                return;
            }
            // Rectangle de sélection.
            if (! e.mods.isShiftDown()) ctx.selectClip (nullptr);
            baseSelection = ctx.getSelectedClips();
            mode = Mode::rubber;
            rubberStart = e.getEventRelativeTo (owner.holder.get()).getPosition();
            return;
        }
        if (e.mods.isShiftDown()) { ctx.toggleClipSelection (clip); return; }
        if (! ctx.isClipSelected (*clip)) ctx.selectClip (clip);
        else { ctx.selectedClipID = clip->itemID; ctx.changed(); }
        if (e.getNumberOfClicks() == 2) { if (ctx.showView) ctx.showView (dynamic_cast<te::MidiClip*> (clip) != nullptr ? 2 : 3); return; }

        if (const int handle = audioHandleAt (clip, e.getPosition()))
        {
            um.beginNewTransaction (handle == 3 ? U ("Gain de l'événement") : U ("Fondu"));
            dragClip = clip;
            dragValue = static_cast<te::WaveAudioClip*> (clip)->getGainDB();
            mode = handle == 1 ? Mode::fadeIn : handle == 2 ? Mode::fadeOut : Mode::clipGain;
            return;
        }

        um.beginNewTransaction (U ("Modifier l'événement"));
        dragClip = clip;
        dragStart = clip->getPosition().getStart().inSeconds();
        dragLength = clip->getPosition().getLength().inSeconds();
        copied = false;
        groupIDs.clearQuick(); groupStarts.clearQuick();
        for (auto* c : ctx.getSelectedClips()) { groupIDs.add (c->itemID); groupStarts.add (c->getPosition().getStart().inSeconds()); }
        const auto edge = edgeAt (clip, (float) e.x);
        mode = edge == Edge::left ? Mode::trimStart : edge == Edge::right ? Mode::trimEnd : Mode::move;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double dt = e.getDistanceFromDragStartX() / owner.pxPerSecond;
        auto& um = ctx.edit->getUndoManager();
        if (auto* w = dynamic_cast<te::WaveAudioClip*> (dragClip); w != nullptr && (mode == Mode::fadeIn || mode == Mode::fadeOut || mode == Mode::clipGain))
        {
            const double t = owner.xToTime ((float) e.x);
            if (mode == Mode::fadeIn)  w->setFadeIn (te::TimeDuration::fromSeconds (juce::jmax (0.0, t - w->getPosition().getStart().inSeconds())));
            if (mode == Mode::fadeOut) w->setFadeOut (te::TimeDuration::fromSeconds (juce::jmax (0.0, w->getPosition().getEnd().inSeconds() - t)));
            if (mode == Mode::clipGain)
            {
                w->setGainDB ((float) juce::jlimit (-60.0, 24.0, dragValue - e.getDistanceFromDragStartY() * 0.25));
                if (ctx.setStatus) ctx.setStatus (U ("Gain de « ") + w->getName() + U (" » : ") + juce::String (w->getGainDB(), 1) + " dB");
            }
            repaint();
            return;
        }
        switch (mode)
        {
            case Mode::headerPress:
                if (e.getDistanceFromDragStart() > 6) { mode = Mode::trackDrag; owner.beginTrackDrag (track); }
                return;
            case Mode::trackDrag:
                owner.updateTrackDrag (e.getEventRelativeTo (owner.holder.get()).getPosition());
                return;
            case Mode::resizeRow:
                owner.rowHeights[track.itemID.toString()] = juce::jlimit (minRowH, 300, (int) dragValue + e.getDistanceFromDragStartY());
                owner.layoutRows();
                return;
            case Mode::automation:
            {
                auto* p = param();
                if (p == nullptr || dragPoint < 0) return;
                const double t = owner.snapTime (juce::jmax (0.0, owner.xToTime ((float) e.x)), e.mods);
                dragPoint = p->getCurve().movePoint (*p, dragPoint, te::TimePosition::fromSeconds (t), yToValue ((float) e.y, autoArea(), *p), false, &um);
                repaint();
                return;
            }
            case Mode::range:
            {
                const double t = owner.snapTime (juce::jmax (0.0, owner.xToTime ((float) e.x)), e.mods);
                owner.rangeSelection = { juce::jmin (dragValue, t), juce::jmax (dragValue, t) };
                owner.repaint();
                return;
            }
            case Mode::draw:
            {
                const double end = juce::jmax (ctx.toSeconds (ctx.toBeats (dragValue) + juce::jmin (1.0, ctx.gridBeats)), ctx.snapToGrid (owner.xToTime ((float) e.x), true));
                drawPreview = { dragValue, end };
                repaint();
                return;
            }
            case Mode::rubber:
            {
                const auto p = e.getEventRelativeTo (owner.holder.get()).getPosition();
                owner.rubber = juce::Rectangle<int> (rubberStart, p);
                juce::Array<te::Clip*> sel (baseSelection);
                for (auto* r : owner.rows)
                    for (auto* c : r->track.getClips())
                    {
                        const float x0 = owner.timeToX (c->getPosition().getStart().inSeconds()), x1 = owner.timeToX (c->getPosition().getEnd().inSeconds());
                        if (owner.rubber.toFloat().intersects (juce::Rectangle<float> (x0, (float) r->getY(), x1 - x0, (float) r->trackH()))) sel.addIfNotAlreadyThere (c);
                    }
                ctx.setClipSelection (sel);
                owner.repaint();
                return;
            }
            default: break;
        }
        if (dragClip == nullptr) return;
        if (mode == Mode::move)
        {
            if (! copied && e.mods.isAltDown() && e.getDistanceFromDragStart() > 4)
            {
                // Alt + glisser : copie (de toute la sélection).
                juce::Array<te::Clip*> copies;
                for (auto& id : groupIDs)
                    if (auto* c = te::findClipForID (*ctx.edit, id))
                        if (auto copy = te::duplicateClip (*c)) { copies.add (copy.get()); if (c == dragClip) dragClip = copy.get(); }
                ctx.setClipSelection (copies);
                groupIDs.clearQuick();
                for (auto* c : copies) groupIDs.add (c->itemID);
                copied = true;
            }
            const double delta = owner.snapTime (dragStart + dt, e.mods) - dragStart;
            for (int i = 0; i < groupIDs.size(); ++i)
                if (auto* c = te::findClipForID (*ctx.edit, groupIDs[i]))
                    c->setStart (te::TimePosition::fromSeconds (juce::jmax (0.0, groupStarts[i] + delta)), false, true);
            // Déplacement vertical vers une autre piste (un seul événement).
            if (groupIDs.size() <= 1)
            {
                auto pos = e.getEventRelativeTo (owner.holder.get()).getPosition();
                for (auto* r : owner.rows)
                    if (r != this && r->getBounds().contains (pos) && dragClip->getTrack() != &r->track)
                        if (dragClip->moveTo (r->track)) { ctx.selectTrack (&r->track); ctx.selectClip (dragClip); break; }
            }
        }
        else if (mode == Mode::trimEnd)
        {
            const double newEnd = owner.snapTime (dragStart + dragLength + dt, e.mods);
            if (newEnd > dragStart + 0.02) dragClip->setLength (te::TimeDuration::fromSeconds (newEnd - dragStart), true);
        }
        else if (mode == Mode::trimStart)
        {
            const double minStart = dragStart - dragClipOffsetLimit();
            const double newStart = juce::jlimit (juce::jmax (0.0, minStart), dragStart + dragLength - 0.02, owner.snapTime (dragStart + dt, e.mods));
            dragClip->setStart (te::TimePosition::fromSeconds (newStart), true, false);
        }
        owner.holder->repaint();
    }

    double dragClipOffsetLimit() const
    {
        // Un événement audio ne peut pas commencer avant le début de son fichier.
        if (auto* w = dynamic_cast<te::WaveAudioClip*> (dragClip)) return w->getPosition().getOffset().inSeconds();
        return 1.0e6;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == Mode::range && owner.rangeSelection.getLength() > 0.001)
        {
            ctx.edit->getTransport().setLoopRange ({ te::TimePosition::fromSeconds (owner.rangeSelection.getStart()), te::TimePosition::fromSeconds (owner.rangeSelection.getEnd()) });
            if (ctx.setStatus) ctx.setStatus (U ("Plage sélectionnée : ") + ctx.formatBars (owner.rangeSelection.getStart(), false).trim() + U (" → ")
                                              + ctx.formatBars (owner.rangeSelection.getEnd(), false).trim() + U (" (délimiteurs placés)"));
        }
        if (mode == Mode::draw && drawPreview.getLength() > 0.01)
        {
            ctx.edit->getUndoManager().beginNewTransaction (U ("Nouveau conteneur MIDI"));
            if (auto mc = track.insertMIDIClip ("Conteneur MIDI", { te::TimePosition::fromSeconds (drawPreview.getStart()), te::TimePosition::fromSeconds (drawPreview.getEnd()) }, nullptr))
                ctx.selectClip (mc.get());
        }
        drawPreview = {};
        if (mode == Mode::trackDrag) { mode = Mode::none; owner.endTrackDrag(); return; }
        if (mode == Mode::rubber) { owner.rubber = {}; owner.repaint(); }
        dragClip = nullptr; mode = Mode::none; dragPoint = -1;
        baseSelection.clear();
        repaint();
    }

    void showHeaderMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Renommer");
        m.addItem (2, U ("Afficher l'automation"), true, autoShown());
        m.addSeparator();
        m.addItem (3, U ("Ajouter une piste audio"));
        m.addItem (4, U ("Ajouter une piste instrument"));
        m.addSeparator();
        // Dossiers.
        juce::PopupMenu into;
        juce::Array<te::FolderTrack*> folders;
        for (auto* f : te::getTracksOfType<te::FolderTrack> (*ctx.edit, true))
            if (f != track.getParentFolderTrack()) { folders.add (f); into.addItem (100 + folders.size() - 1, f->getName()); }
        m.addItem (6, U ("Nouveau dossier contenant cette piste"));
        m.addItem (7, U ("Nouveau groupe (sous-mixage) contenant cette piste"));
        if (! folders.isEmpty()) m.addSubMenu (U ("Ranger dans le dossier"), into);
        if (track.getParentFolderTrack() != nullptr) m.addItem (8, U ("Sortir du dossier « ") + track.getParentFolderTrack()->getName() + U (" »"));
        m.addSeparator();
        m.addItem (5, U ("Supprimer la piste"));
        m.showMenuAsync ({}, [this, folders] (int r)
        {
            auto& um = ctx.edit->getUndoManager();
            if (r == 1) nameLabel.showEditor();
            if (r == 2) ctx.toggleAutomation (track);
            if (r == 3) ctx.perform ("track.addAudio");
            if (r == 4) ctx.perform ("track.addInst");
            if (r == 5) ctx.perform ("track.delete");
            if (r == 6 || r == 7) { um.beginNewTransaction ("Dossier"); ctx.selectTrack (&track); ctx.addFolderTrack (r == 7, true); }
            if (r == 8) { um.beginNewTransaction (U ("Sortir du dossier")); ctx.moveTrackOutOfFolder (track); }
            if (r >= 100 && r - 100 < folders.size())
            {
                um.beginNewTransaction (U ("Ranger dans le dossier"));
                EditContext::expand (*folders[r - 100]);
                ctx.moveTrackIntoFolder (track, *folders[r - 100]);
            }
        });
    }

    void showClipMenu (te::Clip& clip)
    {
        juce::PopupMenu m, tools;
        for (int i = 0; i < 8; ++i) tools.addItem (100 + i, EditContext::toolName ((Tool) i), true, (int) ctx.tool == i);
        m.addSubMenu ("Outils", tools);
        m.addSeparator();
        const bool midi = dynamic_cast<te::MidiClip*> (&clip) != nullptr;
        if (midi) { m.addItem (1, U ("Ouvrir l'éditeur de notes  (Ctrl+E)")); m.addItem (2, U ("Quantifier  (Q)")); m.addSeparator(); }
        else
        {
            m.addItem (9, U ("Ouvrir l'éditeur audio  (Ctrl+E)"));
            if (auto* w = dynamic_cast<te::WaveAudioClip*> (&clip))
                m.addItem (11, U ("Mode musical : suit le tempo  (Ctrl+Maj+M)"), true, w->getAutoTempo());
            m.addItem (10, U ("Séparer les sources (voix, batterie, basse, autres)…  (Ctrl+Maj+U)"));
            m.addSeparator();
        }
        m.addItem (3, U ("Couper au curseur  (Alt+X)"));
        m.addItem (4, U ("Coller à l'événement suivant"));
        m.addItem (5, U ("Dupliquer  (Ctrl+D)"));
        m.addItem (6, clip.isMuted() ? U ("Rendre audible  (Maj+M)") : U ("Rendre muet  (Maj+M)"));
        m.addItem (7, U ("Délimiteurs sur l'événement  (P)"));
        m.addSeparator();
        m.addItem (8, U ("Supprimer  (Suppr)"));
        m.showMenuAsync ({}, [this] (int r)
        {
            if (r >= 100) { ctx.setTool ((Tool) (r - 100)); return; }
            const char* ids[] = { "", "midi.editor", "clip.quantise", "clip.split", "clip.glue", "clip.duplicate", "clip.mute", "transport.locators", "clip.delete",
                                  "midi.editor", "audio.separate", "audio.musical" };
            if (r > 0 && r < 12) ctx.perform (ids[r]);
        });
    }

    void showLaneMenu (double t)
    {
        juce::PopupMenu m, tools;
        for (int i = 0; i < 8; ++i) tools.addItem (100 + i, EditContext::toolName ((Tool) i), true, (int) ctx.tool == i);
        m.addSubMenu ("Outils", tools);
        m.addSeparator();
        if (ctx.getInstrument (track) != nullptr) m.addItem (1, U ("Créer un conteneur MIDI ici"));
        m.addItem (2, U ("Importer un fichier audio au curseur…"));
        m.addItem (3, U ("Placer le curseur ici"));
        m.addSeparator();
        m.addItem (4, U ("Ajouter une piste audio"));
        m.addItem (5, U ("Ajouter une piste instrument"));
        m.showMenuAsync ({}, [this, t] (int r)
        {
            if (r >= 100) { ctx.setTool ((Tool) (r - 100)); return; }
            if (r == 1)
            {
                ctx.edit->getUndoManager().beginNewTransaction (U ("Nouveau conteneur MIDI"));
                const double s = ctx.barStart (t);
                if (auto mc = track.insertMIDIClip ("Conteneur MIDI", { te::TimePosition::fromSeconds (s), te::TimePosition::fromSeconds (ctx.addBars (s, 1.0)) }, nullptr))
                    ctx.selectClip (mc.get());
            }
            if (r == 2) ctx.perform ("file.import");
            if (r == 3) ctx.edit->getTransport().setPosition (te::TimePosition::fromSeconds (juce::jmax (0.0, t)));
            if (r == 4) ctx.perform ("track.addAudio");
            if (r == 5) ctx.perform ("track.addInst");
        });
    }

    void showAutomationMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Volume", true, ! usePan);
        m.addItem (2, "Panoramique", true, usePan);
        m.addSeparator();
        m.addItem (3, U ("Effacer toute l'automation de ce paramètre"));
        m.showMenuAsync ({}, [this] (int r)
        {
            if (r == 1 || r == 2) { usePan = r == 2; sync(); repaint(); }
            if (r == 3) if (auto* p = param())
            {
                auto& um = ctx.edit->getUndoManager();
                um.beginNewTransaction (U ("Effacer l'automation"));
                auto& c = p->getCurve();
                for (int i = c.getNumPoints(); --i >= 0;) c.removePoint (i, &um);
                repaint();
            }
        });
    }

private:
    ArrangementView& owner;
    EditContext& ctx;
    juce::Label nameLabel;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" }, armBtn { U ("●") }, monBtn { U ("◉") }, autoBtn { "A" }, paramBtn, restartBtn { U ("Relancer le plugin") };
    enum class Mode { none, move, trimStart, trimEnd, resizeRow, automation, range, draw, rubber, headerPress, trackDrag, fadeIn, fadeOut, clipGain } mode = Mode::none;
    te::Clip* dragClip = nullptr;
    juce::Array<te::EditItemID> groupIDs;
    juce::Array<double> groupStarts;
    juce::Array<te::Clip*> baseSelection;
    juce::Point<int> rubberStart;
    double dragStart = 0, dragLength = 0, dragValue = 0;
    int dragPoint = -1;
    bool copied = false, wasFailed = false, wasAuto = false;
    juce::Range<double> drawPreview;
    te::Plugin::Ptr meterPlugin;
    te::LevelMeasurer::Client client;
    float level = -100.0f;
    int drawnMeterPx = -1;
};

//==============================================================================
// Piste de tempo : un point par changement de tempo (saut ou rampe), éditable à la souris.
class ArrangementView::TempoRow  : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TempoRow (ArrangementView& o) : owner (o), ctx (o.ctx)
    {
        setTooltip (U ("Clic : nouveau tempo · glisser un point : déplacer / changer le tempo · double-clic : supprimer · clic droit : saut ou rampe"));
    }

    static constexpr double lo = 30.0, hi = 250.0;
    juce::Rectangle<int> lane() const { return { headerW, 4, getWidth() - headerW, getHeight() - 8 }; }
    float bpmToY (double bpm) const   { auto l = lane(); return (float) l.getBottom() - (float) ((juce::jlimit (lo, hi, bpm) - lo) / (hi - lo)) * (float) l.getHeight(); }
    double yToBpm (float y) const     { auto l = lane(); return juce::jlimit (lo, hi, lo + (l.getBottom() - y) / (double) l.getHeight() * (hi - lo)); }

    int pointAt (juce::Point<int> p) const
    {
        auto& ts = ctx.edit->tempoSequence;
        for (int i = ts.getNumTempos(); --i >= 0;)
        {
            auto* t = ts.getTempo (i);
            const float x = owner.timeToX (t->getStartTime().inSeconds()), y = bpmToY (t->getBpm());
            if (std::abs ((float) p.x - x) <= 6.0f && std::abs ((float) p.y - y) <= 6.0f) return i;
        }
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        if (ctx.edit == nullptr) return;
        auto& ts = ctx.edit->tempoSequence;
        const auto line = juce::Colour (0xffe0533f);
        // En-tête.
        g.setColour (theme::header); g.fillRect (0, 0, headerW, getHeight());
        g.setColour (line); g.fillRect (0, 0, 5, getHeight());
        g.setColour (theme::text); g.setFont (theme::ui (13.0f, true));
        g.drawText (U ("Piste de tempo"), 14, 6, headerW - 24, 20, juce::Justification::centredLeft);
        const double now = ctx.edit->getTransport().getPosition().inSeconds();
        g.setColour (theme::lcd); g.setFont (theme::mono (15.0f, true));
        g.drawText (juce::String (ctx.bpmAt (now), 2) + " BPM", 14, 28, headerW - 24, 22, juce::Justification::centredLeft);
        g.setColour (theme::dim); g.setFont (theme::ui (10.5f));
        g.drawText (juce::String (ts.getNumTempos()) + U (" tempo(s) · clic dans la piste : ajouter"), 14, 52, headerW - 24, 16, juce::Justification::centredLeft);
        g.setColour (theme::line); g.drawVerticalLine (headerW - 1, 0.0f, (float) getHeight());

        // Zone de la courbe.
        auto l = juce::Rectangle<int> (headerW, 0, getWidth() - headerW, getHeight());
        g.setColour (juce::Colour (0xff1b1d21)); g.fillRect (l);
        g.setFont (theme::ui (9.5f));
        for (double b : { 60.0, 120.0, 180.0, 240.0 })
        {
            const float y = bpmToY (b);
            g.setColour (theme::lineSoft); g.drawHorizontalLine ((int) y, (float) headerW, (float) getWidth());
            g.setColour (theme::dim); g.drawText (juce::String ((int) b), headerW + 3, (int) y + 1, 30, 10, juce::Justification::left);
        }
        juce::Path p;
        for (int x = headerW; x <= getWidth(); x += 2)
        {
            const float y = bpmToY (ctx.bpmAt (juce::jmax (0.0, owner.xToTime ((float) x))));
            if (x == headerW) p.startNewSubPath ((float) x, y); else p.lineTo ((float) x, y);
        }
        juce::Path fill (p); fill.lineTo ((float) getWidth(), (float) getHeight()); fill.lineTo ((float) headerW, (float) getHeight()); fill.closeSubPath();
        g.setColour (line.withAlpha (0.12f)); g.fillPath (fill);
        g.setColour (line); g.strokePath (p, juce::PathStrokeType (1.6f));
        for (int i = 0; i < ts.getNumTempos(); ++i)
        {
            auto* t = ts.getTempo (i);
            const float x = owner.timeToX (t->getStartTime().inSeconds()), y = bpmToY (t->getBpm());
            if (x < headerW - 4 || x > getWidth() + 4) continue;
            g.setColour (i == dragIndex ? juce::Colours::white : line); g.fillRect (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour (juce::Colours::black); g.drawRect (x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.0f);
            g.setColour (theme::text); g.setFont (theme::ui (10.0f, true));
            g.drawText (juce::String (t->getBpm(), t->getBpm() == std::floor (t->getBpm()) ? 0 : 1) + (t->getCurve() < 0.99f ? U (" ↗") : juce::String()),
                        (int) x + 6, (int) y - 16, 60, 12, juce::Justification::left);
        }
        g.setColour (theme::line); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (ctx.edit == nullptr || e.x < headerW) return;
        auto& ts = ctx.edit->tempoSequence;
        auto& um = ctx.edit->getUndoManager();
        const int i = pointAt (e.getPosition());
        dragIndex = -1;
        if (i >= 0 && e.mods.isPopupMenu()) { showPointMenu (i); return; }
        if (i > 0 && e.getNumberOfClicks() == 2)
        {
            um.beginNewTransaction (U ("Supprimer un tempo"));
            ts.removeTempo (i, true);
            ctx.changed(); repaint(); return;
        }
        um.beginNewTransaction ("Tempo");
        if (i >= 0) dragIndex = i;
        else
        {
            if (e.mods.isPopupMenu()) return;
            // Nouveau tempo sur le temps le plus proche (ou la grille si l'aimant est actif).
            const double step = ctx.snapOn ? juce::jmax (1.0, ctx.gridBeats) : 1.0;
            const double beat = std::round (ctx.toBeats (owner.xToTime ((float) e.x)) / step) * step;
            ctx.changeTempo ([&] { ts.insertTempo (te::BeatPosition::fromBeats (juce::jmax (0.0, beat)), std::round (yToBpm ((float) e.y)), 1.0f); });
            for (int k = 0; k < ts.getNumTempos(); ++k)
                if (std::abs (ts.getTempo (k)->getStartBeat().inBeats() - beat) < 1.0e-6) dragIndex = k;
        }
        ctx.changed();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (ctx.edit == nullptr || dragIndex < 0) return;
        auto& ts = ctx.edit->tempoSequence;
        auto* t = ts.getTempo (dragIndex);
        if (t == nullptr) return;
        const double bpm = e.mods.isShiftDown() ? yToBpm ((float) e.y) : std::round (yToBpm ((float) e.y));
        ctx.changeTempo ([&]
        {
        t->setBpm (bpm);
        if (dragIndex > 0)
        {
            // Déplacement horizontal, entre ses voisins, calé sur les temps.
            const double prev = ts.getTempo (dragIndex - 1)->getStartBeat().inBeats() + 1.0;
            const double next = dragIndex + 1 < ts.getNumTempos() ? ts.getTempo (dragIndex + 1)->getStartBeat().inBeats() - 1.0 : 1.0e6;
            const double beat = juce::jlimit (prev, juce::jmax (prev, next), std::round (ctx.toBeats (juce::jmax (0.0, owner.xToTime ((float) e.x)))));
            if (std::abs (beat - t->getStartBeat().inBeats()) > 1.0e-6) t->setStartBeat (te::BeatPosition::fromBeats (beat));
        }
        });
        ctx.changed();
        owner.repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragIndex = -1; repaint(); }

    void showPointMenu (int index)
    {
        auto* t = ctx.edit->tempoSequence.getTempo (index);
        juce::PopupMenu m;
        m.addItem (1, U ("Saut (changement immédiat)"), true, t->getCurve() >= 0.99f);
        m.addItem (2, U ("Rampe (accélération progressive jusqu'au point suivant)"), true, t->getCurve() < 0.99f);
        m.addSeparator();
        m.addItem (3, U ("Saisir le tempo…"));
        m.addItem (4, U ("Supprimer ce tempo"), index > 0);
        m.showMenuAsync ({}, [this, index] (int r)
        {
            if (ctx.edit == nullptr || r <= 0) return;
            auto& ts = ctx.edit->tempoSequence;
            auto* t = ts.getTempo (index);
            if (t == nullptr) return;
            auto& um = ctx.edit->getUndoManager();
            um.beginNewTransaction ("Tempo");
            if (r == 1) ctx.changeTempo ([&] { t->setCurve (1.0f); });
            if (r == 2) ctx.changeTempo ([&] { t->setCurve (0.0f); });
            if (r == 4) ts.removeTempo (index, true);
            if (r == 3)
            {
                auto* w = new juce::AlertWindow ("Tempo", U ("Tempo (BPM) :"), juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("bpm", juce::String (t->getBpm(), 2));
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, index, w] (int res)
                {
                    const double bpm = w->getTextEditorContents ("bpm").replaceCharacter (',', '.').getDoubleValue();
                    if (res == 1 && bpm >= 20 && bpm <= 400 && ctx.edit != nullptr)
                        if (auto* tt = ctx.edit->tempoSequence.getTempo (index)) { ctx.edit->getUndoManager().beginNewTransaction ("Tempo"); ctx.changeTempo ([&] { tt->setBpm (bpm); }); }
                }), true);
            }
            ctx.changed();
            owner.repaint();
        });
    }

private:
    ArrangementView& owner;
    EditContext& ctx;
    int dragIndex = -1;
};

//==============================================================================
// Dossier de pistes (ou piste de groupe = dossier de sous-mixage) : repli, aperçu du contenu, muet/solo, volume du groupe.
class ArrangementView::FolderRow  : public juce::Component
{
public:
    FolderRow (ArrangementView& o, te::FolderTrack& f) : owner (o), ctx (o.ctx), folder (f)
    {
        depth = EditContext::depthOf (f);
        addAndMakeVisible (nameLabel);
        nameLabel.setFont (theme::ui (13.0f, true));
        nameLabel.setColour (juce::Label::textColourId, theme::text);
        nameLabel.setEditable (false, true, false);
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.onTextChange = [this] { ctx.edit->getUndoManager().beginNewTransaction ("Renommer"); folder.setName (nameLabel.getText()); ctx.changed(); };
        styleStateButton (muteBtn, theme::mute); styleStateButton (soloBtn, theme::solo);
        muteBtn.setTooltip (U ("Rendre muet tout le dossier")); soloBtn.setTooltip (U ("Solo du dossier"));
        muteBtn.onClick = [this] { folder.setMute (muteBtn.getToggleState()); ctx.changed(); };
        soloBtn.onClick = [this] { folder.setSolo (soloBtn.getToggleState()); ctx.changed(); };
        addAndMakeVisible (muteBtn); addAndMakeVisible (soloBtn);
        if (folder.isSubmixFolder())
        {
            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (-60.0, 6.0, 0.1); volume.setSkewFactorFromMidPoint (-12.0); volume.setDoubleClickReturnValue (true, 0.0);
            volume.setWantsKeyboardFocus (false);
            volume.setTooltip (U ("Volume du groupe (toutes les pistes du dossier passent par lui) · double-clic : 0 dB"));
            volume.onDragStart = [this] { ctx.edit->getUndoManager().beginNewTransaction (U ("Volume du groupe")); };
            volume.onValueChange = [this] { if (auto* v = folder.getVolumePlugin()) v->setVolumeDb ((float) volume.getValue()); };
            addAndMakeVisible (volume);
        }
        sync();
    }

    te::FolderTrack& folder;
    int depth = 0;

    void sync()
    {
        muteBtn.setToggleState (folder.isMuted (false), juce::dontSendNotification);
        soloBtn.setToggleState (folder.isSolo (false), juce::dontSendNotification);
        if (! nameLabel.isBeingEdited()) nameLabel.setText (folder.getName(), juce::dontSendNotification);
        if (auto* v = folder.getVolumePlugin()) if (! volume.isMouseButtonDown()) volume.setValue (v->getVolumeDb(), juce::dontSendNotification);
    }

    int indent() const { return depth * indentW; }
    juce::Rectangle<int> triangleArea() const { return { indent() + 8, 4, 18, 18 }; }

    void resized() override
    {
        const int x0 = indent() + 30;
        nameLabel.setBounds (x0, 3, headerW - x0 - 64, 20);
        muteBtn.setBounds (headerW - 58, 4, 22, 18);
        soloBtn.setBounds (headerW - 33, 4, 22, 18);
        if (folder.isSubmixFolder()) volume.setBounds (x0, 24, headerW - x0 - 70, 16);
    }

    void paint (juce::Graphics& g) override
    {
        const bool selected = ctx.selectedFolderID == folder.itemID;
        const auto col = folder.getColour().isTransparent() ? juce::Colour (0xffb08a4a) : folder.getColour();
        g.setColour (selected ? juce::Colour (0xff4a5058) : juce::Colour (0xff2e3236)); g.fillRect (0, 0, headerW, getHeight());
        for (int d = 0; d < depth; ++d) { g.setColour (theme::lineBar); g.fillRect (d * indentW + 6, 0, 2, getHeight()); }
        g.setColour (col); g.fillRect (indent(), 0, 5, getHeight());
        // Triangle de repli.
        juce::Path tri;
        auto t = triangleArea().toFloat().reduced (4.0f);
        if (ctx.isCollapsed (folder)) tri.addTriangle (t.getX(), t.getY(), t.getX(), t.getBottom(), t.getRight(), t.getCentreY());
        else                          tri.addTriangle (t.getX(), t.getY(), t.getRight(), t.getY(), t.getCentreX(), t.getBottom());
        g.setColour (theme::text); g.fillPath (tri);
        // Icône dossier / groupe et résumé.
        auto icon = juce::Rectangle<float> ((float) indent() + 12.0f, 26.0f, 13.0f, 10.0f);
        g.setColour (col.withAlpha (0.9f)); g.fillRoundedRectangle (icon, 1.5f);
        g.fillRect (icon.getX(), icon.getY() - 2.0f, 6.0f, 3.0f);
        const auto subs = folder.getAllAudioSubTracks (true);
        g.setColour (theme::dim); g.setFont (theme::ui (10.5f));
        if (! folder.isSubmixFolder())
            g.drawText (juce::String (subs.size()) + (subs.size() > 1 ? " pistes" : " piste") + (ctx.isCollapsed (folder) ? U (" (replié)") : juce::String()),
                        indent() + 30, 24, headerW - indent() - 40, 16, juce::Justification::centredLeft);
        else if (auto* v = folder.getVolumePlugin())
        {
            g.setColour (theme::lcd); g.setFont (theme::mono (10.5f));
            g.drawText (juce::String (v->getVolumeDb(), 1) + " dB", headerW - 64, 24, 56, 16, juce::Justification::centredRight);
        }
        g.setColour (theme::line); g.drawVerticalLine (headerW - 1, 0.0f, (float) getHeight());

        // Aperçu des événements des pistes du dossier.
        auto lane = juce::Rectangle<int> (headerW, 0, getWidth() - headerW, getHeight());
        g.setColour (juce::Colour (0xff202226)); g.fillRect (lane);
        paintGrid (g, ctx, owner, lane, getWidth());
        g.saveState(); g.reduceClipRegion (lane);
        const int n = juce::jmax (1, juce::jmin (8, subs.size()));
        const float rowH = juce::jmin (7.0f, (getHeight() - 8.0f) / n);   // aperçu compact : ne ressemble pas à un événement
        for (int i = 0; i < juce::jmin (8, subs.size()); ++i)
        {
            auto tc = subs[i]->getColour().isTransparent() ? col : subs[i]->getColour();
            for (auto* c : subs[i]->getClips())
            {
                const float x0 = owner.timeToX (c->getPosition().getStart().inSeconds()), x1 = owner.timeToX (c->getPosition().getEnd().inSeconds());
                if (x1 < headerW || x0 > getWidth()) continue;
                g.setColour (tc.withAlpha (c->isMuted() ? 0.25f : 0.75f));
                g.fillRect (x0, 4.0f + i * rowH, juce::jmax (2.0f, x1 - x0), juce::jmax (1.5f, rowH - 1.0f));
            }
        }
        g.restoreState();
        g.setColour (theme::line); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = false;
        if (triangleArea().expanded (4).contains (e.getPosition())) { ctx.toggleCollapsed (folder); return; }
        if (e.x >= headerW && e.getNumberOfClicks() == 2) { ctx.toggleCollapsed (folder); return; }
        ctx.selectFolder (&folder);
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        if (e.getNumberOfClicks() == 2 && nameLabel.getBounds().contains (e.getPosition())) { editNameLater (nameLabel); return; }
        if (e.x < headerW) pressed = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! pressed) return;
        if (! dragging && e.getDistanceFromDragStart() > 6) { dragging = true; owner.beginTrackDrag (folder); }
        if (dragging) owner.updateTrackDrag (e.getEventRelativeTo (owner.holder.get()).getPosition());
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) owner.endTrackDrag();
        dragging = pressed = false;
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, U ("Renommer"));
        m.addItem (2, ctx.isCollapsed (folder) ? U ("Déplier") : U ("Replier"));
        m.addSeparator();
        m.addItem (3, U ("Ajouter une piste audio dans le dossier"));
        m.addItem (4, U ("Ajouter une piste instrument dans le dossier"));
        if (folder.getParentFolderTrack() != nullptr) m.addItem (5, U ("Sortir du dossier parent"));
        m.addSeparator();
        m.addItem (6, U ("Supprimer le dossier (et ses pistes)"));
        m.addItem (7, U ("Supprimer le dossier (garder ses pistes)"));
        m.showMenuAsync ({}, [this] (int r)
        {
            auto& um = ctx.edit->getUndoManager();
            if (r == 1) nameLabel.showEditor();
            if (r == 2) ctx.toggleCollapsed (folder);
            if (r == 3 || r == 4)
            {
                um.beginNewTransaction (U ("Ajouter une piste"));
                EditContext::expand (folder);
                auto* f = &folder;
                if (auto* t = (r == 3 ? ctx.addAudioTrack() : ctx.addInstrumentTrack())) ctx.moveTrackIntoFolder (*t, *f);
            }
            if (r == 5) { um.beginNewTransaction (U ("Déplacer")); ctx.moveTrackOutOfFolder (folder); }
            if (r == 6 || r == 7)
            {
                um.beginNewTransaction (U ("Supprimer le dossier"));
                te::FolderTrack::Ptr f (&folder);
                if (r == 7)
                    for (auto* sub : f->getAllSubTracks (false)) ctx.edit->moveTrack (sub, te::TrackInsertPoint (*f, true));
                ctx.selectedFolderID = {};
                ctx.selectClip (nullptr);
                ctx.edit->deleteTrack (f.get());
                ctx.changed();
            }
        });
    }

private:
    ArrangementView& owner;
    EditContext& ctx;
    juce::Label nameLabel;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" };
    juce::Slider volume;
    bool pressed = false, dragging = false;
};

//==============================================================================
ArrangementView::ArrangementView (EditContext& c) : ctx (c)
{
    holder = std::make_unique<RowsHolder> (*this);
    tempoRow = std::make_unique<TempoRow> (*this);
    holder->addChildComponent (*tempoRow);
    viewport.setViewedComponent (holder.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (12);
    addAndMakeVisible (viewport);
    addAndMakeVisible (hbar);
    hbar.addListener (this);
    hbar.setAutoHide (false);

    styleButton (addBtn);
    addBtn.setTooltip (U ("Ajouter une piste"));
    addBtn.onClick = [this] { showAddTrackMenu(); };
    addAndMakeVisible (addBtn);

    ctx.addChangeListener (this);
    rebuildRows();
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    if (watchedState.isValid()) watchedState.removeListener (this);
    ctx.removeChangeListener (this);
    hbar.removeListener (this);
    rows.clear();
    tempoRow = nullptr;
}

float ArrangementView::timeToX (double s) const  { return (float) headerW + (float) ((s - viewStart) * pxPerSecond); }
double ArrangementView::xToTime (float x) const  { return viewStart + (x - (float) headerW) / pxPerSecond; }

double ArrangementView::snapTime (double seconds, const juce::ModifierKeys& m) const
{
    // Ctrl maintenu : calage temporairement désactivé.
    return m.isCtrlDown() ? juce::jmax (0.0, seconds) : ctx.snapToGrid (seconds);
}

void ArrangementView::zoom (double factor, double anchor)
{
    const float ax = juce::jlimit ((float) headerW, (float) getWidth(), timeToX (anchor));
    pxPerSecond = juce::jlimit (2.0, 2000.0, pxPerSecond * factor);
    viewStart = juce::jmax (0.0, anchor - (ax - headerW) / pxPerSecond);
    updateScrollBar();
    repaint(); holder->repaint();
}

void ArrangementView::zoomToFit()
{
    if (ctx.edit == nullptr) return;
    const double len = juce::jmax (ctx.barSeconds() * 4, ctx.edit->getLength().inSeconds());
    pxPerSecond = juce::jlimit (2.0, 2000.0, (getWidth() - headerW - 40) / len);
    viewStart = 0.0;
    updateScrollBar();
    repaint(); holder->repaint();
}

int ArrangementView::rowHeightFor (const te::Track& t) const
{
    auto it = rowHeights.find (t.itemID.toString());
    return it != rowHeights.end() ? it->second : defaultRowH;
}

juce::String ArrangementView::trackSignature() const
{
    // Ordre d'affichage : dossiers (et leur état replié) puis pistes, avec leur parent.
    juce::String s;
    if (ctx.edit != nullptr)
        ctx.edit->visitAllTracksRecursive ([&] (te::Track& t)
        {
            if (t.isAudioTrack() || t.isFolderTrack())
            {
                s << t.itemID.toString() << (t.isFolderTrack() ? (ctx.isCollapsed (t) ? "F-" : "F+") : "") << "<"
                  << (t.getParentFolderTrack() ? t.getParentFolderTrack()->itemID.toString() : juce::String()) << ",";
            }
            return true;
        });
    return s;
}

void ArrangementView::rebuildRows()
{
    entries.clear();
    rows.clear();
    folderRows.clear();
    if (ctx.edit == nullptr) return;
    ctx.edit->visitAllTracksRecursive ([&] (te::Track& t)
    {
        // Pistes d'un dossier replié : masquées.
        for (auto* p = t.getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack())
            if (ctx.isCollapsed (*p)) return true;
        if (auto* f = dynamic_cast<te::FolderTrack*> (&t))
        {
            auto* fr = folderRows.add (new FolderRow (*this, *f));
            holder->addAndMakeVisible (fr);
            entries.add ({ fr, f, true });
        }
        else if (auto* a = dynamic_cast<te::AudioTrack*> (&t))
        {
            auto* r = rows.add (new TrackRow (*this, *a));
            r->depth = EditContext::depthOf (*a);
            holder->addAndMakeVisible (r);
            entries.add ({ r, a, false });
        }
        return true;
    });
    lastSignature = trackSignature();
    layoutRows();
}

void ArrangementView::layoutRows()
{
    int y = 0;
    const int w = viewport.getMaximumVisibleWidth();
    tempoRow->setVisible (ctx.tempoTrackShown);
    if (ctx.tempoTrackShown) { tempoRow->setBounds (0, 0, w, tempoRowH); y = tempoRowH; }
    for (auto& e : entries)
    {
        const int h = e.isFolder ? folderRowH : static_cast<TrackRow*> (e.comp)->preferredHeight();
        e.comp->setBounds (0, y, w, h);
        y += h;
    }
    holder->setSize (w, juce::jmax (y + 80, viewport.getHeight()));
}

void ArrangementView::beginTrackDrag (te::Track& t)
{
    draggedTrack = t.itemID;
    dropPoint.reset();
    dropIntoFolder = {};
    dropY = -1;
}

void ArrangementView::updateTrackDrag (juce::Point<int> p)
{
    if (! draggedTrack.isValid() || ctx.edit == nullptr) return;
    auto* dragged = te::findTrackForID (*ctx.edit, draggedTrack);
    dropPoint.reset(); dropIntoFolder = {}; dropY = -1;
    if (dragged == nullptr) return;
    auto isInside = [dragged] (te::Track* t)            // t est-il dragged ou l'un de ses enfants ?
    {
        for (auto* x = t; x != nullptr; x = x->getParentFolderTrack()) if (x == dragged) return true;
        return false;
    };
    for (auto& e : entries)
    {
        auto b = e.comp->getBounds();
        if (p.y < b.getY() || p.y >= b.getBottom()) continue;
        if (isInside (e.track)) return;                  // pas dans lui-même
        if (e.isFolder && p.y > b.getY() + b.getHeight() / 4 && p.y < b.getBottom() - b.getHeight() / 4)
        {
            dropIntoFolder = e.track->itemID;            // déposer DANS le dossier
            dropY = b.getCentreY();
        }
        else if (p.y < b.getCentreY()) { dropPoint = te::TrackInsertPoint (*e.track, true); dropY = b.getY(); }
        else                           { dropPoint = te::TrackInsertPoint (*e.track, false); dropY = b.getBottom(); }
        holder->repaint();
        return;
    }
    if (! entries.isEmpty() && p.y >= entries.getLast().comp->getBottom())
    {
        dropPoint = te::TrackInsertPoint (nullptr, te::getTopLevelTracks (*ctx.edit).getLast());
        dropY = entries.getLast().comp->getBottom();
    }
    holder->repaint();
}

void ArrangementView::endTrackDrag()
{
    if (ctx.edit != nullptr && draggedTrack.isValid())
        if (auto* dragged = te::findTrackForID (*ctx.edit, draggedTrack))
        {
            ctx.edit->getUndoManager().beginNewTransaction (U ("Déplacer la piste"));
            if (dropIntoFolder.isValid())
            {
                if (auto* f = dynamic_cast<te::FolderTrack*> (te::findTrackForID (*ctx.edit, dropIntoFolder)))
                {
                    EditContext::expand (*f);
                    ctx.moveTrackIntoFolder (*dragged, *f);
                    if (ctx.setStatus) ctx.setStatus (U ("« ") + dragged->getName() + U (" » rangée dans « ") + f->getName() + U (" »"));
                }
            }
            else if (dropPoint) { ctx.edit->moveTrack (dragged, *dropPoint); ctx.changed(); }
        }
    draggedTrack = {}; dropPoint.reset(); dropIntoFolder = {}; dropY = -1;
    holder->repaint();
}

void ArrangementView::updateScrollBar()
{
    if (ctx.edit == nullptr) return;
    const double visible = (getWidth() - headerW) / pxPerSecond;
    const double total = juce::jmax (ctx.edit->getLength().inSeconds(), ctx.edit->getTransport().getLoopRange().getEnd().inSeconds(), viewStart + visible) + visible * 0.5;
    hbar.setRangeLimits (0.0, total, juce::dontSendNotification);
    hbar.setCurrentRange (viewStart, visible, juce::dontSendNotification);
}

void ArrangementView::scrollBarMoved (juce::ScrollBar*, double start)
{
    viewStart = juce::jmax (0.0, start);
    repaint(); holder->repaint();
}

void ArrangementView::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (rulerH).withTrimmedBottom (scrollH));
    hbar.setBounds (headerW, getHeight() - scrollH, getWidth() - headerW, scrollH);
    addBtn.setBounds (8, 7, 26, 20);
    layoutRows();
    updateScrollBar();
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (trackSignature() != lastSignature) rebuildRows();
    for (auto* r : rows) r->sync();
    for (auto* f : folderRows) { f->sync(); f->repaint(); }
    layoutRows();
    holder->repaint();
}

void ArrangementView::showAddTrackMenu()
{
    juce::PopupMenu m, inst;
    m.addItem (1, U ("Piste audio  (Ctrl+T)"));
    m.addItem (2, U ("Piste instrument — 4OSC  (Ctrl+Maj+T)"));
    auto bi = ctx.catalog.getBuiltIns (true), vi = ctx.catalog.getVST3 (true);
    for (int i = 0; i < bi.size(); ++i) inst.addItem (100 + i, bi[i].name);
    if (! vi.isEmpty()) inst.addSectionHeader (U ("VST3 (isolés)"));
    for (int i = 0; i < vi.size(); ++i) inst.addItem (1000 + i, vi[i].name + "  -  " + vi[i].manufacturer);
    m.addSubMenu (U ("Piste instrument avec…"), inst);
    juce::PopupMenu fx;
    auto bf = ctx.catalog.getBuiltIns (false), vf = ctx.catalog.getVST3 (false);
    for (int i = 0; i < bf.size(); ++i) fx.addItem (3000 + i, bf[i].name);
    if (! vf.isEmpty()) fx.addSectionHeader (U ("VST3 (isolés)"));
    for (int i = 0; i < vf.size(); ++i) fx.addItem (4000 + i, vf[i].name + "  -  " + vf[i].manufacturer);
    m.addSeparator();
    m.addSubMenu (U ("Piste FX (bus d'effet) avec…"), fx);
    m.addSeparator();
    m.addItem (5, U ("Dossier"));
    m.addItem (6, U ("Groupe (dossier de sous-mixage)"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn), [this, bi, vi, bf, vf] (int r)
    {
        if (r <= 0 || ctx.edit == nullptr) return;
        ctx.edit->getUndoManager().beginNewTransaction (U ("Ajouter une piste"));
        if (r == 5 || r == 6) { ctx.addFolderTrack (r == 6, false); return; }
        if (r == 1) ctx.addAudioTrack();
        else if (r == 2) ctx.addInstrumentTrack();
        else if (r >= 4000) ctx.addFxTrack (&vf.getReference (r - 4000));
        else if (r >= 3000) ctx.addFxTrack (&bf.getReference (r - 3000));
        else ctx.addInstrumentTrack (r >= 1000 ? &vi.getReference (r - 1000) : &bi.getReference (r - 100));
        ctx.changed();
    });
}

void ArrangementView::timerCallback()
{
    if (trackSignature() != lastSignature) rebuildRows();
    if (++frame % 6 == 0) { for (auto* r : rows) r->sync(); for (auto* f : folderRows) f->sync(); }
    for (auto* r : rows) r->updateMeter();

    // Défilement automatique par pages pendant la lecture.
    if (ctx.edit != nullptr && ctx.edit->getTransport().isPlaying())
    {
        const double t = ctx.edit->getTransport().getPosition().inSeconds();
        const double visible = (getWidth() - headerW) / pxPerSecond;
        if (timeToX (t) > (float) getWidth() - 20 || t < viewStart) { viewStart = juce::jmax (0.0, t - visible * 0.05); updateScrollBar(); }
    }
    if (frame % 15 == 0) updateScrollBar();
    watchEdit();

    // Pistes : redessinées si le projet a changé, pendant un enregistrement (prise qui grandit), après un
    // défilement ou un zoom, et deux fois par seconde par sécurité. Le reste du temps, rien.
    const bool recording = ctx.edit != nullptr && ctx.edit->getTransport().isRecording();
    const bool moved = viewStart != lastViewStart || pxPerSecond != lastPxPerSecond;
    if (contentDirty || recording || moved || frame % 15 == 0)
    {
        contentDirty = false;
        lastViewStart = viewStart; lastPxPerSecond = pxPerSecond;
        repaint();
        holder->repaint();
        lastCursorX = -1.0f;
    }
    // Tête de lecture : seulement les bandes de l'ancienne et de la nouvelle position.
    if (ctx.edit != nullptr)
    {
        const float x = timeToX (ctx.edit->getTransport().getPosition().inSeconds());
        if (std::abs (x - lastCursorX) >= 0.5f)
        {
            if (lastCursorX >= 0.0f) repaint (juce::Rectangle<int> ((int) lastCursorX - 8, 0, 18, getHeight()));
            repaint (juce::Rectangle<int> ((int) x - 8, 0, 18, getHeight()));
            lastCursorX = x;
        }
    }
}

void ArrangementView::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    // La position de lecture et les réglages de transport changent sans cesse : pas de redessin pour eux.
    if (tree.hasType (te::IDs::TRANSPORT)) return;
    static const bool traceDirty = juce::SystemStats::getEnvironmentVariable ("ONDE_DIRTY_TRACE", {}).isNotEmpty();
    if (traceDirty) { std::printf ("redessin demandé par %s.%s\n", tree.getType().toString().toRawUTF8(), id.toString().toRawUTF8()); std::fflush (stdout); }
    contentDirty = true;
}

void ArrangementView::watchEdit()
{
    if (ctx.edit == watchedEdit) return;
    if (watchedState.isValid()) watchedState.removeListener (this);
    watchedEdit = ctx.edit;
    watchedState = ctx.edit != nullptr ? ctx.edit->state : juce::ValueTree();
    if (watchedState.isValid()) watchedState.addListener (this);
    contentDirty = true;
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    // En-tête de la liste des pistes.
    auto corner = juce::Rectangle<int> (0, 0, headerW, rulerH);
    g.setColour (theme::panel); g.fillRect (corner);
    g.setColour (theme::muted); g.setFont (theme::ui (11.5f));
    g.drawText (juce::String (rows.size()) + " piste" + (rows.size() > 1 ? "s" : "") + U ("   ·   ") + EditContext::toolName (ctx.tool).upToFirstOccurrenceOf (" (", false, false),
                corner.withTrimmedLeft (42).withTrimmedRight (8), juce::Justification::centredLeft, true);
    g.setColour (theme::line); g.drawVerticalLine (headerW - 1, 0.0f, (float) rulerH);
    g.fillRect (0, getHeight() - scrollH, headerW, scrollH);

    // Règle.
    auto ruler = juce::Rectangle<int> (headerW, 0, getWidth() - headerW, rulerH);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3e44), 0, 0, juce::Colour (0xff2e3136), 0, (float) rulerH, false));
    g.fillRect (ruler);
    const int barsTop = markerTop + markerH;
    if (ctx.edit != nullptr)
    {
        auto& tr = ctx.edit->getTransport();
        auto lr = tr.getLoopRange();
        const float x0 = timeToX (lr.getStart().inSeconds()), x1 = timeToX (lr.getEnd().inSeconds());
        if (x1 > headerW && x1 > x0)
        {
            const float cx0 = juce::jmax ((float) headerW, x0);
            g.setColour (tr.looping ? theme::cycle : juce::Colour (0xff5b6068)); g.fillRect (cx0, 1.0f, x1 - cx0, (float) cycleH);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            if (x0 >= headerW) { juce::Path p; p.addTriangle (x0, 1, x0 + 7, 1, x0, 9); g.fillPath (p); }
            juce::Path q; q.addTriangle (x1, 1, x1 - 7, 1, x1, 9); g.fillPath (q);
        }
    }
    g.setColour (theme::line); g.drawHorizontalLine (cycleH + 1, (float) headerW, (float) getWidth());

    // Piste des repères.
    g.setColour (juce::Colour (0xff2a2d32)); g.fillRect (headerW, markerTop, getWidth() - headerW, markerH);
    if (ctx.edit != nullptr)
    {
        g.setFont (theme::ui (10.5f, true));
        for (auto& mk : ctx.edit->getMarkerManager().getMarkers())
        {
            const float x = timeToX (mk->getPosition().getStart().inSeconds());
            if (x < headerW - 60 || x > getWidth()) continue;
            const auto label = juce::String (mk->getMarkerID()) + "  " + mk->getName();
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (theme::ui (10.5f, true), label) + 10.0f;
            auto flag = juce::Rectangle<float> (x, (float) markerTop + 1, w, (float) markerH - 2);
            g.saveState(); g.reduceClipRegion (headerW, markerTop, getWidth() - headerW, markerH);
            g.setColour (juce::Colour (0xffd9a83b)); g.fillRect (flag);
            g.setColour (juce::Colour (0xff17181a)); g.drawText (label, flag.withTrimmedLeft (4.0f), juce::Justification::centredLeft, false);
            g.restoreState();
        }
        g.setColour (theme::dim); g.setFont (theme::ui (10.0f));
        if (ctx.edit->getMarkerManager().getMarkers().isEmpty())
            g.drawText (U ("Repères : Inser. pour en ajouter au curseur"), headerW + 6, markerTop, 300, markerH, juce::Justification::centredLeft);
    }
    g.setColour (theme::line); g.drawHorizontalLine (barsTop, (float) headerW, (float) getWidth());
    g.setFont (theme::ui (11.5f));
    const int bpb = ctx.beatsPerBar();
    const double pxBar = ctx.barSeconds() * pxPerSecond * ctx.bpm() / juce::jmax (1.0, ctx.bpmAt (viewStart));
    const int every = pxBar < 26 ? 8 : pxBar < 50 ? 4 : pxBar < 90 ? 2 : 1;
    for (int b = juce::jmax (0, (int) (ctx.toBeats (viewStart) / bpb)); ; ++b)
    {
        const float x = timeToX (ctx.toSeconds ((double) b * bpb));
        if (x > (float) getWidth()) break;
        if (x >= headerW)
        {
            g.setColour (theme::text.withAlpha (0.55f)); g.drawVerticalLine ((int) x, (float) barsTop + 1 + (b % every == 0 ? 0 : 9), (float) rulerH);
            if (b % every == 0) { g.setColour (theme::text); g.drawText (juce::String (b + 1), (int) x + 3, barsTop + 1, 40, rulerH - barsTop - 2, juce::Justification::centredLeft); }
        }
        if (pxBar / bpb > 10)
            for (int k = 1; k < bpb; ++k)
            {
                const float bx = timeToX (ctx.toSeconds ((double) b * bpb + k));
                if (bx >= headerW && bx <= getWidth()) { g.setColour (theme::text.withAlpha (0.25f)); g.drawVerticalLine ((int) bx, (float) rulerH - 6, (float) rulerH); }
            }
    }
    g.setColour (theme::line); g.drawHorizontalLine (rulerH - 1, 0.0f, (float) getWidth());
}

void ArrangementView::paintOverChildren (juce::Graphics& g)
{
    if (ctx.edit == nullptr) return;
    const float bottom = (float) (getHeight() - scrollH);
    // Sélection de plage.
    if (rangeSelection.getLength() > 0.0)
    {
        const float x0 = juce::jmax ((float) headerW, timeToX (rangeSelection.getStart())), x1 = timeToX (rangeSelection.getEnd());
        if (x1 > x0) { g.setColour (theme::accent.withAlpha (0.18f)); g.fillRect (x0, (float) rulerH, x1 - x0, bottom - rulerH);
                       g.setColour (theme::accent.withAlpha (0.7f)); g.drawVerticalLine ((int) x0, (float) rulerH, bottom); g.drawVerticalLine ((int) x1, (float) rulerH, bottom); }
    }
    if (! rubber.isEmpty())
    {
        auto r = getLocalArea (holder.get(), rubber).getIntersection (viewport.getBounds());
        g.setColour (theme::accent.withAlpha (0.12f)); g.fillRect (r);
        g.setColour (theme::accent); g.drawRect (r, 1);
    }
    // Curseur de projet.
    const float x = timeToX (ctx.edit->getTransport().getPosition().inSeconds());
    if (x < headerW || x > getWidth()) return;
    const auto c = ctx.edit->getTransport().isRecording() ? theme::rec : theme::cursor;
    const float top = (float) (markerTop + markerH);
    g.setColour (c); g.fillRect (x, top, 1.2f, bottom - top);
    juce::Path head; head.addTriangle (x - 6, top, x + 7, top, x + 0.5f, top + 8);
    g.fillPath (head);
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    if (e.y < rulerH && e.x > headerW && ctx.edit != nullptr && e.y <= 14)
    {
        auto lr = ctx.edit->getTransport().getLoopRange();
        const bool nearEdge = std::abs (e.x - timeToX (lr.getStart().inSeconds())) < 6 || std::abs (e.x - timeToX (lr.getEnd().inSeconds())) < 6;
        setMouseCursor (nearEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    else setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    rulerDrag = RulerDrag::none;
    if (e.y >= rulerH || e.x <= headerW || ctx.edit == nullptr) return;
    auto& tr = ctx.edit->getTransport();
    const double t = juce::jmax (0.0, xToTime ((float) e.x));
    const double st = snapTime (t, e.mods);
    if (e.mods.isCtrlDown() && ! e.mods.isAltDown()) { tr.setLoopIn (te::TimePosition::fromSeconds (st)); return; }    // Ctrl+clic : délimiteur gauche
    if (e.mods.isAltDown()) { tr.setLoopOut (te::TimePosition::fromSeconds (st)); return; }                            // Alt+clic : délimiteur droit
    if (e.y >= markerTop && e.y < markerTop + markerH)
    {
        if (auto* mk = markerAt (e.x))
        {
            if (e.mods.isPopupMenu()) { showMarkerMenu (*mk); return; }
            if (e.getNumberOfClicks() == 2) { renameMarker (ctx, mk->itemID); return; }
            tr.setPosition (mk->getPosition().getStart());
            rulerDrag = RulerDrag::marker;
            dragMarker = mk->itemID;
            rulerAnchor = mk->getPosition().getStart().inSeconds() - t;
            ctx.edit->getUndoManager().beginNewTransaction (U ("Déplacer le repère"));
        }
        else if (e.getNumberOfClicks() == 2) { tr.setPosition (te::TimePosition::fromSeconds (st)); ctx.perform ("transport.addMarker"); }
        return;
    }
    if (e.y <= cycleH + 1)
    {
        auto lr = tr.getLoopRange();
        rulerStartRange = lr;
        const float xl = timeToX (lr.getStart().inSeconds()), xr = timeToX (lr.getEnd().inSeconds());
        if (std::abs (e.x - xl) < 6) rulerDrag = RulerDrag::moveL;
        else if (std::abs (e.x - xr) < 6) rulerDrag = RulerDrag::moveR;
        else if (e.x > xl && e.x < xr)
        {
            if (e.getNumberOfClicks() == 2) { tr.looping = ! tr.looping.get(); return; }
            rulerDrag = RulerDrag::moveCycle;
        }
        else rulerDrag = RulerDrag::newCycle;
        rulerAnchor = st;
        return;
    }
    rulerDrag = RulerDrag::cursor;
    tr.setPosition (te::TimePosition::fromSeconds (t));
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (ctx.edit == nullptr || rulerDrag == RulerDrag::none) return;
    auto& tr = ctx.edit->getTransport();
    const double t = juce::jmax (0.0, xToTime ((float) e.x));
    const double st = snapTime (t, e.mods);
    switch (rulerDrag)
    {
        case RulerDrag::cursor: tr.setPosition (te::TimePosition::fromSeconds (t)); break;
        case RulerDrag::marker:
            if (auto* c = te::findClipForID (*ctx.edit, dragMarker))
            {
                const auto p = te::TimePosition::fromSeconds (snapTime (juce::jmax (0.0, t + rulerAnchor), e.mods));
                c->setStart (p, false, true);
                tr.setPosition (p);
            }
            break;
        case RulerDrag::moveL:  if (st < rulerStartRange.getEnd().inSeconds()) tr.setLoopIn (te::TimePosition::fromSeconds (st)); break;
        case RulerDrag::moveR:  if (st > rulerStartRange.getStart().inSeconds()) tr.setLoopOut (te::TimePosition::fromSeconds (st)); break;
        case RulerDrag::moveCycle:
        {
            const double d = st - rulerAnchor;
            const double s = juce::jmax (0.0, rulerStartRange.getStart().inSeconds() + d);
            tr.setLoopRange ({ te::TimePosition::fromSeconds (s), te::TimePosition::fromSeconds (s + rulerStartRange.getLength().inSeconds()) });
            break;
        }
        case RulerDrag::newCycle:
            if (std::abs (st - rulerAnchor) > 0.001)
            {
                tr.setLoopRange ({ te::TimePosition::fromSeconds (juce::jmin (st, rulerAnchor)), te::TimePosition::fromSeconds (juce::jmax (st, rulerAnchor)) });
                tr.looping = true;
            }
            break;
        case RulerDrag::none: break;
    }
    repaint (0, 0, getWidth(), rulerH);
}

void ArrangementView::mouseUp (const juce::MouseEvent&) { rulerDrag = RulerDrag::none; dragMarker = {}; }

te::MarkerClip* ArrangementView::markerAt (int x) const
{
    if (ctx.edit == nullptr) return nullptr;
    te::MarkerClip* best = nullptr;
    for (auto& mk : ctx.edit->getMarkerManager().getMarkers())
    {
        const float mx = timeToX (mk->getPosition().getStart().inSeconds());
        const auto label = juce::String (mk->getMarkerID()) + "  " + mk->getName();
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (theme::ui (10.5f, true), label) + 10.0f;
        if ((float) x >= mx - 3 && (float) x <= mx + w) best = mk;
    }
    return best;
}

void ArrangementView::showMarkerMenu (te::MarkerClip& mk)
{
    juce::PopupMenu m;
    m.addItem (1, U ("Aller au repère"));
    m.addItem (2, U ("Renommer…"));
    m.addItem (3, U ("Cycle jusqu'au repère suivant"));
    m.addSeparator();
    m.addItem (4, U ("Supprimer le repère"));
    const auto id = mk.itemID;
    m.showMenuAsync ({}, [this, id] (int r)
    {
        auto* c = ctx.edit ? dynamic_cast<te::MarkerClip*> (te::findClipForID (*ctx.edit, id)) : nullptr;
        if (c == nullptr || r <= 0) return;
        auto& tr = ctx.edit->getTransport();
        if (r == 1) tr.setPosition (c->getPosition().getStart());
        if (r == 2) renameMarker (ctx, id);
        if (r == 3)
        {
            auto* next = ctx.edit->getMarkerManager().getNextMarker (c->getPosition().getStart());
            const auto end = next ? next->getPosition().getStart() : te::TimePosition() + ctx.edit->getLength();
            tr.setLoopRange ({ c->getPosition().getStart(), end });
            tr.looping = true;
        }
        if (r == 4) { ctx.edit->getUndoManager().beginNewTransaction (U ("Supprimer le repère")); c->removeFromParent(); }
        repaint();
    });
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        zoom (w.deltaY > 0 ? 1.25 : 0.8, xToTime ((float) juce::jmax (headerW, e.x)));
    else
    {
        viewStart = juce::jmax (0.0, viewStart - (w.deltaY + w.deltaX) * 300.0 / pxPerSecond);
        updateScrollBar();
        repaint(); holder->repaint();
    }
}

bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (ctx.edit == nullptr) return;
    ctx.edit->getUndoManager().beginNewTransaction (U ("Importer de l'audio"));
    const auto local = holder->getLocalPoint (this, juce::Point<int> (x, y));
    te::AudioTrack* target = nullptr;
    for (auto* r : rows) if (r->getBounds().contains (local) && ctx.getInstrument (r->track) == nullptr) target = &r->track;
    double t = ctx.snapToGrid (juce::jmax (0.0, xToTime ((float) x)));
    for (auto& path : files)
    {
        juce::File f (path);
        if (! f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) continue;
        auto* track = target != nullptr ? target : ctx.addAudioTrack();
        if (target == nullptr && track != nullptr) track->setName (f.getFileNameWithoutExtension());
        const double len = te::AudioFile (ctx.engine, f).getLength();
        if (track != nullptr && len > 0)
            if (auto c = track->insertWaveClip (f.getFileNameWithoutExtension(), f, { { te::TimePosition::fromSeconds (t), te::TimePosition::fromSeconds (t + len) }, te::TimeDuration() }, false))
                ctx.selectClip (c.get());
        target = nullptr;                                 // fichiers suivants : une piste chacun
    }
    if (ctx.setStatus) ctx.setStatus (juce::String (files.size()) + U (" fichier(s) importé(s)"));
}
