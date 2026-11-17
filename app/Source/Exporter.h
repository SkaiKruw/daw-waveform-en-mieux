// Export du mixage : WAV, FLAC, OGG Vorbis et MP3 (encodeur MP3 de Windows), normalisation crête ou LUFS,
// dithering, zone (projet ou cycle), export piste par piste.
#pragma once
#include "EditContext.h"

struct ExportSettings
{
    enum class Format { wav, flac, mp3, ogg };
    Format format = Format::wav;
    int bitDepth = 24;                  // WAV : 16, 24, 32 (flottant) ; FLAC : 16, 24
    double sampleRate = 48000.0;
    int mp3Kbps = 320;                  // 128 à 320
    int oggQuality = 8;                 // index de qualité JUCE (0 à 10)
    enum class Normalise { none, peak, lufs } normalise = Normalise::none;
    float normaliseLevel = -0.1f;       // dB crête ou LUFS intégrés
    bool dither = true;                 // pour les sorties entières (16 / 24 bits)
    bool cycleOnly = false;             // zone entre les délimiteurs
    double tailSeconds = 2.0;           // queue de réverbération / délai
    bool separateTracks = false;        // un fichier par piste

    juce::String extension() const;
    juce::String describe() const;
};

namespace exporter
{
    // Exporte vers dest (extension remplacée selon le format). Pour separateTracks, dest est le
    // dossier de sortie + préfixe ; les fichiers créés sont ajoutés à « written ».
    bool exportMix (te::Edit&, const juce::File& dest, const ExportSettings&, juce::StringArray& written, juce::String& error);

    // Encodage MP3 par Media Foundation (Windows) à partir d'un WAV 16 bits.
    bool encodeMp3 (const juce::File& wav16, const juce::File& mp3, int kbps, juce::String& error);

    // Boîte de dialogue « Exporter le mixage audio » ; appelle onExport avec les réglages choisis.
    void showDialog (EditContext&, const juce::String& projectName, std::function<void (const ExportSettings&)> onExport);
}
