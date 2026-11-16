// Ground Shake - Phase 3: reference slots, the learn pass, pairs, follow playhead,
// per-pair corrections and the readout the editor shows.
#include "PluginProcessor.h"

namespace
{
    const juce::uint8 kPairColours = 8;

    // compact text forms for saving analyses with the project
    juce::String floatsToString (const std::vector<float>& v)
    {
        juce::StringArray a;
        for (auto f : v) a.add (juce::String (f, 5));
        return a.joinIntoString (" ");
    }
    std::vector<float> stringToFloats (const juce::String& s)
    {
        std::vector<float> v;
        for (auto& t : juce::StringArray::fromTokens (s, " ", "")) if (t.isNotEmpty()) v.push_back (t.getFloatValue());
        return v;
    }

    juce::String profileToString (const gs::StepProfile& p)
    {
        std::vector<float> v;
        for (int i = 0; i < gs::kStepsPerBar; ++i)
        {
            v.push_back (p.feel[(size_t) i]);  v.push_back (p.punch[(size_t) i]);
            v.push_back (p.mud[(size_t) i]);   v.push_back (p.full[(size_t) i]);
        }
        v.insert (v.end(), { p.feelTotal, p.punchTotal, p.mudTotal, p.fullTotal, p.valid ? 1.0f : 0.0f });
        return floatsToString (v);
    }
    gs::StepProfile stringToProfile (const juce::String& s)
    {
        gs::StepProfile p;
        const auto v = stringToFloats (s);
        if (v.size() < (size_t) gs::kStepsPerBar * 4 + 5) return p;
        size_t k = 0;
        for (int i = 0; i < gs::kStepsPerBar; ++i)
        {
            p.feel[(size_t) i] = v[k++];  p.punch[(size_t) i] = v[k++];
            p.mud[(size_t) i] = v[k++];   p.full[(size_t) i] = v[k++];
        }
        p.feelTotal = v[k++];  p.punchTotal = v[k++];  p.mudTotal = v[k++];  p.fullTotal = v[k++];
        p.valid = v[k] > 0.5f;
        return p;
    }

    juce::String measurementsToString (const gs::Measurements& m)
    {
        std::vector<float> v { m.valid ? 1.0f : 0.0f, m.monoCorr, m.sideDb, m.bassHz, m.bassConf, m.kickHz, m.kickConf,
                               (float) m.keyIndex, m.keyMinor ? 1.0f : 0.0f, m.keyConf, m.subCrestDb, m.lufs, m.peakDb };
        for (auto b : m.bandDb) v.push_back (b);
        v.insert (v.end(), { m.kick.valid ? 1.0f : 0.0f, m.kick.riseMs, m.kick.clickDb, m.kick.decayMs, m.kick.pitchDropSt,
                             m.kick.startHz, (float) (int) m.bassPattern });
        for (auto b : m.bassLevel) v.push_back (b);
        for (auto b : m.bassOnset) v.push_back (b);
        v.push_back ((float) m.bassNotes.count);
        for (auto n : m.bassNotes.midi) v.push_back ((float) n);
        for (auto w : m.bassNotes.weight) v.push_back (w);
        return floatsToString (v);
    }
    gs::Measurements stringToMeasurements (const juce::String& s)
    {
        gs::Measurements m;
        const auto v = stringToFloats (s);
        if (v.size() < 13 + 5 + 7 + 8 + 9) return m;
        size_t k = 0;
        m.valid = v[k++] > 0.5f;  m.monoCorr = v[k++];  m.sideDb = v[k++];  m.bassHz = v[k++];  m.bassConf = v[k++];
        m.kickHz = v[k++];  m.kickConf = v[k++];  m.keyIndex = (int) v[k++];  m.keyMinor = v[k++] > 0.5f;
        m.keyConf = v[k++];  m.subCrestDb = v[k++];  m.lufs = v[k++];  m.peakDb = v[k++];
        for (auto& b : m.bandDb) b = v[k++];
        m.kick.valid = v[k++] > 0.5f;  m.kick.riseMs = v[k++];  m.kick.clickDb = v[k++];  m.kick.decayMs = v[k++];
        m.kick.pitchDropSt = v[k++];  m.kick.startHz = v[k++];  m.bassPattern = (gs::BassPattern) (int) v[k++];
        for (auto& b : m.bassLevel) b = v[k++];
        for (auto& b : m.bassOnset) b = v[k++];
        m.bassNotes.count = (int) v[k++];
        for (auto& n : m.bassNotes.midi) n = (int) v[k++];
        for (auto& w : m.bassNotes.weight) w = v[k++];
        return m;
    }

    juce::String correctionToString (const gs::MatchSettings& c)
    {
        std::vector<float> v { c.feelDb, c.punchDb, c.mono ? 1.0f : 0.0f, c.weight, c.feelHz };
        for (auto d : c.duckDb) v.push_back (d);
        v.insert (v.end(), { c.attackDb, c.tailDb, c.tailStartMs, c.mudDb, c.lowCutHz });
        return floatsToString (v);
    }
    gs::MatchSettings stringToCorrection (const juce::String& s)
    {
        gs::MatchSettings c;
        const auto v = stringToFloats (s);
        if (v.size() < 14) return c;
        size_t k = 0;
        c.feelDb = v[k++];  c.punchDb = v[k++];  c.mono = v[k++] > 0.5f;  c.weight = v[k++];  c.feelHz = v[k++];
        for (auto& d : c.duckDb) d = v[k++];
        c.attackDb = v[k++];  c.tailDb = v[k++];  c.tailStartMs = v[k++];  c.mudDb = v[k++];  c.lowCutHz = v[k++];
        return c;
    }

    float zoneGainDb (float zoneYou, float fullYou, float zoneRef, float fullRef)
    {
        if (fullYou <= 0 || fullRef <= 0 || zoneYou <= 0) return 0.0f;
        const double f = juce::jlimit (1.0e-6, 0.95, (double) zoneYou / fullYou);
        const double t = juce::jlimit (1.0e-6, 0.95, (double) zoneRef / fullRef);
        return (float) (10.0 * std::log10 (t * (1.0 - f) / (f * (1.0 - t))));
    }

    juce::String sdb (float v) { return (v >= 0 ? "+" : "-") + juce::String (std::abs (v), 1) + " dB"; }

    // A stretch of a reference, anti-alias filtered and resampled to the learned track's rate,
    // so both sides go through the same filters
    juce::AudioBuffer<float> windowAtRate (const gs::ReferenceTrack& rt, int s0, int n0, double lsr)
    {
        const double rsr = rt.sampleRate;
        s0 = juce::jlimit (0, juce::jmax (0, rt.audio.getNumSamples() - 1), s0);
        n0 = juce::jmin (n0, rt.audio.getNumSamples() - s0);
        const double ratio = rsr / lsr;
        const int n1 = (int) (n0 / ratio) - 4;
        if (n0 <= 0 || n1 <= 0) return {};
        juce::AudioBuffer<float> src (2, n0), w (2, n1);
        for (int c = 0; c < 2; ++c)
        {
            gs::Biquad f1, f2;
            f1.setLowPass (rsr, lsr * 0.45);  f2.setLowPass (rsr, lsr * 0.45);
            const float* in = rt.audio.getReadPointer (juce::jmin (c, rt.audio.getNumChannels() - 1)) + s0;
            float* o = src.getWritePointer (c);
            for (int i = 0; i < n0; ++i) o[i] = f2.process (f1.process (in[i]));
            juce::LagrangeInterpolator li;
            li.process (ratio, src.getReadPointer (c), w.getWritePointer (c), n1);
        }
        return w;
    }

    // whole-band crest (peak over RMS) and stereo width (side vs mid), for the master check
    struct Spread { float crestDb = 0, sideDb = -60, sideHighDb = -60; bool valid = false; };
    Spread measureSpread (const juce::AudioBuffer<float>& a, int s0, int n, double rate)
    {
        Spread r;
        s0 = juce::jmax (0, s0);
        n = juce::jmin (n, a.getNumSamples() - s0);
        if (n < (int) rate) return r;
        const float* L = a.getReadPointer (0) + s0;
        const float* R = a.getReadPointer (juce::jmin (1, a.getNumChannels() - 1)) + s0;
        gs::Biquad hm1, hm2, hs1, hs2;                       // above 300 Hz: the width you hear
        hm1.setHighPass (rate, 300.0);  hm2.setHighPass (rate, 300.0);
        hs1.setHighPass (rate, 300.0);  hs2.setHighPass (rate, 300.0);
        double pk = 0, ms = 0, mid = 0, side = 0, midH = 0, sideH = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], rr = R[i];
            pk = std::max (pk, (double) std::max (std::abs (l), std::abs (rr)));
            ms += 0.5 * (l * l + rr * rr);
            const float m = 0.5f * (l + rr), sd = 0.5f * (l - rr);
            mid += m * m;  side += sd * sd;
            const float mh = hm2.process (hm1.process (m)), sh = hs2.process (hs1.process (sd));
            midH += mh * mh;  sideH += sh * sh;
        }
        if (ms <= 1.0e-12 || mid <= 1.0e-12) return r;
        r.crestDb = (float) (20.0 * std::log10 (pk / std::sqrt (ms / n)));
        r.sideDb = (float) (10.0 * std::log10 (std::max (1.0e-12, side) / mid));
        r.sideHighDb = (float) (10.0 * std::log10 (std::max (1.0e-12, sideH) / std::max (1.0e-12, midH)));
        r.valid = true;
        return r;
    }
}

//==============================================================================
//  Reference slots A / B
//==============================================================================
std::shared_ptr<gs::ReferenceTrack> GroundShakeProcessor::slotTrack (int slot) const
{
    return slot == activeSlot ? reference : parked[(size_t) juce::jlimit (0, 1, slot)].track;
}
double GroundShakeProcessor::slotBpm (int slot) const    { return slot == activeSlot ? bpm : parked[(size_t) juce::jlimit (0, 1, slot)].bpm; }
double GroundShakeProcessor::slotOffset (int slot) const { return slot == activeSlot ? gridOffsetSec : parked[(size_t) juce::jlimit (0, 1, slot)].offset; }
int GroundShakeProcessor::slotDropBar (int slot) const   { return slot == activeSlot ? dropBar : parked[(size_t) juce::jlimit (0, 1, slot)].dropBar; }
bool GroundShakeProcessor::slotHasReference (int slot) const { return slotTrack (slot) != nullptr; }

juce::String GroundShakeProcessor::getSlotName (int slot) const
{
    auto t = slotTrack (slot);
    return t ? t->name : juce::String();
}

void GroundShakeProcessor::setActiveSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot != activeSlot)
    {
        auto& cur = parked[(size_t) activeSlot];
        cur.track = reference;  cur.resampled = resampled;  cur.resampledRate = resampledRate;
        cur.bpm = bpm;  cur.offset = gridOffsetSec;  cur.windowStartBar = windowStartBar;
        cur.dropBar = dropBar;  cur.breakdownBar = breakdownBar;  cur.refM = refM;

        auto& nx = parked[(size_t) slot];
        reference = nx.track;  resampled = nx.resampled;  resampledRate = nx.resampledRate;
        bpm = nx.bpm;  gridOffsetSec = nx.offset;  windowStartBar = nx.windowStartBar;
        dropBar = nx.dropBar;  breakdownBar = nx.breakdownBar;  refM = nx.refM;
        nx = {};
        activeSlot = slot;
        sectionDismissed = false;
        coach.reset();
        if (reference != nullptr)
        {
            if (resampled == nullptr || std::abs (resampledRate - sr) > 0.5) requestResample (sr);
            rebuildWindow();
        }
        else
        {
            refProfile = {};
            refM = {};
            installPlayback (std::make_unique<RefPlayback>());
            updateCoach();
        }
    }
    // Phase 4: switching the tab never moves a pair (use EDIT PAIR); it lets go of it instead
    if (! switchingForPair && selectedPair >= 0 && selectedPair < (int) pairs.size() && pairs[(size_t) selectedPair].slot != slot)
        deselectForWindowChange();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::deselectForWindowChange()
{
    if (switchingForPair || selectedPair < 0) return;
    selectedPair = -1;
    followPlayhead = false;
    coach.reset();
    updateCoach();
}

//==============================================================================
//  Song start and the learn pass
//==============================================================================
void GroundShakeProcessor::setSongStartBar (int bar)
{
    songStartBar = juce::jlimit (0, 999, bar);
    if (globalSettings != nullptr)
    {
        globalSettings->setValue ("songStartBar", songStartBar);
        globalSettings->saveIfNeeded();
    }
    if (learnState == LearnState::off && learnedTrack == nullptr && pairs.empty())
        originBar = songStartBar > 0 ? songStartBar : 1;
    if (onReferenceChanged) onReferenceChanged();
}

double GroundShakeProcessor::getSongBarNow() const
{
    return hostSteps.load() / 16.0 - (originBar - 1) - trimBars;
}

void GroundShakeProcessor::startLearn()
{
    if (learnState != LearnState::off || buildRunning) return;
    recorder.prepare (sr);
    recorder.clear();
    originBar = songStartBar > 0 ? songStartBar : 1;
    trimBars = 0;
    learnOriginPpq.store ((originBar - 1) * 4.0);
    learnBpm = 0;
    ovMin.clear();
    ovMax.clear();
    learnedBars = 0;
    recorder.ensureChunksAround (juce::jmax (0.0, (hostSteps.load() / 4.0 - learnOriginPpq.load()) * 60.0 / juce::jmax (40.0, hostBpm.load())));
    learnState = LearnState::song;
    learnPairIndex = -1;
    learnMessage = "Learning: play your song from bar " + juce::String (originBar) + " to the end, then press LEARN again.";
    learning.store (true);
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::startLearnPair (int index)
{
    if (learnState != LearnState::off || buildRunning || index < 0 || index >= (int) pairs.size()) return;
    recorder.prepare (sr);                      // keeps what's already recorded
    learnOriginPpq.store ((originBar - 1) * 4.0);
    const auto& p = pairs[(size_t) index];
    recorder.forgetBars (p.songStart + trimBars, p.songEnd + trimBars);
    recorder.ensureChunksAround (juce::jmax (0.0, (p.songStart + trimBars) * 240.0 / juce::jmax (40.0, hostBpm.load()) - 5.0));
    learnState = LearnState::pair;
    learnPairIndex = index;
    learning.store (true);
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::stopLearn()
{
    if (learnState == LearnState::off) return;
    learning.store (false);
    const bool wholeSong = learnState == LearnState::song;
    learnState = LearnState::off;
    if (wholeSong && songStartBar == 0) trimBars = recorder.firstAudibleBar();   // auto start: the first bar with sound
    finishLearn();
}

void GroundShakeProcessor::finishLearn()
{
    if (recorder.lastBar() < 0)
    {
        learnMessage = "Nothing was recorded: press LEARN, then play your song in Ableton.";
        if (onReferenceChanged) onReferenceChanged();
        return;
    }
    // copy the waveform (bars heard this time; keep older bars restored from the project)
    {
        const int last = recorder.lastBar();
        const size_t per = gs::SongRecorder::kOverviewPerBar;
        if (ovMin.size() < (size_t) (last + 1) * per) { ovMin.resize ((size_t) (last + 1) * per, 0.0f); ovMax.resize ((size_t) (last + 1) * per, 0.0f); }
        for (int b = 0; b <= last; ++b)
            if (recorder.barHeard (b))
                for (size_t q = 0; q < per; ++q)
                {
                    ovMin[(size_t) b * per + q] = recorder.overviewMin()[(size_t) b * per + q];
                    ovMax[(size_t) b * per + q] = recorder.overviewMax()[(size_t) b * per + q];
                }
        learnedBars = juce::jmax (learnedBars, last + 1 - trimBars);
    }
    learnMessage = "Building your learned track...";
    buildRunning = true;
    auto flag = alive;
    const int trim = trimBars;
    const float sp = split;
    pool.addJob ([this, flag, trim, sp]
    {
        auto t = std::make_shared<gs::ReferenceTrack>();
        const bool ok = recorder.buildTrack (*t, trim, sp);
        juce::MessageManager::callAsync ([this, flag, ok, t]
        {
            if (! flag->load()) return;
            buildRunning = false;
            if (! ok)
            {
                learnMessage = "The learn pass was too short: play at least a few bars.";
                if (onReferenceChanged) onReferenceChanged();
                return;
            }
            learnedTrack = t;
            learnBpm = t->detectedBpm;
            for (int i = 0; i < (int) pairs.size(); ++i)
                if (std::find (pairsToAnalyze.begin(), pairsToAnalyze.end(), i) == pairsToAnalyze.end()) pairsToAnalyze.push_back (i);
            learnMessage = pairs.empty()
                ? "Learned " + juce::String (learnedBars) + " bars. Press AUTO-PAIR, or drag across your track and press + PAIR."
                : "Learned " + juce::String (learnedBars) + " bars. Measuring your pairs...";
            if (onReferenceChanged) onReferenceChanged();
        });
    });
    if (onReferenceChanged) onReferenceChanged();
}

//==============================================================================
//  Pairs
//==============================================================================
void GroundShakeProcessor::selectPair (int index, bool fromFollow)
{
    if (! fromFollow) followPlayhead = false;
    if (index < 0 || index >= (int) pairs.size())
    {
        selectedPair = -1;
        updateCoach();
        if (onReferenceChanged) onReferenceChanged();
        return;
    }
    selectedPair = index;
    const auto p = pairs[(size_t) index];
    switchingForPair = true;
    if (p.slot != activeSlot && slotHasReference (p.slot)) setActiveSlot (p.slot);
    followLoop = false;
    const int rb = juce::jlimit (1, gs::kBars, p.refBars);
    if (rb != windowBars)
    {
        windowBars = rb;
        loopStepsParam.store (rb * gs::kStepsPerBar);
        updateSections();
    }
    windowStartBar = p.refStart;
    coach.reset();
    rebuildWindow();
    switchingForPair = false;
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::addPair (int songStart, int songEnd)
{
    addPairExplicit (songStart, songEnd, activeSlot, windowStartBar, windowBars);
}

namespace { int snapRefBars (int b) { int r = 1; for (int c : { 1, 2, 4, 8 }) if (b >= c) r = c; return r; } }

void GroundShakeProcessor::addPairExplicit (int songStart, int songEnd, int slot, int refStart, int refBars)
{
    slot = juce::jlimit (0, 1, slot);
    refBars = snapRefBars (refBars);
    if (songEnd < songStart) std::swap (songStart, songEnd);
    songStart = juce::jmax (0, songStart);
    songEnd = juce::jmax (songStart + 1, songEnd);
    if (slotTrack (slot) == nullptr) { learnMessage = "Load a reference first."; if (onReferenceChanged) onReferenceChanged(); return; }
    if (learnedBars <= 0)     { learnMessage = "Press LEARN and play your song first."; if (onReferenceChanged) onReferenceChanged(); return; }
    for (auto& q : pairs)
        if (q.manual && songStart < q.songEnd && songEnd > q.songStart)
        {
            learnMessage = "That overlaps another pair: drag a free stretch, or delete that pair first.";
            if (onReferenceChanged) onReferenceChanged();
            return;
        }
    // auto pairs give way to a hand-made one
    pairs.erase (std::remove_if (pairs.begin(), pairs.end(), [&] (const Pair& q)
                                 { return ! q.manual && songStart < q.songEnd && songEnd > q.songStart; }),
                 pairs.end());
    Pair p;
    p.slot = slot;
    p.refStart = refStart;
    p.refBars = refBars;
    p.songStart = songStart;
    p.songEnd = songEnd;
    p.manual = true;
    int used = 0;
    for (auto& q : pairs) used |= 1 << q.colour;
    for (int c = 0; c < kPairColours; ++c) if (! ((used >> c) & 1)) { p.colour = c; break; }
    pairs.push_back (p);
    std::sort (pairs.begin(), pairs.end(), [] (const Pair& a, const Pair& b) { return a.songStart < b.songStart; });
    int idx = 0;
    for (int i = 0; i < (int) pairs.size(); ++i) if (pairs[(size_t) i].songStart == songStart) idx = i;
    pairsToAnalyze.clear();
    for (int i = 0; i < (int) pairs.size(); ++i) if (! pairs[(size_t) i].analyzed || i == idx) pairsToAnalyze.push_back (i);
    learnMessage = "Pair " + juce::String (idx + 1) + ": your bars " + juce::String (getEffectiveStartBar() + songStart) + "-"
                   + juce::String (getEffectiveStartBar() + songEnd - 1) + " matched to " + getSlotName (slot) + " bars "
                   + juce::String (refStart + 1) + "-" + juce::String (refStart + refBars) + ".";
    selectPair (idx);
    pushCorrectionTable();
}

bool GroundShakeProcessor::editPair (int index, int songStart, int songEnd, int slot, int refStart, int refBars)
{
    if (index < 0 || index >= (int) pairs.size()) return false;
    slot = juce::jlimit (0, 1, slot);
    refBars = snapRefBars (refBars);
    if (songEnd < songStart) std::swap (songStart, songEnd);
    songStart = juce::jmax (0, songStart);
    songEnd = juce::jmax (songStart + 1, songEnd);
    if (slotTrack (slot) == nullptr) { learnMessage = "Load that reference first."; if (onReferenceChanged) onReferenceChanged(); return false; }
    for (int i = 0; i < (int) pairs.size(); ++i)
        if (i != index && pairs[(size_t) i].manual && songStart < pairs[(size_t) i].songEnd && songEnd > pairs[(size_t) i].songStart)
        {
            learnMessage = "That overlaps pair " + juce::String (i + 1) + ": pick a free stretch, or delete that pair first.";
            if (onReferenceChanged) onReferenceChanged();
            return false;
        }
    auto edited = pairs[(size_t) index];
    pairs.erase (pairs.begin() + index);
    // auto pairs give way
    pairs.erase (std::remove_if (pairs.begin(), pairs.end(), [&] (const Pair& q)
                                 { return ! q.manual && songStart < q.songEnd && songEnd > q.songStart; }),
                 pairs.end());
    edited.slot = slot;  edited.refStart = refStart;  edited.refBars = refBars;
    edited.songStart = songStart;  edited.songEnd = songEnd;  edited.manual = true;
    edited.analyzed = false;  edited.outValid = false;  edited.status = 0;  edited.outStatus = 0;
    pairs.push_back (edited);
    std::sort (pairs.begin(), pairs.end(), [] (const Pair& a, const Pair& b) { return a.songStart < b.songStart; });
    int idx = 0;
    for (int i = 0; i < (int) pairs.size(); ++i) if (pairs[(size_t) i].songStart == songStart) idx = i;
    pairsToAnalyze.clear();
    pairsToSimulate.clear();
    for (int i = 0; i < (int) pairs.size(); ++i) if (! pairs[(size_t) i].analyzed) pairsToAnalyze.push_back (i);
    learnMessage = "Pair " + juce::String (idx + 1) + " changed: your bars " + juce::String (getEffectiveStartBar() + songStart) + "-"
                   + juce::String (getEffectiveStartBar() + songEnd - 1) + " matched to " + getSlotName (slot) + " bars "
                   + juce::String (refStart + 1) + "-" + juce::String (refStart + refBars) + ". Measuring...";
    selectPair (idx);
    pushCorrectionTable();
    return true;
}

void GroundShakeProcessor::deletePair (int index)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    pairs.erase (pairs.begin() + index);
    pairsToAnalyze.clear();
    pairsToSimulate.clear();
    if (selectedPair == index) selectedPair = -1;
    else if (selectedPair > index) --selectedPair;
    pushCorrectionTable();
    updateCoach();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setPairRange (int index, int songStart, int songEnd)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    if (songEnd < songStart) std::swap (songStart, songEnd);
    songStart = juce::jmax (0, songStart);
    songEnd = juce::jmax (songStart + 1, songEnd);
    for (int i = 0; i < (int) pairs.size(); ++i)
        if (i != index && songStart < pairs[(size_t) i].songEnd && songEnd > pairs[(size_t) i].songStart) return;   // no overlaps
    auto& p = pairs[(size_t) index];
    if (p.songStart == songStart && p.songEnd == songEnd) return;
    p.songStart = songStart;
    p.songEnd = songEnd;
    p.manual = true;
    p.outValid = false;
    if (std::find (pairsToAnalyze.begin(), pairsToAnalyze.end(), index) == pairsToAnalyze.end()) pairsToAnalyze.push_back (index);
    pushCorrectionTable();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setFollowPlayhead (bool on)
{
    followPlayhead = on;
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::autoPair()
{
    if (learnedTrack == nullptr) { learnMessage = "Press LEARN and play your song first."; if (onReferenceChanged) onReferenceChanged(); return; }
    if (reference == nullptr)    { learnMessage = "Load a reference first."; if (onReferenceChanged) onReferenceChanged(); return; }
    const auto& t = *learnedTrack;
    const double lb = learnBpm;
    const int bars = juce::jmin (learnedBars, gs::numBarsInReference (t, lb, 0.0));
    constexpr int block = 4;
    // brightness (the 1-4 kHz band against the whole mix) tells a low-passed breakdown from a drop:
    // a low-pass keeps the sub, so sub and level alone would call it a drop
    struct Blk { float level, shake, bright; int cls = 0; };    // cls: 0 other, 1 drop, 2 breakdown
    std::vector<Blk> blks;
    float maxLevel = -200.0f, maxBright = -200.0f;
    for (int b = 0; b + block <= bars; b += block)
    {
        Blk k;
        k.level = gs::windowLevelDb (t, lb, 0.0, b, block);
        k.shake = gs::profileForWindow (t, lb, 0.0, b, block).shakeDb();
        k.bright = gs::windowEnvDb (t, t.clickEnv, lb, 0.0, b, block) - k.level;
        maxLevel = std::max (maxLevel, k.level);
        blks.push_back (k);
    }
    if (blks.empty()) return;
    float dropShake = -200.0f;
    for (auto& k : blks) if (k.level >= maxLevel - 6.0f) { dropShake = std::max (dropShake, k.shake); maxBright = std::max (maxBright, k.bright); }
    for (auto& k : blks)
    {
        const bool muffled = k.bright <= maxBright - 10.0f;
        if (k.level >= maxLevel - 4.0f && k.shake >= dropShake - 4.0f && ! muffled) k.cls = 1;
        else if (k.level >= maxLevel - 18.0f && (k.shake <= dropShake - 10.0f || muffled)) k.cls = 2;
    }
    // the reference's own drop and breakdown, 4 bars long
    const int refDrop = gs::findDropBar (*reference, bpm, gridOffsetSec, block);
    const int refBreak = gs::findBreakdownBar (*reference, bpm, gridOffsetSec, block);

    pairs.erase (std::remove_if (pairs.begin(), pairs.end(), [] (const Pair& q) { return ! q.manual; }), pairs.end());
    int made = 0;
    for (size_t i = 0; i < blks.size();)
    {
        size_t j = i;
        while (j < blks.size() && blks[j].cls == blks[i].cls) ++j;
        const int cls = blks[i].cls;
        const int s0 = (int) i * block, s1 = (int) j * block;
        const bool longEnough = (j - i) >= 2;
        bool overlaps = false;
        for (auto& q : pairs) overlaps = overlaps || (s0 < q.songEnd && s1 > q.songStart);
        if (cls != 0 && longEnough && ! overlaps && (cls == 1 || refBreak >= 0))
        {
            Pair p;
            p.slot = activeSlot;
            p.refStart = cls == 1 ? refDrop : refBreak;
            p.refBars = block;
            p.songStart = s0;
            p.songEnd = s1;
            p.manual = false;
            int used = 0;
            for (auto& q : pairs) used |= 1 << q.colour;
            for (int c = 0; c < kPairColours; ++c) if (! ((used >> c) & 1)) { p.colour = c; break; }
            pairs.push_back (p);
            ++made;
        }
        i = j;
    }
    std::sort (pairs.begin(), pairs.end(), [] (const Pair& a, const Pair& b) { return a.songStart < b.songStart; });
    pairsToAnalyze.clear();
    pairsToSimulate.clear();
    for (int i = 0; i < (int) pairs.size(); ++i) pairsToAnalyze.push_back (i);
    selectedPair = -1;
    learnMessage = made > 0 ? "Auto-paired " + juce::String (made) + (made == 1 ? " section" : " sections")
                              + " by type (drops to the reference's drop, breakdowns to its breakdown). Drag to adjust."
                            : "No clear drops or breakdowns found: mark pairs by hand.";
    pushCorrectionTable();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::matchAllPairs()
{
    for (int i = 0; i < (int) pairs.size(); ++i)
    {
        auto& p = pairs[(size_t) i];
        if (! p.analyzed) continue;
        p.corr = autoCorrection (p);
        p.corrAuto = true;
        p.state = MatchState::applied;
        refreshPairCoach (p, i);
        requestSimulation (i);
    }
    simSummaryPending = true;
    pushCorrectionTable();
    if (onReferenceChanged) onReferenceChanged();
}

//==============================================================================
void GroundShakeProcessor::analyzePair (int index)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    auto& p = pairs[(size_t) index];
    auto lt = learnedTrack;
    auto rt = slotTrack (p.slot);
    if (lt == nullptr)                          // nothing learned this session: keep saved results
    {
        p.needsLearn = ! p.analyzed;
        if (onReferenceChanged) onReferenceChanged();
        return;
    }
    if (rt == nullptr) return;
    ++pairAnalysisRunning;
    auto flag = alive;
    const double lb = learnBpm, rb = slotBpm (p.slot), ro = slotOffset (p.slot);
    const Pair spec = p;
    const float sp = split;
    pool.addJob ([this, flag, lt, rt, lb, rb, ro, spec, sp, index]
    {
        // the fix is measured from the fullest bars inside A-B
        const int nb = juce::jmax (1, juce::jmin (spec.refBars, spec.songEnd - spec.songStart));
        int best = spec.songStart;
        float bestScore = -1.0e9f;
        for (int b = spec.songStart; b + nb <= spec.songEnd; ++b)
        {
            const float sc = gs::windowLevelDb (*lt, lb, 0.0, b, nb) + 0.25f * gs::profileForWindow (*lt, lb, 0.0, b, nb).shakeDb();
            if (sc > bestScore) { bestScore = sc; best = b; }
        }
        const double lsr = lt->sampleRate, barL = 240.0 / lb;
        auto youP = gs::profileForWindow (*lt, lb, 0.0, best, nb);
        auto youM = gs::measureAudio (lt->audio, (int) (best * barL * lsr), (int) (nb * barL * lsr), lsr, 0.0, barL / 4.0 * lsr, sp);

        const double rsr = rt->sampleRate, barR = 240.0 / rb;
        auto refP = gs::profileForWindow (*rt, rb, ro, spec.refStart, spec.refBars);
        // measure the reference window at the learned track's rate, so both sides go through the same filters
        gs::Measurements refM2;
        {
            const int s0 = (int) ((rt->firstDownbeatSec + ro + spec.refStart * barR) * rsr);
            const auto w = windowAtRate (*rt, s0, (int) (spec.refBars * barR * rsr), lsr);
            if (w.getNumSamples() > 0) refM2 = gs::measureAudio (w, 0, w.getNumSamples(), lsr, 0.0, barR / 4.0 * lsr, sp);
        }
        const int dropB = gs::findDropBar (*rt, rb, ro, spec.refBars);
        const float dropShake = gs::profileForWindow (*rt, rb, ro, dropB, spec.refBars).shakeDb();
        const int numBars = gs::numBarsInReference (*rt, rb, ro);
        const bool bd = refP.valid && (refP.shakeDb() <= -15.0f || refP.shakeDb() <= dropShake - 10.0f);

        juce::MessageManager::callAsync ([this, flag, spec, index, best, youP, youM, refP, refM2, dropB, numBars, bd, name = rt->name]
        {
            if (! flag->load()) return;
            --pairAnalysisRunning;
            // the pair may have moved in the list or been edited meanwhile
            int i = index;
            auto same = [&] (const Pair& q)
            { return q.songStart == spec.songStart && q.songEnd == spec.songEnd && q.refStart == spec.refStart && q.slot == spec.slot; };
            if (i >= (int) pairs.size() || ! same (pairs[(size_t) i]))
            {
                i = -1;
                for (int k = 0; k < (int) pairs.size(); ++k) if (same (pairs[(size_t) k])) i = k;
                if (i < 0) return;
            }
            auto& p = pairs[(size_t) i];
            p.measureStart = best;
            p.you = youP;  p.youM = youM;  p.ref = refP;  p.refM = refM2;
            p.refDropBar = dropB;  p.refNumBars = numBars;  p.breakdown = bd;  p.refName = name;
            p.analyzed = true;
            p.needsLearn = false;
            refreshPairCoach (p, i);
            if (p.state != MatchState::off) { p.corr = autoCorrection (p); p.corrAuto = true; }   // keep a live correction in step
            p.outValid = false;
            if (p.correctionOn()) requestSimulation (i);
            pushCorrectionTable();
            if (pairAnalysisRunning == 0 && pairsToAnalyze.empty() && learnMessage.contains ("Measuring"))
            {
                int red = 0, amber = 0;
                for (auto& q : pairs) { red += q.status == 3 ? 1 : 0; amber += q.status == 2 ? 1 : 0; }
                learnMessage = "Measured " + juce::String ((int) pairs.size()) + " pairs: " + juce::String (red) + " red, "
                               + juce::String (amber) + " amber. Click a pair to see its fixes, or press MATCH ALL.";
            }
            if (onReferenceChanged) onReferenceChanged();
        });
    });
}

void GroundShakeProcessor::refreshPairCoach (Pair& p, int index)
{
    gs::CoachInputs in;
    in.haveRef = in.haveYou = p.analyzed;
    in.you = p.you;  in.ref = p.ref;  in.youM = p.youM;  in.refM = p.refM;
    in.tolerance = tolerance;
    in.refName = p.refName;
    in.windowStartBar = p.refStart;
    in.windowBars = p.refBars;
    in.numRefBars = p.refNumBars;
    in.dropBar = p.refDropBar;
    in.breakdownMode = p.breakdown;
    in.pairMode = true;
    in.pairLabel = "pair " + juce::String (index + 1);
    in.hostBpm = learnBpm > 0 ? learnBpm : hostBpm.load();
    in.djRefLevel = djRefLevel;
    p.coach.reset();
    p.coach.update (in, 0.0, false);
    int st = p.analyzed ? 1 : 0;
    for (auto& r : p.coach.checks())
    {
        // the light is about the low end: limiter squash and DJ playback belong to the MASTER view
        if (r.id == gs::CheckId::squash || r.id == gs::CheckId::djPlayback) continue;
        if (r.status == gs::Status::fail) st = 3;
        else if (r.status == gs::Status::near && st < 2) st = 2;
    }
    p.status = st;
}

//==============================================================================
//  OUT: the pair's learned audio run through its correction, then measured like the mix
//==============================================================================
void GroundShakeProcessor::requestSimulation (int index, int delayMs)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    if (std::find (pairsToSimulate.begin(), pairsToSimulate.end(), index) == pairsToSimulate.end()) pairsToSimulate.push_back (index);
    simDue = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, delayMs);
}

void GroundShakeProcessor::simulatePair (int index)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    auto& p = pairs[(size_t) index];
    auto lt = learnedTrack;
    if (lt == nullptr || ! p.analyzed || ! p.correctionOn()) { if (! p.correctionOn()) p.outValid = false; return; }
    ++simRunning;
    auto flag = alive;
    const double lb = learnBpm;
    const Pair spec = p;
    auto corr = p.corr;
    corr.split = split;
    const float sp = split;
    const bool tune = p.corrAuto;
    pool.addJob ([this, flag, lt, lb, spec, corr, sp, index, tune]
    {
        const double lsr = lt->sampleRate, barSec = 240.0 / lb;
        const int nb = juce::jmax (1, juce::jmin (spec.refBars, spec.songEnd - spec.songStart));
        const int pre = spec.measureStart >= 1 ? 1 : 0;                    // a bar to settle the filters
        const int s0 = (int) std::round ((spec.measureStart - pre) * barSec * lsr);
        const int len = juce::jmin ((int) std::round ((nb + pre) * barSec * lsr), lt->audio.getNumSamples() - s0);
        gs::StepProfile outP;
        gs::Measurements outM;
        gs::MatchSettings c = corr;
        if (s0 >= 0 && len > (int) (barSec * lsr * 0.5))
        {
            gs::ReferenceTrack t;
            t.sampleRate = lsr;
            t.hop = lt->hop;
            t.firstDownbeatSec = 0.0;
            auto run = [&] (const gs::MatchSettings& cs)
            {
                t.audio.setSize (2, len, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    t.audio.copyFrom (ch, 0, lt->audio, juce::jmin (ch, lt->audio.getNumChannels() - 1), s0, len);
                gs::MatchStage stage;
                stage.prepare (lsr);
                const double stepsPerSample = lb / 60.0 * 4.0 / lsr;
                float* L = t.audio.getWritePointer (0);
                float* R = t.audio.getWritePointer (1);
                for (int i = 0; i < len; i += 512)
                    // a glide of one sample: the correction is fully on from the start
                    stage.process (L + i, R + i, juce::jmin (512, len - i), cs, true, false, i * stepsPerSample, stepsPerSample, 1.0);
                gs::computeEnvelopes (t, sp);
                return gs::profileForWindow (t, lb, 0.0, pre, nb);
            };
            outP = run (c);
            // MATCH's correction is worked out from the two analyses; OUT checks it on the audio and
            // fine-tunes the level and ducking a few times so the result lands on the reference
            if (tune && outP.valid && spec.ref.valid && ! spec.breakdown)
            {
                for (int iter = 0; iter < 4; ++iter)
                {
                    const auto bs = gs::beatShape (outP, spec.ref);
                    const float df = zoneGainDb (outP.feelTotal, outP.fullTotal, spec.ref.feelTotal, spec.ref.fullTotal);
                    const float dp = zoneGainDb (outP.punchTotal, outP.fullTotal, spec.ref.punchTotal, spec.ref.fullTotal);
                    float worst = std::max (std::abs (df), std::abs (dp)) * 2.0f;
                    if (bs.valid) for (auto d : bs.diff) worst = std::max (worst, std::abs (d));
                    if (worst < 0.8f) break;
                    if (bs.valid)
                    {
                        // a kick-attack boost adds low end on the kick 16th: back it off before the duck runs out
                        if (bs.diff[0] > 1.0f && c.duckDb[0] <= -9.0f && c.attackDb > 0.0f) c.attackDb = std::max (0.0f, c.attackDb - 1.5f);
                        for (size_t k = 0; k < 4; ++k)
                            if (std::abs (bs.diff[k]) >= 0.5f) c.duckDb[k] = juce::jlimit (-12.0f, 3.0f, c.duckDb[k] - 0.8f * bs.diff[k]);
                    }
                    if (std::abs (df) > 0.3f) c.feelDb = juce::jlimit (-6.0f, 6.0f, c.feelDb + 0.8f * df);
                    if (std::abs (dp) > 0.3f) c.punchDb = juce::jlimit (-6.0f, 6.0f, c.punchDb + 0.8f * dp);
                    outP = run (c);
                }
            }
            outM = gs::measureAudio (t.audio, (int) (pre * barSec * lsr), (int) (nb * barSec * lsr), lsr, 0.0, barSec / 4.0 * lsr, sp);
        }
        juce::MessageManager::callAsync ([this, flag, spec, corr, c, index, outP, outM]
        {
            if (! flag->load()) return;
            --simRunning;
            int i = index;
            auto same = [&] (const Pair& x)
            { return x.songStart == spec.songStart && x.songEnd == spec.songEnd && x.refStart == spec.refStart && x.slot == spec.slot; };
            if (i >= (int) pairs.size() || ! same (pairs[(size_t) i]))
            {
                i = -1;
                for (int k = 0; k < (int) pairs.size(); ++k) if (same (pairs[(size_t) k])) i = k;
                if (i < 0) return;
            }
            auto& q = pairs[(size_t) i];
            if (! q.correctionOn()) { q.outValid = false; return; }
            {
                auto mine = q.corr;  mine.split = corr.split;
                if (correctionToString (mine) != correctionToString (corr)) return;   // changed meanwhile: a newer job follows
            }
            if (q.corrAuto && correctionToString (c) != correctionToString (corr))
            {
                q.corr = c;                       // the fine-tuned correction is what plays
                pushCorrectionTable();
            }
            q.outYou = outP;
            q.outYouM = outM;
            q.outValid = outP.valid;
            refreshOutCoach (q, i);
            if (onReferenceChanged) onReferenceChanged();
        });
    });
}

void GroundShakeProcessor::refreshOutCoach (Pair& p, int index)
{
    if (! p.outValid) { p.outStatus = 0; p.outCoach.reset(); return; }
    gs::CoachInputs in;
    in.haveRef = in.haveYou = true;
    in.you = p.outYou;  in.ref = p.ref;  in.youM = p.outYouM;  in.refM = p.refM;
    // attack and click can't be heard in the learned audio as well as live: keep the mix's kick reading
    in.youM.kick.clickDb = p.youM.kick.clickDb;
    in.tolerance = tolerance;
    in.refName = p.refName;
    in.windowStartBar = p.refStart;
    in.windowBars = p.refBars;
    in.numRefBars = p.refNumBars;
    in.dropBar = p.refDropBar;
    in.breakdownMode = p.breakdown;
    in.pairMode = true;
    in.pairLabel = "pair " + juce::String (index + 1) + " with Ground Shake";
    in.hostBpm = learnBpm > 0 ? learnBpm : hostBpm.load();
    in.djRefLevel = djRefLevel;
    p.outCoach.reset();
    p.outCoach.update (in, 0.0, false);
    int st = 1;
    for (auto& r : p.outCoach.checks())
    {
        // sound and note choices can't be changed by a correction: they don't count against OUT
        if (r.id == gs::CheckId::kickType || r.id == gs::CheckId::bassPattern || r.id == gs::CheckId::bassNotes
            || r.id == gs::CheckId::tuning || r.id == gs::CheckId::section
            || r.id == gs::CheckId::squash || r.id == gs::CheckId::djPlayback)      // master matters: the MASTER view
            continue;
        if (r.status == gs::Status::fail) st = 3;
        else if (r.status == gs::Status::near && st < 2) st = 2;
    }
    p.outStatus = st;
}

// What Ground Shake can fix by itself for this pair, worked out from the two analyses
gs::MatchSettings GroundShakeProcessor::autoCorrection (const Pair& p) const
{
    gs::MatchSettings c;
    if (! p.analyzed) return c;
    const auto& y = p.you;
    const auto& r = p.ref;
    using B = gs::Measurements::Band;
    const auto& ys = p.youM.bandDb;
    const auto& rs = p.refM.bandDb;

    // breakdown muted with a high-pass in the reference, sub still in yours: a low cut, nothing else on the low end
    if (p.breakdown && p.youM.valid && p.refM.valid && rs[B::bSub] <= -30.0f && ys[B::bSub] - rs[B::bSub] >= 10.0f)
    {
        c.lowCutHz = rs[B::bPunch] >= rs[B::bBody] - 10.0f ? 70.0f : (rs[B::bPunch] >= rs[B::bBody] - 15.0f ? 130.0f : 170.0f);
        c.mono = p.youM.monoCorr < 0.9f;
        return c;
    }
    const float df = zoneGainDb (y.feelTotal, y.fullTotal, r.feelTotal, r.fullTotal);
    const float dp = zoneGainDb (y.punchTotal, y.fullTotal, r.punchTotal, r.fullTotal);
    c.feelDb = std::abs (df) > 0.3f ? juce::jlimit (-6.0f, 6.0f, df) : 0.0f;
    c.punchDb = std::abs (dp) > 0.3f ? juce::jlimit (-6.0f, 6.0f, dp) : 0.0f;
    c.feelHz = p.youM.valid && p.youM.bassConf >= 5.0f ? juce::jlimit (25.0f, split, p.youM.bassHz) : 0.0f;
    c.mono = p.youM.valid && p.youM.monoCorr < 0.9f;
    if (! p.breakdown)
    {
        // built-in ducking: copy the reference's dip shape, step by step
        const auto bs = gs::beatShape (y, r);
        if (bs.valid)
            for (size_t k = 0; k < 4; ++k)
                c.duckDb[k] = std::abs (bs.diff[k]) >= 1.5f ? juce::jlimit (-12.0f, 3.0f, -bs.diff[k]) : 0.0f;
        // kick attack and tail
        if (p.youM.kick.valid && p.refM.kick.valid)
        {
            const auto& yk = p.youM.kick;
            const auto& rk = p.refM.kick;
            // only clear differences: a class boundary alone is too touchy
            const bool slower = yk.riseMs > rk.riseMs * 1.8f && yk.riseMs - rk.riseMs >= 5.0f;
            const bool faster = rk.riseMs > yk.riseMs * 1.8f && rk.riseMs - yk.riseMs >= 5.0f;
            if (slower || (rk.clickDb - yk.clickDb >= 6.0f)) c.attackDb = 3.0f;
            else if (faster || (yk.clickDb - rk.clickDb >= 6.0f)) c.attackDb = -3.0f;
            const float ratio = rk.decayMs > 0 ? yk.decayMs / rk.decayMs : 1.0f;
            if (ratio > 1.3f)       { c.tailStartMs = juce::jlimit (40.0f, 300.0f, rk.decayMs); c.tailDb = ratio > 2.0f ? -9.0f : -6.0f; }
            else if (ratio < 0.75f) { c.tailStartMs = juce::jlimit (40.0f, 300.0f, yk.decayMs); c.tailDb = 3.0f; }
        }
    }
    // mud, only where kick and bass clash
    const float dm = y.mudDb() - r.mudDb();
    if (dm >= 2.0f) c.mudDb = -juce::jlimit (2.0f, 6.0f, dm);
    // weight: the reference's bass reads more in 120-300 Hz
    if (p.youM.valid && p.refM.valid && rs[B::bBody] - ys[B::bBody] >= 3.0f) c.weight = 0.5f;
    return c;
}

//==============================================================================
//  The audio thread's table of corrections
//==============================================================================
void GroundShakeProcessor::pushCorrectionTable()
{
    auto t = std::make_unique<CorrectionTable>();
    if (pairs.empty())
    {
        t->pairMode = false;
        t->entries.push_back ({ -1.0e12, 1.0e12, matchSettings,
                                matchState == MatchState::preview || matchState == MatchState::applied,
                                matchState == MatchState::preview });
    }
    else
    {
        t->pairMode = true;                    // unmarked parts are left alone
        const double base = (originBar - 1 + trimBars) * 4.0;
        for (auto& p : pairs)
            if (p.state != MatchState::off)
                t->entries.push_back ({ base + p.songStart * 4.0, base + p.songEnd * 4.0, p.corr,
                                        p.state == MatchState::preview || p.state == MatchState::applied,
                                        p.state == MatchState::preview });
    }
    if (ownedTable != nullptr) retiredTables.emplace_back (std::move (ownedTable), juce::Time::getMillisecondCounter());
    ownedTable = std::move (t);
    liveTable.store (ownedTable.get());
}

//==============================================================================
//  The readout the editor shows
//==============================================================================
bool GroundShakeProcessor::isShowingSavedPair() const
{
    if (selectedPair < 0 || selectedPair >= (int) pairs.size()) return false;
    const auto& p = pairs[(size_t) selectedPair];
    if (! p.analyzed) return false;
    const double bar = getSongBarNow();
    const bool liveHere = hostPlaying.load() && learnState == LearnState::off && bar >= p.songStart && bar < p.songEnd
                          && liveHasData();
    return ! liveHere;
}

bool GroundShakeProcessor::isShowingOut() const
{
    if (viewMode != ViewMode::out || ! isShowingSavedPair()) return false;
    const auto& p = pairs[(size_t) selectedPair];
    return p.correctionOn() && p.outValid;
}

void GroundShakeProcessor::setViewMode (ViewMode m)
{
    if (m == viewMode) return;
    viewMode = m;
    meterBeforeCorrection.store (viewMode == ViewMode::mix && ! pairs.empty());
    resetRequested.store (true);                // live readings start fresh in the new view
    if (m == ViewMode::master && ! masterValid) masterPending = true;
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setAlignOffsetBars (double bars)
{
    alignOffsetBars = juce::jlimit (-512.0, 512.0, std::round (bars * 4.0) / 4.0);   // whole beats
    if (onReferenceChanged) onReferenceChanged();
}

const gs::Coach& GroundShakeProcessor::getDisplayCoach() const
{
    if (viewMode == ViewMode::master) return masterCoach;
    if (isShowingOut()) return pairs[(size_t) selectedPair].outCoach;
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].coach : coach;
}

gs::StepProfile GroundShakeProcessor::getDisplayYouProfile() const
{
    if (isShowingOut()) return pairs[(size_t) selectedPair].outYou;
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].you : getLiveProfile();
}
const gs::StepProfile& GroundShakeProcessor::getDisplayRefProfile() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].ref : refProfile;
}
gs::Measurements GroundShakeProcessor::getDisplayYouMeasurements() const
{
    if (isShowingOut()) return pairs[(size_t) selectedPair].outYouM;
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].youM : getLiveMeasurements();
}
const gs::Measurements& GroundShakeProcessor::getDisplayRefMeasurements() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].refM : refM;
}
bool GroundShakeProcessor::isDisplayBreakdown() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].breakdown : breakdownMode;
}

const gs::MatchSettings& GroundShakeProcessor::getEditedCorrection() const
{
    return selectedPair >= 0 && selectedPair < (int) pairs.size() ? pairs[(size_t) selectedPair].corr : matchSettings;
}
MatchState GroundShakeProcessor::getEditedMatchState() const
{
    return selectedPair >= 0 && selectedPair < (int) pairs.size() ? pairs[(size_t) selectedPair].state : matchState;
}
void GroundShakeProcessor::setEditedCorrection (const gs::MatchSettings& s)
{
    if (selectedPair >= 0 && selectedPair < (int) pairs.size())
    {
        auto& p = pairs[(size_t) selectedPair];
        p.corr = s;
        p.corrAuto = false;                              // your settings: OUT measures them as they are
        if (p.state == MatchState::off || p.state == MatchState::ready) p.state = MatchState::applied;
        requestSimulation (selectedPair, 400);          // re-work OUT once the knob settles
    }
    else
    {
        matchSettings = s;
        if (matchState == MatchState::off || matchState == MatchState::ready) matchState = MatchState::applied;
    }
    pushCorrectionTable();
}

std::vector<gs::FixCard> GroundShakeProcessor::getDisplayCards() const
{
    auto cards = getDisplayCoach().cards();
    if (viewMode == ViewMode::master) return cards;
    const auto& c = getEditedCorrection();
    const auto st = getEditedMatchState();
    if (st != MatchState::applied && st != MatchState::preview) return cards;
    const juce::String now = st == MatchState::preview ? "Previewing: " : "Ground Shake is doing this now: ";
    juce::StringArray duck;
    for (int k = 0; k < 4; ++k)
        if (std::abs (c.duckDb[(size_t) k]) > 0.05f) duck.add ("16th " + juce::String (k + 1) + " " + sdb (c.duckDb[(size_t) k]));
    for (auto& card : cards)
    {
        juce::StringArray f;
        switch (card.id)
        {
            case gs::CheckId::subLevel:
            case gs::CheckId::punch:
                if (c.lowCutHz > 0) f.add ("low cut at " + juce::String (juce::roundToInt (c.lowCutHz)) + " Hz, 24 dB/oct");
                if (std::abs (c.feelDb) > 0.05f)
                    f.add (sdb (c.feelDb) + " around " + juce::String (juce::roundToInt (c.feelHz > 0 ? c.feelHz : std::sqrt (20.0f * split))) + " Hz");
                if (std::abs (c.punchDb) > 0.05f) f.add (sdb (c.punchDb) + " around " + juce::String (juce::roundToInt (split * 1.414f)) + " Hz");
                break;
            case gs::CheckId::beatShape:
            case gs::CheckId::kickBass:
                if (! duck.isEmpty()) f.add ("built-in ducking: " + duck.joinIntoString (", "));
                break;
            case gs::CheckId::kickType:
                if (std::abs (c.attackDb) > 0.05f) f.add ("kick attack " + sdb (c.attackDb));
                if (std::abs (c.tailDb) > 0.05f) f.add ("kick tail " + sdb (c.tailDb) + " after " + juce::String (juce::roundToInt (c.tailStartMs)) + " ms");
                break;
            case gs::CheckId::mud:
                if (std::abs (c.mudDb) > 0.05f) f.add ("mud " + sdb (c.mudDb) + " around 250 Hz, on the kick 16th only");
                break;
            case gs::CheckId::mono:
                if (c.mono) f.add ("sub mono below 120 Hz");
                break;
            case gs::CheckId::thinMud:
                if (c.weight > 0) f.add ("weight: light sub harmonics so the bass reads on small speakers");
                break;
            case gs::CheckId::section: case gs::CheckId::bassPattern: case gs::CheckId::bassNotes: case gs::CheckId::tuning:
            case gs::CheckId::squash: case gs::CheckId::djPlayback: case gs::CheckId::numChecks: case gs::CheckId::fineTune:
            case gs::CheckId::loudnessGap: case gs::CheckId::keyInfo: case gs::CheckId::oneBeat:
            case gs::CheckId::masterTone: case gs::CheckId::masterWidth: case gs::CheckId::masterLowMono:
            case gs::CheckId::masterSquash: case gs::CheckId::masterLoudness: case gs::CheckId::masterChain:
            default: break;
        }
        if (! f.isEmpty()) card.fixed = now + f.joinIntoString ("; ") + ". The steps below do it in your own tracks.";
        else if (card.adviceOnly) card.fixed = "Advice only: Ground Shake can't change this for you.";
    }
    return cards;
}

//==============================================================================
//  Saving Phase 3 with the project
//==============================================================================
void GroundShakeProcessor::savePhase3 (juce::XmlElement& xml) const
{
    xml.setAttribute ("songStartBar", songStartBar);
    xml.setAttribute ("originBar", originBar);
    xml.setAttribute ("trimBars", trimBars);
    xml.setAttribute ("learnBpm", learnBpm);
    xml.setAttribute ("learnedBars", learnedBars);
    xml.setAttribute ("activeSlot", activeSlot);
    xml.setAttribute ("selectedPair", selectedPair);
    xml.setAttribute ("followPlayhead", followPlayhead);
    xml.setAttribute ("alignOffsetBars", alignOffsetBars);
    xml.setAttribute ("viewMode", (int) viewMode);
    if (! ovMin.empty())
    {
        juce::MemoryBlock mb (ovMin.size() * 2);
        auto* d = static_cast<juce::int8*> (mb.getData());
        for (size_t i = 0; i < ovMin.size(); ++i)
        {
            d[2 * i] = (juce::int8) juce::jlimit (-127, 127, juce::roundToInt (ovMin[i] * 127.0f));
            d[2 * i + 1] = (juce::int8) juce::jlimit (-127, 127, juce::roundToInt (ovMax[i] * 127.0f));
        }
        xml.setAttribute ("overview", mb.toBase64Encoding());
    }
    const int other = 1 - activeSlot;
    if (auto t = parked[(size_t) other].track)
    {
        auto* o = xml.createNewChildElement ("OtherReference");
        o->setAttribute ("slot", other);
        o->setAttribute ("path", t->file.getFullPathName());
        o->setAttribute ("bpm", parked[(size_t) other].bpm);
        o->setAttribute ("offset", parked[(size_t) other].offset);
        o->setAttribute ("startBar", parked[(size_t) other].windowStartBar);
    }
    for (auto& p : pairs)
    {
        auto* e = xml.createNewChildElement ("Pair");
        e->setAttribute ("slot", p.slot);
        e->setAttribute ("refStart", p.refStart);
        e->setAttribute ("refBars", p.refBars);
        e->setAttribute ("songStart", p.songStart);
        e->setAttribute ("songEnd", p.songEnd);
        e->setAttribute ("measureStart", p.measureStart);
        e->setAttribute ("manual", p.manual);
        e->setAttribute ("colour", p.colour);
        e->setAttribute ("analyzed", p.analyzed);
        e->setAttribute ("breakdown", p.breakdown);
        e->setAttribute ("refName", p.refName);
        e->setAttribute ("refDropBar", p.refDropBar);
        e->setAttribute ("refNumBars", p.refNumBars);
        e->setAttribute ("state", (int) p.state);
        e->setAttribute ("corr", correctionToString (p.corr));
        if (p.analyzed)
        {
            e->setAttribute ("you", profileToString (p.you));
            e->setAttribute ("ref", profileToString (p.ref));
            e->setAttribute ("youM", measurementsToString (p.youM));
            e->setAttribute ("refM", measurementsToString (p.refM));
        }
        if (p.outValid)
        {
            e->setAttribute ("outYou", profileToString (p.outYou));
            e->setAttribute ("outYouM", measurementsToString (p.outYouM));
        }
    }
}

void GroundShakeProcessor::loadPhase3 (const juce::XmlElement& xml)
{
    if (globalSettings != nullptr)
        songStartBar = xml.getIntAttribute ("songStartBar", globalSettings->getIntValue ("songStartBar", 9));
    originBar = xml.getIntAttribute ("originBar", songStartBar > 0 ? songStartBar : 1);
    trimBars = xml.getIntAttribute ("trimBars", 0);
    learnBpm = xml.getDoubleAttribute ("learnBpm", 0.0);
    learnedBars = xml.getIntAttribute ("learnedBars", 0);
    learnOriginPpq.store ((originBar - 1) * 4.0);
    followPlayhead = xml.getBoolAttribute ("followPlayhead", true);
    alignOffsetBars = xml.getDoubleAttribute ("alignOffsetBars", 0.0);
    viewMode = (ViewMode) juce::jlimit (0, 2, xml.getIntAttribute ("viewMode", (int) ViewMode::out));
    const int wantSlot = juce::jlimit (0, 1, xml.getIntAttribute ("activeSlot", 0));
    activeSlot = wantSlot;
    ovMin.clear();
    ovMax.clear();
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (xml.getStringAttribute ("overview")) && mb.getSize() >= 2)
        {
            const auto* d = static_cast<const juce::int8*> (mb.getData());
            const size_t n = mb.getSize() / 2;
            ovMin.resize (n);
            ovMax.resize (n);
            for (size_t i = 0; i < n; ++i) { ovMin[i] = d[2 * i] / 127.0f; ovMax[i] = d[2 * i + 1] / 127.0f; }
        }
    }
    pairs.clear();
    for (auto* e : xml.getChildWithTagNameIterator ("Pair"))
    {
        Pair p;
        p.slot = e->getIntAttribute ("slot", 0);
        p.refStart = e->getIntAttribute ("refStart", 0);
        p.refBars = e->getIntAttribute ("refBars", 4);
        p.songStart = e->getIntAttribute ("songStart", 0);
        p.songEnd = e->getIntAttribute ("songEnd", 8);
        p.measureStart = e->getIntAttribute ("measureStart", p.songStart);
        p.manual = e->getBoolAttribute ("manual", true);
        p.colour = e->getIntAttribute ("colour", 0);
        p.analyzed = e->getBoolAttribute ("analyzed", false);
        p.breakdown = e->getBoolAttribute ("breakdown", false);
        p.refName = e->getStringAttribute ("refName");
        p.refDropBar = e->getIntAttribute ("refDropBar", 0);
        p.refNumBars = e->getIntAttribute ("refNumBars", 0);
        p.state = (MatchState) juce::jlimit (0, 3, e->getIntAttribute ("state", 0));
        if (p.state == MatchState::preview) p.state = MatchState::ready;
        p.corr = stringToCorrection (e->getStringAttribute ("corr"));
        if (p.analyzed)
        {
            p.you = stringToProfile (e->getStringAttribute ("you"));
            p.ref = stringToProfile (e->getStringAttribute ("ref"));
            p.youM = stringToMeasurements (e->getStringAttribute ("youM"));
            p.refM = stringToMeasurements (e->getStringAttribute ("refM"));
            p.analyzed = p.you.valid && p.ref.valid;
        }
        if (p.analyzed && e->hasAttribute ("outYou"))
        {
            p.outYou = stringToProfile (e->getStringAttribute ("outYou"));
            p.outYouM = stringToMeasurements (e->getStringAttribute ("outYouM"));
            p.outValid = p.outYou.valid && p.correctionOn();
        }
        pairs.push_back (p);
    }
    for (int i = 0; i < (int) pairs.size(); ++i) { refreshPairCoach (pairs[(size_t) i], i); refreshOutCoach (pairs[(size_t) i], i); }
    selectedPair = juce::jlimit (-1, (int) pairs.size() - 1, xml.getIntAttribute ("selectedPair", -1));
    pushCorrectionTable();

    if (auto* o = xml.getChildByName ("OtherReference"))
    {
        const juce::File f (o->getStringAttribute ("path"));
        const int slot = juce::jlimit (0, 1, o->getIntAttribute ("slot", 1));
        if (f.existsAsFile() && slot != activeSlot)
        {
            otherPendingBpm = o->getDoubleAttribute ("bpm", 0.0);
            otherPendingOffset = o->getDoubleAttribute ("offset", 0.0);
            otherPendingStart = o->getIntAttribute ("startBar", 0);
            loadReferenceAsync (f, slot);
        }
    }
}

//==============================================================================
//  Phase 4: master bus check. Your drop against the reference's drop, measured the same
//  way at the learned rate: tone balance, width, low-end mono, squash and loudness.
//  Advice only: these are changes for your master chain, before Ground Shake.
//==============================================================================
namespace
{
    juce::String db1 (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    float half (float v) { return std::round (v * 2.0f) / 2.0f; }

    void buildMasterReport (const gs::Measurements& y, const gs::Measurements& r, const Spread& ys, const Spread& rs,
                            int yourBar, int refBar, int bars, const juce::String& refName, gs::Coach& out)
    {
        using B = gs::Measurements::Band;
        std::vector<gs::CheckRow> rows;
        std::vector<gs::FixCard> cards;
        const juce::String comparing = "Your drop (Ableton bars " + juce::String (yourBar) + "-" + juce::String (yourBar + bars - 1)
                                       + ") against " + refName + "'s drop (bars " + juce::String (refBar) + "-"
                                       + juce::String (refBar + bars - 1) + "), level-matched.";
        auto card = [&] (gs::CheckId id, gs::Status sev, const juce::String& title, const juce::String& headline)
        {
            gs::FixCard c;
            c.id = id;  c.severity = sev;  c.title = title;  c.headline = headline;
            c.comparing = comparing;
            c.adviceOnly = true;
            c.fixed = "Advice for your master chain: put these before your limiter, with Ground Shake last.";
            return c;
        };
        auto statusOf = [] (float a, float nearAt, float failAt)
        { return a >= failAt ? gs::Status::fail : (a >= nearAt ? gs::Status::near : gs::Status::pass); };

        // ---- tone balance (above the low end: the pairs look after the sub and kick)
        {
            // each band's share of everything above 120 Hz, so a heavier or lighter low end (the pairs'
            // job) doesn't read as a tone difference up top
            auto rel = [] (const gs::Measurements& m, int band)
            {
                double above = 0;
                for (int b : { (int) B::bBody, (int) B::bMid, (int) B::bHigh }) above += std::pow (10.0, m.bandDb[(size_t) b] / 10.0);
                return (float) (m.bandDb[(size_t) band] - 10.0 * std::log10 (std::max (1.0e-12, above)));
            };
            const float dBody = rel (y, B::bBody) - rel (r, B::bBody);
            const float dMid  = rel (y, B::bMid)  - rel (r, B::bMid);
            const float dHigh = rel (y, B::bHigh) - rel (r, B::bHigh);
            struct Bd { const char* name; const char* range; float d; float hz; };
            Bd bands[] = { { "highs", "2-7 kHz", dHigh, 4000.0f }, { "mids", "300 Hz-2 kHz", dMid, 900.0f }, { "low-mids", "120-300 Hz", dBody, 200.0f } };
            const Bd* worst = &bands[0];
            for (auto& b : bands) if (std::abs (b.d) > std::abs (worst->d)) worst = &b;
            const auto st = statusOf (std::abs (worst->d), 1.5f, 3.0f);
            rows.push_back ({ gs::CheckId::masterTone, gs::checkName (gs::CheckId::masterTone), st,
                              juce::String (worst->name) + " " + db1 (worst->d) });
            if (st != gs::Status::pass)
            {
                auto c = card (gs::CheckId::masterTone, st, "MASTER TONE: " + juce::String (worst->name).toUpperCase() + (worst->d < 0 ? " LOW" : " HIGH"),
                               "Your " + juce::String (worst->name) + " (" + worst->range + ") sit " + db1 (worst->d) + " against the reference");
                c.see = "Balance above 120 Hz, you vs reference: low-mids " + db1 (dBody) + ", mids " + db1 (dMid)
                        + ", highs " + db1 (dHigh) + ".";
                c.cause = worst->d < 0 ? "The mix is darker or thinner there than the reference, so the drop sounds duller next to it in a DJ set."
                                       : "The mix has more there than the reference, so the drop can sound harsh or boxy next to it.";
                for (auto& b : bands)
                {
                    if (std::abs (b.d) < 1.5f) continue;
                    const float g = juce::jlimit (0.5f, 3.0f, half (std::abs (b.d) * 0.6f));
                    const juce::String hz = b.hz >= 1000 ? juce::String (b.hz / 1000.0f, 1) + " kHz" : juce::String ((int) b.hz) + " Hz";
                    if (b.d < 0)
                        c.how.add ("On your master EQ (e.g. Pro-Q 4), before the limiter: a wide bell (Q 0.5-0.7) +" + juce::String (g, 1)
                                   + " dB at " + hz + (b.hz >= 4000 ? ", or a gentle high shelf from 3 kHz." : "."));
                    else
                        c.how.add ("On your master EQ, before the limiter: a wide bell -" + juce::String (g, 1) + " dB at " + hz
                                   + (b.hz >= 4000 ? ". If it's harsh rather than just bright, soothe3 on the master tames it more gently." : "."));
                }
                c.how.add ("Start at about half the gap and listen: EQ on the master moves everything, so small moves go a long way.");
                c.worked = "This row goes green after you re-learn (or play the drop in Ableton with the MASTER view open).";
                c.tooFar = "If the reading flips the other way, or the drop starts to sound thin or brittle, back off.";
                c.watch = "If one sound causes it (a bright hat, a boxy synth), fixing that channel beats EQ on the whole master.";
                cards.push_back (c);
            }
        }

        // ---- stereo width above 300 Hz
        if (ys.valid && rs.valid)
        {
            const float yw = std::max (-40.0f, ys.sideHighDb), rw = std::max (-40.0f, rs.sideHighDb);   // below -40 dB is mono
            const float d = yw - rw;
            const auto st = statusOf (std::abs (d), 3.0f, 6.0f);
            rows.push_back ({ gs::CheckId::masterWidth, gs::checkName (gs::CheckId::masterWidth), st,
                              std::abs (d) < 0.5f ? juce::String ("matched") : db1 (d) + (d < 0 ? " narrow" : " wide") });
            if (st != gs::Status::pass)
            {
                auto c = card (gs::CheckId::masterWidth, st, d < 0 ? "MASTER IS NARROWER" : "MASTER IS WIDER",
                               "Side vs centre above 300 Hz: " + db1 (d) + " against the reference");
                auto wtxt = [] (float v) { return v <= -40.0f ? juce::String ("mono") : db1 (v) + " against the centre"; };
                c.see = "Your sides above 300 Hz: " + wtxt (yw) + "; the reference: " + wtxt (rw) + ".";
                if (d < 0)
                {
                    c.cause = "Less going on in the sides, so the drop feels smaller than the reference on a big system.";
                    c.how.add ("On your master, before the limiter: a stereo imager (e.g. Ozone 12 Imager) with the bands above 300 Hz widened. Start around +15-20% and compare.");
                    c.how.add ("Keep the low band (below 120-200 Hz) at 0% or narrower: width goes in the top, never the sub.");
                    c.how.add ("Better still, widen the sounds that should be wide (pads, chords, FX returns) on their own channels.");
                }
                else
                {
                    c.cause = "More in the sides than the reference. Clubs often play mono or near-mono, and very wide parts can drop out or feel hollow.";
                    c.how.add ("On your master imager, narrow the bands above 300 Hz a little (-10 to -15%) and compare.");
                    c.how.add ("Check the drop in mono (Ableton Utility on the master, Width 0%): anything that vanishes is too wide on its own channel.");
                }
                c.tooFar = "If the centre (kick, bass, lead) starts to feel weaker, you've widened too much.";
                c.watch = "Wideners can upset mono. Check in mono after every change.";
                cards.push_back (c);
            }
        }

        // ---- low end mono
        if (y.valid)
        {
            const float mc = y.monoCorr;
            const auto st = mc < 0.8f ? gs::Status::fail : (mc < 0.9f ? gs::Status::near : gs::Status::pass);
            rows.push_back ({ gs::CheckId::masterLowMono, gs::checkName (gs::CheckId::masterLowMono), st,
                              juce::String (mc, 2) + (st == gs::Status::pass ? " mono" : " wide") });
            if (st != gs::Status::pass)
            {
                auto c = card (gs::CheckId::masterLowMono, st, "LOW END NOT MONO", "Below 120 Hz your left and right differ (" + juce::String (mc, 2) + "; 1.00 is mono)");
                c.see = "Correlation of left and right below 120 Hz: " + juce::String (mc, 2) + " (reference " + juce::String (r.monoCorr, 2) + ").";
                c.cause = "Stereo sub or bass loses power on mono club systems and can cancel itself out.";
                c.how.add ("On your master: Ozone 12 Imager with the low band (up to 120 Hz) at -100% (mono), or Ozone Low End Focus / Bass Control.");
                c.how.add ("Find the source: a stereo bass patch (unison, chorus) or reverb on the bass. Make that channel mono below 120 Hz.");
                c.how.add ("Per section, Ground Shake's MONO button does the same thing in a pair's correction.");
                c.tooFar = "Mono below 120 Hz can't go too far; just don't push the crossover much above 150-200 Hz.";
                cards.push_back (c);
            }
        }

        // ---- squash: whole-band crest (peak over RMS), plus the sub's own peaks
        if (ys.valid && rs.valid)
        {
            const float d = ys.crestDb - rs.crestDb;
            const float dSub = y.subCrestDb - r.subCrestDb;
            const auto st = d <= -3.0f ? gs::Status::fail : (d <= -1.5f || dSub <= -3.0f ? gs::Status::near : gs::Status::pass);
            rows.push_back ({ gs::CheckId::masterSquash, gs::checkName (gs::CheckId::masterSquash), st,
                              "crest " + db1 (d) });
            if (st != gs::Status::pass)
            {
                auto c = card (gs::CheckId::masterSquash, st, "MASTER SQUASHED MORE",
                               "Peaks over average: " + db1 (d) + " against the reference" + (dSub <= -3.0f ? " (sub peaks " + db1 (dSub) + ")" : juce::String()));
                c.see = "Crest (peak over RMS) in the drop: you " + juce::String (ys.crestDb, 1) + " dB, reference " + juce::String (rs.crestDb, 1)
                        + " dB. Sub peaks: " + db1 (dSub) + ".";
                c.cause = "Your limiter (or clipper) is working harder, so the kick has less room to hit. On a club system that reads as smaller, not louder.";
                c.how.add ("On your limiter (e.g. Pro-L 2): lower the gain by about " + juce::String (juce::jlimit (0.5f, 4.0f, half (-d)), 1) + " dB and compare level-matched.");
                c.how.add ("Try a cleaner style (Transparent or Modern) and a slightly slower release.");
                if (dSub <= -3.0f)
                    c.how.add ("The sub is losing its peaks: ease the sub into the limiter (a little less sub before it, or a gentle clipper before the limiter) so the limiter isn't driven by the low end.");
                c.worked = "Crest within 1.5 dB of the reference. rekordbox Auto Gain levels the loudness anyway, so the punch is what's left.";
                c.tooFar = "If you end up more than 2-3 dB quieter than the reference, the trim in rekordbox has to work harder; meet in the middle.";
                cards.push_back (c);
            }
            else if (d >= 3.0f)
            {
                auto c = card (gs::CheckId::masterSquash, gs::Status::info, "ROOM TO PUSH",
                               "Your peaks stand " + db1 (d) + " further out than the reference's");
                c.see = "Crest in the drop: you " + juce::String (ys.crestDb, 1) + " dB, reference " + juce::String (rs.crestDb, 1) + " dB.";
                c.cause = "Your master is more open than the reference. That's fine, but you have room to get louder before it gets squashed.";
                c.how.add ("If you want to match its level: raise your limiter's gain by 1 dB at a time, stopping before the crest drops below the reference's.");
                cards.push_back (c);
            }
        }

        // ---- loudness of the drop
        if (y.valid && r.valid && y.lufs > -60.0f && r.lufs > -60.0f)
        {
            const float d = y.lufs - r.lufs;
            const auto st = std::abs (d) >= 1.0f ? gs::Status::near : gs::Status::pass;
            rows.push_back ({ gs::CheckId::masterLoudness, gs::checkName (gs::CheckId::masterLoudness), st,
                              (d >= 0 ? "+" : "") + juce::String (d, 1) + " LU" });
            if (st != gs::Status::pass)
            {
                auto c = card (gs::CheckId::masterLoudness, gs::Status::near, d < 0 ? "DROP IS QUIETER" : "DROP IS LOUDER",
                               "Drop loudness " + juce::String (d, 1) + " LU against the reference");
                c.see = "Drop loudness: you " + juce::String (y.lufs, 1) + " LUFS, reference " + juce::String (r.lufs, 1) + " LUFS (measured the same way, so the difference is what counts).";
                c.cause = "rekordbox Auto Gain evens this out when you play, which is why the trim moves. It matters less than the squash and tone above.";
                if (d < 0)
                    c.how.add (ys.valid && rs.valid && ys.crestDb > rs.crestDb + 1.0f
                                   ? "You have peak room: raise your limiter's gain by about " + juce::String (juce::jlimit (0.5f, 4.0f, half (-d)), 1) + " dB."
                                   : "You're already as squashed as the reference: don't push the limiter. Close the gap with tone (the highs card) and arrangement instead.");
                else
                    c.how.add ("Lower your limiter's gain by about " + juce::String (juce::jlimit (0.5f, 4.0f, half (d)), 1) + " dB: the kick gets back room to hit.");
                c.how.add ("Fix tone and squash first; then set the level.");
                cards.push_back (c);
            }
        }

        // ---- chain order (always, information)
        {
            auto c = card (gs::CheckId::masterChain, gs::Status::info, "MASTER CHAIN ORDER", "Where each change goes on your master");
            c.comparing = {};
            c.how.add ("Master EQ (tone): first.");
            c.how.add ("Stereo imager (width, low end mono): after the EQ.");
            c.how.add ("Glue compressor if you use one, then the limiter (e.g. Pro-L 2).");
            c.how.add ("Ground Shake last, after the limiter: its corrections and safety ceiling follow your limiter's peak level.");
            c.watch = "Re-learn after changing the master, so the pairs and this check measure the new sound.";
            cards.push_back (c);
        }

        // fix order: the first real problem gets the "fix this first" tag
        for (auto& c : cards) if (c.severity == gs::Status::fail || c.severity == gs::Status::near) { c.fixFirst = true; break; }
        int scored = 0;
        float sum = 0;
        int fails = 0, nears = 0;
        for (auto& rw : rows)
        {
            ++scored;
            sum += rw.status == gs::Status::pass ? 1.0f : (rw.status == gs::Status::near ? 0.5f : 0.0f);
            fails += rw.status == gs::Status::fail ? 1 : 0;
            nears += rw.status == gs::Status::near ? 1 : 0;
        }
        const float pct = scored > 0 ? 100.0f * sum / (float) scored : -1.0f;
        const juce::String line = fails + nears == 0 ? "Master: your drop matches the reference's tone, width, squash and loudness."
                                : "Master: " + juce::String (fails) + " to fix, " + juce::String (nears) + " close (your drop vs the reference's drop).";
        out.setReport (std::move (rows), std::move (cards), pct, line);
    }
}

void GroundShakeProcessor::runMasterCheck()
{
    masterPending = false;
    auto lt = learnedTrack;
    auto rt = reference;
    if (lt == nullptr || rt == nullptr || learnBpm <= 0 || bpm <= 0) return;
    masterRunning = true;
    auto flag = alive;
    const double lb = learnBpm, rb = bpm, ro = gridOffsetSec;
    const float sp = split;
    const int startBar = getEffectiveStartBar();
    pool.addJob ([this, flag, lt, rt, lb, rb, ro, sp, startBar]
    {
        const double lsr = lt->sampleRate, barL = 240.0 / lb, barR = 240.0 / rb;
        const int bars = juce::jlimit (1, gs::kBars, gs::numBarsInReference (*lt, lb, 0.0));
        const int yDrop = gs::findDropBar (*lt, lb, 0.0, bars);
        const int rDrop = gs::findDropBar (*rt, rb, ro, bars);
        const int ys0 = (int) (yDrop * barL * lsr), yn = (int) (bars * barL * lsr);
        auto yM = gs::measureAudio (lt->audio, ys0, yn, lsr, 0.0, barL / 4.0 * lsr, sp);
        auto yS = measureSpread (lt->audio, ys0, yn, lsr);
        const auto w = windowAtRate (*rt, (int) ((rt->firstDownbeatSec + ro + rDrop * barR) * rt->sampleRate),
                                     (int) (bars * barR * rt->sampleRate), lsr);
        gs::Measurements rM;
        Spread rS;
        if (w.getNumSamples() > 0)
        {
            rM = gs::measureAudio (w, 0, w.getNumSamples(), lsr, 0.0, barR / 4.0 * lsr, sp);
            rS = measureSpread (w, 0, w.getNumSamples(), lsr);
        }
        auto report = std::make_shared<gs::Coach>();
        if (yM.valid && rM.valid)
            buildMasterReport (yM, rM, yS, rS, startBar + yDrop, rDrop + 1, bars, rt->name, *report);
        juce::MessageManager::callAsync ([this, flag, report, ok = yM.valid && rM.valid, lt, rt]
        {
            if (! flag->load()) return;
            masterRunning = false;
            if (learnedTrack != lt || reference != rt) { masterPending = learnedTrack != nullptr && reference != nullptr; return; }
            if (ok) { masterCoach = *report; masterValid = true; }
            if (onReferenceChanged) onReferenceChanged();
        });
    });
}

void GroundShakeProcessor::runQueuedWorkNow()
{
    timerCallback();
}
