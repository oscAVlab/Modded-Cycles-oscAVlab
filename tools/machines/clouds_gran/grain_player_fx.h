// Planificateur de grains en point fixe pour le MCF54418, d'après granular_sample_player.h de Clouds (Émilie
// Gillet, MIT). Adaptation au Sampler de Model-TG : la source est un sample chargé (lecture seule), donc pas de
// tête d'enregistrement ni de tampon qui s'écrit. La position choisit le point de départ dans le sample.
//
// Paramètres (entiers, sans float) :
//   position  Q15 0..32767 : place dans le sample (0 = début, 32767 = fin)
//   size      échantillons : durée d'un grain
//   pitch_q16 rapport de hauteur en Q16 (65536 = 1.0x)
//   density   Q15 0..32767 : part des grains actifs visée (espacement déterministe entre les grains)
//   spread    Q15 0..32767 : largeur du panoramique aléatoire (texture stéréo)
#ifndef CLOUDS_GRAN_GRAIN_PLAYER_FX_H_
#define CLOUDS_GRAN_GRAIN_PLAYER_FX_H_

#include <stdint.h>

#include "grain_fx.h"

namespace clouds_gran {

static const int32_t kMaxGrains = 8;

class GrainPlayerFx {
 public:
  void Init(uint32_t seed) {
    for (int32_t i = 0; i < kMaxGrains; ++i) grains_[i].Init();
    rng_ = seed ? seed : 1;
    spacing_phasor_ = 0;
    size_hint_ = 1024;
  }

  // Un bloc de `size` échantillons stéréo. `buf_l`/`buf_r` : le sample (int16), de longueur `len`.
  // `out` : cumul int32 Q15 (le bloc doit être mis à zéro par l'appelant).
  void Play(const int16_t* buf_l, const int16_t* buf_r, int32_t len,
            int32_t position_q15, int32_t size_samples, int32_t pitch_q16,
            int32_t density_q15, int32_t spread_q15, int32_t window_shape_q15,
            int32_t overlap_q15, int32_t* out, int32_t size) {
    window_shape_ = window_shape_q15;
    overlap_ = overlap_q15;
    // Espacement cible entre deux grains : plus de densité = grains plus serrés.
    // Nombre de grains actifs visé = density * kMaxGrains.
    int32_t target = (density_q15 * kMaxGrains) >> 15;
    if (target < 1) target = 1;
    int32_t spacing = size_hint_ / target;  // échantillons (Clouds : grain_size_hint / cible)
    if (spacing < 1) spacing = 1;

    for (int32_t t = 0; t < size; ++t) {
      spacing_phasor_ += 1;
      if (spacing_phasor_ >= spacing) {
        int32_t slot = FindFreeGrain();
        if (slot >= 0) {
          ScheduleGrain(&grains_[slot], t, len, position_q15, size_samples,
                        pitch_q16, spread_q15);
          spacing_phasor_ = 0;
        }
      }
    }
    int32_t active = 0;
    for (int32_t i = 0; i < kMaxGrains; ++i) {
      if (grains_[i].active()) {
        grains_[i].OverlapAdd(buf_l, buf_r, len, out, size);
      }
      if (grains_[i].active()) ++active;
    }
    // Normalisation de Clouds : 1/sqrt(n-1) au-delà de deux grains actifs, puis un gain de fenêtre
    // (1 + 2*shape, borné à [1, 2]) fondu selon overlap. Table en Q15 pour n = 1..8.
    static const int32_t kInvSqrt[kMaxGrains + 1] = {
        kOne, kOne, kOne, 23170, 18919, 16384, 14654, 13377, 12385};
    int32_t norm = kInvSqrt[active < 0 ? 0 : (active > kMaxGrains ? kMaxGrains : active)];
    int32_t w = kOne + 2 * window_shape_;
    if (w > 2 * kOne) w = 2 * kOne;
    if (w < kOne) w = kOne;
    int32_t window_gain = kOne + (((w - kOne) * overlap_) >> 15);
    norm = (norm * window_gain) >> 15;
    for (int32_t t = 0; t < 2 * size; ++t) {
      out[t] = (out[t] * norm) >> 15;
    }
  }

  bool any_active() const {
    for (int32_t i = 0; i < kMaxGrains; ++i) {
      if (grains_[i].active()) return true;
    }
    return false;
  }

 private:
  // Générateur pseudo-aléatoire (xorshift32), Q15 dans [0, 32767].
  int32_t Random15() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<int32_t>(rng_ >> 17);
  }

  int32_t FindFreeGrain() {
    for (int32_t i = 0; i < kMaxGrains; ++i) {
      if (!grains_[i].active()) return i;
    }
    return -1;
  }

  void ScheduleGrain(GrainFx* g, int32_t pre_delay, int32_t len,
                     int32_t position_q15, int32_t size_samples, int32_t pitch_q16,
                     int32_t spread_q15) {
    // Taille de grain bornée : au moins 16 échantillons, pair, et pas plus que le sample.
    int32_t size = size_samples & ~1;
    if (size < 16) size = 16;
    if (size > len / 2) size = (len / 2) & ~1;
    // Le grain lit `size * pitch` échantillons du sample : on borne le départ pour rester dans le sample.
    int32_t eaten = (static_cast<int64_t>(size) * pitch_q16 >> 16) > len
        ? len
        : static_cast<int32_t>((static_cast<int64_t>(size) * pitch_q16) >> 16);
    int32_t available = len - eaten;
    if (available < 1) available = 1;
    int32_t start = (static_cast<int64_t>(position_q15) * available) >> 15;

    // Panoramique aléatoire autour du centre, selon la largeur de spread.
    int32_t pan = 16384 + (((Random15() - 16384) * spread_q15) >> 15);  // Q15 0..32767
    int32_t gain_l, gain_r;
    if (pan < 16384) {
      gain_l = kOne;
      gain_r = (2 * pan);           // 2 * pan, Q15
    } else {
      gain_r = kOne;
      gain_l = 2 * (kOne - pan);
    }
    if (gain_l > kOne) gain_l = kOne;
    if (gain_r > kOne) gain_r = kOne;

    // Fenêtre : forme 0.5 (triangle plein), comme la valeur par défaut de Clouds.
    g->Start(pre_delay, len, start, size, pitch_q16, window_shape_, gain_l, gain_r);
    // Estimation de la taille de grain pour l'espacement (lissage 1/10).
    size_hint_ += (size - size_hint_) / 10;
  }

  GrainFx grains_[kMaxGrains];
  uint32_t rng_ = 1;
  int32_t spacing_phasor_ = 0;
  int32_t size_hint_ = 1024;
  int32_t window_shape_ = kOne / 2;
  int32_t overlap_ = 0;
};

}  // namespace clouds_gran

#endif  // CLOUDS_GRAN_GRAIN_PLAYER_FX_H_
