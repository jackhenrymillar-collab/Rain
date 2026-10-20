// Copyright 2017 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Grain.

#ifndef BEADS_DSP_DELAY_GRAIN_H_
#define BEADS_DSP_DELAY_GRAIN_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

#include "beads/dsp/delay_line.h"
#include "beads/dsp/grain.h"
#include "beads/dsp/rain_sinc.h"

#include <cmath>

namespace beads {

class DelayGrain;

// Rain: per-grain quality options (see Parameters in granular_player.h). The
// defaults reproduce upstream Beads exactly.
struct GrainOptions {
  bool clean_reader = false;
  bool formant = false;
  bool phase_align = false;
  float glide_coefficient = 0.0f;   // one-pole coefficient per sample, 0 = off
  const DelayGrain* previous = NULL;  // the grain started before this one
};

enum HeadCrossingPolicy {
  HEAD_CROSSING_POLICY_STOP,
  HEAD_CROSSING_POLICY_WRAP,
  HEAD_CROSSING_POLICY_PING_PONG,
  HEAD_CROSSING_POLICY_STUTTER,
  HEAD_CROSSING_POLICY_CROSSFADE,
};

class DelayGrain : public Grain {
 public:
  DelayGrain() { }
  ~DelayGrain() { }
  
  enum {
    // Length of the "safety zone" near the head.
    head_crossing_safety_zone = 256
  };
  
  void Init(float sample_rate) {
    initial_delay_ = delay_ = 0;
    speed_ = 1024;
    sample_rate_ = sample_rate;
    SincBank::Get();  // build the tables now, not on the audio thread
    speed_f_ = speed_target_ = 1.0f;
    glide_k_ = 0.0f;
    clean_ = false;
    sinc_level_ = 0;
    psola_ = false;
    voiced_ = false;
    num_subs_ = 0;
    period_ = 1.0f;
    mark_ = next_sub_ = 0.0f;
    gain_[0][0] = gain_[0][1] = 0.5f;
    gain_[1][0] = gain_[1][1] = 0.5f;
    
    envelope_.Init(sample_rate);
    voice_stealing_envelope_.Init();
    remaining_samples_ = 0;
  }
  
  template<typename DelayLineType>
  inline void Start(
      const DelayLineType* delay_line,
      float delay,
      float speed,
      float duration,
      float shape,
      float gain,
      float pan,
      float fractional_sample,
      const GrainOptions& options = GrainOptions()) {
    // Rain: formant keep (PSOLA) needs a pitched source; noisy material is
    // resampled as usual. A formant grain's pointer follows the content at
    // normal speed, so it is trimmed for that speed.
    float period = 0.0f;
    bool voiced = false;
    if (options.formant && std::fabs(std::fabs(speed) - 1.0f) > 0.003f) {
      period = EstimatePeriod(delay_line, std::max(delay, 0.0f));
      voiced = period > 0.0f;
      if (!voiced) {
        // Noisy material: short slices from around the content, at jittered
        // spacing, so its colour stays put without a buzz at the slice rate.
        period = sample_rate_ * 0.006f;
      }
    }
    const float travel_speed = period > 0.0f ? (speed > 0.0f ? 1.0f : -1.0f) : speed;

    float consumption_speed = travel_speed - (delay_line->frozen() ? 0.0f : 1.0f);
    delay -= fractional_sample * consumption_speed;

    // Trim duration so that there's enough space in the buffer.
    if (consumption_speed != 0.0f) {
      duration = std::min(
          duration,
          DelayLineType::MAX_DELAY / fabsf(consumption_speed));
    }
    
    if (consumption_speed > 0.0f) {
      // Trim delay time so we don't hit the recording head.
      delay = std::max(delay, duration * consumption_speed);
    } else if (consumption_speed < 0.0f) {
      // Trim delay time so we don't loop and hit back of the recording head.
      delay = std::min(delay, 
          DelayLineType::MAX_DELAY + duration * consumption_speed);
    }

    // Rain: the grain playing before this one, if it is still sounding.
    const DelayGrain* previous = options.previous;
    if (previous == this || (previous && previous->done())) {
      previous = NULL;
    }

    // Rain: phase alignment. Move the start (by up to 5 ms) to where the source
    // best matches what the previous grain is playing, so the two overlap in
    // phase instead of partly cancelling.
    if (options.phase_align && previous &&
        (previous->speed_target_ > 0.0f) == (speed > 0.0f) &&
        std::fabs(std::fabs(previous->speed_target_) - std::fabs(speed)) <
            0.03f * std::fabs(speed)) {
      const float lowest = consumption_speed > 0.0f
          ? std::max(float(head_crossing_safety_zone), duration * consumption_speed)
          : float(head_crossing_safety_zone);
      const float highest = consumption_speed < 0.0f
          ? float(DelayLineType::MAX_DELAY) + duration * consumption_speed
          : float(DelayLineType::MAX_DELAY - 2 * head_crossing_safety_zone);
      const float aligned = AlignTo(
          delay_line, delay, float(previous->delay_) / 1024.0f, lowest, highest);
      if (aligned != delay) {
        delay = aligned;
        // In-phase grains add up louder than Beads' level rule (which assumes
        // random phases) expects; take some of that back. gain is
        // 1 / sqrt(active grains), so this is an extra 1 / active^(1/4).
        gain *= std::sqrt(gain);
      }
    }
    
    initial_delay_ = delay_ = int32_t(delay * 1024.0f);
    speed_ = int32_t(travel_speed * 1024.0f);
    speed_f_ = speed_target_ = speed;
    clean_ = options.clean_reader;

    // Rain: pitch glide from the previous grain's pitch.
    glide_k_ = 0.0f;
    if (options.glide_coefficient > 0.0f && previous &&
        (previous->speed_target_ > 0.0f) == (speed > 0.0f) &&
        std::fabs(previous->speed_target_ - speed) > 0.0005f) {
      speed_f_ = previous->speed_target_;
      if (period <= 0.0f) {
        speed_ = int32_t(speed_f_ * 1024.0f);
      }
      glide_k_ = options.glide_coefficient;
    }
    sinc_level_ = SincBank::Get().LevelFor(speed_f_);

    psola_ = period > 0.0f;
    num_subs_ = 0;
    if (psola_) {
      period_ = period;
      voiced_ = voiced;
      mark_ = voiced ? FindPulse(delay_line, delay, period) : delay;
      next_sub_ = 0.0f;
      // Lay slices down in step with the grain before, so the two pulse trains
      // add up in phase instead of comb filtering the tone.
      if (voiced && previous && previous->psola_ && previous->voiced_ &&
          (previous->speed_target_ > 0.0f) == (speed > 0.0f) &&
          std::fabs(std::fabs(previous->speed_target_) - std::fabs(speed)) <
              0.03f * std::fabs(speed)) {
        next_sub_ = previous->next_sub_;
      }
    }
    
    // Roughly equal power. Uses first order approximation of square root.
    if (DelayLineType::NUM_CHANNELS == 2) {
      if (pan < 0.5f) {
        // Destination Source
        pan *= 2.0f;
        gain_[0][0] = gain;
        gain_[1][0] = 0.0f;
        gain_[1][1] = pan * (2.0f - pan) * gain;
        gain_[0][1] = (1.0f - pan) * (1.0f + pan) * gain;
      } else {
        // Destination Source
        pan = 1.0f - pan;
        pan *= 2.0f;
        gain_[1][1] = gain;
        gain_[0][1] = 0.0f;
        gain_[0][0] = pan * (2.0f - pan) * gain;
        gain_[1][0] = (1.0f - pan) * (1.0f + pan) * gain;
      }
    } else {
      gain_[1][0] = pan * (2.0f - pan) * gain;
      gain_[0][0] = (1.0f - pan) * (1.0f + pan) * gain;
      gain_[0][1] = gain_[1][1] = 0.0f;
    }
    
    envelope_.Start(duration, shape, fractional_sample);
    remaining_samples_ = int(duration);
    
    voice_stealing_envelope_.ResetAndPause();
  }
  
  template<
      HeadCrossingPolicy head_crossing_policy,
      bool fade_out,
      typename DelayLineType>
  inline void Render(const DelayLineType* delay_line, FloatFrame* in_out) {
    if (done()) {
      return;
    }

    if (glide_k_ > 0.0f) {
      speed_f_ += (speed_target_ - speed_f_) * glide_k_;
      if (std::fabs(speed_target_ - speed_f_) < 0.0001f) {
        speed_f_ = speed_target_;
        glide_k_ = 0.0f;
      }
      if (!psola_) {
        speed_ = int32_t(speed_f_ * 1024.0f);
      }
      sinc_level_ = SincBank::Get().LevelFor(speed_f_);
    }
    
    delay_ -= speed_;
    if (!delay_line->frozen()) {
      delay_ += 1024;
    }
    
    const int32_t tail = DelayLineType::MAX_DELAY << 10;
    const int32_t fade_zone = head_crossing_safety_zone << 10;
    const int32_t xfade_tail = tail - fade_zone;

    switch (head_crossing_policy) {
      case HEAD_CROSSING_POLICY_STOP:
        if (delay_ < 0 || delay_ >= tail) {
          delay_ = 0;
          envelope_.Stop();
        }
        break;

      case HEAD_CROSSING_POLICY_WRAP:
        if (delay_ < 0) {
          delay_ += tail;
        } else if (delay_ >= tail) {
          delay_ -= tail;
        }
        break;

      case HEAD_CROSSING_POLICY_PING_PONG:
        if (delay_ < 0 || delay_ >= tail) {
          delay_ += speed_;
          speed_ = -speed_;
        }
        break;
      
      case HEAD_CROSSING_POLICY_STUTTER:
        if (delay_ < 0 || delay_ >= tail) {
          delay_ = initial_delay_;
        }
        break;
  
      case HEAD_CROSSING_POLICY_CROSSFADE:
        if (delay_ < 0) {
          delay_ += xfade_tail;
        } else if (delay_ >= tail) {
          delay_ -= xfade_tail;
        }
        break;
    }
    
    const int32_t d = abs(delay_ >= tail / 2 ? delay_ - tail : delay_);
    const float fade_ramp = d < fade_zone ? float(d) / float(fade_zone) : 1.0f;

    int32_t delay_integral = delay_ >> 10;
    float delay_fractional = float(delay_ & 1023) / 1024.0f;
    FloatFrame sample;
    if (psola_) {
      sample = RenderPsola(delay_line);
    } else if (clean_) {
      const SincBank& bank = SincBank::Get();
      sample = delay_line->ReadSinc(
          delay_integral, delay_fractional,
          bank.table(sinc_level_), bank.taps(sinc_level_), SincBank::kPhases);
    } else {
      sample = delay_line->template Read<INTERPOLATION_HERMITE>(
          delay_integral, delay_fractional);
    }
    
    // Crossfade with samples near the tail.
    if (head_crossing_policy == HEAD_CROSSING_POLICY_CROSSFADE) {
      sample *= fade_ramp;
      if (delay_integral < head_crossing_safety_zone) {
        sample += (1.0f - fade_ramp) * \
            delay_line->template Read<INTERPOLATION_HERMITE>(
                delay_integral + (xfade_tail >> 10), delay_fractional);
      } else if (delay_ >= xfade_tail) {
        sample += (1.0f - fade_ramp) * \
            delay_line->template Read<INTERPOLATION_HERMITE>(
                delay_integral - (xfade_tail >> 10), delay_fractional);
      }
    } else {
      if (fade_out) {
        sample *= stmlib::SmoothStep(fade_ramp);
      }
    }
    
    sample *= envelope_.Render() * voice_stealing_envelope_.Render();

    in_out->l += sample.l * gain_[0][0] + sample.r * gain_[0][1];
    in_out->r += sample.l * gain_[1][0] + sample.r * gain_[1][1];

    --remaining_samples_;
    if (done()) {
      remaining_samples_ = 0;
    }
  }
  
 private:
  // Rain: finds the delay within 5 ms of `delay` whose source best matches the
  // source at `reference` (normalised cross-correlation), keeping the
  // reference's fractional position so both grains interpolate alike.
  template<typename DelayLineType>
  float AlignTo(const DelayLineType* delay_line, float delay, float reference,
                float lowest, float highest) const {
    enum { kWindow = 128, kStep = 2, kMaxShift = 512 };
    const int max_shift = std::min(int(kMaxShift), int(sample_rate_ * 0.005f));
    const int ref = int(reference);
    const float ref_frac = reference - float(ref);
    const int centre = int(delay);
    if (ref < 0 || centre - max_shift < 0) {
      return delay;
    }
    float seg[kWindow];
    float seg_energy = 0.0f;
    for (int j = 0; j < kWindow; ++j) {
      seg[j] = delay_line->ReadMono(ref + j * kStep);
      seg_energy += seg[j] * seg[j];
    }
    if (seg_energy < 1e-7f) {
      return delay;
    }
    const int span = 2 * max_shift + kWindow * kStep;
    float cand[2 * kMaxShift + kWindow * kStep];
    for (int k = 0; k < span; ++k) {
      cand[k] = delay_line->ReadMono(centre - max_shift + k);
    }
    float best = 0.25f;  // demand a real match
    int best_shift = 0;
    bool found = false;
    for (int shift = -max_shift; shift <= max_shift; ++shift) {
      const float* c = cand + (shift + max_shift);
      float dot = 0.0f, energy = 0.0f;
      for (int j = 0; j < kWindow; ++j) {
        const float x = c[j * kStep];
        dot += seg[j] * x;
        energy += x * x;
      }
      if (energy < 1e-7f) {
        continue;
      }
      const float score = dot / std::sqrt(seg_energy * energy);
      if (score > best) {
        best = score;
        best_shift = shift;
        found = true;
      }
    }
    if (!found) {
      return delay;
    }
    const float aligned = float(centre + best_shift) + ref_frac;
    return aligned < lowest || aligned > highest ? delay : aligned;
  }

  // Rain: the pitch period of the source around `delay`, in samples, from a
  // normalised autocorrelation (coarse search at a quarter of the rate, then
  // refined). Returns 0 for noisy or silent material.
  template<typename DelayLineType>
  float EstimatePeriod(const DelayLineType* delay_line, float delay) const {
    enum { kDecimation = 4, kWindow = 256, kMaxLag = 260 };
    const int min_lag = std::max(2, int(sample_rate_ / 700.0f / kDecimation));
    const int max_lag = std::min(int(kMaxLag), int(sample_rate_ / 55.0f / kDecimation));
    const int start = int(delay);
    if (start < 0 || start + (kWindow + max_lag + 1) * kDecimation >=
        DelayLineType::MAX_DELAY) {
      return 0.0f;
    }
    float x[kWindow + kMaxLag + 1];
    for (int n = 0; n < kWindow + max_lag; ++n) {
      float sum = 0.0f;
      for (int k = 0; k < kDecimation; ++k) {
        sum += delay_line->ReadMono(start + n * kDecimation + k);
      }
      x[n] = sum * (1.0f / kDecimation);
    }
    float e0 = 0.0f;
    for (int n = 0; n < kWindow; ++n) {
      e0 += x[n] * x[n];
    }
    if (e0 < 1e-6f) {
      return 0.0f;
    }
    float r[kMaxLag + 1];
    float r_max = 0.0f;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
      float dot = 0.0f, e1 = 0.0f;
      for (int n = 0; n < kWindow; ++n) {
        dot += x[n] * x[n + lag];
        e1 += x[n + lag] * x[n + lag];
      }
      r[lag] = e1 > 1e-9f ? dot / std::sqrt(e0 * e1) : 0.0f;
      r_max = std::max(r_max, r[lag]);
    }
    if (r_max < 0.5f) {
      return 0.0f;
    }
    // The first strong peak, to avoid picking a multiple of the period.
    int coarse = 0;
    for (int lag = min_lag + 1; lag < max_lag; ++lag) {
      if (r[lag] >= 0.88f * r_max && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
        coarse = lag;
        break;
      }
    }
    if (!coarse) {
      return 0.0f;
    }
    // Refine at the full rate.
    enum { kFine = 384 };
    int best_lag = coarse * kDecimation;
    float best = -1.0f;
    for (int lag = (coarse - 1) * kDecimation; lag <= (coarse + 1) * kDecimation; ++lag) {
      float dot = 0.0f, ea = 0.0f, eb = 0.0f;
      for (int n = 0; n < kFine; n += 2) {
        const float a = delay_line->ReadMono(start + n);
        const float b = delay_line->ReadMono(start + n + lag);
        dot += a * b;
        ea += a * a;
        eb += b * b;
      }
      const float score = ea > 0.0f && eb > 0.0f ? dot / std::sqrt(ea * eb) : 0.0f;
      if (score > best) {
        best = score;
        best_lag = lag;
      }
    }
    return float(best_lag);
  }

  // Rain: the strongest peak within half a period of `delay`: a pitch mark,
  // so slices are centred on the source's pulses.
  template<typename DelayLineType>
  float FindPulse(const DelayLineType* delay_line, float delay, float period) const {
    const int half = int(period * 0.5f);
    const int centre = int(delay);
    int best = centre;
    float peak = -1.0f;
    for (int d = std::max(0, centre - half); d <= centre + half; ++d) {
      const float x = std::fabs(delay_line->ReadMono(d));
      if (x > peak) {
        peak = x;
        best = d;
      }
    }
    return float(best);
  }

  // Rain: move the pitch mark on by one period, to wherever the source best
  // repeats the cycle around the current mark (so marks follow vibrato and stay
  // on the pulses), and update the period from the step taken.
  template<typename DelayLineType>
  void StepMark(const DelayLineType* delay_line, float dir) {
    const float period = period_;
    const int origin = int(mark_);
    if (voiced_) {
      const int half = int(period * 0.5f);
      const int stride = std::max(1, int(period / 48.0f));
      const int lo = int(period * 0.88f), hi = int(period * 1.12f) + 1;
      const int coarse = std::max(1, (hi - lo) / 20);
      float ea = 0.0f;
      for (int j = -half; j <= half; j += stride) {
        const float a = delay_line->ReadMono(origin + j);
        ea += a * a;
      }
      float best = -1.0f;
      int best_lag = int(period + 0.5f);
      auto score = [&](int lag) {
        const int target = origin - int(dir) * lag;
        if (target - half < 0) {
          return -1.0f;
        }
        float dot = 0.0f, eb = 0.0f;
        for (int j = -half; j <= half; j += stride) {
          const float a = delay_line->ReadMono(origin + j);
          const float b = delay_line->ReadMono(target + j);
          dot += a * b;
          eb += b * b;
        }
        return ea > 1e-9f && eb > 1e-9f ? dot / std::sqrt(ea * eb) : 0.0f;
      };
      for (int lag = lo; lag <= hi; lag += coarse) {
        const float c = score(lag);
        if (c > best) {
          best = c;
          best_lag = lag;
        }
      }
      const int centre = best_lag;
      for (int lag = std::max(lo, centre - coarse + 1); lag <= std::min(hi, centre + coarse - 1); ++lag) {
        if (lag == centre) {
          continue;
        }
        const float c = score(lag);
        if (c > best) {
          best = c;
          best_lag = lag;
        }
      }
      if (best >= 0.5f) {
        mark_ -= dir * float(best_lag);
        period_ += 0.5f * (float(best_lag) - period_);
        return;
      }
    }
    mark_ -= dir * period;
  }

  // Rain: one output sample of a formant-keeping (PSOLA) grain. Hann-windowed
  // slices two pitch periods long, centred on the source's pitch marks, are read
  // at normal speed and laid down every period / ratio samples, so the pitch
  // moves but the spectral envelope does not.
  template<typename DelayLineType>
  FloatFrame RenderPsola(const DelayLineType* delay_line) {
    const bool frozen = delay_line->frozen();
    const float dir = speed_target_ > 0.0f ? 1.0f : -1.0f;
    const float ratio = std::fabs(speed_f_);
    const float content = float(delay_) / 1024.0f;
    if (!frozen) {
      mark_ += 1.0f;
    }

    // Keep the mark nearest the content as the content moves on.
    for (int step = 0; step < 4; ++step) {
      const float next = mark_ - dir * period_;
      if (std::fabs(next - content) >= std::fabs(mark_ - content)) {
        break;
      }
      StepMark(delay_line, dir);
    }

    // Start a new slice, centred on the mark.
    next_sub_ -= 1.0f;
    if (next_sub_ <= 0.0f) {
      float hop = period_ / ratio;
      float centre = mark_;
      if (!voiced_) {
        hop *= 0.7f + 0.6f * stmlib::Random::GetFloat();
        centre = content + (stmlib::Random::GetFloat() - 0.5f) * period_;
      }
      next_sub_ += std::max(1.0f, hop);
      if (num_subs_ < kMaxSubs) {
        Sub& sub = subs_[num_subs_++];
        sub.delay = centre + dir * period_;
        sub.position = 0.0f;
        sub.length = 2.0f * period_;
      }
    }

    FloatFrame sum;
    sum.l = sum.r = 0.0f;
    const SincBank& bank = SincBank::Get();
    for (int i = 0; i < num_subs_; ) {
      Sub& sub = subs_[i];
      const float phase = sub.position / sub.length;
      const float window = 0.5f - 0.5f * std::cos(6.2831853f * phase);
      const float d = std::max(0.0f, sub.delay);
      const int d_integral = int(d);
      const float d_fractional = d - float(d_integral);
      FloatFrame x = clean_
          ? delay_line->ReadSinc(d_integral, d_fractional, bank.table(0),
                                 bank.taps(0), SincBank::kPhases)
          : delay_line->template Read<INTERPOLATION_HERMITE>(d_integral, d_fractional);
      sum.l += x.l * window;
      sum.r += x.r * window;
      sub.delay -= dir;
      if (!frozen) {
        sub.delay += 1.0f;
      }
      sub.position += 1.0f;
      if (sub.position >= sub.length) {
        subs_[i] = subs_[--num_subs_];
      } else {
        ++i;
      }
    }
    // Slices overlap about `ratio` deep. Pitched slices add up in phase, so that
    // is divided out going up; going down, each pulse keeps its level. Noise
    // slices add up in power instead.
    const float gain = voiced_
        ? (ratio >= 1.0f ? kVoicedDown / std::pow(ratio, 0.25f) : kVoicedDown)
        : kNoise / std::sqrt(ratio);
    sum.l *= gain;
    sum.r *= gain;
    return sum;
  }

  struct Sub {
    float delay;
    float position;
    float length;
  };
  enum { kMaxSubs = 12 };
  static constexpr float kVoicedDown = 0.8f;
  static constexpr float kNoise = 1.4f;

  float gain_[2][2];
  
  int32_t delay_;
  int32_t speed_;
  int32_t initial_delay_;

  float sample_rate_;
  float speed_f_;       // current speed (glides towards speed_target_)
  float speed_target_;
  float glide_k_;
  bool clean_;
  int sinc_level_;

  bool psola_;
  float period_;
  float mark_;
  float next_sub_;
  bool voiced_;
  Sub subs_[kMaxSubs];
  int num_subs_;

  DISALLOW_COPY_AND_ASSIGN(DelayGrain);
};

}  // namespace beads

#endif  // BEADS_DSP_DELAY_GRAIN_H_
