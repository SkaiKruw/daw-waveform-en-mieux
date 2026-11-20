# Onde face à Cubase Pro — état et feuille de route

Comparatif fonction par fonction avec Cubase Pro (versions 13 à 15). **✅** présent dans Onde, **🟡** partiel,
**❌** absent. Les priorités tiennent compte de l'usage courant (enregistrement, production, mixage) et de ce
que les utilisateurs mettent en avant dans les tests de logiciels et de plugins.

## Projet, édition, arrangement

| Fonction Cubase Pro | Onde | Remarque |
| --- | --- | --- |
| Fenêtre de projet, outils (sélection, plage, ciseaux, colle, gomme, zoom, muet, crayon) | ✅ | |
| Ligne d'infos éditable, inspecteur | ✅ | |
| Piste de tempo (sauts, rampes), signature, mode musical | ✅ | |
| Repères, délimiteurs, cycle | ✅ | |
| Dossiers, groupes (sous-mixage) | ✅ | |
| Historique des modifications | ✅ | |
| Écran d'accueil (Hub), modèles de projet | ✅ | |
| Piste d'arrangement (sections, enchaînements) | ❌ | priorité moyenne |
| Piste d'accords, assistant d'accords | ❌ | |
| Versions de piste, couloirs et comping de prises | ❌ | priorité haute pour l'enregistrement |
| Geler / rendre une piste sur place | 🟡 | « Rendre définitif » par événement ; pas encore par piste |
| Courbes de volume dessinées dans les événements | 🟡 | gain et fondus par poignées |
| Alignement audio, détection de transitoires, AudioWarp | ❌ | |
| VariAudio (correction de hauteur) | ❌ | |
| Piste vidéo | ❌ | |
| Éditeur logique, macros | ❌ | |
| Éditeur de raccourcis | 🟡 | liste des raccourcis (F1), palette Ctrl+K ; pas de réaffectation |

## MIDI et instruments

| Fonction | Onde | Remarque |
| --- | --- | --- |
| Éditeur de notes, vélocité, quantification, assistant de gamme | ✅ | |
| Couloirs de contrôleurs (CC, pitch bend) | ❌ | priorité haute |
| Éditeur de batterie, piste de batterie, boîte à rythmes | ❌ | |
| Séquenceur de motifs | ❌ | |
| Éditeur de partition | ❌ | |
| Échantillonneur (piste sampler) | ❌ | |
| Instruments intégrés | 🟡 | 4OSC (Tracktion) ; VST3 isolés |

## Mixage et effets

| Fonction | Onde | Remarque |
| --- | --- | --- |
| MixConsole, inserts, envois, pistes FX, groupes, vumètres | ✅ | |
| Automation (lecture, écriture, points) | ✅ | |
| Plugins VST3 isolés (plantage sans effet sur Onde) | ✅ | **avantage d'Onde** : Cubase n'isole pas les plugins |
| Égaliseur paramétrique avec analyseur | ✅ | Onde EQ |
| Égalisation dynamique, M/S par bande | 🟡 → ✅ | Onde EQ 2 (cette version) |
| Compresseur, limiteur | ✅ | Onde Comp, Onde Limiteur |
| Limiteur True Peak, mesure LUFS | 🟡 → ✅ | Onde Limiteur 2 (cette version) |
| Bande de voie intégrée (EQ, comp, gate, saturation par voie) | ❌ | |
| Modulateurs (LFO, enveloppe, pas à pas) sur n'importe quel paramètre | ✅ | LFO, pas à pas, suiveur d'enveloppe, aléatoire ; profondeur par affectation |
| Automation et modulation des paramètres des VST3 isolés | ❌ | priorité haute : exposer les paramètres du processus isolé |
| Faders VCA | ❌ | |
| Control Room | ❌ | |
| Réverbération, delay, modulation, saturation de qualité | 🟡 | effets de base Tracktion ; versions Onde prévues |
| Séparation de sources (stems) | ✅ | **avantage d'Onde** : Demucs v4 intégré |

## Fichiers, export, autres

| Fonction | Onde | Remarque |
| --- | --- | --- |
| Export WAV, FLAC, MP3, OGG, normalisation LUFS, pistes séparées | ✅ | |
| Enregistrement, punch, monitoring | ✅ | |
| Télécommande sur téléphone | ✅ | **avantage d'Onde** : Cubase passe par une application payante |
| Format DAWproject (échange avec d'autres logiciels) | ❌ | |
| Moteur 64 bits, 192 kHz | 🟡 | 32 bits flottant |
| macOS | ❌ | Windows seulement |

## Ordre de travail proposé

1. Effets Onde 2 : EQ dynamique et M/S, compresseur complet, limiteur True Peak et LUFS (en cours).
2. ~~Modulateurs (LFO, enveloppe)~~ fait. Ensuite : paramètres des VST3 isolés (automation, modulation), automation de tous les paramètres dans la fenêtre de projet.
3. Couloirs de contrôleurs MIDI dans l'éditeur de notes.
4. Geler / rendre une piste sur place ; faders VCA.
5. Couloirs de prises et comping.
6. Piste d'arrangement.
7. Effets Onde : réverbération, delay, saturation, de-esser, gate.
