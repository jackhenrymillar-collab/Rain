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
// Grain Envelope.

#ifndef BEADS_DSP_GRAIN_ENVELOPE_H_
#define BEADS_DSP_GRAIN_ENVELOPE_H_

#include "stmlib/stmlib.h"

#include <algorithm>

// Rain: the shortest grain attack is configurable (the module uses 0.5 ms).
#ifndef BEADS_MIN_ATTACK_SECONDS
#define BEADS_MIN_ATTACK_SECONDS 0.0005f
#endif

namespace beads {

class GrainEnvelope {
 public:
  GrainEnvelope() { }
  ~GrainEnvelope() { }
  
  inline void Init(float sample_rate) {
    stage_ = STAGE_DONE;
    phase_ = 0.0f;
    phase_increment_[STAGE_ATTACK] = 0.5f;
    phase_increment_[STAGE_SUSTAIN] = 0.5f;
    phase_increment_[STAGE_RELEASE] = 0.5f;
    phase_increment_[STAGE_DONE] = 0.0f;
    smoothness_ = 0.0f;
    attack_smoothness_ = 0.0f;
    
    min_attack_ = BEADS_MIN_ATTACK_SECONDS * sample_rate;
    min_release_ = 0.010f * sample_rate;
  }

  // Rain: smudge (0 to 1) stretches the attack (up to a quarter of the grain,
  // or 30 ms) and rounds it into an S-curve, so grains swell in instead of
  // clicking. The release is left to SHAPE. 0 is the module's envelope.
  inline void Start(float duration, float shape, float smudge = 0.0f) {
    stage_ = STAGE_ATTACK;
    phase_ = 0.0f;
    
    const float sustain = std::max(1.0f - 3.0f * shape, 0.0f);
    shape = (shape - 0.33333333f) * 1.5f;
    if (shape < 0.0f) {
      shape = 0.0f;
    } else if (shape < 0.5f) {
      shape = shape * shape * shape * 4.0f;
    }
    const float attack = shape;
    const float release = (1.0f - attack) * (1.0f - sustain);
    
    //          attack     sustain     release     smoothness
    // ------------------------------------------------------
    // Square        0           1           0              0
    //               0         0.5           0              0
    // Decay         0           0           1              0
    //            0.25           0        0.75            0.5
    // Bell        0.5           0         0.5              1
    //            0.75           0        0.25            0.5
    // Reverse       1           0           0              0
    
    float attack_duration = std::max(
        duration * attack,
        min_attack_ * (1.0f + 3.0f * sustain));
    float sustain_duration = std::max(duration * sustain, 2.0f);
    float release_duration = std::max(duration * release, min_release_);
    if (smudge > 0.0f) {
      const float edge = smudge * std::min(0.25f * duration, 60.0f * min_attack_);
      attack_duration = std::max(attack_duration, edge);
      sustain_duration = std::max(
          std::min(sustain_duration,
                   duration - attack_duration - release_duration), 2.0f);
    }
    
    phase_increment_[STAGE_ATTACK] = 1.0f / attack_duration;
    phase_increment_[STAGE_SUSTAIN] = 1.0f / sustain_duration;
    phase_increment_[STAGE_RELEASE] = 1.0f / release_duration;
    smoothness_ = attack < 0.5f ? 2.0f * attack : 2.0f - 2.0f * attack;
    attack_smoothness_ = smoothness_ + (1.0f - smoothness_) * smudge;
  }

  inline void Start(float duration, float shape, float fractional_sample,
                    float smudge) {
    Start(duration, shape, smudge);
    phase_ += phase_increment_[stage_] * fractional_sample;
    WrapPhase();
  }
  
  inline void Stop() {
    stage_ = STAGE_DONE;
  }

  inline void WrapPhase() {
    if (phase_ >= 1.0f) {
      const float t = (phase_ - 1.0f) / phase_increment_[stage_];
      stage_ = Stage(int(stage_) + 1);
      phase_ = t * phase_increment_[stage_];
    }
  }

  inline float Render() {
    if (stage_ == STAGE_DONE) {
      return 0.0f;
    }

    float value;
    if (stage_ == STAGE_SUSTAIN) {
      value = 1.0f;
    } else {
      const bool attack = stage_ == STAGE_ATTACK;
      const float t = attack ? phase_ : 1.0f - phase_;
      const float base = attack ? t : (t * t) * (t * t);
      value = base + (stmlib::SmoothStep(t) - base) *
          (attack ? attack_smoothness_ : smoothness_);
    }

    phase_ += phase_increment_[stage_];
    WrapPhase();
    
    return value;
  }

  inline bool done() const {
    return stage_ >= STAGE_DONE;
  }

 private:
  enum Stage {
    STAGE_ATTACK,
    STAGE_SUSTAIN,
    STAGE_RELEASE,
    STAGE_DONE,
    STAGE_LAST
  };
  
  Stage stage_;
  float phase_;
  float phase_increment_[STAGE_LAST];
  float smoothness_;
  float attack_smoothness_ = 0.0f;
  
  float min_attack_;
  float min_release_;

  DISALLOW_COPY_AND_ASSIGN(GrainEnvelope);
};

}  // namespace beads

#endif  // BEADS_DSP_GRAIN_ENVELOPE_H_
