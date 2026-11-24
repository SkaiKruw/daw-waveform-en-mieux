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
| Écran d'accueil (Hub), modèles de projet, modèles personnels, fenêtre de paramètres | ✅ | Fichier › Enregistrer comme modèle, Fichier › Paramètres |
| Piste d'arrangement (sections, enchaînements) | ❌ | priorité moyenne |
| Piste d'accords, assistant d'accords | ❌ | |
| Versions de piste, couloirs et comping de prises | ❌ | priorité haute pour l'enregistrement |
| Geler / rendre une piste sur place | ✅ | Geler la piste (Ctrl+Alt+F) et « Rendre définitif » par événement |
| Piste de paroles | ✅ | paroles calées au tapotement, prompteur, affichées dans la règle |
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
| Couloirs de contrôleurs (CC, pitch bend) | ✅ | vélocité, CC 0-127, pitch bend, aftertouch ; dessin libre et lignes |
| Éditeur de batterie, piste de batterie, boîte à rythmes | 🟡 | Batteur automatique (6 styles), modifiable note à note |
| Séquenceur de motifs | ❌ | |
| Éditeur de partition | 🟡 | tablature (guitare, basse, ukulélé) ; pas de portée classique |
| Échantillonneur (piste sampler) | ❌ | |
| Instruments intégrés | ✅ | Onde Synth (5 moteurs dont cordes pincées en modèle physique, 44 sons), 4OSC ; VST3 isolés |

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
| Automation et modulation des paramètres des VST3 isolés | ✅ | paramètres publiés par le processus isolé, dans les deux sens (fenêtre du plugin → Onde) |
| Automation de tous les paramètres dans la fenêtre de projet | ✅ | volume, panoramique et chaque paramètre de chaque effet |
| Faders VCA | ✅ | dossier VCA |
| Inversion de polarité, analyse du mix (LUFS, crête vraie, corrélation), écoute mono et atténuée | ✅ | MixConsole ; l'écoute n'agit jamais sur l'export |
| Control Room | 🟡 | Mono et -20 dB sur la sortie ; pas de sorties casque séparées |
| Réverbération, delay, modulation, saturation de qualité | ✅ | Onde Espace, Onde Écho, Onde Saturation, Onde Modulation (chorus, ensemble, flanger, phaser, vibrato) |
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
2. ~~Modulateurs (LFO, enveloppe)~~, ~~paramètres des VST3 isolés~~, ~~automation de tous les paramètres~~ : fait. Ensuite : « tout est déjà là » (voir la dernière section).
3. ~~Couloirs de contrôleurs MIDI dans l'éditeur de notes~~ : fait.
4. ~~Geler une piste ; faders VCA ; paroles ; tablature ; analyse du mix~~ : fait.
5. Couloirs de prises et comping.
6. Piste d'arrangement.
7. ~~Effets Onde : réverbération, delay, saturation, modulation~~ : fait. Reste : de-esser, gate.
8. ~~Paramètres, modèles personnels, fenêtre d'instrument redessinée~~ : fait.
9. Instruments échantillonnés (piano, batterie, cordes enregistrés) en packs téléchargeables à la demande, hors installateur.

## « Tout est déjà là » : l'accueil d'un logiciel grand public, la puissance de Cubase

Ce que les débutants aiment dans les logiciels de musique grand public (GarageBand en tête) : on l'ouvre, et il y a
déjà de quoi faire un morceau entier. Des instruments qui sonnent tout de suite, une batterie qui joue toute seule,
des boucles, des réglages simples, aucun téléchargement ni configuration. Onde reprend cette **idée** (pas le contenu
ni l'apparence : instruments, sons et boucles sont créés par Onde, de façon originale).

| Ce qu'on attend en ouvrant le logiciel | Onde aujourd'hui | Ce qu'il faut faire |
| --- | --- | --- |
| Des instruments prêts à jouer, rangés par famille (piano, claviers, basses, cordes, nappes, leads, batterie) | ✅ **Onde Synth** : 5 moteurs (dont cordes pincées en modèle physique : guitares 6, 7, 8, 12 cordes, basses, harpe), 44 sons en 10 familles | **Bibliothèque d'instruments Onde** : moteurs de synthèse originaux (piano et piano électrique par modélisation, basses, nappes, cordes, cuivres synthétiques, boîte à rythmes par synthèse) avec des dizaines de préréglages nommés en français. Rien à télécharger. |
| Une batterie qui joue toute seule et suit le morceau | ✅ Batteur (6 styles, pavé, remplissages) ; 🟡 suivre la piste d'arrangement : à faire | **Batteur Onde** : styles (pop, rock, hip-hop, électro, ballade), complexité et intensité réglables, remplissages en fin de section, suit la piste d'arrangement. |
| Des boucles à glisser, calées sur le tempo et la tonalité du projet | ❌ | **Boucles Onde** : générées par Onde à partir de ses instruments (batterie, basse, accords, arpèges), toujours au tempo et dans la tonalité du projet, prévisualisation en un clic. |
| Quelques gros boutons simples par instrument | ✅ commandes rapides (chaînes et Onde Synth) | **Commandes rapides** : 4 à 8 macros par piste (« Chaleur », « Espace », « Brillance », « Punch ») qui pilotent plusieurs réglages à la fois (s'appuie sur les modulateurs et l'automation de tous les paramètres). |
| Des sons de voix et de guitare tout faits | ✅ 14 chaînes prêtes | **Chaînes prêtes** : « Voix pop », « Voix rap », « Podcast », « Guitare claire », « Guitare saturée », « Basse ronde »… construites avec les effets Onde. |
| Jouer sans clavier MIDI | ✅ **Clavier** (écran, AZERTY / QWERTY), notes enregistrées | **Clavier à l'écran et clavier de l'ordinateur** (touches A à L = notes, vélocité, octaves). |
| Démarrer par ce qu'on veut faire | 🟡 écran d'accueil, modèle « Production » avec batterie, basse, accords, nappe, lead | Modèles « Beat », « Groupe », « Voix et guitare », « Podcast », prêts avec instruments et chaînes. |
| Apprendre | ❌ | Bulles d'aide au premier lancement, projet de démonstration commenté. |

Fait : clavier à l'écran, bibliothèque d'instruments, chaînes prêtes, commandes rapides, batteur, nouvelle piste à cartes. Ensuite :
boucles générées → modèles enrichis.
