// Bibliothèques d'instruments enregistrés (MIDI › Bibliothèques d'instruments…) : un catalogue de bibliothèques
// libres (format SFZ) à télécharger d'un clic, installées dans %APPDATA%\Onde\Bibliothèques et jouées par Onde
// Échantillonneur. On peut aussi ajouter ses propres dossiers SFZ. Rien n'est téléchargé sans un clic.
#pragma once
#include "EditContext.h"

namespace libraries
{
    struct Info
    {
        const char* id;
        const char* family;
        const char* name;
        const char* description;
        const char* licence;
        int sizeMb;
        const char* repository;          // dépôt GitHub « propriétaire/nom » (archive zip téléchargée)
    };
    const std::vector<Info>& catalog();
    juce::File folderFor (const Info&);
    bool isInstalled (const Info&);
    juce::String downloadUrl (const Info&);
    // Installation d'une archive zip déjà téléchargée (aussi pour les tests) : décompressée dans le dossier de la bibliothèque.
    juce::Result installFromZip (const Info&, const juce::File& zip, std::function<void (float)> progress = nullptr);
    // Téléchargement complet (archive du dépôt) puis installation ; progress (étape 0 téléchargement, 1 installation, fraction).
    // shouldStop : annulation. Appelé hors du fil de l'interface.
    juce::Result downloadAndInstall (const Info&, std::function<void (int stage, float fraction, juce::int64 bytes)> progress, std::function<bool()> shouldStop = nullptr);
    const Info* find (const juce::String& id);
}

class LibrariesPanel  : public juce::Component, private juce::Timer
{
public:
    explicit LibrariesPanel (EditContext&);
    ~LibrariesPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    static juce::DialogWindow* show (EditContext&);

    struct Job;
private:
    void timerCallback() override;
    void start (int index);
    juce::Rectangle<int> rowBounds (int i) const;
    juce::Rectangle<int> buttonBounds (int i) const;
    EditContext& ctx;
    juce::OwnedArray<Job> jobs;
    juce::TextButton openFolder, addFolder;
    std::unique_ptr<juce::FileChooser> chooser;
    int scroll = 0;
    static constexpr int rowH = 74, top = 96;
};
