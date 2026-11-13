#pragma once

#include "Analysis.h"
#include <cmath>

// =============================================================================
//  Match correction stage (spec section 6). The only part of Ground Shake that changes
//  the audio, and only after Preview or Apply. Feel and punch EQ bands, optional mono
//  below 120 Hz, optional sub "weight" saturation, and a safety ceiling that follows the
//  peak level coming out of your limiter (fallback -1 dB).
// =============================================================================
namespace gs
{
struct MatchSettings
{
    float feelDb = 0.0f, punchDb = 0.0f;
    bool mono = false;
    float weight = 0.0f;                 // 0..1
    float split = kDefaultSplit;
    float feelHz = 0.0f;                 // centre of the feel band boost (your bass note); 0 = band centre
};

class MatchStage
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        cached = {};
        cached.feelDb = 999.0f;          // force coefficients on first use
        monoLp1.setLowPass (sr, 120.0); monoLp2.setLowPass (sr, 120.0);
        lowLp1.setLowPass (sr, 120.0);  lowLp2.setLowPass (sr, 120.0);
        hHp1.setHighPass (sr, 100.0);   hHp2.setHighPass (sr, 100.0);
        for (auto* b : { &fL, &fR, &pL, &pR, &monoLp1, &monoLp2, &lowLp1, &lowLp2, &hHp1, &hHp2 }) b->reset();
        relCoef = (float) std::exp (-1.0 / (0.08 * sr));
        peakDecay = (float) std::pow (0.5, 1.0 / (10.0 * sr));            // -6 dB over 10 s
        msCoef = (float) std::exp (-1.0 / (1.5 * sr));
        limGain = 1.0f;  fade = 0.0f;  preMs = postMs = 1.0e-6f;
    }

    // L/R processed in place. active = Preview or Apply; levelMatch = Preview (A/B at equal loudness)
    void process (float* L, float* R, int n, const MatchSettings& s, bool active, bool levelMatch) noexcept
    {
        if (std::abs (s.feelDb - cached.feelDb) > 0.01f || std::abs (s.punchDb - cached.punchDb) > 0.01f
            || std::abs (s.split - cached.split) > 0.1f || std::abs (s.feelHz - cached.feelHz) > 0.1f)
        {
            const double fc = s.feelHz > 0 ? (double) s.feelHz : std::sqrt (20.0 * s.split), pc = s.split * 1.414;
            for (auto* b : { &fL, &fR }) b->setPeak (sr, fc, 0.7, s.feelDb);
            for (auto* b : { &pL, &pR }) b->setPeak (sr, pc, 0.9, s.punchDb);
            cached = s;
        }
        const float fadeStep = (float) (1.0 / (0.02 * sr));
        for (int i = 0; i < n; ++i)
        {
            const float dl = L[i], dr = R[i];
            peakHold = std::max ({ std::abs (dl), std::abs (dr), peakHold * peakDecay });
            fade = juce::jlimit (0.0f, 1.0f, fade + (active ? fadeStep : -fadeStep));
            if (fade <= 0.0f) continue;                                   // fully bypassed: untouched

            float l = fL.process (pL.process (dl));
            float r = fR.process (pR.process (dr));
            if (s.mono)
            {
                const float side = 0.5f * (l - r);
                const float lowSide = monoLp2.process (monoLp1.process (side));
                l -= lowSide;  r += lowSide;                               // remove the side below 120 Hz
            }
            if (s.weight > 0.0f)
            {
                const float low = lowLp2.process (lowLp1.process (0.5f * (l + r)));
                const float h = hHp2.process (hHp1.process (std::tanh (4.0f * low) * 0.25f)) * 3.0f * s.weight;
                l += h;  r += h;
            }
            preMs = dl * dl + dr * dr + msCoef * (preMs - (dl * dl + dr * dr));
            postMs = l * l + r * r + msCoef * (postMs - (l * l + r * r));
            if (levelMatch)
            {
                const float mk = juce::jlimit (0.25f, 4.0f, std::sqrt (preMs / std::max (1.0e-9f, postMs)));
                l *= mk;  r *= mk;
            }
            // safety ceiling
            const float ceilLin = peakHold > 0.1f ? std::min (peakHold, 1.0f) : 0.891f;
            const float pk = std::max (std::abs (l), std::abs (r));
            const float target = pk > ceilLin ? ceilLin / pk : 1.0f;
            limGain = target < limGain ? target : target + relCoef * (limGain - target);
            l *= limGain;  r *= limGain;

            L[i] = dl + (l - dl) * fade;
            R[i] = dr + (r - dr) * fade;
        }
    }

    float ceilingDb() const { return 20.0f * std::log10 (peakHold > 0.1f ? std::min (peakHold, 1.0f) : 0.891f); }
    bool isBypassed() const { return fade <= 0.0f; }

private:
    double sr = 44100.0;
    MatchSettings cached;
    Biquad fL, fR, pL, pR, monoLp1, monoLp2, lowLp1, lowLp2, hHp1, hHp2;
    float relCoef = 0.999f, peakDecay = 0.99999f, msCoef = 0.9999f;
    float limGain = 1.0f, fade = 0.0f, peakHold = 0.0f, preMs = 1.0e-6f, postMs = 1.0e-6f;
};
} // namespace gs
