#pragma once

#include "Analysis.h"
#include <cmath>

// =============================================================================
//  Correction stage. The only part of Ground Shake that changes the audio, and only after
//  Preview or Apply (globally, or per pair in Phase 3).
//
//  Steady fixes : low cut, feel and punch EQ, sub mono below 120 Hz, sub "weight" harmonics
//  Beat-synced  : built-in ducking (low-end gain per 16th of the beat, like a volume shaper),
//                 kick attack (low end in the first ~12 ms of each beat), kick tail (low end
//                 after the kick has rung for a while), and a mud cut on the kick 16th only
//                 (where kick and bass clash)
//  Safety       : a ceiling that follows the peak level coming out of your limiter (fallback -1 dB)
//
//  Every setting glides to its new value over about one beat, so moving between pairs
//  crossfades instead of jumping.
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
    // Phase 3
    std::array<float, 4> duckDb {};      // built-in ducking: low-end gain on each 16th of the beat
    float attackDb = 0.0f;               // kick attack (+ sharper / - softer)
    float tailDb = 0.0f;                 // kick tail (- shorter / + longer)
    float tailStartMs = 150.0f;          // where the tail starts, after the beat
    float mudDb = 0.0f;                  // 150-400 Hz cut on the kick 16th only
    float lowCutHz = 0.0f;               // 0 = off; a breakdown low cut, 24 dB/oct

    bool isNeutral() const
    {
        bool duck = false;
        for (auto d : duckDb) duck = duck || std::abs (d) > 0.05f;
        return std::abs (feelDb) < 0.05f && std::abs (punchDb) < 0.05f && ! mono && weight <= 0.0f && ! duck
               && std::abs (attackDb) < 0.05f && std::abs (tailDb) < 0.05f && std::abs (mudDb) < 0.05f && lowCutHz <= 0.0f;
    }
};

class MatchStage
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& c : ch) c = {};
        cur = {};
        curLowCut = std::log (10.0f);
        curMono = 0.0f;
        lastCoef = {};
        lastCoef.feelDb = 999.0f;
        relCoef = (float) std::exp (-1.0 / (0.08 * sr));
        peakDecay = (float) std::pow (0.5, 1.0 / (10.0 * sr));            // -6 dB over 10 s
        msCoef = (float) std::exp (-1.0 / (1.5 * sr));
        g2ms = (float) std::exp (-1.0 / (0.002 * sr));
        g1ms = (float) std::exp (-1.0 / (0.001 * sr));
        g5ms = (float) std::exp (-1.0 / (0.005 * sr));
        limGain = 1.0f;  fade = 0.0f;  preMs = postMs = 1.0e-6f;
        gLow = gAtt = gMud = 1.0f;
        for (auto& c : ch)
        {
            c.monoLp1.setLowPass (sr, 120.0);  c.monoLp2.setLowPass (sr, 120.0);
            c.wLp1.setLowPass (sr, 120.0);     c.wLp2.setLowPass (sr, 120.0);
            c.hHp1.setHighPass (sr, 100.0);    c.hHp2.setHighPass (sr, 100.0);
            c.att1.setLowPass (sr, 300.0);     c.att2.setLowPass (sr, 300.0);
            c.mud.setBandPass (sr, 250.0, 1.0);
        }
    }

    // L/R in place. target = the settings for here; active = Preview or Apply; levelMatch = Preview.
    // startSteps / stepsPerSample = the host position in 16ths; glideSamples = how long changes take.
    void process (float* L, float* R, int n, const MatchSettings& target, bool active, bool levelMatch,
                  double startSteps, double stepsPerSample, double glideSamples) noexcept
    {
        const float a = (float) juce::jlimit (0.0, 1.0, n / juce::jmax (1.0, glideSamples) * 3.0);   // ~95% in one glide
        auto glide = [a] (float& c, float t) { c += (t - c) * a; };
        glide (cur.feelDb, target.feelDb);
        glide (cur.punchDb, target.punchDb);
        glide (cur.weight, target.weight);
        glide (curMono, target.mono ? 1.0f : 0.0f);
        for (size_t k = 0; k < 4; ++k) glide (cur.duckDb[k], target.duckDb[k]);
        glide (cur.attackDb, target.attackDb);
        glide (cur.tailDb, target.tailDb);
        glide (cur.tailStartMs, target.tailStartMs);
        glide (cur.mudDb, target.mudDb);
        glide (curLowCut, std::log (std::max (10.0f, target.lowCutHz)));
        cur.split = target.split;
        cur.feelHz = target.feelHz > 0 ? target.feelHz : cur.feelHz;
        if (target.feelHz <= 0) cur.feelHz = 0;

        // coefficients (cheap; only when something moved)
        const float lowCutHz = std::exp (curLowCut);
        if (std::abs (cur.feelDb - lastCoef.feelDb) > 0.005f || std::abs (cur.punchDb - lastCoef.punchDb) > 0.005f
            || std::abs (cur.split - lastCoef.split) > 0.05f || std::abs (cur.feelHz - lastCoef.feelHz) > 0.05f
            || std::abs (lowCutHz - lastCoef.lowCutHz) > 0.05f)
        {
            const double fc = cur.feelHz > 0 ? (double) cur.feelHz : std::sqrt (20.0 * cur.split), pc = cur.split * 1.414;
            for (auto& c : ch)
            {
                c.fPeak.setPeak (sr, fc, 0.7, cur.feelDb);
                c.pPeak.setPeak (sr, pc, 0.9, cur.punchDb);
                c.hp1.setHighPass (sr, lowCutHz);  c.hp2.setHighPass (sr, lowCutHz);
                c.low1.setLowPass (sr, cur.split * 2.0);  c.low2.setLowPass (sr, cur.split * 2.0);
            }
            lastCoef = cur;
            lastCoef.lowCutHz = lowCutHz;
        }
        const bool lowCutOn = lowCutHz > 12.0f;
        bool beatFx = std::abs (cur.attackDb) > 0.02f || std::abs (cur.tailDb) > 0.02f || std::abs (cur.mudDb) > 0.02f;
        for (auto d : cur.duckDb) beatFx = beatFx || std::abs (d) > 0.02f;

        const float fadeStep = (float) (1.0 / juce::jmax (1.0, glideSamples));
        const double msPerStep = stepsPerSample > 0 ? 1000.0 / (stepsPerSample * sr) : 100.0;   // one 16th, in ms
        const double beatMs = msPerStep * 4.0;
        for (int i = 0; i < n; ++i)
        {
            const float dl = L[i], dr = R[i];
            peakHold = std::max ({ std::abs (dl), std::abs (dr), peakHold * peakDecay });
            fade = juce::jlimit (0.0f, 1.0f, fade + (active ? fadeStep : -fadeStep));
            if (fade <= 0.0f) continue;                                   // fully bypassed: untouched

            float x[2] = { dl, dr };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                if (lowCutOn) x[c] = s.hp2.process (s.hp1.process (x[c]));
                x[c] = s.pPeak.process (s.fPeak.process (x[c]));
            }

            if (beatFx)
            {
                const double steps = startSteps + i * stepsPerSample;
                double inBeat = std::fmod (steps, 4.0);
                if (inBeat < 0) inBeat += 4.0;
                const int k = juce::jlimit (0, 3, (int) inBeat);
                const double ms = inBeat * msPerStep;
                // built-in ducking x kick tail, on the low end
                float tailDb = 0.0f;
                if (std::abs (cur.tailDb) > 0.02f && ms >= cur.tailStartMs && ms < beatMs * 0.5)
                {
                    const double ramp = juce::jmin ((ms - cur.tailStartMs) / 20.0, (beatMs * 0.5 - ms) / 20.0, 1.0);
                    tailDb = cur.tailDb * (float) ramp;
                }
                const float tLow = juce::Decibels::decibelsToGain (cur.duckDb[(size_t) k] + tailDb);
                gLow = tLow + g2ms * (gLow - tLow);
                // kick attack: the first ~12 ms of the beat, easing back by 30 ms
                const float attShape = ms < 12.0 ? 1.0f : (ms < 30.0 ? (float) (1.0 - (ms - 12.0) / 18.0) : 0.0f);
                const float tAtt = juce::Decibels::decibelsToGain (cur.attackDb * attShape);
                gAtt = tAtt + g1ms * (gAtt - tAtt);
                // mud cut on the kick 16th
                const float tMud = juce::Decibels::decibelsToGain (k == 0 ? cur.mudDb : 0.0f);
                gMud = tMud + g5ms * (gMud - tMud);

                for (int c = 0; c < 2; ++c)
                {
                    auto& s = ch[(size_t) c];
                    const float in = x[c];
                    const float low = s.low2.process (s.low1.process (in));
                    const float att = s.att2.process (s.att1.process (in));
                    const float md = s.mud.process (in);
                    x[c] = in + (gLow - 1.0f) * low + (gAtt - 1.0f) * att + (gMud - 1.0f) * md;
                }
            }

            float l = x[0], r = x[1];
            if (curMono > 0.001f)
            {
                const float side = 0.5f * (l - r);
                const float lowSide = ch[0].monoLp2.process (ch[0].monoLp1.process (side)) * curMono;
                l -= lowSide;  r += lowSide;                               // remove the side below 120 Hz
            }
            if (cur.weight > 0.001f)
            {
                const float low = ch[0].wLp2.process (ch[0].wLp1.process (0.5f * (l + r)));
                const float h = ch[0].hHp2.process (ch[0].hHp1.process (std::tanh (4.0f * low) * 0.25f)) * 3.0f * cur.weight;
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
            const float tgt = pk > ceilLin ? ceilLin / pk : 1.0f;
            limGain = tgt < limGain ? tgt : tgt + relCoef * (limGain - tgt);
            l *= limGain;  r *= limGain;

            L[i] = dl + (l - dl) * fade;
            R[i] = dr + (r - dr) * fade;
        }
    }

    float ceilingDb() const { return 20.0f * std::log10 (peakHold > 0.1f ? std::min (peakHold, 1.0f) : 0.891f); }
    bool isBypassed() const { return fade <= 0.0f; }

private:
    struct Channel
    {
        Biquad hp1, hp2, fPeak, pPeak, low1, low2, att1, att2, mud, monoLp1, monoLp2, wLp1, wLp2, hHp1, hHp2;
    };
    double sr = 44100.0;
    std::array<Channel, 2> ch;
    MatchSettings cur, lastCoef;
    float curLowCut = 2.3f, curMono = 0.0f;
    float relCoef = 0.999f, peakDecay = 0.99999f, msCoef = 0.9999f, g2ms = 0.99f, g1ms = 0.98f, g5ms = 0.995f;
    float limGain = 1.0f, fade = 0.0f, peakHold = 0.0f, preMs = 1.0e-6f, postMs = 1.0e-6f;
    float gLow = 1.0f, gAtt = 1.0f, gMud = 1.0f;
};
} // namespace gs
