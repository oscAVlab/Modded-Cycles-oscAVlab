// Grain granulaire en point fixe pour le MCF54418 (pas de FPU), d'après grain.h de Clouds
// (Émilie Gillet, MIT, pichenettes/eurorack, commit 08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4).
// Porté : enveloppe (fenêtre de Clouds sans table, pente `slope`), lecture de la mémoire à l'interpolation
// linéaire, panoramique. Non porté : la table de fenêtre (lut_window) et l'interpolation Hermite de Clouds.
//
// Formats : position du grain en Q16 (`phase`), enveloppe et gains en Q15 (0..32767), échantillons en int16.
#ifndef CLOUDS_GRAN_GRAIN_FX_H_
#define CLOUDS_GRAN_GRAIN_FX_H_

#include <stdint.h>

namespace clouds_gran {

// Q15 : 1.0 = 32768. Les constantes de l'enveloppe sont en Q15 aussi.
static const int32_t kOne = 32768;

class GrainFx {
 public:
  void Init() { active_ = false; }

  // width : durée du grain en échantillons (Clouds : envelope_phase_increment_ = 2 / width, phase sur [0, 2)).
  // Le même calcul en entier : env_inc (Q16) = 2 * 65536 / width, env_phase en Q16 dans [0, 2 * 65536).
  void Start(int32_t pre_delay, int32_t buffer_size, int32_t start, int32_t width,
             int32_t phase_increment_q16, int32_t window_shape_q15,
             int32_t gain_l_q15, int32_t gain_r_q15) {
    pre_delay_ = pre_delay;
    first_sample_ = (start + buffer_size) % buffer_size;
    phase_increment_ = phase_increment_q16;
    phase_ = 0;
    env_phase_ = 0;
    env_inc_ = width > 0 ? (2 * 65536) / width : 0;
    // window_shape < 0.5 : pente, >= 0.5 : lissage (ici la pente seule, lissage à 0).
    if (window_shape_q15 >= kOne / 2) {
      slope_q15_ = kOne;  // pas de pente : enveloppe triangulaire pleine
    } else {
      // slope = 0.5 / (shape + 0.01), borné à 1.0 en Q15
      int32_t denom = window_shape_q15 + kOne / 100;
      int32_t s = denom > 0 ? (kOne / 2) * kOne / denom : kOne;
      slope_q15_ = s > kOne ? kOne : s;
    }
    gain_l_ = gain_l_q15;
    gain_r_ = gain_r_q15;
    active_ = true;
  }

  // Ajoute `size` échantillons stéréo de ce grain dans `out` (entrelacé L,R, int32 Q15 de cumul).
  // `buf_l` / `buf_r` : mémoire d'échantillons int16 ; `buf_size` : taille du tampon.
  void OverlapAdd(const int16_t* buf_l, const int16_t* buf_r, int32_t buf_size,
                  int32_t* out, int32_t size) {
    if (!active_) return;
    while (pre_delay_ && size) {
      out += 2;
      --size;
      --pre_delay_;
    }
    while (size--) {
      // enveloppe : triangle sur [0, 2) avec la pente de Clouds (gain = min(1, slope * g))
      int32_t g = env_phase_ >= 65536 ? (2 * 65536 - env_phase_) : env_phase_;  // Q16 dans [0, 65536]
      int32_t gain = g >> 1;  // Q15
      gain = (gain * slope_q15_) >> 15;
      if (gain > kOne) gain = kOne;
      env_phase_ += env_inc_;
      if (env_phase_ >= 2 * 65536) {
        active_ = false;
        break;
      }
      int32_t idx = first_sample_ + (phase_ >> 16);
      if (idx >= buf_size) idx -= buf_size;
      int32_t frac = phase_ & 0xffff;  // Q16
      int32_t idx2 = idx + 1;
      if (idx2 >= buf_size) idx2 -= buf_size;
      int32_t l0 = buf_l[idx], l1 = buf_l[idx2];
      int32_t r0 = buf_r[idx], r1 = buf_r[idx2];
      int32_t l = l0 + (((l1 - l0) * frac) >> 16);
      int32_t r = r0 + (((r1 - r0) * frac) >> 16);
      l = (l * gain) >> 15;
      r = (r * gain) >> 15;
      out[0] += (l * gain_l_) >> 15;
      out[1] += (r * gain_r_) >> 15;
      out += 2;
      phase_ += phase_increment_;
    }
  }

  bool active() const { return active_; }

 private:
  int32_t first_sample_ = 0;
  int32_t phase_ = 0;
  int32_t phase_increment_ = 0;
  int32_t pre_delay_ = 0;
  int32_t env_phase_ = 0;
  int32_t env_inc_ = 0;
  int32_t slope_q15_ = kOne;
  int32_t gain_l_ = 0;
  int32_t gain_r_ = 0;
  bool active_ = false;
};

}  // namespace clouds_gran

#endif  // CLOUDS_GRAN_GRAIN_FX_H_
