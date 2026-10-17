# 58 · Clouds dans le Sampler de Model-TG : granulaire, reverb, spectral

Demande du 15/10/2026 : un mode de lecture de plus dans le Sampler de Model-TG, à côté de Granular, Stretch, Pluck et
Wave, qui granule un sample avec le moteur de Clouds (Émilie Gillet, MIT). Pas de nouvelle machine : le Sampler a déjà
la mémoire des samples, le rééchantillonnage, les p-locks, les slide trigs et le routage des LFO.

Statut : **étape 0 (investigation)**. Rien n'est compilé dans le firmware. Les étapes sont dans la section 7.

## 1. Ce qu'est Clouds

Source : `clouds/` de pichenettes/eurorack, commit `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4` (celui de Braids dans
`gen_macro.py`). Quatre modes de lecture, `enum PlaybackMode` dans `clouds/dsp/granular_processor.h` :
`GRANULAR`, `STRETCH`, `LOOPING_DELAY`, `SPECTRAL`.

Paramètres (`clouds/dsp/parameters.h`, tous en `float`) : `position`, `size`, `pitch`, `density`, `texture`,
`dry_wet`, `stereo_spread`, `feedback`, `reverb`, et les booléens `freeze`, `trigger`, `gate`.

## 2. Ce que Model-TG a déjà, et ce qui reste à faire

Model-TG (commit `70b39dd6787770ebefc7a2d78dea1678ec012679`, v1.1.0) a un granulaire et un stretch dans son Sampler
(`docs/USER_GUIDE.md`, `src/model_tg.s` : `sw_gran`, `sw_g_pos`, `gran_fill`). Pour ne pas faire un doublon :

| Fonction de Clouds | Déjà dans Model-TG | Apport de ce mode |
|---|---|---|
| Granulaire (position, taille, densité, texture) | oui (Granular : Position, Grain Size, Density, Spray) | grains de Clouds, texture et spread propres |
| Stretch (durée et hauteur séparées) | oui | — |
| Looping delay | non | oui, si on le retient plus tard |
| Spectral (phase vocoder) | non | oui : la partie la plus coûteuse, étape 3 |
| Reverb stéréo de Clouds | non | oui, étape 2 |
| Freeze par pas (p-lock) | non | oui, étape 1 |

## 3. Points d'insertion dans Model-TG (`src/model_tg.s`)

- **Sélection du mode** : `loop_mode` (0..6 aujourd'hui, Clouds = 7). La condition de rendu est en `sv_*` : `cmpil #5`
  (Pluck), `cmpil #6` (Wave), puis `subql #3` / `cmpil #1` (granulaire et stretch) vers `gran_fill`.
- **Déclenchement par note** : `sp_gran_trig`, `sp_pluck_trig`, `sp_wave_trig` (ligne ~1114-1126).
- **Dessin des potards** : `sw_*` (ligne ~530-900), selon le mode, avec les valeurs des quatre potards.
- **Libellés** : `src/sampler_strings.inc` ; icônes : `src/sampler_icons`.
- **Sauvegarde** : le mode vit dans le même champ que les autres, donc pas de format de projet nouveau à priori
  (à vérifier, voir section 6).

Le fichier fait environ 21 500 lignes d'assembleur au total.

## 4. Coût et contraintes à mesurer

- Clouds est écrit en `float` : 372 lignes qui en utilisent dans `dsp/`, et le cœur ColdFire 54418 n'a pas de FPU.
  Il faut une version en point fixe, comme celle de MACRO (`notes/43`) et ACID (`notes/54`).
- Le phase vocoder (`dsp/pvoc/stft.cc`) appelle la FFT de CMSIS (`arm_rfft_fast_f32`). Il faut une FFT propre, en point
  fixe, à écrire.
- Le granulaire de Clouds se compile sans erreur pour `-mcpu=54418` avec GCC 13.3 (`granular_processor.cc`), sauf
  l'en-tête matériel `clouds/drivers/debug_pin.h` (STM32), à remplacer par un stub. Test du 15/10/2026, `-O2`, sans
  exceptions ni RTTI.
- Budget : le coût d'une voix de Model-TG et de chaque mode n'est pas encore mesuré pour ce firmware. À mesurer en
  émulation avant de fixer le nombre de voix de Clouds.

## 5. Décisions prises

- **Un mode, pas une machine** : Clouds est un mode de reproduction du Sampler (`loop_mode` = 7).
- **Entrée = un sample du Sampler** : la source est un sample chargé (preset Sampler ou rééchantillonnage). Pas
  d'entrée audio en direct : le Model:Cycles n'en a pas.
- **Ordre des étapes** : granulaire d'abord, reverb ensuite, spectral en dernier (chaque étape a sa preuve en émulation
  et son PR en brouillon).
- Tout code de Clouds est porté avec sa licence MIT conservée (`LICENSE-Clouds` à ajouter) et une ligne dans
  `PROVENANCE.md`.

## 6. Questions ouvertes

1. Les paramètres de Clouds (position, taille, pitch, densité, texture, freeze) : quatre potards suffisent-ils pour la
   première version, ou faut-il une deuxième page (SHIFT ou SETTINGS) ?
2. La sauvegarde : confirmer que les valeurs de Clouds passent par le même champ que celles de Model-TG dans le pattern
   (lecture de `gran_fill` et des enregistrements, sans changer le format).
3. Le freeze par pas est-il un p-lock sur un paramètre dédié, ou un trig à part ?

## 7. Étapes

0. Investigation et cette note. **Fait** (brouillon).
1. Granulaire en point fixe, mode 7, sans reverb ni spectral. Preuve en émulation contre le Model-TG d'origine.
2. Reverb de Clouds, mesure de CPU, prise en compte dans le budget.
3. Spectral : FFT en point fixe, phase vocoder, mesure de CPU, décision de le garder ou non.

Test sur la machine : à faire par le propriétaire du Model:Cycles après chaque étape, pas avant.

## 8. Mesures (étape 1, en cours)

Ordinateur, sans émulateur. Référence : le planificateur `granular_sample_player.h` de Clouds, en `float`, compilé
pour l'ordinateur (stmlib en mode TEST), un sinus de 220 Hz, 32 échantillons par bloc, 1200 blocs.

- Clouds, `window_shape` = 0.6 : pic 0.47, RMS 0.18.
- Clouds, `window_shape` = 0.3 : pic 0.59, RMS 0.21.
- Clouds, `window_shape` = 0.5 **exactement** : silence (pic 0.00). Dans `grain.h`, pour 0.5 la pente est à 0 et le
  lissage aussi, donc l'enveloppe passe par la branche « pente » qui multiplie par 0. Quirk de Clouds à noter ; le port
  point fixe ne le reproduit pas.
- Port point fixe (`tools/machines/clouds_gran/`), même nombre de grains (1 visé) et même taille (4096) : pic 0.36,
  RMS 0.07.

Écart à résorber avant la prise en compte en émulation : loi de panoramique (Clouds, mono : gains à sin(pan) = 0.71),
lissage de l'enveloppe par la table de fenêtre (`lut_window`, absente du port), normalisation de gain (`rsqrt` au-delà
de deux grains) et correspondance des paramètres (`overlap` élevé au cube dans Clouds). Pas de mesure d'émulation
encore.

## 9. Alignement du port sur Clouds (étape 1, même réglage des deux côtés)

Mêmes réglages : stéréo, `size` = 4096 échantillons, `overlap` = 0.5 (une cible de 1 grain), `window_shape` = 0.3,
position 0.5, sinus de 220 Hz à 0.366 plein échelle, 1200 blocs de 32.

| | pic | RMS |
|---|---|---|
| Clouds (float) | 0.834 | 0.294 |
| Port point fixe | 0.905 | 0.294 |

Corrections faites dans le port, d'après cette mesure :
- normalisation de gain de Clouds (`1/sqrt(n-1)` au-delà de deux grains, gain de fenêtre `1 + 2·shape` fondu par
  `overlap`) : absente avant ;
- pente de fenêtre non bornée à 1 (elle vaut 1.61 pour shape 0.3) ;
- espacement des grains : `grain_size_hint / cible`, et non `× 8` (erreur du port).

Reste : le pic est 8 % plus haut dans le port. Pistes : lissage de la normalisation dans Clouds (`ONE_POLE` 0.01 par
échantillon, qui part de 1.0), interpolation Hermite de Clouds (absente du port, qui lit linéairement), et la pente
bornée à 2.0 dans le port. Mesure faite sur l'ordinateur seulement.

### 9 bis. Suivi du nombre de grains et lissage de la normalisation

Ajoutés au port, comme dans Clouds : `SLOPE(num_grains_, actifs, 0.9, 0.2)` (en Q16), normalisation `1/sqrt(n-1)`
sur le compte lissé, et `ONE_POLE(gain_normalization_, cible, 0.01)` par échantillon.

| | pic (tout le test) | pic (après 600 blocs) | RMS (après 600 blocs) |
|---|---|---|---|
| Clouds (float) | 0.834 | 0.477 | 0.296 |
| Port point fixe | 0.903 | 0.475 | 0.292 |

En régime établi, le port suit Clouds à 0.5 % près. L'écart restant de 8 % est dans le démarrage (premiers 600 blocs) :
le compte de grains actifs n'est pas pris au même instant que dans Clouds (Clouds le prend après l'affectation des
nouveaux grains du bloc, le port après leur rendu), et l'interpolation Hermite de Clouds n'est pas portée. À traiter
avec la prise en compte en émulation, pas avant.
Coût : les produits 32 × 32 bits passent par `int64_t`, donc par libgcc sur le 54418 ; à mesurer en émulation.
