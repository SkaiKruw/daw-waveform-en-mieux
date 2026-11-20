<img src="app/Resources/icon-1024.png" alt="Logo d'Onde" width="96">

# Onde — v0.5

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

## Installateur

Après une compilation : `powershell -File windows\creer-installateur.ps1` (option `-SansModeles` : sans les modèles du
séparateur, ≈ 23 Mo au lieu de ≈ 320 Mo). Produit dans `build-windows\installateur\` :

- `Onde-Setup-<version>.exe` : installateur (IExpress, intégré à Windows). Installe pour l'utilisateur courant, sans droits
  administrateur, dans `%LOCALAPPDATA%\Programs\Onde`, avec raccourcis (menu Démarrer, Bureau) et entrée dans
  Paramètres › Applications (désinstallation qui conserve projets et réglages). Le runtime Visual C++ est embarqué.
- `Onde-<version>-portable.zip` : le même programme, à décompresser n'importe où.

L'installateur n'est pas signé : Windows SmartScreen peut demander « Exécuter quand même ».

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
- **Mode musical** (Audio › Mode musical, Ctrl+Maj+M, ou clic droit sur un événement audio) : l'événement suit le tempo du projet, étiré en temps réel (SoundTouch) ; son tempo d'origine est celui du projet au moment de l'activation. Marqué « ♪ musical » sur l'événement. Quand le tempo change, tout reste calé sur les mesures comme dans Cubase : conteneurs MIDI, débuts des événements audio (les événements normaux gardent leur durée, les événements musicaux s'étirent), délimiteurs et automation.
- **Piste de tempo** (Projet › Piste de tempo, Alt+T) : clic pour ajouter un changement de tempo, glisser un point pour le déplacer ou changer sa valeur (Maj : réglage fin), double-clic pour le supprimer, clic droit pour choisir saut ou rampe. Règle, grille, compteur, calage et conteneurs MIDI suivent le tempo variable ; la barre de transport affiche le tempo au curseur.
- **Éditeur audio** (zone inférieure, double-clic sur un événement audio ou Ctrl+E) : forme d'onde stéréo du fichier, partie jouée en couleur, règle en mesures, zoom (Ctrl+molette) et défilement ; gain de l'événement, Normaliser (crête à -0,1 dB), Inverser, transposition ±12 demi-tons (durée conservée), fondus d'entrée/sortie par poignées (clic droit : linéaire, convexe, concave, en S) ; glisser pour sélectionner une plage puis « Rogner à la sélection » ou « Supprimer la sélection ». Dans la fenêtre de projet, les événements audio ont aussi leurs poignées de fondu (coins hauts) et de gain (milieu, glisser verticalement). Menu Audio : normaliser, inverser, fondus d'un temps sur la sélection. Tout est non destructif et annulable.
- **Dossiers et groupes** : Projet › Ajouter une piste › Dossier / Groupe, ou Ctrl+Maj+F pour ranger la piste sélectionnée dans un nouveau dossier. Glisser l'en-tête d'une piste pour la réordonner ou la déposer dans un dossier ; clic sur le triangle (ou double-clic dans la bande) pour replier (enregistré avec le projet) ; muet / solo du dossier ; un **groupe** est un dossier de sous-mixage avec son volume et son vumètre (dans l'en-tête et dans la MixConsole). Clic droit sur un en-tête : ranger, sortir, supprimer le dossier avec ou sans ses pistes. Les nouvelles pistes se placent sous la piste sélectionnée.
- **Repères** : Inser. ajoute un repère, Maj+N / Maj+B pour naviguer, pavé 3 à 9 pour les atteindre, renommer / déplacer / supprimer dans la règle.
- **Enregistrement** audio et MIDI, monitoring, punch ; « Enregistrer sous » copie les prises dans « <projet> - Audio ».
- **Projet** : sauvegarde `.onde`, sauvegarde automatique toutes les 15 s, récupération après arrêt brutal, import audio, réglages de la carte son.
- **Export audio** (Fichier › Exporter, Ctrl+Maj+E) : WAV (16, 24 ou 32 bits flottant), FLAC (16 ou 24 bits), MP3 (128 à 320 kbit/s, encodeur de Windows), OGG Vorbis ; 44,1, 48 ou 96 kHz ; normalisation crête (-0,1 dB) ou sonie (-14 LUFS streaming, -23 LUFS diffusion) ; dithering ; tout le projet ou la zone du cycle ; queue de 2 s pour les réverbérations ; export **piste par piste** (un fichier par piste, effets compris).
- **Séparateur de sources** (Audio › Séparer les sources, Ctrl+Maj+U, ou clic droit sur un événement audio) : sépare un événement en **voix, batterie, basse et autres** avec Demucs v4 « fine-tuned » (htdemucs_ft : un modèle spécialisé par source, la référence en qualité). Le calcul tourne dans un processus isolé (`onde-stems`), avec progression, temps restant et annulation ; les 4 sources arrivent dans un dossier « <événement> (séparé) », alignées sur l'événement (gain, fondus et transposition repris), l'original est rendu muet. Annulable d'un bloc. Les fichiers sont écrits dans « Séparations » à côté du projet. Deux qualités au choix (la fenêtre affiche la durée estimée) : **Très précis** (les 4 modèles spécialisés ; environ 14 fois la durée de l'audio sur un processeur 4 cœurs Core i5-9300H) et **Rapide** (un seul modèle donne les 4 sources, résidu redistribué pour que leur somme redonne exactement le mix ; environ 2 fois plus rapide). On peut continuer à travailler pendant le calcul.
- **Plugins** : VST3 isolés (◆) avec traitement anticipé (le plugin calcule un bloc d'avance, latence compensée automatiquement), transport transmis (tempo, signature, position, lecture, boucle), latence propre déclarée, mode hors temps réel pendant l'export ; effets intégrés (égaliseur, compresseur, réverbération, délai, chorus, phaser, passe-bas), synthé 4OSC.
- **Paramètres des VST3 isolés** : tous les paramètres automatisables du plugin apparaissent dans Onde (de 3 à 138 selon les plugins testés), avec le texte du plugin (« -14.4 dB »). Automation, modulateurs, enregistrement avec le projet ; un réglage fait dans la fenêtre du plugin ou un préréglage chargé est suivi par Onde.
- **Automation de tous les paramètres** : le bouton du paramètre de la piste d'automation (ou clic droit) propose le volume, le panoramique et chaque paramètre de chaque effet ; « ● » marque les paramètres déjà automatisés.
- **Télécommande sur téléphone** (Studio › Télécommande (téléphone)…, ou bouton téléphone de la barre d'outils, allumé quand la télécommande est active, avec un voyant vert quand un téléphone est connecté) : le téléphone devient un Stream Deck pour Onde. Activez la télécommande, scannez le QR code (même Wi-Fi que le PC, ou Tailscale) : grille de gros boutons avec retour haptique, compteur, tempo et état (lecture, enregistrement, cycle). Pages Transport (lecture, stop, enregistrer, mesure ±1, cycle, métronome, punch, repères), Édition (annuler, rétablir, enregistrer, couper, dupliquer, quantifier, calage, MixConsole…), Pistes (sélection, muet, solo, armement) et Perso : 12 boutons à composer parmi toutes les commandes d'Onde (réglage gardé sur le téléphone). « Ajouter à l'écran d'accueil » l'ouvre comme une application. Sécurité : désactivée par défaut, code d'appairage à 6 chiffres renouvelé à chaque activation, blocage 30 s après 10 codes faux, commandes de fichier et « Quitter » non exposées. Au premier lancement, Windows demande d'autoriser Onde sur les réseaux privés.
- **Fluidité** : les vues ne se redessinent que lorsque le projet change ; pendant la lecture, seules la tête de lecture, les compteurs et les vumètres qui bougent sont redessinés, et seuls les événements visibles dans la zone à redessiner sont dessinés.
- **Effets Onde** (inserts, en tête de la liste des effets ; code original, conçu d'après les avis des utilisateurs de mixage) :
  - **Onde EQ** : égaliseur paramétrique 7 bandes, analyseur de spectre en temps réel, points de bande à glisser (fréquence et gain), molette pour la largeur (Q), double-clic pour activer ou désactiver. Coupe-bas et coupe-haut à 12, 24 ou 48 dB/oct. Chaque bande agit au choix en stéréo, à gauche, à droite, sur le Mid ou sur le Side. Les bandes 2 à 6 peuvent être **dynamiques** (seuil, plage) : le gain ne bouge que quand la fréquence dépasse le seuil, et la courbe affiche le réglage et le gain réel.
  - **Onde Comp** : détection crête ou RMS, styles Propre, Opto (relâchement qui s'allonge avec la compression) et Vintage (genou large, saturation douce), relâchement automatique à double enveloppe, liaison stéréo réglable, anticipation de 5 ms (latence compensée), écoute de la détection, filtre coupe-bas de détection, mélange parallèle, courbe de transfert et réduction de gain affichées.
  - **Onde Limiteur** : anticipation 1,5 ms (latence compensée), plafond garanti en **crête vraie** (suréchantillonnage x4, ITU-R BS.1770-4), mesure de sonie EBU R128 (momentanée, court terme, intégrée avec portes -70 LUFS et -10 LU), cible -14 / -16 / -23 LUFS avec écart affiché.
  Paramètres automatisables, enregistrés avec le projet, annulables.
- **Modulateurs** (Projet › Modulateurs de la piste…, Ctrl+Maj+L, ou clic droit sur une piste) : LFO (11 formes, libre, calé sur le transport ou relancé à chaque note, cadence en hertz ou en valeurs de notes), pas à pas (jusqu'à 64 pas à dessiner), suiveur d'enveloppe (ducking : il suit le niveau d'une piste et peut agir sur une autre) et aléatoire. Chaque modulateur s'affecte à n'importe quel paramètre (volume, panoramique, réglages des effets Onde et Tracktion) avec une profondeur de -100 à +100 %, plusieurs affectations possibles. Oscilloscope en direct, enregistré avec le projet, annulable.
- **Comparatif avec Cubase Pro** et feuille de route : [docs/comparatif-cubase-pro.md](docs/comparatif-cubase-pro.md).
- **Historique des modifications** (Édition › Historique des modifications) : toutes les actions du projet dans l'ordre, l'état actuel en surbrillance, les étapes annulées en grisé ; un clic ramène le projet à n'importe quelle étape.
- **Rendre définitif** (Audio › Rendre définitif, ou bouton de l'éditeur audio) : réécrit l'événement tel qu'il sonne (gain, fondus, inversion, transposition, mode musical ; sans les effets de la piste) dans un nouveau fichier qui le remplace, au même endroit. Annulable. Fichiers dans « <projet> - Audio ».
- **Écran d'accueil** (au démarrage, ou Fichier › Écran d'accueil, Ctrl+Maj+H) : modèles de projet (vide, Enregistrement : voix, guitare, basse, batterie et réverbération ; Production : basse, accords et lead au synthé, voix et réverbération ; Démonstration), projets récents (double-clic pour rouvrir), Ouvrir…, et « Afficher au démarrage » à décocher.
- **Projet neuf** : un projet créé ou ouvert n'est pas marqué « modifié » tant que vous n'y touchez pas (pas de question inutile à la fermeture).
- **Plantage** : si Onde s'arrête brutalement, la pile d'appels est écrite dans `%APPDATA%\Onde\plantage.txt` (diagnostic).
- **Logo** : une onde dont l'amplitude enfle puis s'éteint, en ruban effilé bleu → turquoise (`app/Resources/logo.svg`, vectoriel). Icône de l'application (générée par `Onde --render-logo <dossier>`, PNG de 16 à 1024 px), fenêtre « À propos », barre d'outils, et page du téléphone (icône d'onglet et d'écran d'accueil).
- Toutes les commandes : Ctrl+K (recherche) et F1 (liste des raccourcis).

## Tests automatiques

| Test | Commande | Résultat |
| --- | --- | --- |
| Isolation du cœur | `onde-sandbox-test onde-plugin-worker` | plantage détecté, plugin bloqué arrêté, piste voisine intacte |
| Vrai VST3 | `onde-vst3-test onde-plugin-host <synth.vst3> <crash.vst3>` | 10/10 |
| Plugins installés | `Onde --plugin-test <dossier>` | chaque VST3 du PC : chargement isolé, son, fenêtre, réglage sauvegardé, paramètres exposés (réglés depuis Onde, suivis au chargement d'un préréglage), automation audible au rendu, 3 s de lecture en temps réel sans retard (7/7 sur ce PC, dont Archetype (Neural DSP), Valhalla Supermassive, TDR Nova ; SSDSampler5 muet tant qu'aucun kit n'est choisi) |
| Interface | `Onde --ui-test <dossier>` | 113/113 : automation d'un paramètre d'effet (0 dB puis +11,9 dB au rendu) ; modulateurs (LFO carré 2 Hz sur le volume : 16,3 dB de variation au rendu, fenêtre et cartes, enregistrés avec le projet, annuler) ; effets Onde (EQ : cloche +11,9 dB pour +12, coupe-bas -80 dB à 100 Hz, pentes 12 dB/oct -40,0 dB et 48 dB/oct -128 dB, canaux gauche / droite / mid / side, bande dynamique -6,00 dB pour -6 ; Comp : -19,49 dBFS pour -19,5 attendus, RMS -17,27 pour -17,25, anticipation 5 ms, opto ; Limiteur : crête -1,00 dBFS, latence 1,5 ms, sonie -20,00 LUFS pour un 1 kHz à -20 dBFS, crête vraie -1,00 dBTP au plafond -1 sur un signal inter-échantillons ; réglages enregistrés), historique des modifications, rendre définitif (même son à 129 dB), écran d'accueil (modèles, projets récents), télécommande (vraies requêtes HTTP : page, code exigé, état, commandes, pistes, refus des commandes dangereuses, blocage après 10 codes faux, QR code), mode musical (étirement réel au tempo doublé, MIDI et audio recalés sur les mesures, annuler), repli des dossiers enregistré, vumètre de groupe ; export WAV / FLAC / MP3 / OGG lisibles, normalisation à l'export, export piste par piste ; éditeur audio (gain, fondus, normaliser, inverser, transposer appliqués au rendu ; supprimer / rogner une plage), dossiers (muet effectif au rendu), groupes (volume effectif au rendu), annuler ; piste de tempo (sauts, rampes, compteur, annuler), pistes, événements, outils, sélection multiple, copier/coller, automation appliquée au rendu, pistes FX et envois audibles, repères, enregistrer / rouvrir (audio retrouvé), export, plantage et relance |
| Séparateur | `Onde --stems-test <dossier>` | sur le projet de démo (mix à 48 kHz) : très précis : SDR batterie 28 dB, basse 28 dB, autres 13,6 dB, aucune voix inventée (-46 dB), somme des sources = mix (31 dB) ; rapide (`ONDE_STEMS_MODE=rapide`) : batterie 15,3 dB, basse 17,6 dB, autres 12,3 dB, somme exacte, import aligné, original muet, annuler |
| Fluidité | `Onde --perf-test <dossier>` | projet de 14 pistes / 96 événements joué sur la carte son, une mesure par vue : fil de l'interface 17 à 24 % d'un cœur (56 à 63 % avant optimisation), processus ≈ 50 % (≈ 145 % avant), charge audio ≈ 6 % ; `ONDE_PERF_PROFILE=1` ajoute le profil des fonctions les plus coûteuses |
| Temps réel | `Onde --live-test <dossier>` | lecture sur la carte son, son au master, vumètre de la tranche de groupe, enregistrement d'une prise, copie de la prise à l'enregistrement du projet |

Traces de diagnostic : définir la variable d'environnement `ONDE_TRACE=1`.

## Limites connues

- Pas encore : macOS.
- MP3 : encodeur de Windows (Media Foundation), 44,1 ou 48 kHz uniquement ; ailleurs, choisir FLAC ou OGG.
- Séparateur : les sources de demucs.cpp (MIT) sont dans `app/Worker/demucs`, réécrites par endroits pour MSVC (normalisations, softmax, im2col : mêmes calculs, résultats identiques à 60 dB près) ; les 4 modèles (≈ 340 Mo, `deps/demucs-models`) sont copiés à côté d'Onde dans `modeles-demucs`.
- Les VST3 de type effet ne reçoivent pas le MIDI.
- Licence : Tracktion Engine et JUCE sont GPLv3 ou commerciaux ; distribuer Onde impose l'un ou l'autre.

