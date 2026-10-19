// Adapted from Mutable Instruments Beads, beads/gain_control.cc.
// Original copyright 2021 Emilie Gillet, MIT License (see third_party/mutable/LICENSE).
// Changes for Rain: float input instead of codec frames, the manual gain is set in dB,
// and the "nothing patched" handling is removed (a plugin always has an input).
//
// Brings the input up to a healthy level before it reaches the granular buffer,
// either automatically (Beads' AGC) or with a manual gain, and soft clips it in the
// tape modes the same way the module does.

#pragma once

#include "beads/dsp/frame.h"

#include <cstddef>

namespace rain::dsp
{

class InputGain
{
public:
    enum class SoftClipping { off, on, dynamic };

    void init (float sampleRate, SoftClipping mode, bool resetState);

    // gainDb is 0 to 32 dB (Beads' 1x to 40x). Ignored when autoGain is on.
    void process (bool autoGain, float gainDb, beads::FloatFrame* frames, std::size_t size);

    float currentGain() const noexcept { return gain; }

private:
    float peak = 0.5f, agcPeak = 0.5f, stableAgcPeak = 0.5f;
    float gain = 1.0f, manualGain = 1.0f, softClippingAmount = 0.0f;
    float oneHertz = 1.0f / 48000.0f;
    std::size_t time = 0, ignoreTime = 0, activeTime = 0;
    SoftClipping softClipping = SoftClipping::off;
};

} // namespace rain::dsp
