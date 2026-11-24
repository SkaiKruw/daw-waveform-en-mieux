// Mises à jour : Onde demande la dernière version publiée (GitHub Releases du dépôt réglé dans les paramètres)
// et prévient quand elle est plus récente. Aucune donnée personnelle n'est envoyée (adresse publique, en-tête
// « Onde »). Vérification au démarrage au plus une fois par jour (désactivable), ou par Aide › Rechercher des
// mises à jour.
#pragma once
#include <JuceHeader.h>

namespace updates
{
    struct Info
    {
        bool ok = false, newer = false;
        juce::String latest, pageUrl, notes, error;
    };
    juce::String currentVersion();
    juce::String repository();                               // « propriétaire/dépôt »
    bool isNewer (const juce::String& candidate, const juce::String& current);
    // Interroge le dépôt en arrière-plan ; done est appelé sur le fil de l'interface.
    void check (std::function<void (const Info&)> done);
    Info parseRelease (const juce::String& json);           // réponse de l'API (aussi pour les tests)
    bool shouldCheckToday();                                 // réglage actif et dernière vérification > 24 h
}
