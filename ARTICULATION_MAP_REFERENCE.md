# Articulation maps : signes détectés automatiquement

Référence pour nommer les articulations d'une map (éditeur ou fichier `.txt`) afin qu'un signe de la partition les
sélectionne automatiquement dans l'articulation lane.

## Règles de correspondance

- **Nom implicite** (ligne sans `=`) : seul le **dernier segment** du nom compte (`Strings > Short > Staccato` →
  `Staccato`), **casse et espaces ignorés**. `Snap Pizzicato`, `snappizzicato` et `SnapPizzicato` sont équivalents.
- **Liste explicite** (`Nom = a, b, c`, ou les déclencheurs de l'éditeur) : casse ignorée, mais **pas d'espaces** :
  écrire `SnapPizzicato`, pas `snap pizzicato` (sinon erreur « unknown score articulation »). Une liste explicite
  remplace le nom implicite.
- **Priorité** : si une note porte plusieurs signes (ex. accent + staccato, ou staccato sous « pizz. »), c'est la
  **première entrée de la map** qui correspond qui gagne. Une entrée désactivée (`-`) est ignorée.
- **Ordre de résolution** pour chaque note : marque posée dans la lane > signe détecté (ce document) > marque
  latched précédente > articulation par défaut (`*`).
- Un nom absent de ce document (`Spiccato`, `Flautando`…) n'est jamais détecté seul : soit on le pose à la main dans
  la lane, soit on lui donne une liste explicite (`Spiccato = Staccato, Staccatissimo`).

```text
*C0       Legato Sustain                 ; défaut (pas "Legato" seul, voir ⚠️ plus bas)
D0        Spiccato   = Staccato, Staccatissimo
E0        Pizzicato                      ; signe "+" ET texte "pizz."
F0        Sul Ponticello                 ; texte "sul pont."
G0        Short > Marcato
A0        Tremolo    = Tremolo16th, Tremolo32nd, Tremolo64th
```

## 1. Signes de notation

Détectés sur l'**accord** (pas sur une note isolée de l'accord). L'option « Lire » (play) de l'élément doit être
active.

### Articulations

| Signe | Nom dans la map |
|---|---|
| Staccato (point) | `Staccato` |
| Staccatissimo (goutte, trait, coin) | `Staccatissimo` |
| Tenuto | `Tenuto` |
| Accent `>` | `Accent` |
| Marcato `^` | `Marcato` |
| Accent doux | `SoftAccent` |
| Accent + staccato | `Accent` **et** `Staccato` |
| Marcato + staccato | `Marcato` **et** `Staccato` |
| Marcato + tenuto | `Marcato` **et** `Tenuto` |
| Tenuto + staccato (louré) | `Tenuto` **et** `Staccato` |
| Tenuto + accent | `Tenuto` **et** `Accent` |
| Accent doux + staccato / + tenuto / + tenuto + staccato | `SoftAccent` + `Staccato` / `Tenuto` |
| Laissez vibrer (signe d'articulation) | `LaissezVibrer` |
| sf, sfz, sff, sfff, sffz, sfffz, sfp, sfpp, s (nuances) | `Subito` |

Un signe combiné active les deux noms : c'est l'ordre des entrées de la map qui décide lequel joue.

### Cordes, cuivres, guitare

| Signe | Nom dans la map |
|---|---|
| Pizz. main gauche (`+`) | `Pizzicato` |
| Pizz. Bartók (snap) | `SnapPizzicato` |
| Tiré / poussé | `DownBow` / `UpBow` |
| Jeté | `Jete` |
| Harmonique (symbole) | `Harmonic` |
| Sourdine (`+`, sourdine fermée / mi-fermée, harmon fermée, electric mute) | `Mute` |
| Ouvert (`o`, sourdine ouverte, harmon ouverte, electric unmute) | `Open` |
| Fade in / fade out (guitare) | `FadeIn` / `FadeOut` |
| Slap / pop (basse) | `Slap` / `Pop` |
| Tapping main gauche / main droite | `LeftHandTapping` / `RightHandTapping` |
| Chute (chord line « Fall ») | `Fall` |
| Chute courte (symboles cuivres « lip » / « rough ») | `QuickFall` |
| Doit / plop / scoop | `Doit` / `Plop` / `Scoop` |
| Bend cuivres (symbole) | `BrassBend` |
| Bend guitare | `Multibend` |

### Lignes et liaisons (toutes les notes couvertes)

| Signe | Nom dans la map |
|---|---|
| Liaison d'expression, hammer-on / pull-off | `Legato` ⚠️ |
| Pédale, let ring | `Pedal` |
| Palm mute | `PalmMute` |
| Ligne de vibrato | `Vibrato` |
| Vibrato large (symbole) | `WideVibrato` |
| Ligne de trille | `Trill` |
| Lignes prall (up prall / prall down / prall prall) | `UpPrall` / `PrallDown` / `LinePrall` |
| Glissando (style chromatique, touches blanches / noires, diatonique) | `DiscreteGlissando` |
| Glissando de style portamento | `ContinuousGlissando` |

⚠️ **`Legato`** : une entrée nommée `Legato` (même `Strings > Legato`) est sélectionnée par **chaque note sous une
liaison**. Si ce n'est pas voulu, renommer (`Legato Sustain`, `Leg`…) ou donner une liste explicite.

### Ornements

| Signe | Nom dans la map |
|---|---|
| Trille (style par défaut / baroque) | `Trill` / `TrillBaroque` |
| Trille courte / pralltriller (défaut / baroque) | `ShortTrill` / `UpperMordentBaroque` |
| Mordant | `Mordent` |
| Prall mordent | `PrallMordent` |
| Up mordent / down mordent | `UpMordent` / `DownMordent` |
| Prall up / prall down / up prall / line prall | `PrallUp` / `PrallDown` / `UpPrall` / `LinePrall` |
| Tremblement | `Tremblement` |
| Grupetto | `Turn` |
| Grupetto inversé | `InvertedTurn` |

### Trémolos, arpèges, notes d'agrément, respiration

| Signe | Nom dans la map |
|---|---|
| Trémolo 8 / 16 / 32 / 64 (128 et 256 → 64) | `Tremolo8th` / `Tremolo16th` / `Tremolo32nd` / `Tremolo64th` |
| Buzz roll | `TremoloBuzz` |
| Arpège / vers le haut / vers le bas | `Arpeggio` / `ArpeggioUp` / `ArpeggioDown` |
| Arpège droit vers le haut / vers le bas | `ArpeggioStraightUp` / `ArpeggioStraightDown` |
| Acciaccatura (petite note barrée) | `Acciaccatura` |
| Appoggiature avant (1/4, 1/8, 1/16, 1/32) | `PreAppoggiatura` |
| Notes d'agrément après | `PostAppoggiatura` |
| Respiration / césure | `Breath` |

Les notes d'agrément sélectionnent l'articulation de la **note principale** qui les porte.

### Handbells

| Signe | Nom dans la map |
|---|---|
| TD / BD / RT / PL / SB / VIB | `ThumbDamp` / `BrushDamp` / `RingTouch` / `Pluck` / `SingingBell` / `SingingVibrate` |
| Mallet on table / suspended / lift | `MalletBellOnTable` / `MalletBellSuspended` / `MalletLift` |
| Pluck lift / gyro | `PluckLift` / `Gyro` |
| Martellato / lift / hand / muted | `Martellato` / `MartellatoLift` / `HandMartellato` / `MutedMartellato` |

## 2. Techniques de jeu en texte

Seuls les **textes de technique de jeu** sont reconnus : ceux de la palette « Texte » (ou un texte dont le type de
technique est réglé dans Propriétés). Un texte de portée ordinaire tapé à la main (« pizz. ») ne l'est pas.

La technique **reste active** sur toutes les notes de l'instrument jusqu'au texte suivant ; « arco » / « normal »
la terminent et l'articulation par défaut (`*`) reprend.

| Texte de la palette | Nom dans la map |
|---|---|
| pizz. | `Pizzicato` |
| arco, normal | — (fin de la technique) |
| mute | `Mute` |
| open | `Open` |
| col legno | `ColLegno` |
| sul pont. | `SulPonticello` |
| sul tasto | `SulTasto` |
| détaché | `Detache` |
| martelé | `Martele` |
| tremolo | `Tremolo64th` |
| vibrato | `Vibrato` |
| legato | `Legato` |
| harmonics | `Harmonic` |
| distort | `Distortion` |
| overdrive | `Overdrive` |
| jazz tone | `JazzTone` |
| Handbells : swing (up / down), echo, LV, R | `Swing`, `Echo`, `Pedal`, `Ring` |

Exemple : sous « pizz. », une note avec un point de staccato prend `Staccato` si cette entrée est placée **avant**
`Pizzicato` dans la map, sinon `Pizzicato`.

## 3. Noms valides mais jamais détectés

Ces noms sont acceptés (pas d'erreur) mais aucun signe ne les déclenche aujourd'hui : à poser à la main dans la lane.

- Têtes de notes et notes fantômes : `GhostNote`, `CrossNote`, `CrossLargeNote`, `CrossOrnateNote`, `CircleNote`,
  `CircleCrossNote`, `CircleDotNote`, `DiamondNote`, `SlashNote`, `PlusNote`, `SquareNote`, `MoonNote`,
  `TriangleUpNote`, `TriangleDownNote`, `TriangleLeftNote`, `TriangleRightNote`, `TriangleRoundDownNote`,
  `SlashedForwardsNote`, `SlashedBackwardsNote`
- Autres : `RandomPizzicato`, `MoltoVibrato`, `SenzaVibrato`, `SlideInAbove`, `SlideInBelow`, `SlideOutUp`,
  `SlideOutDown`, `Crescendo`, `Diminuendo`, `TremoloBar`
- `LaissezVibrer` via la **liaison** l.v. attachée à la note (seul le signe d'articulation la déclenche)
- `Standard` et `Undefined` ne déclenchent jamais rien.
