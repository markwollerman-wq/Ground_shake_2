// Headless end-to-end test: plays one audio file through Ground Shake as "your mix"
// with a fake host transport, loads another as the reference, and saves a PNG of the UI.
// Usage: GroundShakeRenderTest yourMix.wav reference.wav out.png [listenB|match|gain=dB]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

struct FakeHead : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
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

int main (int argc, char* argv[])
{
    if (argc < 4) { std::printf ("usage: mix.wav ref.wav out.png [listenB|match|gain=dB]\n"); return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    bool listen = false, doMatch = false;
    float lowGainDb = 0.0f;
    int forceWindow = -1;
    for (int a = 4; a < argc; ++a)
    {
        const juce::String opt (argv[a]);
        listen = listen || opt == "listenB";
        doMatch = doMatch || opt == "match";
        if (opt.startsWith ("window=")) forceWindow = opt.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        if (opt.startsWith ("gain=")) lowGainDb = opt.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
    }

    gs::ReferenceTrack mix;  juce::String err;
    if (! gs::loadReference (juce::File (juce::String::fromUTF8 (argv[1])), mix, err)) { std::printf ("mix: %s\n", err.toRawUTF8()); return 1; }
    std::printf ("your mix: %s  (tempo used for the fake host: %.2f BPM)\n", mix.name.toRawUTF8(), mix.detectedBpm);

    // optional: change the mix's sub to test the checks (low shelf-ish boost/cut via a peak at 45 Hz)
    if (std::abs (lowGainDb) > 0.01f)
        for (int c = 0; c < 2; ++c)
        {
            gs::Biquad b;  b.setPeak (mix.sampleRate, 45.0, 0.7, lowGainDb);
            auto* d = mix.audio.getWritePointer (c);
            for (int i = 0; i < mix.audio.getNumSamples(); ++i) d[i] = b.process (d[i]);
        }

    GroundShakeProcessor proc;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, mix.sampleRate, block);
    proc.prepareToPlay (mix.sampleRate, block);
    FakeHead head;  head.bpm = mix.detectedBpm;
    proc.setPlayHead (&head);

    proc.loadReferenceAsync (juce::File (juce::String::fromUTF8 (argv[2])));
    for (int i = 0; i < 400 && (proc.isLoading() || ! proc.hasReference()); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    if (! proc.hasReference()) { std::printf ("reference failed: %s\n", proc.getLoadError().toRawUTF8()); return 1; }
    pump (600);                                       // reference window measurement + auto range
    if (forceWindow >= 0) { proc.setWindowStartBar (forceWindow); pump (600); }
    std::printf ("reference: %s  BPM %.2f  bars %d  window bar %d  drop bar %d  split %.0f Hz (%s)\n",
                 proc.getReference()->name.toRawUTF8(), proc.getBpm(), proc.getNumReferenceBars(),
                 proc.getWindowStartBar() + 1, proc.getDropBar() + 1, proc.getSplit(), proc.isRangeAuto() ? "auto" : "manual");
    proc.setListenToReference (listen);

    // play the mix from its first bar line for 8 bars (or the whole clip), twice
    const int start = (int) (mix.firstDownbeatSec * mix.sampleRate);
    const int len = juce::jmin (mix.audio.getNumSamples() - start, (int) (8 * 240.0 / mix.detectedBpm * mix.sampleRate));
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
                head.ppq = (loopPass * 32.0) + pos / mix.sampleRate * head.bpm / 60.0;
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
        head.ppq = loopPass * 32.0 + ((i * block) % (len - block)) / mix.sampleRate * head.bpm / 60.0;
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
