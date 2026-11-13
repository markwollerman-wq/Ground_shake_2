// Runs Ground Shake's reference analysis on audio files and prints the results.
// Usage: GroundShakeAnalysisTest file1.wav [file2.wav ...]
#include "../Source/Analysis.h"
#include <cstdio>

int main (int argc, char* argv[])
{
    for (int a = 1; a < argc; ++a)
    {
        gs::ReferenceTrack ref;
        juce::String err;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        if (! gs::loadReference (juce::File (juce::String::fromUTF8 (argv[a])), ref, err))
        {
            std::printf ("%s: ERROR %s\n", argv[a], err.toRawUTF8());
            continue;
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const int bars = gs::numBarsInReference (ref, ref.detectedBpm, 0.0);
        const int drop = gs::findDropBar (ref, ref.detectedBpm, 0.0);
        std::printf ("%s\n  length %.2f s | BPM %.2f | first downbeat %.3f s | whole bars %d | drop at bar %d | analysis %.0f ms\n",
                     ref.name.toRawUTF8(), ref.lengthSeconds(), ref.detectedBpm, ref.firstDownbeatSec, bars, drop + 1, ms);

        if (std::getenv ("GS_DEBUG"))
            for (int b = juce::jmax (0, drop - 3); b <= juce::jmin (bars - 8, drop + 3); ++b)
                std::printf ("   window %d: level %.2f shake %.2f\n", b, gs::windowLevelDb (ref, ref.detectedBpm, 0.0, b),
                             gs::profileForWindow (ref, ref.detectedBpm, 0.0, b).shakeDb());
        for (int w : { 0, drop })
        {
            const auto prof = gs::profileForWindow (ref, ref.detectedBpm, 0.0, w);
            std::printf ("  window bar %d: level %.1f dB | shake %.1f dB  feel %.1f  punch %.1f  mud %.1f\n   per 16th:",
                         w + 1, gs::windowLevelDb (ref, ref.detectedBpm, 0.0, w), prof.shakeDb(), prof.feelDb(),
                         prof.punchDb(), prof.mudDb());
            for (int s = 0; s < gs::kStepsPerBar; ++s)
                std::printf ("%s%5.1f", s % 4 == 0 ? " |" : "", prof.stepDb (s));
            const double barSec = 240.0 / ref.detectedBpm;
            const int start = (int) ((ref.firstDownbeatSec + w * barSec) * ref.sampleRate);
            const int len = (int) (juce::jmin (8, bars) * barSec * ref.sampleRate);
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            const auto m = gs::measureAudio (ref.audio, start, len, ref.sampleRate, 0.0, barSec / 4.0 * ref.sampleRate, 60.0f);
            std::printf ("\n   measure (%.0f ms): mono %.2f side %.1f dB | bass %s %.1f Hz (conf %.1f) | kick %s %.1f Hz (conf %.1f)"
                         " | key %s (%.2f) | crest %.1f dB | %.1f LUFS | peak %.1f dB\n",
                         juce::Time::getMillisecondCounterHiRes() - t1, m.monoCorr, m.sideDb,
                         gs::noteName (m.bassHz).toRawUTF8(), m.bassHz, m.bassConf, gs::noteName (m.kickHz).toRawUTF8(),
                         m.kickHz, m.kickConf, gs::keyName (m.keyIndex, m.keyMinor).toRawUTF8(), m.keyConf,
                         m.subCrestDb, m.lufs, m.peakDb);
        }
    }
    return 0;
}
