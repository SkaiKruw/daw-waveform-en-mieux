// Palette de commandes (Ctrl+K) : toutes les actions d'Onde, filtrées en tapant.
#pragma once
#include "EditContext.h"

class CommandPalette  : public juce::Component,
                        private juce::ListBoxModel,
                        private juce::TextEditor::Listener
{
public:
    explicit CommandPalette (EditContext&);
    void open();
    void close();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { close(); }
    bool keyPressed (const juce::KeyPress&) override;
    void setQuery (const juce::String& q) { search.setText (q, true); }

private:
    int getNumRows() override { return results.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { runRow (row); }
    void textEditorTextChanged (juce::TextEditor&) override { filter(); }
    void textEditorReturnKeyPressed (juce::TextEditor&) override { runRow (list.getSelectedRow()); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
    void filter();
    void runRow (int row);
    juce::Rectangle<int> boxBounds() const;

    EditContext& ctx;
    juce::TextEditor search;
    juce::ListBox list;
    juce::Array<Action*> results;
};
