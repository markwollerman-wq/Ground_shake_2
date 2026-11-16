// Headless end-to-end test: plays one audio file through Ground Shake as "your mix"
// with a fake host transport, loads another as the reference, and saves a PNG of the UI.
// Usage: GroundShakeRenderTest yourMix.wav reference.wav out.png [listenB|match|gain=dB]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <cstdlib>

struct FakeHead : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0;
    int loopBars = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        if (loopBars > 0)
        {
            p.setIsLooping (true);
            p.setLoopPoints (juce::AudioPlayHead::LoopPoints { 0.0, loopBars * 4.0 });
        }
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (true);
        return p;
    }
};

static float rms (const juce::AudioBuffer<float>& b)
{
    double s = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) s += b.getSample (c, i) * b.getSample (c, i);
    return (float) std::sqrt (s / (b.getNumChannels() * (double) b.getNumSamples()));
}

static const char* statusName (gs::Status s)
{
    switch (s)
    {
        case gs::Status::pass: return "PASS";
        case gs::Status::holding: return "HOLD";
        case gs::Status::near: return "NEAR";
        case gs::Status::fail: return "FAIL";
        case gs::Status::info: return "info";
        case gs::Status::waiting:
        default: return "wait";
    }
}

static void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

// Phase 3: a song that starts at bar 9 (drop 16 bars, low-passed breakdown 8 bars, drop 16 bars),
// learned by playing it through, auto-paired against the reference, matched, played again.
static int runPhase3 (gs::ReferenceTrack& mix, const juce::File& refFile, const juce::File& outPng)
{
    const double sr = mix.sampleRate, bpm = mix.detectedBpm, barSec = 240.0 / bpm;
    const int clipBars = juce::jmin (8, gs::numBarsInReference (mix, bpm, 0.0));
    const int clipLen = (int) (clipBars * barSec * sr);
    // build the song
    juce::AudioBuffer<float> lp (2, clipLen);
    for (int c = 0; c < 2; ++c)
    {
        gs::Biquad a, b;  a.setLowPass (sr, 300.0);  b.setLowPass (sr, 300.0);
        for (int i = 0; i < clipLen; ++i) lp.setSample (c, i, b.process (a.process (mix.audio.getSample (c, i))));
    }
    const int songBars = 5 * clipBars;                      // drop, drop, breakdown, drop, drop
    juce::AudioBuffer<float> song (2, songBars / clipBars * clipLen);
    for (int part = 0; part < 5; ++part)
        for (int c = 0; c < 2; ++c)
            song.copyFrom (c, part * clipLen, part == 2 ? lp : mix.audio, c, 0, clipLen);
    std::printf ("song: %d bars at %.2f BPM, starting at Ableton bar 9 (drop %d, breakdown %d (low-passed), drop %d)\n",
                 songBars, bpm, 2 * clipBars, clipBars, 2 * clipBars);

    GroundShakeProcessor proc;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    FakeHead head;  head.bpm = bpm;
    proc.setPlayHead (&head);
    proc.setSongStartBar (9);
    proc.loadReferenceAsync (refFile);
    for (int i = 0; i < 400 && (proc.isLoading() || ! proc.hasReference()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    pump (600);
    std::printf ("reference: %s, %d bars, drop bar %d, breakdown bar %d\n", proc.getReference()->name.toRawUTF8(),
                 proc.getNumReferenceBars(), proc.getDropBar() + 1, proc.getBreakdownBar() + 1);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, block);
    const int delaySamples = std::getenv ("GS_DELAYMS") != nullptr ? (int) (std::atof (std::getenv ("GS_DELAYMS")) * sr / 1000.0) : 0;
    auto playSong = [&] (bool checkUntouched, double* maxDiffIn, double* maxDiffOut, int* blocksIn)
    {
        // two bars of lead-in before bar 9, then the song
        const double startPpq = 32.0 - 8.0;
        const int lead = (int) (2 * barSec * sr);
        for (int pos = 0; pos + block <= lead + song.getNumSamples(); pos += block)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                {
                    const int sidx = pos + i - lead - delaySamples;      // GS_DELAYMS: the track sits late on Ableton's grid
                    buf.setSample (c, i, sidx >= 0 && sidx < song.getNumSamples() ? song.getSample (c, sidx) : 0.01f * std::sin (i * 0.05f));
                }
            juce::AudioBuffer<float> in;  in.makeCopyOf (buf);
            head.ppq = startPpq + pos / sr * bpm / 60.0;
            proc.processBlock (buf, midi);
            if (checkUntouched)
            {
                double d = 0;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) d = std::max (d, (double) std::abs (in.getSample (c, i) - buf.getSample (c, i)));
                // inside a pair (song bars) or not?
                const double songBar = (head.ppq - 32.0) / 4.0;
                bool inside = false;
                for (auto& p : proc.getPairs()) inside = inside || (songBar >= p.songStart + 1.0 && songBar < p.songEnd);
                bool nearAny = false;   // allow the one-beat glide around pair edges
                for (auto& p : proc.getPairs()) nearAny = nearAny || std::abs (songBar - p.songStart) < 1.0 || std::abs (songBar - p.songEnd) < 1.0;
                if (! nearAny)
                {
                    if (inside) { *maxDiffIn = std::max (*maxDiffIn, d); ++*blocksIn; }
                    else *maxDiffOut = std::max (*maxDiffOut, d);
                }
            }
            if ((pos / block) % 200 == 0) pump (30);
        }
    };

    proc.startLearn();
    pump (100);
    if (std::getenv ("GS_RANDSTART") != nullptr)
    {
        // start playback at an odd point (bar 12 and a bit), like pressing play anywhere
        const double startPpq = 32.0 + 3 * 4.0 + 1.37;
        const int startSample = (int) ((startPpq - 32.0) / (bpm / 60.0) * sr);
        for (int pos = startSample; pos + block <= song.getNumSamples(); pos += block)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, song.getSample (c, pos + i));
            head.ppq = 32.0 + pos / sr * bpm / 60.0;
            proc.processBlock (buf, midi);
            if ((pos / block) % 200 == 0) pump (30);
        }
        // then from the start, like playing it again
    }
    playSong (false, nullptr, nullptr, nullptr);
    pump (200);
    std::printf ("learned waveform bars: %d (status: %s)\n", proc.getLearnedBars(), proc.getLearnMessage().toRawUTF8());
    proc.stopLearn();
    for (int i = 0; i < 300 && ! proc.hasLearnedTrack(); ++i) pump (20);
    std::printf ("learned track: %s, effective start bar %d\n", proc.hasLearnedTrack() ? "yes" : "NO", proc.getEffectiveStartBar());
    if (auto* lt = proc.getLearnedTrack())
    {
        // where does the learned audio sit against the song? (envelope cross-correlation, +-150 ms)
        const double lr = lt->sampleRate;
        auto env = [] (const float* x, int n, double rate)        // energy per millisecond, by time
        {
            std::vector<float> e;
            for (int ms = 0;; ++ms)
            {
                const int a = (int) (ms * rate / 1000.0), b = (int) ((ms + 1) * rate / 1000.0);
                if (b > n) break;
                double v = 0;
                for (int k = a; k < b; ++k) v += x[k] * x[k];
                e.push_back ((float) (v / juce::jmax (1, b - a)));
            }
            return e;
        };
        const int nL = juce::jmin (lt->audio.getNumSamples(), (int) (8 * barSec * lr));
        auto eL = env (lt->audio.getReadPointer (0), nL, lr);
        auto eS = env (song.getReadPointer (0), juce::jmin (song.getNumSamples(), (int) (8 * barSec * sr)), sr);
        int best = 0; double bestV = -1;
        for (int lag = -150; lag <= 150; ++lag)
        {
            double v = 0;
            for (int i = 200; i + 200 < (int) juce::jmin (eL.size(), eS.size()); ++i) v += eL[(size_t) i] * eS[(size_t) (i - lag)];
            if (v > bestV) { bestV = v; best = lag; }
        }
        std::printf ("learned audio vs song: %d ms late (0 = placed exactly)\n", best);
    }
    proc.autoPair();
    for (int i = 0; i < 300; ++i)
    {
        pump (30);
        bool all = true;
        for (auto& p : proc.getPairs()) all = all && p.analyzed;
        if (all && ! proc.getPairs().empty()) break;
    }
    std::printf ("%s\n", proc.getLearnMessage().toRawUTF8());
    for (int i = 0; i < (int) proc.getPairs().size(); ++i)
    {
        const auto& p = proc.getPairs()[(size_t) i];
        const char* light[] = { "--", "GREEN", "AMBER", "RED" };
        std::printf ("\nPAIR %d: your bars %d-%d (song %d-%d) -> ref %c bars %d-%d  %s  light %s  measured from bar %d\n", i + 1,
                     proc.getEffectiveStartBar() + p.songStart, proc.getEffectiveStartBar() + p.songEnd - 1, p.songStart, p.songEnd,
                     p.slot == 0 ? 'A' : 'B', p.refStart + 1, p.refStart + p.refBars, p.breakdown ? "BREAKDOWN" : "drop",
                     light[p.status], proc.getEffectiveStartBar() + p.measureStart);
        std::printf ("  kick numbers you/ref: rise %.1f/%.1f ms click %.1f/%.1f dB decay %.0f/%.0f ms\n", p.youM.kick.riseMs, p.refM.kick.riseMs,
                     p.youM.kick.clickDb, p.refM.kick.clickDb, p.youM.kick.decayMs, p.refM.kick.decayMs);
        std::printf ("  you: kick %s | bass %s | notes %s\n", p.youM.kick.label().toRawUTF8(),
                     gs::bassPatternName (p.youM.bassPattern).toRawUTF8(), p.youM.bassNotes.describe().toRawUTF8());
        std::printf ("  ref: kick %s | bass %s | notes %s\n", p.refM.kick.label().toRawUTF8(),
                     gs::bassPatternName (p.refM.bassPattern).toRawUTF8(), p.refM.bassNotes.describe().toRawUTF8());
        for (auto& r : p.coach.checks())
            if (r.status != gs::Status::pass) std::printf ("    [%s] %s %s\n", statusName (r.status), r.name.toRawUTF8(), r.value.toRawUTF8());
        for (auto& c : p.coach.cards())
            std::printf ("    card: %s%s  (%s)\n", c.fixFirst ? "FIX FIRST " : "", c.title.toRawUTF8(), c.headline.toRawUTF8());
    }

    {
        const auto& p0 = proc.getPairs()[0];
        const auto bs = gs::beatShape (p0.you, p0.ref);
        std::printf ("\nGRID before: pair 1 light %d, kick 16th %+.2f, 2nd %+.2f, 4th %+.2f, kick riseMs %.1f\n", p0.status, bs.diff[0], bs.diff[1], bs.diff[3], p0.youM.kick.riseMs);
    }
    if (delaySamples > 0)
    {
        // what double-clicking the first kick of bar 4 does
        const double clickBar = 4.0 + delaySamples / sr / barSec * 0.7;
        const double t = proc.snapToHit (clickBar);
        proc.setSongGridOffsetBars (t - std::round (t));
        std::printf ("double-click near bar 4 (+%.0f ms): snapped to +%.1f ms; grid offset %.4f bars = %.1f ms (delay was %.1f ms)\n",
                     (clickBar - 4.0) * barSec * 1000.0, (t - 4.0) * barSec * 1000.0, proc.getSongGridOffsetBars(), proc.songGridSec() * 1000.0,
                     delaySamples / sr * 1000.0);
        pump (4000);                       // the pairs are re-measured one by one
        const auto& p0 = proc.getPairs()[0];
        const auto bs = gs::beatShape (p0.you, p0.ref);
        std::printf ("GRID after:  pair 1 light %d, kick 16th %+.2f, 2nd %+.2f, 4th %+.2f, kick riseMs %.1f\n", p0.status, bs.diff[0], bs.diff[1], bs.diff[3], p0.youM.kick.riseMs);
    }

    proc.matchAllPairs();
    for (int i = 0; i < (int) proc.getPairs().size(); ++i)
    {
        const auto& c = proc.getPairs()[(size_t) i].corr;
        std::printf ("pair %d correction: feel %+.1f punch %+.1f duck %+.1f %+.1f %+.1f %+.1f attack %+.1f tail %+.1f@%.0fms mud %+.1f lowcut %.0f mono %d weight %.1f\n",
                     i + 1, c.feelDb, c.punchDb, c.duckDb[0], c.duckDb[1], c.duckDb[2], c.duckDb[3], c.attackDb, c.tailDb,
                     c.tailStartMs, c.mudDb, c.lowCutHz, (int) c.mono, c.weight);
    }
    // ---- Phase 4: OUT readouts, worked out without playback
    for (int i = 0; i < 400; ++i) { pump (30); if (! proc.isWorkingOutOut()) break; }
    pump (300);
    std::printf ("\nPHASE 4 - MIX vs OUT (no playback): %s\n", proc.getLearnMessage().toRawUTF8());
    {
        const char* light[] = { "--", "GREEN", "AMBER", "RED" };
        for (int i = 0; i < (int) proc.getPairs().size(); ++i)
        {
            const auto& p = proc.getPairs()[(size_t) i];
            std::printf ("  pair %d: MIX %s (shake %.2f dB)  OUT %s (shake %.2f dB) outValid %d neutral %d\n", i + 1, light[p.status], p.you.shakeDb(),
                         light[p.outStatus], p.outYou.shakeDb(), (int) p.outValid, (int) p.corr.isNeutral());
            for (auto& r : p.outCoach.checks())
                if (r.status != gs::Status::pass) std::printf ("      OUT [%s] %s %s\n", statusName (r.status), r.name.toRawUTF8(), r.value.toRawUTF8());
        }
    }
    // OUT with a neutral correction must read the same as MIX; one knob must move the right reading
    {
        proc.selectPair (0);
        const auto saved = proc.getPairs()[0].corr;
        proc.setEditedCorrection (gs::MatchSettings());
        pump (500);
        for (int i = 0; i < 200; ++i) { pump (30); if (! proc.isWorkingOutOut()) break; }
        const auto& p0 = proc.getPairs()[0];
        auto bsM = gs::beatShape (p0.you, p0.ref), bsO = gs::beatShape (p0.outYou, p0.ref);
        std::printf ("\nneutral correction: MIX shake %.3f dB, OUT shake %.3f dB, kick 16th %.2f vs %.2f (must match)\n",
                     p0.you.shakeDb(), p0.outYou.shakeDb(), bsM.diff[0], bsO.diff[0]);
        gs::MatchSettings d;  d.duckDb[0] = -6.0f;
        proc.setEditedCorrection (d);
        pump (500);
        for (int i = 0; i < 200; ++i) { pump (30); if (! proc.isWorkingOutOut()) break; }
        bsO = gs::beatShape (proc.getPairs()[0].outYou, p0.ref);
        std::printf ("duck 16th 1 by -6 dB: kick 16th %.2f -> %.2f dB (must drop)\n", bsM.diff[0], bsO.diff[0]);
        proc.setEditedCorrection (saved);
        proc.matchAllPairs();
        for (int i = 0; i < 400; ++i) { pump (30); if (! proc.isWorkingOutOut()) break; }
        pump (300);
        std::printf ("MATCH ALL again (fine-tuned by OUT): %s\n", proc.getLearnMessage().toRawUTF8());
        const char* light[] = { "--", "GREEN", "AMBER", "RED" };
        for (int i = 0; i < (int) proc.getPairs().size(); ++i)
        {
            const auto& p = proc.getPairs()[(size_t) i];
            std::printf ("  pair %d: MIX %s  OUT %s  duck %+.1f %+.1f %+.1f %+.1f feel %+.1f punch %+.1f\n", i + 1, light[p.status], light[p.outStatus],
                         p.corr.duckDb[0], p.corr.duckDb[1], p.corr.duckDb[2], p.corr.duckDb[3], p.corr.feelDb, p.corr.punchDb);
            for (auto& r : p.outCoach.checks())
                if (r.status != gs::Status::pass && r.status != gs::Status::info) std::printf ("      OUT [%s] %s %s\n", statusName (r.status), r.name.toRawUTF8(), r.value.toRawUTF8());
        }
    }
    // master bus check
    for (int i = 0; i < 300; ++i) { pump (30); if (proc.hasMasterReport()) break; }
    proc.setViewMode (GroundShakeProcessor::ViewMode::master);
    std::printf ("\nMASTER CHECK: %s\n", proc.getMasterCoach().statusLine().toRawUTF8());
    for (auto& r : proc.getMasterCoach().checks())
        std::printf ("  [%s] %s: %s\n", statusName (r.status), r.name.toRawUTF8(), r.value.toRawUTF8());
    for (auto& c : proc.getDisplayCards())
    {
        std::printf ("  card: %s - %s\n", c.title.toRawUTF8(), c.headline.toRawUTF8());
        for (auto& h : c.how) std::printf ("      - %s\n", h.toRawUTF8());
    }
    proc.setViewMode (GroundShakeProcessor::ViewMode::out);
    proc.selectPair (1);
    pump (200);
    std::printf ("\nselected pair 2: showing %s readout, OUT on show %d. Cards with what Ground Shake fixed:\n",
                 proc.isShowingSavedPair() ? "SAVED" : "LIVE", (int) proc.isShowingOut());
    for (auto& c : proc.getDisplayCards())
        std::printf ("  %s\n    fixed: %s\n", c.title.toRawUTF8(), c.fixed.toRawUTF8());

    double inD = 0, outD = 0;
    int blocksIn = 0;
    playSong (true, &inD, &outD, &blocksIn);
    std::printf ("\nplayback with corrections: max change inside pairs %.4f (%d blocks), outside pairs %.9f (must be 0)\n", inD, blocksIn, outD);

    // hand-made pair over part of the first drop, then re-learn just that pair
    proc.deletePair (0);
    proc.selectPair (-1);                 // what dragging a new stretch does in the editor
    proc.setWindowStartBar (0);
    proc.addPair (0, 8);
    for (int i = 0; i < 200; ++i) { pump (30); if (! proc.getPairs().empty() && proc.getPairs()[0].analyzed) break; }
    std::printf ("manual pair: %d pairs, pair 1 bars %d-%d analyzed %d light %d | %s\n", (int) proc.getPairs().size(),
                 proc.getEffectiveStartBar() + proc.getPairs()[0].songStart, proc.getEffectiveStartBar() + proc.getPairs()[0].songEnd - 1,
                 (int) proc.getPairs()[0].analyzed, proc.getPairs()[0].status, proc.getLearnMessage().toRawUTF8());
    proc.startLearnPair (0);
    playSong (false, nullptr, nullptr, nullptr);
    pump (300);
    std::printf ("learn pair: state %s, %s\n", proc.getLearnState() == GroundShakeProcessor::LearnState::off ? "stopped by itself" : "STILL LEARNING",
                 proc.getLearnMessage().toRawUTF8());
    for (int i = 0; i < 200; ++i) { pump (30); if (proc.hasLearnedTrack() && proc.getPairs()[0].analyzed) break; }
    for (int i = 0; i < (int) proc.getPairs().size(); ++i)
        std::printf ("  after re-learn pair %d: bars %d-%d state %d analyzed %d breakdown %d lowcut %.0f light %d\n", i + 1,
                     proc.getPairs()[(size_t) i].songStart, proc.getPairs()[(size_t) i].songEnd, (int) proc.getPairs()[(size_t) i].state,
                     (int) proc.getPairs()[(size_t) i].analyzed, (int) proc.getPairs()[(size_t) i].breakdown,
                     proc.getPairs()[(size_t) i].corr.lowCutHz, proc.getPairs()[(size_t) i].status);
    inD = 0;  outD = 0;  blocksIn = 0;
    proc.selectPair (1);
    std::printf ("\nplayback with corrections: max change inside pairs %.4f (%d blocks), outside pairs %.9f (must be 0)\n", inD, blocksIn, outD);

    // Phase 4: moving the window with a pair selected must not change it
    {
        proc.selectPair (0);
        const auto before = proc.getPairs()[0];
        proc.setWindowStartBar (before.refStart + 3);
        std::printf ("\nwindow moved with pair 1 selected: pair ref start %d -> %d (must not change), selected now %d\n",
                     before.refStart, proc.getPairs()[0].refStart, proc.getSelectedPair());
        // EDIT PAIR: same stretch, new reference bars
        const bool ok = proc.editPair (0, before.songStart, before.songEnd, 0, 2, 2);
        for (int i = 0; i < 200; ++i) { pump (30); if (proc.getPairs()[0].analyzed && ! proc.isWorkingOutOut()) break; }
        std::printf ("edit pair 1 -> ref bars 3-4: ok %d, now ref %d-%d, analyzed %d, %s\n", (int) ok, proc.getPairs()[0].refStart + 1,
                     proc.getPairs()[0].refStart + proc.getPairs()[0].refBars, (int) proc.getPairs()[0].analyzed, proc.getLearnMessage().toRawUTF8());
        // overlapping edit is refused
        int manualOther = -1;
        for (int i = 1; i < (int) proc.getPairs().size(); ++i) if (proc.getPairs()[(size_t) i].manual) manualOther = i;
        if (manualOther < 0 && proc.getPairs().size() > 1) { proc.setPairRange (1, proc.getPairs()[1].songStart, proc.getPairs()[1].songEnd - 1); manualOther = 1; }
        const bool bad = manualOther > 0 && proc.editPair (0, proc.getPairs()[(size_t) manualOther].songStart, proc.getPairs()[(size_t) manualOther].songEnd, 0, 0, 4);
        for (int i = 0; i < 200; ++i) { pump (30); bool all = true; for (auto& q : proc.getPairs()) all = all && q.analyzed; if (all) break; }
        std::printf ("overlapping edit refused: %d (%s)\n", (int) ! bad, proc.getLearnMessage().toRawUTF8());
        proc.setAlignOffsetBars (2.25);
    }

    // save and restore
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    GroundShakeProcessor proc2;
    proc2.setPlayConfigDetails (2, 2, sr, block);
    proc2.prepareToPlay (sr, block);
    proc2.setStateInformation (state.getData(), (int) state.getSize());
    pump (1500);
    std::printf ("state: %d bytes; restored %d pairs, learned bars %d, pair 1 light %d, pair 2 corr lowcut %.0f\n", (int) state.getSize(),
                 (int) proc2.getPairs().size(), proc2.getLearnedBars(), proc2.getPairs().empty() ? -1 : proc2.getPairs()[0].status,
                 proc2.getPairs().size() > 1 ? proc2.getPairs()[1].corr.lowCutHz : -1.0f);
    {
        int outs = 0;
        for (auto& p : proc2.getPairs()) outs += p.outValid ? 1 : 0;
        std::printf ("restored: align offset %.2f bars, view mode %d, %d pairs with OUT readouts\n", proc2.getAlignOffsetBars(),
                     (int) proc2.getViewMode(), outs);
    }

    // screenshot with pair 2 selected
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1400, 840);
    for (int i = 0; i < 30; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (17);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    outPng.deleteFile();
    juce::FileOutputStream os (outPng);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("saved %s\n", outPng.getFullPathName().toRawUTF8());
    // more shots: guided pairing, zoomed in, master view
    std::function<ArrangementView* (juce::Component*)> findArr = [&] (juce::Component* c) -> ArrangementView*
    {
        if (auto* a = dynamic_cast<ArrangementView*> (c)) return a;
        for (auto* ch : c->getChildren()) if (auto* r = findArr (ch)) return r;
        return nullptr;
    };
    auto shot = [&] (const juce::String& suffix)
    {
        for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (17);
        auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outPng.getSiblingFile (outPng.getFileNameWithoutExtension() + suffix + ".png");
        f.deleteFile();
        juce::FileOutputStream o2 (f);
        juce::PNGImageFormat().writeImageToStream (im, o2);
        std::printf ("saved %s\n", f.getFullPathName().toRawUTF8());
    };
    if (auto* arr = findArr (ed.get()))
    {
        proc.setAlignOffsetBars (-2.0);
        for (auto* ch : arr->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (ch); b != nullptr && b->getButtonText() == "<")
            {
                for (int k = 0; k < 2; ++k) { b->triggerClick(); for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10); }
            }
        std::printf ("nudge < twice from -2 bars: offset now %.5f bars (expect -2.0625)\n", proc.getAlignOffsetBars());
        shot ("_nudged");
        proc.setAlignOffsetBars (0.0);
        arr->startPairing (-1);
        shot ("_pairing");
        arr->cancelPairing();
        arr->zoomBy (0.12, 300.0f);
        shot ("_zoom");
        arr->zoomToFit();
    }
    proc.setViewMode (GroundShakeProcessor::ViewMode::master);
    shot ("_master");
    ed.reset();
    return 0;
}

int main (int argc, char* argv[])
{
    if (argc < 4) { std::printf ("usage: mix.wav ref.wav out.png [listenB|match|gain=dB]\n"); return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    bool listen = false, doMatch = false;
    float lowGainDb = 0.0f;
    int forceWindow = -1, windowBars = 0, hostLoop = 0;
    bool findBreak = false;
    float lowpassHz = 0, highpassHz = 0;
    for (int a = 4; a < argc; ++a)
    {
        const juce::String opt (argv[a]);
        listen = listen || opt == "listenB";
        doMatch = doMatch || opt == "match";
        if (opt.startsWith ("bars=")) windowBars = opt.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        if (opt.startsWith ("loop=")) hostLoop = opt.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        if (opt.startsWith ("lowpass=")) lowpassHz = opt.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (opt.startsWith ("highpass=")) highpassHz = opt.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        findBreak = findBreak || opt == "breakdown";
        if (opt.startsWith ("window=")) forceWindow = opt.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        if (opt.startsWith ("gain=")) lowGainDb = opt.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
    }

    gs::ReferenceTrack mix;  juce::String err;
    if (! gs::loadReference (juce::File (juce::String::fromUTF8 (argv[1])), mix, err)) { std::printf ("mix: %s\n", err.toRawUTF8()); return 1; }
    std::printf ("your mix: %s  (tempo used for the fake host: %.2f BPM)\n", mix.name.toRawUTF8(), mix.detectedBpm);
    for (int a2 = 4; a2 < argc; ++a2)
        if (juce::String (argv[a2]) == "phase3")
            return runPhase3 (mix, juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));

    // optional: change the mix's sub to test the checks (low shelf-ish boost/cut via a peak at 45 Hz)
    if (std::abs (lowGainDb) > 0.01f)
        for (int c = 0; c < 2; ++c)
        {
            gs::Biquad b;  b.setPeak (mix.sampleRate, 45.0, 0.7, lowGainDb);
            auto* d = mix.audio.getWritePointer (c);
            for (int i = 0; i < mix.audio.getNumSamples(); ++i) d[i] = b.process (d[i]);
        }

    // optional: simulate a filtered ("muted") breakdown on your mix, 24 dB/oct
    if (lowpassHz > 0 || highpassHz > 0)
        for (int c = 0; c < 2; ++c)
        {
            gs::Biquad a, b;
            if (lowpassHz > 0) { a.setLowPass (mix.sampleRate, lowpassHz); b.setLowPass (mix.sampleRate, lowpassHz); }
            else               { a.setHighPass (mix.sampleRate, highpassHz); b.setHighPass (mix.sampleRate, highpassHz); }
            auto* d = mix.audio.getWritePointer (c);
            for (int i = 0; i < mix.audio.getNumSamples(); ++i) d[i] = b.process (a.process (d[i]));
        }

    GroundShakeProcessor proc;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, mix.sampleRate, block);
    proc.prepareToPlay (mix.sampleRate, block);
    FakeHead head;  head.bpm = mix.detectedBpm;  head.loopBars = hostLoop;
    proc.setPlayHead (&head);

    proc.loadReferenceAsync (juce::File (juce::String::fromUTF8 (argv[2])));
    for (int i = 0; i < 400 && (proc.isLoading() || ! proc.hasReference()); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    if (! proc.hasReference()) { std::printf ("reference failed: %s\n", proc.getLoadError().toRawUTF8()); return 1; }
    pump (600);                                       // reference window measurement + auto range
    if (windowBars > 0) { proc.setWindowBars (windowBars); pump (600); }
    if (findBreak) { proc.findTheBreakdown(); pump (600); }
    if (forceWindow >= 0) { proc.setWindowStartBar (forceWindow); pump (600); }
    auto refLine = [&]
    {
        std::printf ("reference: %s  BPM %.2f  bars %d  window bar %d (%d bars)  drop bar %d  breakdown bar %d  %s  split %.0f Hz (%s)\n",
                     proc.getReference()->name.toRawUTF8(), proc.getBpm(), proc.getNumReferenceBars(),
                     proc.getWindowStartBar() + 1, proc.getWindowBars(), proc.getDropBar() + 1, proc.getBreakdownBar() + 1,
                     proc.isBreakdownMode() ? "BREAKDOWN MODE" : "drop mode", proc.getSplit(), proc.isRangeAuto() ? "auto" : "manual");
    };
    refLine();
    proc.setListenToReference (listen);

    // play the mix from its first bar line for 8 bars (or the whole clip), twice
    const int start = (int) (mix.firstDownbeatSec * mix.sampleRate);
    const int loopLenBars = hostLoop > 0 ? hostLoop : proc.getWindowBars();
    const int len = juce::jmin (mix.audio.getNumSamples() - start, (int) (loopLenBars * 240.0 / mix.detectedBpm * mix.sampleRate));
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double samplesPlayed = 0;
    int loopPass = 0;
    auto play = [&] (int passes, float* inR, float* outR, float* maxDiff)
    {
        for (int pass = 0; pass < passes; ++pass)
        {
            for (int pos = 0; pos + block <= len; pos += block)
            {
                for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, mix.audio, c, start + pos, block);
                juce::AudioBuffer<float> in;  in.makeCopyOf (buf);
                // like Ableton's loop brace: every pass starts on the next 8-bar boundary
                head.ppq = hostLoop > 0 ? pos / mix.sampleRate * head.bpm / 60.0
                                        : (loopPass * loopLenBars * 4.0) + pos / mix.sampleRate * head.bpm / 60.0;
                samplesPlayed += block;
                proc.processBlock (buf, midi);
                if (inR != nullptr && pass == passes - 1) { const float a = rms (in), b = rms (buf); *inR += a * a; *outR += b * b; }
                if (maxDiff != nullptr)
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < block; ++i)
                            *maxDiff = std::max (*maxDiff, std::abs (in.getSample (c, i) - buf.getSample (c, i)));
            }
            ++loopPass;
        }
    };
    float inR = 0, outR = 0, maxDiff = 0;
    play (2, &inR, &outR, listen ? nullptr : &maxDiff);
    pump (300);                                       // the timer picks up the host loop length
    if (hostLoop > 0) { refLine(); play (8, nullptr, nullptr, nullptr); }
    std::printf ("level check (pass 2): input RMS %.4f  output RMS %.4f  (%s)\n", std::sqrt (inR), std::sqrt (outR),
                 listen ? "B = reference, should be close to input = level-matched" : "A = untouched, must be equal");
    if (! listen) std::printf ("A pass-through: max sample difference %.9f (must be 0)\n", maxDiff);

    proc.runLiveAnalysisNow();
    pump (400);
    proc.updateCoachNow();

    auto report = [&] (const char* label)
    {
        const auto live = proc.getLiveProfile();
        std::printf ("\n=== %s ===\nshake: you %.2f dB  ref %.2f dB  diff %+.2f dB\n", label, live.shakeDb(),
                     proc.getReferenceProfile().shakeDb(), live.shakeDb() - proc.getReferenceProfile().shakeDb());
        std::printf ("you  steps:");
        for (int s = 0; s < 16; ++s) std::printf ("%s%5.1f", s % 4 == 0 ? " |" : "", live.stepDb (s));
        std::printf ("\nref  steps:");
        for (int s = 0; s < 16; ++s) std::printf ("%s%5.1f", s % 4 == 0 ? " |" : "", proc.getReferenceProfile().stepDb (s));
        const auto ym = proc.getLiveMeasurements();
        const auto& rm = proc.getReferenceMeasurements();
        std::printf ("\nyou: mono %.2f bass %s kick %s key %s crest %.1f LUFS %.1f peak %.1f\n", ym.monoCorr,
                     gs::noteName (ym.bassHz).toRawUTF8(), gs::noteName (ym.kickHz).toRawUTF8(),
                     gs::keyName (ym.keyIndex, ym.keyMinor).toRawUTF8(), ym.subCrestDb, ym.lufs, ym.peakDb);
        std::printf ("ref: mono %.2f bass %s kick %s key %s crest %.1f LUFS %.1f peak %.1f\n", rm.monoCorr,
                     gs::noteName (rm.bassHz).toRawUTF8(), gs::noteName (rm.kickHz).toRawUTF8(),
                     gs::keyName (rm.keyIndex, rm.keyMinor).toRawUTF8(), rm.subCrestDb, rm.lufs, rm.peakDb);
        const auto& coach = proc.getCoach();
        std::printf ("checklist (match %.0f%%):\n", coach.matchPercent());
        for (auto& r : coach.checks())
            std::printf ("  [%s] %-18s %s\n", statusName (r.status), r.name.toRawUTF8(), r.value.toRawUTF8());
        std::printf ("status: %s\ncards:\n", coach.statusLine().toRawUTF8());
        for (auto& c : coach.cards())
        {
            std::printf ("  %s%s  [%s]  %s\n", c.fixFirst ? "(FIX THIS FIRST) " : "", c.title.toRawUTF8(),
                         statusName (c.severity), c.headline.toRawUTF8());
            std::printf ("     see: %s\n", c.see.toRawUTF8());
            for (int i = 0; i < c.how.size(); ++i) std::printf ("     %d. %s\n", i + 1, c.how[i].toRawUTF8());
            if (c.progress.isNotEmpty()) std::printf ("     progress: %s\n", c.progress.toRawUTF8());
            if (c.worse.isNotEmpty()) std::printf ("     CAREFUL: %s\n", c.worse.toRawUTF8());
        }
    };
    report ("after two passes");

    if (doMatch)
    {
        proc.takeSnapshot();
        proc.computeMatch();
        const auto& ms = proc.getMatchSettings();
        std::printf ("\nMATCH: feel %+.1f dB punch %+.1f dB mono %d\n", ms.feelDb, ms.punchDb, (int) ms.mono);
        proc.applyMatch();
        float a = 0, b = 0;
        play (2, &a, &b, nullptr);
        std::printf ("after APPLY: in RMS %.4f out RMS %.4f, ceiling %.1f dB\n", std::sqrt (a), std::sqrt (b), proc.getMatchCeilingDb());
        proc.runLiveAnalysisNow();
        pump (300);
        proc.updateCoachNow();
        report ("after Match + Apply");
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1400, 820);
    // let the feet animate while a little more audio plays
    for (int i = 0; i < 40; ++i)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, mix.audio, c, start + (i * block) % (len - block), block);
        head.ppq = (hostLoop > 0 ? 0.0 : loopPass * loopLenBars * 4.0) + ((i * block) % (len - block)) / mix.sampleRate * head.bpm / 60.0;
        proc.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (17);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File outFile (juce::String::fromUTF8 (argv[3]));
    outFile.deleteFile();
    juce::FileOutputStream os (outFile);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("saved %s\n", argv[3]);
    ed.reset();
    return 0;
}
