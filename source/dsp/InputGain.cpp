// Adapted from Mutable Instruments Beads, beads/gain_control.cc.
// Original copyright 2021 Emilie Gillet, MIT License (see third_party/mutable/LICENSE).

#include "InputGain.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include <algorithm>
#include <cmath>

namespace rain::dsp
{

namespace
{
    constexpr float kIgnoreTime = 1.0f; // seconds
    constexpr float kActiveTime = 5.0f; // seconds
    constexpr float kMaxGain = 40.0f;
}

void InputGain::init (float sampleRate, SoftClipping mode, bool resetState)
{
    if (resetState)
    {
        stableAgcPeak = agcPeak = peak = 0.5f;
        gain = 1.0f;
        softClippingAmount = 0.0f;
    }
    softClipping = mode;
    ignoreTime = static_cast<std::size_t> (sampleRate * kIgnoreTime);
    const bool noReadjust = ! resetState && time && time >= activeTime;
    activeTime = static_cast<std::size_t> (sampleRate * (kIgnoreTime + kActiveTime));
    time = noReadjust ? activeTime : 0;
    oneHertz = 1.0f / sampleRate;
}

void InputGain::process (bool autoGain, float gainDb, beads::FloatFrame* frames, std::size_t size)
{
    const float hz = static_cast<float> (size) * oneHertz;
    if (time < ignoreTime)
    {
        agcPeak = 0.5f;
    }
    else if (time < activeTime)
    {
        SLOPE (agcPeak, peak, 10.0f * hz, 2.0f * hz);
        stableAgcPeak = agcPeak;
    }
    else
    {
        SLOPE (agcPeak, peak, 5.0f * hz, (peak > stableAgcPeak ? 0.5f : 0.01f) * hz);
    }

    if (time > ignoreTime || peak > 0.001f)
        time += size;
    time = std::min (time, activeTime);

    const float targetManual = std::clamp (std::pow (10.0f, gainDb / 20.0f), 1.0f, kMaxGain);
    ONE_POLE (manualGain, targetManual, 0.01f);

    const float optimalGain = std::min (0.9f / std::min (agcPeak, 0.9f), kMaxGain);
    const float targetGain = autoGain ? optimalGain : manualGain;

    float targetSoftClip = 0.0f;
    if (softClipping == SoftClipping::on)
        targetSoftClip = 1.0f;
    else if (softClipping == SoftClipping::dynamic)
        targetSoftClip = std::clamp (2.0f * (manualGain / optimalGain - 0.75f), 0.0f, 1.0f);

    stmlib::ParameterInterpolator gainMod (&gain, targetGain, size);
    stmlib::ParameterInterpolator softClipMod (&softClippingAmount, targetSoftClip, size);

    for (std::size_t i = 0; i < size; ++i)
    {
        float l = frames[i].l;
        float r = frames[i].r;

        const float p = std::max (std::fabs (l), std::fabs (r));
        SLOPE (peak, p, 0.1f, 2.0f * oneHertz);

        const float g = gainMod.Next();
        l *= g;
        r *= g;

        if (! autoGain)
        {
            if (softClipping == SoftClipping::off)
            {
                l = std::clamp (l, -1.0f, 1.0f);
                r = std::clamp (r, -1.0f, 1.0f);
            }
            else
            {
                const float amount = softClipMod.Next();
                constexpr float bias = 0.05f;
                const float softL = (1.0f - bias) * stmlib::SoftClip (l + bias) - bias;
                const float softR = (1.0f - bias) * stmlib::SoftClip (r + bias) - bias;
                l = stmlib::Crossfade (std::clamp (l, -1.0f, 1.0f), softL, amount);
                r = stmlib::Crossfade (std::clamp (r, -1.0f, 1.0f), softR, amount);
            }
        }

        frames[i].l = l;
        frames[i].r = r;
    }
}

} // namespace rain::dsp
