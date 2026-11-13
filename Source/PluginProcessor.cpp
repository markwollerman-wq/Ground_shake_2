#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Resample a stereo buffer to the host rate (done once per load, off the audio thread)
    std::shared_ptr<const juce::AudioBuffer<float>> resampleTo (const juce::AudioBuffer<float>& in,
                                                               double fromRate, double toRate)
    {
        auto out = std::make_shared<juce::AudioBuffer<float>>();
        if (std::abs (fromRate - toRate) < 0.5)
        {
            out->makeCopyOf (in);
            return out;
        }
        const double ratio = fromRate / toRate;
        const int outLen = (int) std::floor (in.getNumSamples() / ratio) - 4;
        out->setSize (2, juce::jmax (1, outLen));
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, in.getReadPointer (ch), out->getWritePointer (ch), out->getNumSamples());
        }
        return out;
    }

    // "fullest, loudest" score of an 8-bar window, same as the drop finder
    float sectionScore (const gs::ReferenceTrack& ref, double bpm, double offset, int bar, int bars)
    {
        const auto prof = gs::profileForWindow (ref, bpm, offset, bar, bars);
        return gs::windowLevelDb (ref, bpm, offset, bar, bars) + 0.25f * prof.shakeDb();
    }
}

//==============================================================================
GroundShakeProcessor::GroundShakeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    startTimer (100);
    analyzer.startThread (juce::Thread::Priority::low);
}

GroundShakeProcessor::~GroundShakeProcessor()
{
    analyzer.signalThreadShouldExit();
    analyzer.notify();
    analyzer.stopThread (3000);
    alive->store (false);
    stopTimer();
    pool.removeAllJobs (true, 10000);
    livePlayback.store (nullptr);
}

bool GroundShakeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void GroundShakeProcessor::prepareToPlay (double sampleRate, int)
{
    {
        const juce::ScopedLock al (ringAllocLock);
        sr = sampleRate;
        ringSize = (int) (sampleRate * 34.0);               // 8 bars at 60 BPM fit
        ringL.assign ((size_t) ringSize, 0.0f);
        ringR.assign ((size_t) ringSize, 0.0f);
        ringWriteLocal = 0;
        ringCountLocal = 0;
        const juce::SpinLock::ScopedLockType l (ringLock);
        ringPos = {};
    }
    currentSplit = splitParam.load();
    splitter.prepare (sampleRate, currentSplit);
    matchStage.prepare (sampleRate);
    meter.reset();
    subPow = 0.0f;
    abFade = 0.0f;

    // reference must play at the host rate
    if (reference != nullptr && std::abs (resampledRate - sampleRate) > 0.5)
        requestResample (sampleRate);
}

void GroundShakeProcessor::requestResample (double sampleRate)
{
    auto ref = reference;
    auto flag = alive;
    pool.addJob ([this, ref, flag, sampleRate]
    {
        auto res = resampleTo (ref->audio, ref->sampleRate, sampleRate);
        juce::MessageManager::callAsync ([this, ref, flag, res, sampleRate]
        {
            if (! flag->load() || reference != ref) return;
            resampled = res;
            resampledRate = sampleRate;
            rebuildWindow();
        });
    });
}

//==============================================================================
void GroundShakeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (n == 0 || numCh == 0) return;

    // --- settings from the message thread
    const float sp = splitParam.load();
    if (std::abs (sp - currentSplit) > 0.01f)
    {
        currentSplit = sp;
        splitter.prepare (sr, sp);
        meter.reset();
    }
    meter.setLoopSteps (loopStepsParam.load());
    if (resetRequested.exchange (false))
    {
        meter.reset();
        ringCountLocal = 0;
        lastPublishedSlot = -1;
    }

    // --- where is the host?
    double bpmNow = hostBpm.load(), ppq = 0.0;
    bool havePpq = false, playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpmNow = *b;
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; havePpq = true; }
            int loopBars = 0;
            if (pos->getIsLooping())
                if (auto lp = pos->getLoopPoints())
                    loopBars = (int) std::round ((lp->ppqEnd - lp->ppqStart) / 4.0);   // 4/4: 4 quarter notes a bar
            hostLoopBars.store (loopBars);
        }
    if (! havePpq) playing = true;                       // standalone: free-running clock
    bpmNow = juce::jlimit (40.0, 300.0, bpmNow);
    const double stepsPerSample = bpmNow / 60.0 * 4.0 / sr;
    const double startSteps = havePpq ? ppq * 4.0 : freeRunSteps;

    // --- Match correction stage (bypassed and untouched unless Preview or Apply is on)
    float* wL = buffer.getWritePointer (0);
    float* wR = buffer.getWritePointer (numCh > 1 ? 1 : 0);
    {
        gs::MatchSettings ms;
        ms.feelDb = mFeel.load();  ms.punchDb = mPunch.load();  ms.mono = mMono.load();
        ms.weight = mWeight.load();  ms.split = currentSplit;  ms.feelHz = mFeelHz.load();
        matchStage.process (wL, wR, n, ms, mActive.load(), mLevelMatch.load());
        ceilingDb.store (matchStage.ceilingDb());
    }

    // --- analyse YOUR mix (what leaves the plugin, before the A/B swap)
    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getReadPointer (numCh > 1 ? 1 : 0);
    const float subCoef = (float) std::exp (-1.0 / (0.03 * sr));     // ~30 ms for the shake
    const double msCoef = std::exp (-1.0 / (3.0 * sr));              // ~3 s for loudness
    const bool ringOk = ringSize > 0 && (int) ringL.size() == ringSize;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        float feel, punch, mud;
        splitter.process (m, feel, punch, mud);
        const float fs = feel * feel, ps = punch * punch, ms = m * m;
        if (playing)
        {
            meter.addSample (startSteps + i * stepsPerSample, fs, ps, mud * mud, ms);
            if (ringOk)
            {
                ringL[(size_t) ringWriteLocal] = L[i];
                ringR[(size_t) ringWriteLocal] = R[i];
                if (++ringWriteLocal >= ringSize) ringWriteLocal = 0;
            }
        }
        subPow = (fs + gs::kPunchWeight * ps) + subCoef * (subPow - (fs + gs::kPunchWeight * ps));
        mixMeanSquare = ms + msCoef * (mixMeanSquare - ms);
    }
    if (! havePpq) freeRunSteps += n * stepsPerSample;
    const double endSteps = startSteps + n * stepsPerSample;
    if (playing && ringOk)
    {
        ringCountLocal += n;
        const juce::SpinLock::ScopedTryLockType tl (ringLock);
        if (tl.isLocked())
            ringPos = { ringWriteLocal, ringCountLocal, endSteps, bpmNow };
    }

    liveSubDb.store (gs::powerToDb (subPow / (float) juce::jmax (1.0e-9, mixMeanSquare)));
    hostSteps.store (endSteps);
    hostBpm.store (bpmNow);
    hostPlaying.store (playing && havePpq);

    // publish the step profile once per 16th
    const long slot = (long) std::floor (endSteps);
    if (slot != lastPublishedSlot)
    {
        const juce::SpinLock::ScopedTryLockType tl (profileLock);
        if (tl.isLocked())
        {
            publishedLive = meter.profile();
            publishedHasData = publishedLive.valid;
            publishedSlots = meter.slotsFilled();
            lastPublishedSlot = slot;
        }
    }

    // --- A/B: level-matched reference playback
    auto* pb = livePlayback.load();
    const bool wantB = listenB.load() && pb != nullptr && pb->audio != nullptr && pb->windowLength > 0;
    if (! wantB && abFade <= 0.0f)
    {
        wasListening = false;
        refPosSec.store (-1.0);
        return;                                          // audio passes through untouched
    }
    if (pb == nullptr || pb->audio == nullptr || pb->windowLength <= 0)
    {
        abFade = 0.0f;
        return;
    }

    const auto& ra = *pb->audio;
    const int wStart = pb->windowStart, wLen = pb->windowLength, total = ra.getNumSamples();

    // line the reference loop up with your 8-bar loop when the tempos match; otherwise
    // it runs at its own speed (no time-stretching, so the sub stays untouched)
    const bool sameTempo = std::abs (pb->bpm - bpmNow) < 0.5;
    const auto loopPosSamples = [&] (double steps)
    {
        const double loop = (double) meter.getLoopSteps();
        double s = std::fmod (steps, loop);
        if (s < 0) s += loop;
        return (int) (s / 4.0 * 60.0 / pb->bpm * sr) % juce::jmax (1, wLen);
    };
    if (wantB && ! wasListening)
        refReadPos = sameTempo && havePpq ? loopPosSamples (startSteps) : 0;
    else if (sameTempo && havePpq && playing && std::abs (refReadPos - loopPosSamples (startSteps)) > (int) (0.05 * sr))
        refReadPos = loopPosSamples (startSteps);
    wasListening = wantB;

    const float gain = juce::jlimit (0.063f, 4.0f,
                                     std::sqrt ((float) mixMeanSquare / juce::jmax (1.0e-9f, pb->windowMeanSquare)));
    const float fadeStep = (float) (1.0 / (0.015 * sr));
    const float* rl = ra.getReadPointer (0);
    const float* rr = ra.getReadPointer (1);
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (numCh > 1 ? 1 : 0);
    const bool advance = playing || ! havePpq;

    for (int i = 0; i < n; ++i)
    {
        abFade = juce::jlimit (0.0f, 1.0f, abFade + (wantB ? fadeStep : -fadeStep));
        const int idx = juce::jlimit (0, total - 1, wStart + refReadPos);
        const float a = 1.0f - abFade, b = abFade * gain;
        outL[i] = outL[i] * a + rl[idx] * b;
        if (numCh > 1) outR[i] = outR[i] * a + rr[idx] * b;
        if (advance && ++refReadPos >= wLen) refReadPos = 0;
    }
    refPosSec.store ((wStart + refReadPos) / sr);
}

//==============================================================================
//  Live measurements: every second, the last 8 bars you played are measured on a
//  background thread with the same function that measures the reference window.
//==============================================================================
void GroundShakeProcessor::runLiveAnalysis()
{
    const juce::ScopedLock al (ringAllocLock);
    if (ringSize <= 0 || (int) ringL.size() != ringSize) return;
    RingPos p;
    {
        const juce::SpinLock::ScopedLockType l (ringLock);
        p = ringPos;
    }
    const double samplesPerBar = 240.0 / juce::jlimit (40.0, 300.0, p.bpm) * sr;
    int N = (int) std::min<double> (juce::jlimit (1, gs::kBars, loopStepsParam.load() / gs::kStepsPerBar) * samplesPerBar,
                                    (double) p.count);
    N = juce::jmin (N, ringSize - (int) sr);
    if (N < (int) juce::jmin (2.0 * samplesPerBar, (double) loopStepsParam.load() / gs::kStepsPerBar * samplesPerBar)
        || N < (int) (1.05 * sr)) return;

    juce::AudioBuffer<float> buf (2, N);
    int idx = p.write - N;
    if (idx < 0) idx += ringSize;
    for (int i = 0; i < N; ++i)
    {
        buf.setSample (0, i, ringL[(size_t) idx]);
        buf.setSample (1, i, ringR[(size_t) idx]);
        if (++idx >= ringSize) idx = 0;
    }
    const double stepsPerSample = p.bpm / 60.0 * 4.0 / sr;
    const double stepsAtStart = p.stepsAtWrite - N * stepsPerSample;
    const double firstBeatSteps = std::ceil (stepsAtStart / 4.0) * 4.0;
    const double firstBeat = (firstBeatSteps - stepsAtStart) / stepsPerSample;
    const double beatLen = 60.0 / p.bpm * sr;
    auto m = gs::measureAudio (buf, 0, N, sr, firstBeat, beatLen, splitParam.load());

    const juce::ScopedLock l (liveMLock);
    liveM = m;
}

gs::Measurements GroundShakeProcessor::getLiveMeasurements() const
{
    const juce::ScopedLock l (liveMLock);
    return liveM;
}

//==============================================================================
gs::StepProfile GroundShakeProcessor::getLiveProfile() const
{
    const juce::SpinLock::ScopedLockType sl (profileLock);
    return publishedLive;
}

bool GroundShakeProcessor::liveHasData() const
{
    const juce::SpinLock::ScopedLockType sl (profileLock);
    return publishedHasData;
}

float GroundShakeProcessor::getRefSubDbNow() const
{
    if (reference == nullptr || ! refProfile.valid) return -60.0f;
    const auto& ref = *reference;
    const double hopSec = ref.hop / ref.sampleRate;

    double t = refPosSec.load();                         // where the reference is playing
    if (t < 0)                                           // not playing: follow your loop's step
    {
        const double steps = std::fmod (hostSteps.load(), (double) (windowBars * gs::kStepsPerBar));
        t = getGridStartSec() + windowStartBar * 240.0 / bpm + steps / 4.0 * 60.0 / bpm;
    }
    const auto f = (size_t) juce::jmax (0.0, t / hopSec);
    if (f >= ref.feelEnv.size()) return -60.0f;
    // short average around the position, like the live meter's ~30 ms
    float p = 0;
    int c = 0;
    for (size_t i = f > 3 ? f - 3 : 0; i <= f && i < ref.feelEnv.size(); ++i, ++c)
        p += ref.feelEnv[i] + gs::kPunchWeight * ref.punchEnv[i];
    return gs::powerToDb ((p / juce::jmax (1, c)) / juce::jmax (1.0e-9f, refProfile.fullTotal));
}

//==============================================================================
void GroundShakeProcessor::loadReferenceAsync (const juce::File& file)
{
    loading.store (true);
    loadError.clear();
    if (onReferenceChanged) onReferenceChanged();

    auto flag = alive;
    const double hostRate = sr;
    const float sp = split;
    pool.addJob ([this, file, flag, hostRate, sp]
    {
        auto ref = std::make_shared<gs::ReferenceTrack>();
        juce::String err;
        const bool ok = gs::loadReference (file, *ref, err, sp);
        std::shared_ptr<const juce::AudioBuffer<float>> res;
        if (ok) res = resampleTo (ref->audio, ref->sampleRate, hostRate);

        juce::MessageManager::callAsync ([this, flag, ok, ref, res, err, hostRate]
        {
            if (! flag->load()) return;
            loading.store (false);
            if (! ok)
            {
                loadError = err;
                if (onReferenceChanged) onReferenceChanged();
                return;
            }
            const bool sameFile = reference != nullptr && reference->file == ref->file;
            reference = ref;
            resampled = res;
            resampledRate = hostRate;
            bpm = pendingBpm > 0 ? pendingBpm : ref->detectedBpm;
            gridOffsetSec = pendingOffset;
            dropBar = gs::findDropBar (*reference, bpm, gridOffsetSec, windowBars);
            breakdownBar = gs::findBreakdownBar (*reference, bpm, gridOffsetSec, windowBars);
            // a new track opens on its drop; a re-analyzed or restored one keeps its window
            windowStartBar = pendingStartBar >= 0 ? pendingStartBar : dropBar;
            pendingBpm = 0;  pendingOffset = 0;  pendingStartBar = -1;
            if (! sameFile)
            {
                sectionDismissed = false;
                coach.reset();
            }
            refM = {};
            rebuildWindow();
            if (std::abs (resampledRate - sr) > 0.5)     // host rate changed while loading
                requestResample (sr);
            if (onReferenceChanged) onReferenceChanged();
        });
    });
}

void GroundShakeProcessor::reanalyzeReference()
{
    if (reference == nullptr) return;
    pendingBpm = bpm;
    pendingOffset = gridOffsetSec;
    pendingStartBar = windowStartBar;
    loadReferenceAsync (reference->file);
}

void GroundShakeProcessor::setBpm (double newBpm)
{
    if (newBpm < 40.0 || newBpm > 300.0) return;
    bpm = std::round (newBpm * 100.0) / 100.0;
    updateSections();
    rebuildWindow();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::nudgeGrid (double seconds)
{
    gridOffsetSec = juce::jlimit (-2.0, 2.0, gridOffsetSec + seconds);
    updateSections();
    rebuildWindow();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setWindowStartBar (int bar)
{
    windowStartBar = bar;
    rebuildWindow();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::findTheDrop()
{
    if (reference == nullptr) return;
    updateSections();
    setWindowStartBar (dropBar);
}

void GroundShakeProcessor::findTheBreakdown()
{
    if (reference == nullptr) return;
    updateSections();
    if (breakdownBar < 0) return;
    sectionDismissed = true;              // comparing breakdown to breakdown is intended
    setWindowStartBar (breakdownBar);
}

void GroundShakeProcessor::updateSections()
{
    if (reference == nullptr) return;
    dropBar = gs::findDropBar (*reference, bpm, gridOffsetSec, windowBars);
    breakdownBar = gs::findBreakdownBar (*reference, bpm, gridOffsetSec, windowBars);
}

void GroundShakeProcessor::setWindowBars (int bars, bool fromLoop)
{
    int b = 1;
    for (int c : { 1, 2, 4, 8 }) if (bars >= c) b = c;
    if (! fromLoop) followLoop = false;
    if (b == windowBars) { if (onReferenceChanged) onReferenceChanged(); return; }
    // keep the window over the same music: stay on the drop / breakdown if it was there
    const bool wasDrop = windowStartBar == dropBar, wasBreak = windowStartBar == breakdownBar;
    windowBars = b;
    loopStepsParam.store (b * gs::kStepsPerBar);
    updateSections();
    if (wasDrop) windowStartBar = dropBar;
    else if (wasBreak && breakdownBar >= 0) windowStartBar = breakdownBar;
    coach.reset();
    rebuildWindow();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setFollowLoop (bool on)
{
    followLoop = on;
    if (on) applyHostLoop();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::applyHostLoop()
{
    const int hb = hostLoopBars.load();
    if (followLoop && hb >= 1) setWindowBars (juce::jmin (hb, gs::kBars), true);
}

int GroundShakeProcessor::getNumReferenceBars() const
{
    return reference ? gs::numBarsInReference (*reference, bpm, gridOffsetSec) : 0;
}

double GroundShakeProcessor::getGridStartSec() const
{
    return reference ? reference->firstDownbeatSec + gridOffsetSec : 0.0;
}

void GroundShakeProcessor::rebuildWindow()
{
    if (reference == nullptr || bpm <= 0) return;
    const int barsAvail = getNumReferenceBars();
    const int bars = juce::jlimit (1, windowBars, barsAvail);
    windowStartBar = juce::jlimit (0, juce::jmax (0, barsAvail - windowBars), windowStartBar);
    refProfile = gs::profileForWindow (*reference, bpm, gridOffsetSec, windowStartBar, bars);
    windowVsDropDb = barsAvail > windowBars
                         ? sectionScore (*reference, bpm, gridOffsetSec, windowStartBar, windowBars)
                               - sectionScore (*reference, bpm, gridOffsetSec, dropBar, windowBars)
                         : 0.0f;
    // breakdown mode: this window carries far less sub than the drop (muted / filtered kick and bass)
    {
        const float dropShake = barsAvail > windowBars
                                    ? gs::profileForWindow (*reference, bpm, gridOffsetSec, dropBar, windowBars).shakeDb()
                                    : refProfile.shakeDb();
        breakdownMode = refProfile.valid && (refProfile.shakeDb() <= -15.0f || refProfile.shakeDb() <= dropShake - 10.0f);
    }
    measureReferenceWindow();

    if (resampled != nullptr)
    {
        auto p = std::make_unique<RefPlayback>();
        p->audio = resampled;
        p->sampleRate = resampledRate;
        p->bpm = bpm;
        const double barSec = 240.0 / bpm;
        const double startSec = juce::jmax (0.0, getGridStartSec() + windowStartBar * barSec);
        p->windowStart = juce::jlimit (0, resampled->getNumSamples() - 1, (int) std::round (startSec * resampledRate));
        p->windowLength = juce::jlimit (1, resampled->getNumSamples() - p->windowStart,
                                        (int) std::round (bars * barSec * resampledRate));
        p->windowMeanSquare = juce::jmax (1.0e-9f, refProfile.fullTotal);
        installPlayback (std::move (p));
    }
    updateCoach();
}

void GroundShakeProcessor::measureReferenceWindow()
{
    refMeasurePending = true;                            // picked up by the timer (debounces window drags)
}

void GroundShakeProcessor::recomputeReferenceBands (float newSplit)
{
    split = juce::jlimit (45.0f, 80.0f, newSplit);
    splitParam.store (split);
    if (reference == nullptr) { updateCoach(); return; }
    auto ref = reference;
    auto flag = alive;
    const int gen = ++bandGen;
    const float sp = split;
    pool.addJob ([this, ref, flag, gen, sp]
    {
        auto tmp = std::make_shared<gs::ReferenceTrack>();
        tmp->sampleRate = ref->sampleRate;
        tmp->audio.setDataToReferTo (const_cast<float* const*> (ref->audio.getArrayOfReadPointers()), 2,
                                     ref->audio.getNumSamples());
        gs::computeEnvelopes (*tmp, sp);
        juce::MessageManager::callAsync ([this, ref, flag, gen, tmp]
        {
            if (! flag->load() || reference != ref || gen != bandGen) return;
            ref->split = tmp->split;
            ref->hop = tmp->hop;
            ref->feelEnv.swap (tmp->feelEnv);
            ref->punchEnv.swap (tmp->punchEnv);
            ref->mudEnv.swap (tmp->mudEnv);
            ref->fullEnv.swap (tmp->fullEnv);
            ref->clickEnv.swap (tmp->clickEnv);
            rebuildWindow();
            if (onReferenceChanged) onReferenceChanged();
        });
    });
}

void GroundShakeProcessor::setSplitManual (float hz)
{
    rangeAuto = false;
    if (std::abs (hz - split) >= 0.5f) recomputeReferenceBands (hz);
}

void GroundShakeProcessor::setRangeAuto()
{
    rangeAuto = true;
    applyAutoRange();
    if (! (refM.valid && refM.bassConf >= 5.0f) && std::abs (split - gs::kDefaultSplit) > 0.5f)
        recomputeReferenceBands (gs::kDefaultSplit);
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::applyAutoRange()
{
    // Auto: the feel/punch split sits a little above the reference's bass note
    // (an E1 bass at 41 Hz -> split ~60 Hz; a G1 at 49 Hz -> ~71 Hz)
    // (not from a breakdown: a muted bass's note isn't where the track's sub lives)
    if (! rangeAuto || ! refM.valid || refM.bassConf < 5.0f || breakdownMode) return;
    const float target = std::round (juce::jlimit (50.0f, 75.0f, refM.bassHz * 1.45f));
    if (std::abs (target - split) >= 1.5f)
        recomputeReferenceBands (target);
}

void GroundShakeProcessor::installPlayback (std::unique_ptr<RefPlayback> p)
{
    if (ownedPlayback != nullptr)
        retired.emplace_back (std::move (ownedPlayback), juce::Time::getMillisecondCounter());
    ownedPlayback = std::move (p);
    livePlayback.store (ownedPlayback.get());
}

//==============================================================================
void GroundShakeProcessor::updateCoach()
{
    gs::CoachInputs in;
    in.haveRef = reference != nullptr && refProfile.valid;
    {
        const juce::SpinLock::ScopedLockType sl (profileLock);
        in.you = publishedLive;
        in.haveYou = publishedHasData && publishedSlots >= juce::jmin (32, windowBars * gs::kStepsPerBar);   // 2 bars (or the whole loop) heard
    }
    in.ref = refProfile;
    in.youM = getLiveMeasurements();
    in.refM = refM;
    in.tolerance = tolerance;
    in.refName = reference ? reference->name : juce::String();
    in.windowStartBar = windowStartBar;
    in.numRefBars = getNumReferenceBars();
    in.windowBars = juce::jlimit (1, windowBars, juce::jmax (1, in.numRefBars));
    in.dropBar = dropBar;
    in.breakdownMode = breakdownMode;
    in.breakdownBar = breakdownBar;
    in.windowVsDropDb = windowVsDropDb;
    in.sectionDismissed = sectionDismissed;
    in.hostBpm = hostBpm.load();
    in.djRefLevel = djRefLevel;
    coach.update (in, juce::Time::getMillisecondCounterHiRes() / 1000.0);
}

void GroundShakeProcessor::takeSnapshot()
{
    updateCoach();
    coach.takeSnapshot();
    snapshotProfile = getLiveProfile();
}

void GroundShakeProcessor::resetMeasurements()
{
    coach.reset();
    snapshotProfile = {};
    resetRequested.store (true);
    {
        const juce::SpinLock::ScopedLockType sl (profileLock);
        publishedLive = {};
        publishedHasData = false;
        publishedSlots = 0;
    }
    {
        const juce::ScopedLock l (liveMLock);
        liveM = {};
    }
    updateCoach();
}

//==============================================================================
//  Match stage
//==============================================================================
void GroundShakeProcessor::pushMatchToAudio()
{
    mFeel.store (matchSettings.feelDb);
    mFeelHz.store (matchSettings.feelHz);
    mPunch.store (matchSettings.punchDb);
    mMono.store (matchSettings.mono);
    mWeight.store (matchSettings.weight);
    mActive.store (matchState == MatchState::preview || matchState == MatchState::applied);
    mLevelMatch.store (matchState == MatchState::preview);
}

void GroundShakeProcessor::computeMatch()
{
    const auto live = getLiveProfile();
    if (! live.valid || ! refProfile.valid) return;
    // while applied, what we hear already includes the correction: refine it
    const auto base = matchState == MatchState::applied ? matchSettings : gs::MatchSettings();
    // gain that moves a zone's share of the mix from f to t: boosting a zone also raises the
    // whole mix, so the share moves less than the boost. Solve f*G / (1 - f + f*G) = t for G.
    auto zoneGainDb = [] (float zoneYou, float fullYou, float zoneRef, float fullRef)
    {
        if (fullYou <= 0 || fullRef <= 0 || zoneYou <= 0) return 0.0f;
        const double f = juce::jlimit (1.0e-6, 0.95, (double) zoneYou / fullYou);
        const double t = juce::jlimit (1.0e-6, 0.95, (double) zoneRef / fullRef);
        return (float) (10.0 * std::log10 (t * (1.0 - f) / (f * (1.0 - t))));
    };
    const float df = zoneGainDb (live.feelTotal, live.fullTotal, refProfile.feelTotal, refProfile.fullTotal);
    const float dp = zoneGainDb (live.punchTotal, live.fullTotal, refProfile.punchTotal, refProfile.fullTotal);
    const auto lm = getLiveMeasurements();
    matchSettings.feelDb = juce::jlimit (-6.0f, 6.0f, base.feelDb + (std::abs (df) > 0.3f ? df : 0.0f));
    matchSettings.punchDb = juce::jlimit (-6.0f, 6.0f, base.punchDb + (std::abs (dp) > 0.3f ? dp : 0.0f));
    matchSettings.mono = base.mono || (lm.valid && lm.monoCorr < 0.9f);
    // centre the feel boost on your bass note, where the sub energy actually is
    matchSettings.feelHz = matchState == MatchState::applied && base.feelHz > 0 ? base.feelHz
                         : (lm.valid && lm.bassConf >= 5.0f ? juce::jlimit (25.0f, split, lm.bassHz) : 0.0f);
    if (matchState != MatchState::applied) matchState = MatchState::ready;
    pushMatchToAudio();
}

void GroundShakeProcessor::setMatchPreview (bool on)
{
    if (on && matchState == MatchState::ready) matchState = MatchState::preview;
    else if (! on && matchState == MatchState::preview) matchState = MatchState::ready;
    pushMatchToAudio();
}

void GroundShakeProcessor::applyMatch()
{
    if (matchState == MatchState::ready || matchState == MatchState::preview)
        matchState = MatchState::applied;
    pushMatchToAudio();
}

void GroundShakeProcessor::revertMatch()
{
    const float w = matchSettings.weight;
    matchSettings = {};
    matchSettings.weight = w;
    matchState = MatchState::off;
    pushMatchToAudio();
}

void GroundShakeProcessor::setMatchWeight (bool on)
{
    matchSettings.weight = on ? 0.5f : 0.0f;
    pushMatchToAudio();
}

//==============================================================================
void GroundShakeProcessor::timerCallback()
{
    // free old playback objects once the audio thread can no longer be using them
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now] (const auto& r) { return now - r.second > 1500; }),
                   retired.end());

    // measure the reference window in the background (debounced while you drag it)
    if (refMeasurePending && ! refMeasureRunning && reference != nullptr && bpm > 0)
    {
        refMeasurePending = false;
        refMeasureRunning = true;
        auto ref = reference;
        auto flag = alive;
        const double barSec = 240.0 / bpm;
        const int bars = juce::jlimit (1, windowBars, juce::jmax (1, getNumReferenceBars()));
        const int start = (int) ((getGridStartSec() + windowStartBar * barSec) * ref->sampleRate);
        const int len = (int) (bars * barSec * ref->sampleRate);
        const float sp = split;
        pool.addJob ([this, ref, flag, start, len, barSec, sp]
        {
            auto m = gs::measureAudio (ref->audio, start, len, ref->sampleRate, 0.0, barSec / 4.0 * ref->sampleRate, sp);
            juce::MessageManager::callAsync ([this, ref, flag, m]
            {
                if (! flag->load()) return;
                refMeasureRunning = false;
                if (reference != ref) return;
                refM = m;
                applyAutoRange();
                updateCoach();
                if (onReferenceChanged) onReferenceChanged();
            });
        });
    }

    if (followLoop && hostLoopBars.load() >= 1 && juce::jmin (hostLoopBars.load(), gs::kBars) != windowBars)
    {
        int b = 1;
        for (int c : { 1, 2, 4, 8 }) if (juce::jmin (hostLoopBars.load(), gs::kBars) >= c) b = c;
        if (b != windowBars) applyHostLoop();
    }
    if (++timerTicks % 3 == 0) updateCoach();
}

//==============================================================================
juce::AudioProcessorEditor* GroundShakeProcessor::createEditor()
{
    return new GroundShakeEditor (*this);
}

void GroundShakeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("GroundShake");
    xml.setAttribute ("version", 2);
    if (reference) xml.setAttribute ("reference", reference->file.getFullPathName());
    xml.setAttribute ("bpm", bpm);
    xml.setAttribute ("offset", gridOffsetSec);
    xml.setAttribute ("startBar", windowStartBar);
    xml.setAttribute ("listenB", listenB.load());
    xml.setAttribute ("tolerance", tolerance);
    xml.setAttribute ("split", split);
    xml.setAttribute ("rangeAuto", rangeAuto);
    xml.setAttribute ("djRef", djRefLevel);
    xml.setAttribute ("windowBars", windowBars);
    xml.setAttribute ("followLoop", followLoop);
    xml.setAttribute ("sectionDismissed", sectionDismissed);
    xml.setAttribute ("matchApplied", matchState == MatchState::applied);
    xml.setAttribute ("matchFeel", matchSettings.feelDb);
    xml.setAttribute ("matchPunch", matchSettings.punchDb);
    xml.setAttribute ("matchMono", matchSettings.mono);
    xml.setAttribute ("matchWeight", matchSettings.weight);
    xml.setAttribute ("matchFeelHz", matchSettings.feelHz);
    copyXmlToBinary (xml, dest);
}

void GroundShakeProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("GroundShake")) return;
    listenB.store (xml->getBoolAttribute ("listenB", false));
    tolerance = (float) xml->getDoubleAttribute ("tolerance", 1.0);
    split = (float) xml->getDoubleAttribute ("split", gs::kDefaultSplit);
    splitParam.store (split);
    rangeAuto = xml->getBoolAttribute ("rangeAuto", true);
    djRefLevel = (float) xml->getDoubleAttribute ("djRef", -9.0);
    windowBars = juce::jlimit (1, gs::kBars, xml->getIntAttribute ("windowBars", gs::kBars));
    loopStepsParam.store (windowBars * gs::kStepsPerBar);
    followLoop = xml->getBoolAttribute ("followLoop", true);
    sectionDismissed = xml->getBoolAttribute ("sectionDismissed", false);
    matchSettings.feelDb = (float) xml->getDoubleAttribute ("matchFeel", 0.0);
    matchSettings.punchDb = (float) xml->getDoubleAttribute ("matchPunch", 0.0);
    matchSettings.mono = xml->getBoolAttribute ("matchMono", false);
    matchSettings.weight = (float) xml->getDoubleAttribute ("matchWeight", 0.0);
    matchSettings.feelHz = (float) xml->getDoubleAttribute ("matchFeelHz", 0.0);
    matchState = xml->getBoolAttribute ("matchApplied", false) ? MatchState::applied : MatchState::off;
    pushMatchToAudio();

    const juce::File f (xml->getStringAttribute ("reference"));
    if (f.existsAsFile())
    {
        pendingBpm = xml->getDoubleAttribute ("bpm", 0.0);
        pendingOffset = xml->getDoubleAttribute ("offset", 0.0);
        pendingStartBar = xml->getIntAttribute ("startBar", 0);
        loadReferenceAsync (f);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GroundShakeProcessor();
}
