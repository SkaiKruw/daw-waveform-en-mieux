// Signale qu'un projet a changé (arbre de l'edit), sans tenir compte du transport (position de lecture,
// qui bouge en permanence). Sert aux vues pour ne se redessiner entièrement que lorsque c'est utile ;
// la tête de lecture, elle, ne redessine que deux bandes étroites.
#pragma once
#include "EditContext.h"

class EditWatcher  : private juce::ValueTree::Listener
{
public:
    ~EditWatcher() override { if (state.isValid()) state.removeListener (this); }

    // À appeler régulièrement (minuteur) : suit le projet ouvert dans le contexte.
    void watch (te::Edit* e)
    {
        if (e == edit) return;
        if (state.isValid()) state.removeListener (this);
        edit = e;
        state = e != nullptr ? e->state : juce::ValueTree();
        if (state.isValid()) state.addListener (this);
        dirty = true;
    }

    void markDirty() noexcept          { dirty = true; }
    bool consumeDirty() noexcept       { const bool d = dirty; dirty = false; return d; }

private:
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override { if (! t.hasType (te::IDs::TRANSPORT)) dirty = true; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override               { dirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override        { dirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override                { dirty = true; }

    te::Edit* edit = nullptr;
    juce::ValueTree state;
    bool dirty = true;
};

// Redessin d'une tête de lecture verticale : efface l'ancienne bande et dessine la nouvelle.
inline void repaintCursorStrip (juce::Component& c, float& lastX, float newX, int top, int bottom)
{
    if (std::abs (newX - lastX) < 0.5f) return;
    if (lastX >= 0.0f) c.repaint ((int) lastX - 4, top, 10, bottom - top);
    if (newX >= 0.0f)  c.repaint ((int) newX - 4, top, 10, bottom - top);
    lastX = newX;
}
