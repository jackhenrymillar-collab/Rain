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
// The whole FX chain.

#ifndef BEADS_DSP_PROCESSOR_H_
#define BEADS_DSP_PROCESSOR_H_

#include "stmlib/stmlib.h"

#include "beads/dsp/granular_processor.h"
#include "beads/dsp/modulated_parameter.h"
#include "beads/dsp/fx/reverb.h"
#include "beads/dsp/fx/wow_and_flutter.h"

// Setting this flag slows down the codec to 24kHz for the MEDIA_DIRTY_TAPE
// setting. The codec becomes very hissy at this sample rate, but this has
// a certain charm!
// 
// When this flag is not set, the codec is run at 48kHz and the wet signal is
// downsampled by a 0.5 factor before being processed. Low-pass filtering
// is also applied to the output signal within the wow and flutter code.
#define ENABLE_HARDWARE_24K

namespace beads {

// (512 + 256) * 1000 bytes, 48kHz 16-bit stereo = 4 seconds
const size_t kDelayBufferSizeBits = 768 * 1000 * 8;

// Rain: Studio quality, 8 seconds of 48 kHz 32-bit float stereo in three banks of
// 1 MB (supplied by Rain; the module's own 768 kB is not big enough).
const int kStudioDelayFrames = 48000 * 8;
const size_t kStudioBankSize = 1048576;
const size_t kStudioBufferSize = 3 * kStudioBankSize;
const size_t kStudioReverbSize = 32768;

class Processor {
 public:
  Processor() { }
  ~Processor() { }
  
  enum Media {
    MEDIA_DIRTY_TAPE,
    MEDIA_CLEAN_TAPE,
    MEDIA_VINTAGE_DIGITAL,
    MEDIA_DIGITAL,
    MEDIA_STUDIO  // Rain: 48 kHz, 32-bit float buffer and reverb
  };
  
  struct MixParameters {
    float feedback;
    float dry_wet;
    float reverb_amount;
    bool output_gate;
    // Rain: 0 to 1, darkens and warms the grains (not the dry signal).
    float soften = 0.0f;
    // Rain: when set, the dry signal is left out of the output (it still feeds
    // the reverb) and the gain it would have had is written here for every
    // sample, so the host can mix its own untouched copy of the input.
    float* direct_gain = NULL;
  };
  
  // Rain: studio_buffers (three banks of kStudioBankSize bytes) and
  // studio_reverb_buffer (kStudioReverbSize floats) enable MEDIA_STUDIO.
  void Init(uint8_t* buffers[], uint16_t* reverb_buffer,
            uint8_t* studio_buffers[] = NULL, float* studio_reverb_buffer = NULL);
  void Configure(Media media, bool mono);
  
  inline Media media() const { return media_; }

  // Rain: the recording, as stereo frames at history_rate() (the rate the buffer
  // stores at, after any tape decimation), so it can be carried across a change of
  // quality in pieces. ExportHistory copies frames out, newest first, starting
  // first_age frames back. After Configure, BeginImport empties the new buffer and
  // ImportHistory appends frames, oldest first, as if they had just been recorded.
  inline float history_rate() const { return sample_rate() / float(ratio()); }
  int history_capacity() const;
  int history_length() const;
  void ExportHistory(FloatFrame* newest_first, int first_age, int count) const;
  void BeginImport();
  void ImportHistory(const FloatFrame* oldest_first, int count);
  inline bool mono() const { return mono_; }
  
  inline float sample_rate() const { return SampleRateFor(media_); }
  inline int ratio() const { return RatioFor(media_); }

  // Rain: the same, for any media (so a quality change can be planned ahead).
  static inline float HistoryRateFor(Media media) {
    return SampleRateFor(media) / float(RatioFor(media));
  }
  static int HistoryCapacityFor(Media media);

  static inline float SampleRateFor(Media media) {
#ifdef ENABLE_HARDWARE_24K
    float sr[5] = { 24000.0f, 48000.0f, 32000.0f, 48000.0f, 48000.0f };
#else
    float sr[5] = { 48000.0f, 48000.0f, 32000.0f, 48000.0f, 48000.0f };
#endif  // ENABLE_HARDWARE_24K
    return sr[media];
  }
  
  static inline int RatioFor(Media media) {
#ifdef ENABLE_HARDWARE_24K
    int ratio[5] = { 1, 2, 1, 1, 1 };
#else
    int ratio[5] = { 2, 2, 1, 1, 1 };
#endif  // ENABLE_HARDWARE_24K
    return ratio[media];
  }
  
  inline uint8_t* delay_buffer_raw_bytes(size_t offset) {
    // Independently of the quality settings, we're writing in the same
    // bytes anyway!
    return processor_vintage_digital_.delay_buffer_raw_bytes(mono_, offset);
  }
  
  inline int* mutable_delay_write_ptr() {
    switch (media_) {
      case MEDIA_DIRTY_TAPE:
        return processor_dirty_tape_.mutable_delay_write_ptr(mono_);
      case MEDIA_CLEAN_TAPE:
        return processor_clean_tape_.mutable_delay_write_ptr(mono_);
      case MEDIA_VINTAGE_DIGITAL:
        return processor_vintage_digital_.mutable_delay_write_ptr(mono_);
      case MEDIA_STUDIO:
        return processor_studio_.mutable_delay_write_ptr(mono_);
      case MEDIA_DIGITAL:
      default:
        return processor_digital_.mutable_delay_write_ptr(mono_);
    }
  }
  
  inline float overwrite_reserve() const {
    float reserve = float(remaining());
    float fade_duration = sample_rate() * 0.15f / float(ratio());
    return reserve >= fade_duration ? 1.0f : reserve / fade_duration;
  }
  
  inline int remaining() const {
    switch (media_) {
      case MEDIA_DIRTY_TAPE:
        return processor_dirty_tape_.remaining(mono_);
      case MEDIA_CLEAN_TAPE:
        return processor_clean_tape_.remaining(mono_);
      case MEDIA_VINTAGE_DIGITAL:
        return processor_vintage_digital_.remaining(mono_);
      case MEDIA_STUDIO:
        return processor_studio_.remaining(mono_);
      case MEDIA_DIGITAL:
      default:
        return processor_digital_.remaining(mono_);
    }
  }

  inline void Trim() {
    if (media_ == MEDIA_STUDIO) {
      processor_studio_.Trim(mono_);
      return;
    }
    switch (media_) {
      case MEDIA_DIRTY_TAPE:
        processor_dirty_tape_.Trim(mono_);
      case MEDIA_CLEAN_TAPE:
        processor_clean_tape_.Trim(mono_);
      case MEDIA_VINTAGE_DIGITAL:
        processor_vintage_digital_.Trim(mono_);
      case MEDIA_DIGITAL:
      default:
        processor_digital_.Trim(mono_);
    }
  }
  
  void WasteCPUCycles(size_t size) {
    // The one with the most convoluted memory access!
    processor_vintage_digital_.WasteCPUCycles(size);
  }

  void Process(
      SeedButtonState seed_button_state,
      const stmlib::GateFlags* seed_input,
      const bool* freeze,
      const ModulatedParameter& density,
      const Parameters& parameters,
      const MixParameters& mix_parameters,
      const FloatFrame* in,
      FloatFrame* out,
      size_t size);
  
 private:
  GranularProcessor<
      RESOLUTION_16_BIT,
      kDelayBufferSizeBits / (16 * 2),
      1> processor_digital_;
  GranularProcessor<
      RESOLUTION_12_BIT,
      kDelayBufferSizeBits / (12 * 2),
      1> processor_vintage_digital_;
  GranularProcessor<
      RESOLUTION_12_BIT,
      kDelayBufferSizeBits / (12 * 2),
      2> processor_clean_tape_;
#ifdef ENABLE_HARDWARE_24K
  GranularProcessor<
      RESOLUTION_8_BIT,
      kDelayBufferSizeBits / (8 * 2),
      1> processor_dirty_tape_;
#else
  GranularProcessor<
      RESOLUTION_8_BIT,
      kDelayBufferSizeBits / (8 * 2),
      2> processor_dirty_tape_;
#endif  // ENABLE_HARDWARE_24K

  // Rain: Studio quality.
  GranularProcessor<
      RESOLUTION_FLOAT,
      kStudioDelayFrames,
      1,
      kStudioBankSize> processor_studio_;
  ReverbT<FORMAT_32_BIT> reverb_studio_;
  bool has_studio_ = false;

  TriggerGenerator trigger_generator_;
  DelayController delay_controller_;
  
  Media media_;
  bool process_dry_;
  bool mono_;
  
  float delay_mode_transition_;

  float dry_;
  float wet_;

  // Rain: state of the SOFTEN low-pass (two one-pole stages per channel).
  void Soften(float amount, FloatFrame* out, size_t size);
  float soften_state_[2][4] = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
  
  WowAndFlutter wow_and_flutter_;
  Reverb reverb_;
  Limiter pre_reverb_limiter_;
  Limiter output_limiter_;
  
  DISALLOW_COPY_AND_ASSIGN(Processor);
};

}  // namespace beads

#endif  // BEADS_DSP_PROCESSOR_H_
