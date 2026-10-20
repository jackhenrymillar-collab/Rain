// Rain addition (not part of upstream Beads). MIT, like the surrounding code.
//
// Band-limited interpolation kernels for reading grains at any speed. Each level is a
// Kaiser-windowed sinc stored as a polyphase table. Level 0 is for grains at normal
// speed or slower; each further level is a quarter octave of speed-up, with the
// cut-off lowered and the kernel stretched to match, so grains pitched up to two
// octaves are read without aliasing.

#ifndef BEADS_DSP_RAIN_SINC_H_
#define BEADS_DSP_RAIN_SINC_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace beads {

class SincBank {
 public:
  enum { kLevels = 9, kPhases = 128 };

  static const SincBank& Get() {
    static const SincBank bank;
    return bank;
  }

  // The level whose cut-off keeps a grain read at `speed` (source samples per
  // output sample) free of aliasing.
  inline int LevelFor(float speed) const {
    const float s = std::fabs(speed);
    if (s <= 1.001f) {
      return 0;
    }
    const int level = int(std::ceil(4.0f * std::log2(s) - 0.001f));
    return std::min(level, int(kLevels) - 1);
  }

  inline const float* table(int level) const { return tables_[level].data(); }
  inline int taps(int level) const { return taps_[level]; }

 private:
  SincBank() {
    for (int level = 0; level < kLevels; ++level) {
      const double stretch = std::pow(2.0, 0.25 * level);
      const int taps = 2 * int(std::ceil(8.0 * stretch));
      const double half = taps / 2;
      const double cutoff = 0.45 / stretch;  // cycles per source sample
      const double beta = 8.0;               // about 80 dB stopband
      taps_[level] = taps;
      std::vector<float>& t = tables_[level];
      t.assign(size_t((kPhases + 1) * taps), 0.0f);
      for (int p = 0; p <= kPhases; ++p) {
        const double frac = double(p) / kPhases;
        double sum = 0.0;
        for (int m = 0; m < taps; ++m) {
          const double x = (half - 1.0 + frac) - m;  // distance from tap m
          const double w = x / half;
          const double window = std::fabs(w) >= 1.0
              ? 0.0
              : BesselI0(beta * std::sqrt(1.0 - w * w)) / BesselI0(beta);
          const double arg = 2.0 * cutoff * x;
          const double sinc = std::fabs(arg) < 1e-12
              ? 1.0
              : std::sin(kPi * arg) / (kPi * arg);
          const double h = 2.0 * cutoff * sinc * window;
          t[size_t(p * taps + m)] = float(h);
          sum += h;
        }
        // Unity gain at DC for every phase.
        for (int m = 0; m < taps; ++m) {
          t[size_t(p * taps + m)] = float(t[size_t(p * taps + m)] / sum);
        }
      }
    }
  }

  static double BesselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
      term *= (x / (2.0 * k)) * (x / (2.0 * k));
      sum += term;
      if (term < 1e-12 * sum) {
        break;
      }
    }
    return sum;
  }

  static constexpr double kPi = 3.14159265358979323846;
  int taps_[kLevels];
  std::vector<float> tables_[kLevels];
};

}  // namespace beads

#endif  // BEADS_DSP_RAIN_SINC_H_
