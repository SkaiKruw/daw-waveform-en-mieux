# Onde — v0.4

Une station de travail audio (DAW) au fonctionnement et à l'apparence d'une station professionnelle de type Cubase,
qui ne tombe pas quand un plugin plante : chaque plugin VST3 tourne dans son propre processus. S'il plante ou se bloque,
seule sa piste se tait, et « Relancer » le redémarre avec son dernier réglage.

## Lancer

- **Déjà compilé sur ce PC** : `build-windows\app\Onde_artefacts\Release\Onde.exe` (épinglez-le à la barre des tâches).
- **Recompiler après une mise à jour** : double-cliquez sur `windows\Installer-et-lancer-Onde.bat` (ne recompile que ce qui a changé).
- **Sur un nouveau PC** (Windows 10/11) : clonez le dépôt dans un chemin court (ex. `C:\onde`), puis double-cliquez sur
  `windows\Installer-et-lancer-Onde.bat`. Le script installe Git, CMake et les outils C++ de Visual Studio si besoin,
  télécharge les dépendances aux versions exactes utilisées (Tracktion Engine + JUCE, Eigen, modèles Demucs) dans `deps\`,
  compile, lance les tests d'isolation et ouvre Onde. Première fois : 30 à 60 minutes.

Technologie : C++20, [JUCE](https://juce.com) et [Tracktion Engine](https://github.com/Tracktion/tracktion_engine), CMake,
Visual Studio 2022 (MSVC). Application de bureau Windows (macOS et Linux pas encore testés).

Le dépôt ne contient que les sources : `build-windows\` et `deps\` (≈ 1,5 Go avec les modèles) sont recréés par le script.

## La fenêtre de projet

| Zone | Contenu |
| --- | --- |
| Barre de menus | Fichier, Édition, Projet, Audio, MIDI, Transport, Studio, Fenêtre, Aide — chaque commande avec son raccourci. |
| Barre d'outils | Zones (gauche / bas / droite), transport, 8 outils (Sélection, Plage, Ciseaux, Colle, Gomme, Zoom, Muet, Crayon — touches 1 à 8), calage (J), grille, quantification. |
| Ligne d'infos | Nom, début, fin, longueur, décalage et état muet de l'événement sélectionné — modifiables par double-clic. |
| Inspecteur (gauche) | Couleur, nom, M / S / ● / moniteur, lecture/écriture d'automation, instrument, volume, panoramique, vumètre, 8 inserts (contourner, retirer, remplacer), envois vers les pistes FX. |
| Pistes | En-têtes avec numéro, type, M / S / ● / moniteur / A (automation), vumètre ; hauteur réglable en tirant le bas de l'en-tête. |
| Règle | Bande de cycle (tirer : nouveau cycle ; poignées L/R ; double-clic : cycle on/off ; Ctrl+clic / Alt+clic : délimiteur gauche / droit), piste des repères, mesures. |
| Zone inférieure | **MixConsole** (inserts, panoramique, M/S/●/moniteur, fader gradué, vumètre avec crête, Stereo Out) et **Éditeur de notes** (zoom, défilement, sélection au rectangle, vélocité, assistant de gamme, écoute des notes). |
| Zone droite | Instruments, effets et fichiers audio (Musique, Documents\Onde, Bureau) : double-clic pour utiliser. |
| Barre de transport | Charge audio, délimiteurs L/R, punch, transport, compteur mesures + temps, métronome, tempo (glisser ou double-clic), signature, sortie stéréo. |

## Fonctions

- **Édition** : sélection multiple (Maj+clic, rectangle, Ctrl+A), déplacement en groupe (Alt : copie, Ctrl : sans calage), rognage des deux bords, couper (Alt+X ou outil Ciseaux), coller (outil Colle), dupliquer (Ctrl+D), copier / couper / coller (Ctrl+C / X / V), muet (Maj+M), annuler / rétablir.
- **Automation** : volume et panoramique par piste (A), points ajoutés / déplacés / supprimés à la souris, appliquée en lecture et à l'export.
- **Pistes FX et envois** : bus d'effet (réverbération ou tout effet), envois post-fader réglés dans l'inspecteur.
- **Piste de tempo** (Projet › Piste de tempo, Alt+T) : clic pour ajouter un changement de tempo, glisser un point pour le déplacer ou changer sa valeur (Maj : réglage fin), double-clic pour le supprimer, clic droit pour choisir saut ou rampe. Règle, grille, compteur, calage et conteneurs MIDI suivent le tempo variable ; la barre de transport affiche le tempo au curseur.
- **Éditeur audio** (zone inférieure, double-clic sur un événement audio ou Ctrl+E) : forme d'onde stéréo du fichier, partie jouée en couleur, règle en mesures, zoom (Ctrl+molette) et défilement ; gain de l'événement, Normaliser (crête à -0,1 dB), Inverser, transposition ±12 demi-tons (durée conservée), fondus d'entrée/sortie par poignées (clic droit : linéaire, convexe, concave, en S) ; glisser pour sélectionner une plage puis « Rogner à la sélection » ou « Supprimer la sélection ». Dans la fenêtre de projet, les événements audio ont aussi leurs poignées de fondu (coins hauts) et de gain (milieu, glisser verticalement). Menu Audio : normaliser, inverser, fondus d'un temps sur la sélection. Tout est non destructif et annulable.
- **Dossiers et groupes** : Projet › Ajouter une piste › Dossier / Groupe, ou Ctrl+Maj+F pour ranger la piste sélectionnée dans un nouveau dossier. Glisser l'en-tête d'une piste pour la réordonner ou la déposer dans un dossier ; clic sur le triangle (ou double-clic dans la bande) pour replier ; muet / solo du dossier ; un **groupe** est un dossier de sous-mixage avec son volume (dans l'en-tête et dans la MixConsole). Clic droit sur un en-tête : ranger, sortir, supprimer le dossier avec ou sans ses pistes. Les nouvelles pistes se placent sous la piste sélectionnée.
- **Repères** : Inser. ajoute un repère, Maj+N / Maj+B pour naviguer, pavé 3 à 9 pour les atteindre, renommer / déplacer / supprimer dans la règle.
- **Enregistrement** audio et MIDI, monitoring, punch ; « Enregistrer sous » copie les prises dans « <projet> - Audio ».
- **Projet** : sauvegarde `.onde`, sauvegarde automatique toutes les 15 s, récupération après arrêt brutal, import audio, réglages de la carte son.
- **Export audio** (Fichier › Exporter, Ctrl+Maj+E) : WAV (16, 24 ou 32 bits flottant), FLAC (16 ou 24 bits), MP3 (128 à 320 kbit/s, encodeur de Windows), OGG Vorbis ; 44,1, 48 ou 96 kHz ; normalisation crête (-0,1 dB) ou sonie (-14 LUFS streaming, -23 LUFS diffusion) ; dithering ; tout le projet ou la zone du cycle ; queue de 2 s pour les réverbérations ; export **piste par piste** (un fichier par piste, effets compris).
- **Séparateur de sources** (Audio › Séparer les sources, Ctrl+Maj+U, ou clic droit sur un événement audio) : sépare un événement en **voix, batterie, basse et autres** avec Demucs v4 « fine-tuned » (htdemucs_ft : un modèle spécialisé par source, la référence en qualité). Le calcul tourne dans un processus isolé (`onde-stems`), avec progression, temps restant et annulation ; les 4 sources arrivent dans un dossier « <événement> (séparé) », alignées sur l'événement (gain, fondus et transposition repris), l'original est rendu muet. Annulable d'un bloc. Les fichiers sont écrits dans « Séparations » à côté du projet. Deux qualités au choix (la fenêtre affiche la durée estimée) : **Très précis** (les 4 modèles spécialisés ; environ 14 fois la durée de l'audio sur un processeur 4 cœurs Core i5-9300H) et **Rapide** (un seul modèle donne les 4 sources, résidu redistribué pour que leur somme redonne exactement le mix ; environ 2 fois plus rapide). On peut continuer à travailler pendant le calcul.
- **Plugins** : VST3 isolés (◆) avec traitement anticipé (le plugin calcule un bloc d'avance, latence compensée automatiquement), transport transmis (tempo, signature, position, lecture, boucle), latence propre déclarée, mode hors temps réel pendant l'export ; effets intégrés (égaliseur, compresseur, réverbération, délai, chorus, phaser, passe-bas), synthé 4OSC.
- Toutes les commandes : Ctrl+K (recherche) et F1 (liste des raccourcis).

## Tests automatiques

| Test | Commande | Résultat |
| --- | --- | --- |
| Isolation du cœur | `onde-sandbox-test onde-plugin-worker` | plantage détecté, plugin bloqué arrêté, piste voisine intacte |
| Vrai VST3 | `onde-vst3-test onde-plugin-host <synth.vst3> <crash.vst3>` | 10/10 |
| Plugins installés | `Onde --plugin-test <dossier>` | chaque VST3 du PC : chargement isolé, son, fenêtre, réglage sauvegardé, 3 s de lecture en temps réel sans retard (7/7 sur ce PC ; SSDSampler5 muet tant qu'aucun kit n'est choisi) |
| Interface | `Onde --ui-test <dossier>` | 61/61 : export WAV / FLAC / MP3 / OGG lisibles, normalisation à l'export, export piste par piste ; éditeur audio (gain, fondus, normaliser, inverser, transposer appliqués au rendu ; supprimer / rogner une plage), dossiers (muet effectif au rendu), groupes (volume effectif au rendu), annuler ; piste de tempo (sauts, rampes, compteur, annuler), pistes, événements, outils, sélection multiple, copier/coller, automation appliquée au rendu, pistes FX et envois audibles, repères, enregistrer / rouvrir (audio retrouvé), export, plantage et relance |
| Séparateur | `Onde --stems-test <dossier>` | sur le projet de démo (mix à 48 kHz) : très précis : SDR batterie 28 dB, basse 28 dB, autres 13,6 dB, aucune voix inventée (-46 dB), somme des sources = mix (31 dB) ; rapide (`ONDE_STEMS_MODE=rapide`) : batterie 15,3 dB, basse 17,6 dB, autres 12,3 dB, somme exacte, import aligné, original muet, annuler |
| Temps réel | `Onde --live-test <dossier>` | lecture sur la carte son, son au master, enregistrement d'une prise, copie de la prise à l'enregistrement du projet |

Traces de diagnostic : définir la variable d'environnement `ONDE_TRACE=1`.

## Limites connues

- Les événements audio restent calés sur le temps (secondes) quand le tempo change ; seuls les conteneurs MIDI suivent les mesures.
- Le repli des dossiers n'est pas enregistré dans le projet ; la tranche de groupe n'a pas encore de vumètre.
- Pas encore : macOS, traitements destructifs (l'éditeur audio règle l'événement, il ne réécrit pas le fichier).
- MP3 : encodeur de Windows (Media Foundation), 44,1 ou 48 kHz uniquement ; ailleurs, choisir FLAC ou OGG.
- Séparateur : les sources de demucs.cpp (MIT) sont dans `app/Worker/demucs`, réécrites par endroits pour MSVC (normalisations, softmax, im2col : mêmes calculs, résultats identiques à 60 dB près) ; les 4 modèles (≈ 340 Mo, `deps/demucs-models`) sont copiés à côté d'Onde dans `modeles-demucs`.
- Les VST3 de type effet ne reçoivent pas le MIDI.
- Licence : Tracktion Engine et JUCE sont GPLv3 ou commerciaux ; distribuer Onde impose l'un ou l'autre.

