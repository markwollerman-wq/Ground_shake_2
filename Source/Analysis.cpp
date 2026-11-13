#include "Analysis.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace gs
{
//==============================================================================
void Biquad::setLowPass (double sr, double f, double q)
{
    const double w0 = juce::MathConstants<double>::twoPi * f / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
    b0 = (1.0 - c) * 0.5 / a0;  b1 = (1.0 - c) / a0;  b2 = b0;
    a1 = -2.0 * c / a0;         a2 = (1.0 - alpha) / a0;
}

void Biquad::setHighPass (double sr, double f, double q)
{
    const double w0 = juce::MathConstants<double>::twoPi * f / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
    b0 = (1.0 + c) * 0.5 / a0;  b1 = -(1.0 + c) / a0;  b2 = b0;
    a1 = -2.0 * c / a0;         a2 = (1.0 - alpha) / a0;
}

void Biquad::setPeak (double sr, double f, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * f / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;  b1 = -2.0 * c / a0;  b2 = (1.0 - alpha * A) / a0;
    a1 = -2.0 * c / a0;           a2 = (1.0 - alpha / A) / a0;
}

void BandSplitter::prepare (double sr, float split, float feelLo)
{
    // two cascaded Butterworth stages = 4th-order (24 dB/oct) slopes
    fHp1.setHighPass (sr, feelLo);    fHp2.setHighPass (sr, feelLo);
    fLp1.setLowPass  (sr, split);     fLp2.setLowPass  (sr, split);
    pHp1.setHighPass (sr, split);     pHp2.setHighPass (sr, split);
    pLp1.setLowPass  (sr, split * 2); pLp2.setLowPass  (sr, split * 2);
    mHp1.setHighPass (sr, kMudLo);    mHp2.setHighPass (sr, kMudLo);
    mLp1.setLowPass  (sr, kMudHi);    mLp2.setLowPass  (sr, kMudHi);
    reset();
}

void BandSplitter::reset()
{
    for (auto* b : { &fHp1, &fHp2, &fLp1, &fLp2, &pHp1, &pHp2, &pLp1, &pLp2, &mHp1, &mHp2, &mLp1, &mLp2 })
        b->reset();
}

//==============================================================================
float powerToDb (float p) noexcept { return 10.0f * std::log10 (std::max (p, 1.0e-12f)); }

float StepProfile::shakeDb() const
{
    if (! valid || fullTotal <= 0.0f) return -100.0f;
    return powerToDb ((feelTotal + kPunchWeight * punchTotal) / fullTotal);
}

float StepProfile::feelDb() const  { return valid && fullTotal > 0 ? powerToDb (feelTotal / fullTotal) : -100.0f; }
float StepProfile::punchDb() const { return valid && fullTotal > 0 ? powerToDb (punchTotal / fullTotal) : -100.0f; }
float StepProfile::mudDb() const   { return valid && fullTotal > 0 ? powerToDb (mudTotal / fullTotal) : -100.0f; }

float StepProfile::stepDb (int s) const
{
    if (! valid || fullTotal <= 0.0f) return -100.0f;
    return powerToDb ((feel[(size_t) s] + kPunchWeight * punch[(size_t) s]) / fullTotal);
}

float StepProfile::stepMudDb (int s) const
{
    if (! valid || fullTotal <= 0.0f) return -100.0f;
    return powerToDb (mud[(size_t) s] / fullTotal);
}

//==============================================================================
void computeEnvelopes (ReferenceTrack& out, float split)
{
    const double sr = out.sampleRate;
    const int n = out.audio.getNumSamples();
    out.split = split;
    out.hop = juce::jmax (64, (int) std::round (sr / 172.0));          // ~5.8 ms
    const int frames = n / out.hop;
    for (auto* v : { &out.feelEnv, &out.punchEnv, &out.mudEnv, &out.fullEnv, &out.clickEnv })
        v->assign ((size_t) frames, 0.0f);

    BandSplitter bands;
    bands.prepare (sr, split);
    Biquad c1, c2, c3, c4;                       // 1-4 kHz band for the kick's click
    c1.setHighPass (sr, 1000.0); c2.setHighPass (sr, 1000.0);
    c3.setLowPass (sr, 4000.0);  c4.setLowPass (sr, 4000.0);
    const float* L = out.audio.getReadPointer (0);
    const float* R = out.audio.getReadPointer (1);
    for (int f = 0; f < frames; ++f)
    {
        double fe = 0, pu = 0, mu = 0, fu = 0, cl = 0;
        for (int i = f * out.hop, end = i + out.hop; i < end; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            float a, b, d;
            bands.process (m, a, b, d);
            const float c = c4.process (c3.process (c2.process (c1.process (m))));
            fe += (double) a * a;  pu += (double) b * b;  mu += (double) d * d;  fu += (double) m * m;  cl += (double) c * c;
        }
        out.clickEnv[(size_t) f] = (float) (cl / out.hop);
        out.feelEnv[(size_t) f]  = (float) (fe / out.hop);
        out.punchEnv[(size_t) f] = (float) (pu / out.hop);
        out.mudEnv[(size_t) f]   = (float) (mu / out.hop);
        out.fullEnv[(size_t) f]  = (float) (fu / out.hop);
    }
}

bool loadReference (const juce::File& file, ReferenceTrack& out, juce::String& error, float split)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Can't read this file. Use WAV, AIFF or MP3.";
        return false;
    }

    const double sr = reader->sampleRate;
    const auto maxSamples = (juce::int64) (sr * 60.0 * 15.0);          // 15 minutes is plenty
    const int n = (int) std::min (reader->lengthInSamples, maxSamples);
    if (n < (int) (sr * 1.0))
    {
        error = "This file is shorter than 1 second.";
        return false;
    }

    out = ReferenceTrack();
    out.file = file;
    out.name = file.getFileNameWithoutExtension();
    out.sampleRate = sr;
    out.audio.setSize (2, n);
    reader->read (&out.audio, 0, n, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        out.audio.copyFrom (1, 0, out.audio, 0, 0, n);

    computeEnvelopes (out, split);

    // waveform overview
    const float* L = out.audio.getReadPointer (0);
    const float* R = out.audio.getReadPointer (1);
    const int points = 1600;
    out.overviewMin.assign (points, 0.0f);
    out.overviewMax.assign (points, 0.0f);
    for (int p = 0; p < points; ++p)
    {
        const int a = (int) ((juce::int64) n * p / points), b = (int) ((juce::int64) n * (p + 1) / points);
        float lo = 0, hi = 0;
        for (int i = a; i < b; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            lo = std::min (lo, m);  hi = std::max (hi, m);
        }
        out.overviewMin[(size_t) p] = lo;  out.overviewMax[(size_t) p] = hi;
    }

    detectTempo (out);
    return true;
}

//==============================================================================
namespace
{
    // positive log-energy change = how much "new" energy starts in this frame
    std::vector<float> onsetCurve (const std::vector<float>& env)
    {
        std::vector<float> o (env.size(), 0.0f);
        for (size_t i = 1; i < env.size(); ++i)
            o[i] = std::max (0.0f, std::log (env[i] + 1.0e-9f) - std::log (env[i - 1] + 1.0e-9f));
        return o;
    }

    void removeLocalMean (std::vector<float>& o, int radius)
    {
        std::vector<double> csum (o.size() + 1, 0.0);
        for (size_t i = 0; i < o.size(); ++i) csum[i + 1] = csum[i] + o[i];
        std::vector<float> r (o.size());
        for (size_t i = 0; i < o.size(); ++i)
        {
            const size_t a = i > (size_t) radius ? i - (size_t) radius : 0;
            const size_t b = std::min (o.size(), i + (size_t) radius + 1);
            r[i] = std::max (0.0f, o[i] - (float) ((csum[b] - csum[a]) / (double) (b - a)));
        }
        o.swap (r);
    }

    float sampleAt (const std::vector<float>& v, double pos)
    {
        if (pos < 0 || pos >= (double) v.size() - 1) return 0.0f;
        const auto i = (size_t) pos;
        const float t = (float) (pos - (double) i);
        return v[i] * (1.0f - t) + v[i + 1] * t;
    }
}

void detectTempo (ReferenceTrack& ref)
{
    const size_t N = ref.feelEnv.size();
    const double hopSec = ref.hop / ref.sampleRate;

    std::vector<float> low (N), full = ref.fullEnv;
    for (size_t i = 0; i < N; ++i) low[i] = ref.feelEnv[i] + ref.punchEnv[i];
    auto oLow = onsetCurve (low), oFull = onsetCurve (full);
    std::vector<float> o (N);
    for (size_t i = 0; i < N; ++i) o[i] = oLow[i] + 0.5f * oFull[i];
    removeLocalMean (o, (int) std::round (0.25 / hopSec));

    // --- coarse: autocorrelation over 70-190 BPM with a beat-multiple comb
    auto ac = [&] (double lag)
    {
        if (lag < 1 || lag >= (double) N / 2) return 0.0;
        double s = 0;
        const size_t L = (size_t) lag;
        const double t = lag - (double) L;
        for (size_t i = 0; i + L + 1 < N; ++i)
            s += o[i] * ((1.0 - t) * o[i + L] + t * o[i + L + 1]);
        return s / (double) (N - L);
    };
    double bestBpm = 120, bestScore = -1;
    for (double bpm = 70; bpm <= 190; bpm += 0.5)
    {
        const double lag = 60.0 / bpm / hopSec;
        const double s = ac (lag) + 0.5 * ac (2 * lag) + 0.25 * ac (4 * lag);
        if (s > bestScore) { bestScore = s; bestBpm = bpm; }
    }
    while (bestBpm < 100) bestBpm *= 2;                 // dance music: fold 75 -> 150 etc.
    while (bestBpm >= 200) bestBpm *= 0.5;

    // --- fine: pulse-train fit over tempo and phase
    double fineBpm = bestBpm, fineScore = -1, fineFrame = 0;
    for (double bpm = bestBpm - 1.5; bpm <= bestBpm + 1.5; bpm += 0.01)
    {
        const double period = 60.0 / bpm / hopSec;
        for (double ph = 0; ph < period; ph += 0.5)
        {
            double s = 0;
            int beats = 0;
            for (double p = ph; p < (double) N - 1; p += period, ++beats)
                s += sampleAt (o, p);
            s /= std::max (1, beats);
            if (s > fineScore) { fineScore = s; fineBpm = bpm; fineFrame = ph; }
        }
    }
    // round to 0.01 BPM; most dance tracks sit on a whole BPM (short clips get a wider snap,
    // since a few seconds can't pin the tempo down to a tenth)
    const double snap = ref.lengthSeconds() < 30.0 ? 0.3 : 0.12;
    if (std::abs (fineBpm - std::round (fineBpm)) < snap) fineBpm = std::round (fineBpm);
    ref.detectedBpm = std::round (fineBpm * 100.0) / 100.0;

    // --- beat PHASE: rolling basses put big low-end onsets BETWEEN the kicks and the sub
    //     peaks late, so the beat is placed on the kick's click instead: linear energy
    //     rises in the 1-4 kHz band (tested on rolling-bass techno at 146-155 BPM: 0-4 ms).
    {
        std::vector<float> oKick (N, 0.0f);
        for (size_t i = 1; i < N; ++i) oKick[i] = std::max (0.0f, ref.clickEnv[i] - ref.clickEnv[i - 1]);
        const double per = 60.0 / ref.detectedBpm / hopSec;
        double bestPh = 0, bestS = -1;
        for (double ph = 0; ph < per; ph += 0.25)
        {
            double sc = 0;
            for (double p = ph; p < (double) N - 1; p += per)
                sc += sampleAt (oKick, p);
            if (sc > bestS) { bestS = sc; bestPh = ph; }
        }
        // A muted (high-passed) kick has no click, and then the 1-4 kHz band is just hi-hats on
        // every 16th: the four 16th positions score about the same. In that case pick the 16th
        // where the low thump (sub + punch + body) starts.
        auto scoreAt = [&] (const std::vector<float>& o2, double ph)
        {
            double sc = 0;
            for (double p = ph; p < (double) N - 1; p += per)
            {
                float pk = 0;
                for (int d = -1; d <= 1; ++d) pk = std::max (pk, sampleAt (o2, p + d));
                sc += pk;
            }
            return sc;
        };
        std::array<double, 4> cs {};
        for (int k = 0; k < 4; ++k) cs[(size_t) k] = scoreAt (oKick, bestPh + k * per / 4.0);
        auto sorted = cs;
        std::sort (sorted.begin(), sorted.end());
        if (sorted[3] < 1.35 * sorted[2])
        {
            std::vector<float> oBody (N, 0.0f);
            for (size_t i = 1; i < N; ++i)
            {
                const float a = ref.feelEnv[i] + ref.punchEnv[i] + ref.mudEnv[i];
                const float b = ref.feelEnv[i - 1] + ref.punchEnv[i - 1] + ref.mudEnv[i - 1];
                oBody[i] = std::max (0.0f, a - b);
            }
            int bestK = 0;
            double bk = -1;
            for (int k = 0; k < 4; ++k)
            {
                const double sc = scoreAt (oBody, bestPh + k * per / 4.0);
                if (sc > bk) { bk = sc; bestK = k; }
            }
            bestPh = std::fmod (bestPh + bestK * per / 4.0, per);
        }
        fineFrame = bestPh;
    }

    // --- downbeat: which of the 4 beats gets the most "new" energy (crashes, phrase starts)
    const double period = 60.0 / ref.detectedBpm / hopSec;
    double bestBar = -1;
    int bestOffset = 0;
    for (int m = 0; m < 4; ++m)
    {
        double s = 0;
        for (double p = fineFrame + m * period; p < (double) N - 3; p += 4 * period)
        {
            float pk = 0;
            for (int d = -2; d <= 2; ++d) pk = std::max (pk, sampleAt (oFull, p + d));
            s += pk;
        }
        if (s > bestBar) { bestBar = s; bestOffset = m; }
    }
    double first = (fineFrame + bestOffset * period) * hopSec;
    const double barSec = 240.0 / ref.detectedBpm;
    const double beatSec = barSec / 4.0;

    // exported loops (short files) start on a bar line: if a beat sits at the very start, use it
    const double phase0 = std::fmod (fineFrame * hopSec, beatSec);
    if (ref.lengthSeconds() < 60.0 && (phase0 < 0.035 || beatSec - phase0 < 0.035))
    {
        ref.firstDownbeatSec = phase0 < 0.035 ? phase0 : 0.0;
        if (ref.firstDownbeatSec < 0.035) ref.firstDownbeatSec = 0.0;
        return;
    }
    while (first - barSec >= -0.02) first -= barSec;    // earliest bar line in the file
    if (first < 0) first += barSec;
    if (first > barSec - 0.03) first -= barSec;         // within a few ms of 0 -> start of file
    ref.firstDownbeatSec = std::max (0.0, first);
}

//==============================================================================
int numBarsInReference (const ReferenceTrack& ref, double bpm, double gridOffsetSec)
{
    if (bpm <= 0) return 0;
    const double barSec = 240.0 / bpm;
    const double usable = ref.lengthSeconds() - (ref.firstDownbeatSec + gridOffsetSec);
    return juce::jmax (0, (int) std::floor (usable / barSec + 0.02));
}

StepProfile profileForWindow (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int startBar, int bars)
{
    StepProfile p;
    if (bpm <= 0 || ref.feelEnv.empty()) return p;

    const double hopSec = ref.hop / ref.sampleRate;
    const double barSec = 240.0 / bpm, stepSec = barSec / kStepsPerBar;
    const double start = ref.firstDownbeatSec + gridOffsetSec + startBar * barSec;
    const auto frames = ref.feelEnv.size();

    std::array<double, kStepsPerBar> fe {}, pu {}, mu {}, fu {};
    std::array<int, kStepsPerBar> cnt {};
    double tf = 0, tp = 0, tm = 0, tu = 0;
    long tc = 0;

    for (int b = 0; b < bars; ++b)
        for (int s = 0; s < kStepsPerBar; ++s)
        {
            const double t0 = start + (b * kStepsPerBar + s) * stepSec;
            const auto i0 = (long) std::floor (t0 / hopSec), i1 = (long) std::floor ((t0 + stepSec) / hopSec);
            for (long i = std::max (0L, i0); i < i1 && i < (long) frames; ++i)
            {
                const auto k = (size_t) i;
                fe[(size_t) s] += ref.feelEnv[k];
                pu[(size_t) s] += ref.punchEnv[k];
                mu[(size_t) s] += ref.mudEnv[k];
                fu[(size_t) s] += ref.fullEnv[k];
                ++cnt[(size_t) s];
                tf += ref.feelEnv[k];  tp += ref.punchEnv[k];  tm += ref.mudEnv[k];  tu += ref.fullEnv[k];
                ++tc;
            }
        }

    if (tc == 0) return p;
    for (size_t s = 0; s < (size_t) kStepsPerBar; ++s)
        if (cnt[s] > 0)
        {
            p.feel[s] = (float) (fe[s] / cnt[s]);
            p.punch[s] = (float) (pu[s] / cnt[s]);
            p.mud[s] = (float) (mu[s] / cnt[s]);
            p.full[s] = (float) (fu[s] / cnt[s]);
        }
    p.feelTotal = (float) (tf / tc);  p.punchTotal = (float) (tp / tc);
    p.mudTotal = (float) (tm / tc);   p.fullTotal = (float) (tu / tc);
    p.valid = true;
    return p;
}

float windowLevelDb (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int startBar, int bars)
{
    if (bpm <= 0 || ref.fullEnv.empty()) return -100.0f;
    const double hopSec = ref.hop / ref.sampleRate, barSec = 240.0 / bpm;
    const double t0 = ref.firstDownbeatSec + gridOffsetSec + startBar * barSec;
    const long i0 = std::max (0L, (long) (t0 / hopSec));
    const long i1 = std::min ((long) ref.fullEnv.size(), (long) ((t0 + bars * barSec) / hopSec));
    if (i1 <= i0) return -100.0f;
    double s = 0;
    for (long i = i0; i < i1; ++i) s += ref.fullEnv[(size_t) i];
    return powerToDb ((float) (s / (double) (i1 - i0)));
}

int findDropBar (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int bars)
{
    const int barsAvail = numBarsInReference (ref, bpm, gridOffsetSec);
    if (barsAvail <= bars) return 0;
    std::vector<float> lv;
    float best = -200.0f;
    for (int b = 0; b + bars <= barsAvail; ++b)
    {
        // "fullest, loudest": the whole mix level plus how much sub it carries
        const auto prof = profileForWindow (ref, bpm, gridOffsetSec, b, bars);
        const float v = windowLevelDb (ref, bpm, gridOffsetSec, b, bars) + 0.25f * prof.shakeDb();
        lv.push_back (v);
        best = std::max (best, v);
    }
    for (int b = 0; b < (int) lv.size(); ++b)
        if (lv[(size_t) b] >= best - 0.2f)
            return b;
    return 0;
}

int findBreakdownBar (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int bars)
{
    const int barsAvail = numBarsInReference (ref, bpm, gridOffsetSec);
    if (barsAvail <= bars) return -1;
    std::vector<float> lv, sh;
    float loudest = -200.0f, drop = -200.0f;
    for (int b = 0; b + bars <= barsAvail; ++b)
    {
        lv.push_back (windowLevelDb (ref, bpm, gridOffsetSec, b, bars));
        sh.push_back (profileForWindow (ref, bpm, gridOffsetSec, b, bars).shakeDb());
        loudest = std::max (loudest, lv.back());
        drop = std::max (drop, sh.back());
    }
    float least = 100.0f;
    for (size_t i = 0; i < lv.size(); ++i)
        if (lv[i] >= loudest - 12.0f) least = std::min (least, sh[i]);
    if (least > drop - 8.0f) return -1;                 // no section with clearly less sub
    for (size_t i = 0; i < lv.size(); ++i)
        if (lv[i] >= loudest - 12.0f && sh[i] <= least + 1.0f)
            return (int) i;
    return -1;
}

//==============================================================================
//  Measurements: mono, notes, key, sub crest, loudness, peak
//==============================================================================
namespace
{
    // BS.1770 K-weighting for any sample rate (coefficients as in libebur128)
    void kWeighting (double sr, Biquad& shelf, Biquad& hp)
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.setRaw ((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                          2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            hp.setRaw (1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        }
        shelf.reset();  hp.reset();
    }

    float integratedLoudness (const float* L, const float* R, int n, double sr)
    {
        Biquad sL, hL, sR, hR;
        kWeighting (sr, sL, hL);
        kWeighting (sr, sR, hR);
        const int sub = juce::jmax (1, (int) (sr * 0.1));     // 100 ms pieces, 400 ms blocks
        std::vector<double> pieces;
        double acc = 0;
        int c = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = hL.process (sL.process (L[i])), r = hR.process (sR.process (R[i]));
            acc += (double) l * l + (double) r * r;
            if (++c == sub) { pieces.push_back (acc / sub); acc = 0; c = 0; }
        }
        if (pieces.size() < 4) return -70.0f;
        std::vector<double> blocks;
        for (size_t i = 0; i + 3 < pieces.size(); ++i)
            blocks.push_back ((pieces[i] + pieces[i + 1] + pieces[i + 2] + pieces[i + 3]) * 0.25);
        auto lufsOf = [] (double ms) { return -0.691 + 10.0 * std::log10 (std::max (ms, 1.0e-12)); };
        double s = 0;  int k = 0;
        for (auto b : blocks) if (lufsOf (b) > -70.0) { s += b; ++k; }
        if (k == 0) return -70.0f;
        const double rel = lufsOf (s / k) - 10.0;
        s = 0;  k = 0;
        for (auto b : blocks) if (lufsOf (b) > -70.0 && lufsOf (b) > rel) { s += b; ++k; }
        return k == 0 ? -70.0f : (float) lufsOf (s / k);
    }

    // peak of an averaged power spectrum in [lo, hi] Hz, with parabolic interpolation
    void spectrumPeak (const std::vector<double>& pw, double binHz, float lo, float hi, float& hz, float& conf)
    {
        hz = 0;  conf = 0;
        const int a = juce::jmax (1, (int) std::ceil (lo / binHz)), b = juce::jmin ((int) pw.size() - 2, (int) (hi / binHz));
        if (b <= a) return;
        int best = a;
        double sum = 0;
        for (int i = a; i <= b; ++i) { sum += pw[(size_t) i]; if (pw[(size_t) i] > pw[(size_t) best]) best = i; }
        const double mean = sum / (b - a + 1);
        if (pw[(size_t) best] <= 1.0e-18 || mean <= 0) return;
        const double y0 = std::log (pw[(size_t) best - 1] + 1e-30), y1 = std::log (pw[(size_t) best] + 1e-30),
                     y2 = std::log (pw[(size_t) best + 1] + 1e-30);
        const double den = y0 - 2.0 * y1 + y2;
        const double off = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
        hz = (float) ((best + off) * binHz);
        conf = (float) (pw[(size_t) best] / mean);
    }
}

juce::String noteName (float hz, bool withOctave)
{
    if (hz <= 0) return "--";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::round (69.0 + 12.0 * std::log2 (hz / 440.0));
    const int pc = ((midi % 12) + 12) % 12;
    return juce::String (names[pc]) + (withOctave ? juce::String (midi / 12 - 1) : juce::String());
}

juce::String keyName (int index, bool minor)
{
    if (index < 0) return "--";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[index % 12]) + (minor ? " minor" : " major");
}

float semitonesBetween (float fromHz, float toHz)
{
    if (fromHz <= 0 || toHz <= 0) return 0.0f;
    return 12.0f * std::log2 (toHz / fromHz);
}

Measurements measureAudio (const juce::AudioBuffer<float>& audio, int start, int length, double sr,
                           double firstBeat, double beatLen, float split)
{
    Measurements m;
    start = juce::jlimit (0, audio.getNumSamples(), start);
    length = juce::jmin (length, audio.getNumSamples() - start);
    if (length < (int) (sr * 1.0) || audio.getNumChannels() < 1) return m;
    const float* L = audio.getReadPointer (0) + start;
    const float* R = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0) + start;

    // --- peak + loudness
    float pk = 0;
    for (int i = 0; i < length; ++i) pk = std::max ({ pk, std::abs (L[i]), std::abs (R[i]) });
    m.peakDb = 20.0f * std::log10 (std::max (pk, 1.0e-6f));
    m.lufs = integratedLoudness (L, R, length, sr);

    // --- mono below 120 Hz, sub crest, and a decimated low band for the note finders
    Biquad l1, l2, r1, r2, d1, d2;
    l1.setLowPass (sr, 120.0); l2.setLowPass (sr, 120.0);
    r1.setLowPass (sr, 120.0); r2.setLowPass (sr, 120.0);
    d1.setLowPass (sr, 160.0); d2.setLowPass (sr, 160.0);
    BandSplitter bands;
    bands.prepare (sr, split);
    const int D = juce::jmax (1, (int) std::round (sr / 1500.0));
    const double dsr = sr / D;
    std::vector<float> low;
    low.reserve ((size_t) (length / D + 1));
    const int hop = juce::jmax (64, (int) std::round (sr / 172.0));
    std::vector<float> subFrames;
    double sLL = 0, sRR = 0, sLR = 0, sMid = 0, sSide = 0, acc = 0;
    int accN = 0;
    for (int i = 0; i < length; ++i)
    {
        const float lo = l2.process (l1.process (L[i])), ro = r2.process (r1.process (R[i]));
        sLL += (double) lo * lo;  sRR += (double) ro * ro;  sLR += (double) lo * ro;
        const double mid = 0.5 * (lo + ro), side = 0.5 * (lo - ro);
        sMid += mid * mid;  sSide += side * side;

        const float mono = 0.5f * (L[i] + R[i]);
        float fe, pu, mu;
        bands.process (mono, fe, pu, mu);
        acc += (double) fe * fe + kPunchWeight * (double) pu * pu;
        if (++accN == hop) { subFrames.push_back ((float) (acc / hop)); acc = 0; accN = 0; }

        const float dl = d2.process (d1.process (mono));
        if (i % D == 0) low.push_back (dl);
    }
    if (sLL > 1e-12 && sRR > 1e-12)
    {
        m.monoCorr = (float) (sLR / std::sqrt (sLL * sRR));
        m.sideDb = (float) (10.0 * std::log10 (std::max (sSide, 1e-20) / std::max (sMid, 1e-20)));
    }

    if (subFrames.size() > 20)
    {
        const double mean = std::accumulate (subFrames.begin(), subFrames.end(), 0.0) / (double) subFrames.size();
        auto sorted = subFrames;
        const auto k = (size_t) (0.99 * (double) (sorted.size() - 1));
        std::nth_element (sorted.begin(), sorted.begin() + (long) k, sorted.end());
        if (mean > 1e-15) m.subCrestDb = (float) (10.0 * std::log10 (sorted[k] / mean));
    }

    // --- notes: the bass from long frames, the kick from short windows just after each beat
    constexpr int order = 13, fftN = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) fftN * 2);
    const double binHz = dsr / fftN;
    auto addSpectrum = [&] (const float* x, int n, std::vector<double>& pw)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < n && i < fftN; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1));
            buf[(size_t) i] = x[i] * w;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int i = 0; i < fftN / 2; ++i) pw[(size_t) i] += (double) buf[(size_t) i] * buf[(size_t) i];
    };
    {
        std::vector<double> pw ((size_t) fftN / 2, 0.0);
        const int frame = juce::jmin ((int) low.size(), 2048);
        if (frame > 256)
            for (int p = 0; p + frame <= (int) low.size(); p += frame / 2)
                addSpectrum (low.data() + p, frame, pw);
        spectrumPeak (pw, binHz, 27.0f, 72.0f, m.bassHz, m.bassConf);
    }
    if (beatLen > 0)
    {
        // kick windows minus the same-length windows half a beat later (bass only), so the
        // sustained bass note doesn't win the search. Each window starts from the kick's own
        // onset (found within -40..+60 ms of the grid beat), so a kick that sits a few ms off
        // the grid, or a host clock a little off, doesn't change the reading.
        std::vector<float> env (low.size(), 0.0f);
        {
            float e = 0.0f;
            const float k = std::exp (-1.0f / (0.004f * (float) dsr));
            for (size_t i = 0; i < low.size(); ++i) { e = low[i] * low[i] + k * (e - low[i] * low[i]); env[i] = e; }
        }
        std::vector<double> pw ((size_t) fftN / 2, 0.0), between ((size_t) fftN / 2, 0.0);
        const int a = (int) (0.05 * dsr), w = (int) (0.14 * dsr), half = (int) (0.5 * beatLen / D);
        const int pre = (int) (0.04 * dsr), post = (int) (0.06 * dsr), look = (int) (0.12 * dsr);
        int count = 0;
        for (double b = firstBeat; b < length; b += beatLen)
        {
            const int g0 = (int) (b / D);
            const int lo = juce::jmax (0, g0 - pre), hi = juce::jmin ((int) env.size() - 1, g0 + look);
            if (hi <= lo) break;
            float mx = 0.0f;
            for (int i = lo; i <= hi; ++i) mx = std::max (mx, env[(size_t) i]);
            int onset = g0;
            for (int i = lo; i <= juce::jmin (hi, g0 + post); ++i)
                if (env[(size_t) i] >= 0.3f * mx) { onset = i; break; }
            const int s0 = onset + a;
            if (s0 + half + w >= (int) low.size()) break;
            addSpectrum (low.data() + s0, w, pw);
            addSpectrum (low.data() + s0 + half, w, between);
            ++count;
        }
        for (size_t i = 0; i < pw.size(); ++i) pw[i] = std::max (0.0, pw[i] - between[i]) + 1e-20;
        if (count >= 4) spectrumPeak (pw, binHz, 35.0f, 90.0f, m.kickHz, m.kickConf);
    }

    // --- key: chroma over 80 Hz - 2 kHz, matched to the Krumhansl key profiles
    {
        constexpr int kOrder = 13, kN = 1 << kOrder;
        juce::dsp::FFT kfft (kOrder);
        std::vector<float> kb ((size_t) kN * 2);
        std::array<double, 12> chroma {};
        std::array<double, Measurements::numBands> bands {};
        double bandTotal = 0;
        const int frames = length / kN;
        const int stride = juce::jmax (1, frames / 48);
        for (int f = 0; f < frames; f += stride)
        {
            std::fill (kb.begin(), kb.end(), 0.0f);
            for (int i = 0; i < kN; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kN - 1));
                kb[(size_t) i] = 0.5f * (L[f * kN + i] + R[f * kN + i]) * w;
            }
            kfft.performFrequencyOnlyForwardTransform (kb.data());
            for (int i = 1; i < kN / 2; ++i)
            {
                const double hz = i * sr / kN, pw = (double) kb[(size_t) i] * kb[(size_t) i];
                bandTotal += pw;
                const int b = hz < 20 ? -1 : hz < 60 ? 0 : hz < 120 ? 1 : hz < 300 ? 2 : hz < 2000 ? 3 : hz < 10000 ? 4 : -1;
                if (b >= 0) bands[(size_t) b] += pw;
            }
            double tot = 1e-12;
            std::array<double, 12> c {};
            for (int i = 1; i < kN / 2; ++i)
            {
                const double hz = i * sr / kN;
                if (hz < 80.0 || hz > 2000.0) continue;
                const int pc = (((int) std::round (69.0 + 12.0 * std::log2 (hz / 440.0)) % 12) + 12) % 12;
                c[(size_t) pc] += kb[(size_t) i];
                tot += kb[(size_t) i];
            }
            for (int k = 0; k < 12; ++k) chroma[(size_t) k] += c[(size_t) k] / tot;
        }
        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [&] (const double* prof, int root)
        {
            double mx = 0, my = 0;
            for (int k = 0; k < 12; ++k) { mx += chroma[(size_t) k]; my += prof[k]; }
            mx /= 12;  my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int k = 0; k < 12; ++k)
            {
                const double x = chroma[(size_t) ((k + root) % 12)] - mx, y = prof[k] - my;
                sxy += x * y;  sxx += x * x;  syy += y * y;
            }
            return sxx > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
        };
        for (size_t b = 0; b < bands.size(); ++b)
            m.bandDb[b] = (float) (10.0 * std::log10 (std::max (bands[b], 1e-20) / std::max (bandTotal, 1e-20)));
        if (frames > 0)
        {
            double best = -2, second = -2;
            for (int root = 0; root < 12; ++root)
                for (int mi = 0; mi < 2; ++mi)
                {
                    const double c = corr (mi ? minor : major, root);
                    if (c > best) { second = best; best = c; m.keyIndex = root; m.keyMinor = mi == 1; }
                    else if (c > second) second = c;
                }
            m.keyConf = (float) (best - second);
        }
    }

    m.valid = true;
    return m;
}

//==============================================================================
void LiveStepMeter::setLoopSteps (int steps)
{
    steps = juce::jlimit (kStepsPerBar, kLoopSteps, steps);
    if (steps != loopSteps)
    {
        loopSteps = steps;
        reset();
    }
}

void LiveStepMeter::reset()
{
    done = {};
    current = {};
    currentSlot = -1;
    serial = 0;
}

void LiveStepMeter::addSample (double positionInSteps, float feelSq, float punchSq, float mudSq, float fullSq) noexcept
{
    long whole = (long) std::floor (positionInSteps);
    int slot = (int) (((whole % loopSteps) + loopSteps) % loopSteps);
    if (slot != currentSlot)
    {
        if (currentSlot >= 0 && current.count > 0)
        {
            current.serial = ++serial;
            done[(size_t) currentSlot] = current;
        }
        current = {};
        currentSlot = slot;
    }
    current.feel += feelSq;  current.punch += punchSq;  current.mud += mudSq;  current.full += fullSq;
    ++current.count;
}

bool LiveStepMeter::hasData() const
{
    for (auto& s : done) if (s.count > 0) return true;
    return false;
}

int LiveStepMeter::slotsFilled() const
{
    int c = 0;
    for (auto& s : done) if (s.count > 0 && s.serial + (long) loopSteps >= serial) ++c;
    return c;
}

StepProfile LiveStepMeter::profile() const
{
    StepProfile p;
    std::array<double, kStepsPerBar> fe {}, pu {}, mu {}, fu {};
    std::array<int, kStepsPerBar> cnt {};
    double tf = 0, tp = 0, tm = 0, tu = 0;
    long tc = 0;
    for (size_t i = 0; i < done.size(); ++i)
    {
        const auto& s = done[i];
        if (s.count == 0 || s.serial + (long) loopSteps < serial) continue;   // only the last loop pass
        const size_t st = i % (size_t) kStepsPerBar;
        fe[st] += s.feel / s.count;  pu[st] += s.punch / s.count;  mu[st] += s.mud / s.count;  fu[st] += s.full / s.count;
        ++cnt[st];
        tf += s.feel;  tp += s.punch;  tm += s.mud;  tu += s.full;  tc += s.count;
    }
    if (tc == 0) return p;
    for (size_t s = 0; s < (size_t) kStepsPerBar; ++s)
        if (cnt[s] > 0)
        {
            p.feel[s] = (float) (fe[s] / cnt[s]);
            p.punch[s] = (float) (pu[s] / cnt[s]);
            p.mud[s] = (float) (mu[s] / cnt[s]);
            p.full[s] = (float) (fu[s] / cnt[s]);
        }
    p.feelTotal = (float) (tf / tc);  p.punchTotal = (float) (tp / tc);
    p.mudTotal = (float) (tm / tc);   p.fullTotal = (float) (tu / tc);
    p.valid = true;
    return p;
}
} // namespace gs
