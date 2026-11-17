// Séparateur de sources d'Onde : voix, batterie, basse et autres instruments à partir d'un événement
// audio. Moteur : Demucs v4 « fine-tuned » (htdemucs_ft, 4 modèles spécialisés) via demucs.cpp,
// exécuté dans un processus isolé (onde-stems) : un plantage ou une annulation n'affecte pas Onde.
#pragma once
#include "EditContext.h"

namespace stems
{
    struct Stem { const char* id; const char* label; juce::uint32 colour; };
    const std::array<Stem, 4>& list();                              // drums, bass, other, vocals

    juce::File executable();
    juce::File modelsFolder();
    bool isAvailable (juce::String& why);

    // Écrit la partie jouée de l'événement (inversion comprise), rééchantillonnée à 44,1 kHz, en WAV float.
    bool writeClipInput (te::WaveAudioClip&, const juce::File& wav44k, juce::String& error);
    bool resampleTo44k (const juce::File& source, double startSeconds, double lengthSeconds, bool reversed,
                        const juce::File& wav44k, juce::String& error);

    // Lance la séparation et attend la fin. progress (0..1) est appelé depuis ce thread ; renvoyer false annule.
    bool separateFile (const juce::File& wav44k, const juce::File& outDir, std::function<bool (float)> progress,
                       juce::String& error, int threads = 0, const juce::String& mode = "precis");

    // Crée un dossier « <événement> (séparé) » contenant une piste par source, alignée sur l'événement,
    // et rend l'événement d'origine muet. Annulable d'un bloc.
    te::FolderTrack* importStems (EditContext&, te::WaveAudioClip&, const juce::File& outDir, juce::String& error);

    // Tâche complète avec fenêtre de progression (annulable) : préparation, séparation, import.
    void separateSelectedClip (EditContext&);
    juce::File workFolderFor (te::Edit&, const juce::String& clipName);
}
