#include "HistoryPanel.h"

HistoryPanel::HistoryPanel (EditContext& c) : ctx (c)
{
    list.setModel (this);
    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1c1f24));
    addAndMakeVisible (list);
    setSize (360, 460);
    attach();
    startTimerHz (4);      // suit le changement de projet (nouveau, ouverture)
}

HistoryPanel::~HistoryPanel()
{
    if (attached != nullptr && ctx.edit == attached) attached->getUndoManager().removeChangeListener (this);
    list.setModel (nullptr);
}

void HistoryPanel::attach()
{
    if (attached == ctx.edit) return;
    // L'ancien projet peut déjà être détruit : on ne s'en désabonne que s'il est encore le projet ouvert.
    attached = ctx.edit;
    if (attached != nullptr) attached->getUndoManager().addChangeListener (this);
    refresh();
}

void HistoryPanel::timerCallback()
{
    if (attached != ctx.edit) attach();
}

void HistoryPanel::refresh()
{
    entries.clear();
    entries.add (U ("Ouverture du projet"));
    current = 0;
    if (attached != nullptr)
    {
        auto& um = attached->getUndoManager();
        const auto done = um.getUndoDescriptions();      // de la plus récente à la plus ancienne
        const auto undone = um.getRedoDescriptions();    // de la prochaine à rétablir à la plus lointaine
        for (int i = done.size(); --i >= 0;) entries.add (done[i].isNotEmpty() ? done[i] : U ("Modification"));
        current = done.size();
        for (auto& d : undone) entries.add (d.isNotEmpty() ? d : U ("Modification"));
    }
    list.updateContent();
    list.selectRow (current, false, true);
    list.scrollToEnsureRowIsOnscreen (current);
    repaint();
}

void HistoryPanel::goTo (int index)
{
    if (attached == nullptr || index < 0 || index >= entries.size()) return;
    auto& um = attached->getUndoManager();
    // Plusieurs annulations ou rétablissements d'affilée ; les notifications sont regroupées par refresh().
    int guard = 0;
    while (current > index && um.canUndo() && guard++ < 1000) { um.undo(); --current; }
    while (current < index && um.canRedo() && guard++ < 1000) { um.redo(); ++current; }
    ctx.changed();
    refresh();
    if (ctx.setStatus) ctx.setStatus (index == 0 ? U ("Historique : état à l'ouverture du projet") : U ("Historique : après « ") + entries[index] + U (" »"));
}

void HistoryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= entries.size()) return;
    const bool isCurrent = row == current, undone = row > current;
    if (isCurrent) { g.setColour (theme::accentBg); g.fillRect (0, 0, w, h); g.setColour (theme::accent); g.fillRect (0, 0, 3, h); }
    g.setColour (undone ? theme::dim : theme::text);
    g.setFont (theme::ui (12.5f, isCurrent));
    g.drawText (entries[row], 14, 0, w - 50, h, juce::Justification::centredLeft, true);
    g.setColour (theme::dim); g.setFont (theme::mono (10.5f));
    if (row > 0) g.drawText (juce::String (row), w - 40, 0, 32, h, juce::Justification::centredRight);
    g.setColour (juce::Colour (0xff262a30)); g.drawHorizontalLine (h - 1, 0.0f, (float) w);
}

void HistoryPanel::resized()
{
    list.setBounds (getLocalBounds().withTrimmedTop (34).reduced (8, 0).withTrimmedBottom (8));
}

void HistoryPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d22));
    g.setColour (theme::muted); g.setFont (theme::ui (12.0f));
    g.drawText (U ("Cliquez sur une étape pour y revenir ; les étapes grisées sont annulées."), 12, 6, getWidth() - 24, 22,
                juce::Justification::centredLeft, true);
}

juce::DialogWindow* HistoryPanel::show (EditContext& c)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new HistoryPanel (c));
    o.dialogTitle = U ("Historique des modifications");
    o.dialogBackgroundColour = juce::Colour (0xff1a1d22);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    return o.launchAsync();
}
