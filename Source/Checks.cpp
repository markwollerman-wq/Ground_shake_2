#include "Checks.h"
#include <cmath>
#include <algorithm>

namespace gs
{
namespace
{
    juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
    const juce::String arrow = U (" \xe2\x86\x92 ");          // " → "
    const juce::String dash  = U ("\xe2\x80\x93");             // "–"

    juce::String signedDb (float v, int dp = 1)
    {
        return (v >= 0 ? "+" : "-") + juce::String (std::abs (v), dp) + " dB";
    }
    juce::String absDb (float v, int dp = 1) { return juce::String (std::abs (v), dp) + " dB"; }
    int round5 (float v) { return (int) (std::round (v / 5.0f) * 5.0f); }

    float wrapSemis (float s)                 // into [-6, 6)
    {
        while (s >= 6.0f) s -= 12.0f;
        while (s < -6.0f) s += 12.0f;
        return s;
    }

    int maskFor (std::initializer_list<int> positions)     // a 16th within the beat, on all 4 beats
    {
        int m = 0;
        for (int p : positions)
            for (int b = 0; b < 4; ++b) m |= 1 << (b * 4 + p);
        return m;
    }
    // sub level of some 16th positions within the beat (0 = kick 16th), share of the mix in dB
    float zoneDb (const StepProfile& p, std::initializer_list<int> positions)
    {
        double sum = 0;
        int n = 0;
        for (int beat = 0; beat < 4; ++beat)
            for (int k : positions)
            {
                const auto st = (size_t) (beat * 4 + k);
                sum += p.feel[st] + kPunchWeight * p.punch[st];
                ++n;
            }
        return powerToDb ((float) (sum / std::max (1, n)) / std::max (1.0e-12f, p.fullTotal));
    }

    // "it's the roll, not the kick": which part of the beat carries the difference
    juce::String whoIsIt (const StepProfile& you, const StepProfile& ref, bool over)
    {
        const float k = zoneDb (you, { 0 }) - zoneDb (ref, { 0 });
        const float r = zoneDb (you, { 1, 2, 3 }) - zoneDb (ref, { 1, 2, 3 });
        const juce::String dir = over ? " over" : " under";
        auto f = [] (float v) { return juce::String (std::abs (v), 1) + " dB"; };
        if ((over ? r - k : k - r) >= 2.5f)
            return " It's mostly the roll, not the kick: the bass 16ths are " + f (r) + dir + ", the kick 16ths " + f (k) + ".";
        if ((over ? k - r : r - k) >= 2.5f)
            return " It's mostly the kick 16ths (" + f (k) + dir + "); the roll is " + f (r) + ".";
        return " The kick 16ths and the roll are both about the same amount" + dir + ".";
    }

    int beatBits (std::initializer_list<int> positions)
    {
        int m = 0;
        for (int p : positions) m |= 1 << p;
        return m;
    }
}

juce::String checkName (CheckId id)
{
    switch (id)
    {
        case CheckId::section:    return "Right section";
        case CheckId::kickType:   return "Kick type";
        case CheckId::bassPattern:return "Bass pattern";
        case CheckId::bassNotes:  return "Bass notes";
        case CheckId::mono:       return "Mono below 120 Hz";
        case CheckId::tuning:     return "Kick tuning";
        case CheckId::kickBass:   return "Kick / bass";
        case CheckId::beatShape:  return "Beat shape";
        case CheckId::subLevel:   return "Sub level";
        case CheckId::punch:      return "Kick punch";
        case CheckId::mud:        return "Low-mid mud";
        case CheckId::squash:     return "Limiter squash";
        case CheckId::djPlayback: return "DJ playback";
        case CheckId::masterTone:     return "Tone balance";
        case CheckId::masterWidth:    return "Stereo width";
        case CheckId::masterLowMono:  return "Low end mono";
        case CheckId::masterSquash:   return "Limiter squash";
        case CheckId::masterLoudness: return "Drop loudness";
        case CheckId::masterChain:    return "Master chain";
        case CheckId::numChecks:
        case CheckId::fineTune:
        case CheckId::loudnessGap:
        case CheckId::keyInfo:
        case CheckId::oneBeat:
        case CheckId::thinMud:
        default: break;
    }
    return {};
}

//==============================================================================
BeatShape beatShape (const StepProfile& you, const StepProfile& ref)
{
    BeatShape b;
    if (! you.valid || ! ref.valid || you.fullTotal <= 0 || ref.fullTotal <= 0) return b;
    std::array<float, 4> y {}, r {};
    for (int k = 0; k < 4; ++k)
    {
        double py = 0, pr = 0;
        for (int beat = 0; beat < 4; ++beat)
        {
            const auto s = (size_t) (beat * 4 + k);
            py += you.feel[s] + kPunchWeight * you.punch[s];
            pr += ref.feel[s] + kPunchWeight * ref.punch[s];
        }
        y[(size_t) k] = powerToDb ((float) (py / 4.0) / you.fullTotal);
        r[(size_t) k] = powerToDb ((float) (pr / 4.0) / ref.fullTotal);
    }
    // line each curve up on its 2nd-4th 16ths (the kick 16th is judged on its own)
    const float my = (y[1] + y[2] + y[3]) / 3.0f, mr = (r[1] + r[2] + r[3]) / 3.0f;
    for (size_t k = 0; k < 4; ++k)
    {
        b.you[k] = y[k] - my;
        b.ref[k] = r[k] - mr;
        b.diff[k] = b.you[k] - b.ref[k];
    }
    b.levelDb = my - mr;
    b.valid = true;
    return b;
}

//==============================================================================
void Coach::reset()
{
    tracks = {};
    hasLastCard = {};
    snapValid = false;
    snapHas = {};
}

void Coach::takeSnapshot()
{
    for (int i = 0; i < kNumChecks; ++i)
    {
        snapValues[(size_t) i] = tracks[(size_t) i].now;
        snapHas[(size_t) i] = (size_t) i < rows.size() && rows[(size_t) i].status != Status::waiting
                              && rows[(size_t) i].status != Status::info;
    }
    snapValid = true;
}

void Coach::update (const CoachInputs& in, double nowSec, bool holdForLoop)
{
    rows.clear();
    fixCards.clear();

    const bool refOk = in.haveRef && in.ref.valid;
    const bool youOk = in.haveYou && in.you.valid;
    const bool both = refOk && youOk;
    const bool bothM = both && in.youM.valid && in.refM.valid;
    const juce::String R = in.refName.isNotEmpty() ? in.refName : juce::String ("the reference");
    const double bpm = juce::jlimit (40.0, 300.0, in.hostBpm);
    const double loopSec = juce::jlimit (1, kBars, in.windowBars) * 240.0 / bpm;
    const bool bd = in.breakdownMode;
    const float tol = juce::jlimit (0.25f, 6.0f, in.tolerance);

    const int firstBar = in.windowStartBar + 1, lastBar = in.windowStartBar + in.windowBars;
    const bool onDrop = in.windowStartBar == in.dropBar;
    const bool onBreakdown = bd && in.windowStartBar == in.breakdownBar;
    const juce::String comparing = R + " " + dash + " " + (bd ? "breakdown " : (onDrop ? "drop " : juce::String()))
                                   + "(bars " + juce::String (firstBar) + dash + juce::String (lastBar) + ")";

    struct Raw
    {
        Status st = Status::waiting;
        float metric = 0;
        juce::String value;
        bool hasCard = false;
        FixCard card;
    };
    std::array<Raw, kNumChecks> raw {};
    std::vector<FixCard> info;

    auto grade = [] (float mag, float passBelow, float nearBelow)
    {
        return mag <= passBelow ? Status::pass : (mag <= nearBelow ? Status::near : Status::fail);
    };
    auto newCard = [&] (CheckId id, const juce::String& title, const juce::String& headline)
    {
        FixCard c;
        c.id = id;
        c.title = title;
        c.headline = headline;
        c.comparing = comparing;
        return c;
    };

    //--------------------------------------------------------------------------
    // 1. Right section
    {
        auto& r = raw[(size_t) CheckId::section];
        if (refOk)
        {
            r.metric = in.windowVsDropDb;
            r.st = (in.sectionDismissed || onDrop || in.pairMode) ? Status::pass : grade (-in.windowVsDropDb, 2.0f, 4.0f);
            r.value = in.pairMode ? in.pairLabel : bd ? juce::String ("breakdown") : onDrop ? juce::String ("drop")
                         : "bars " + juce::String (firstBar) + dash + juce::String (lastBar);
            juce::ignoreUnused (onBreakdown);
            if (r.st != Status::pass && bd)
            {
                const int d0 = in.dropBar + 1, d1 = in.dropBar + in.windowBars;
                auto c = newCard (CheckId::section, "COMPARING AGAINST THE BREAKDOWN", "almost no sub in this section");
                c.see = R + "'s bars " + juce::String (firstBar) + dash + juce::String (lastBar)
                        + " are a breakdown: the sub is " + absDb (in.ref.shakeDb()) + " down in the mix. If your loop is your drop, "
                          "compare it with " + R + "'s drop (bars " + juce::String (d0) + dash + juce::String (d1) + ") instead. "
                          "The other fixes wait until this one is sorted.";
                c.cause = "Breakdowns and drops need different advice: a breakdown is meant to lose its sub.";
                c.how.add ("Your loop is a drop: press FIND THE DROP.");
                c.how.add ("Your loop is your breakdown: press IT'S MY BREAKDOWN, and the cards switch to breakdown advice.");
                c.worked = "Comparing to reads \"drop\" or \"breakdown\".";
                c.offerFindDrop = c.offerDismiss = true;
                c.dismissLabel = "IT'S MY BREAKDOWN";
                r.hasCard = true;  r.card = c;
            }
            else if (r.st != Status::pass)
            {
                const int d0 = in.dropBar + 1, d1 = in.dropBar + in.windowBars;
                auto c = newCard (CheckId::section, "COMPARING AGAINST A QUIETER SECTION",
                                  absDb (in.windowVsDropDb) + " quieter than the drop");
                c.see = R + "'s bars " + juce::String (firstBar) + dash + juce::String (lastBar) + " are about "
                        + absDb (in.windowVsDropDb) + " quieter than its drop (bars " + juce::String (d0) + dash
                        + juce::String (d1) + "). Advice taken from an intro or breakdown can overcorrect your drop, "
                          "so the other fixes wait until this one is sorted.";
                c.cause = "The 8-bar window isn't on the reference's main drop.";
                c.how.add ("Press FIND THE DROP to jump the window to bars " + juce::String (d0) + dash + juce::String (d1) + ".");
                c.how.add ("If your loop is also an intro or breakdown, press THAT'S INTENDED.");
                c.worked = "Comparing to reads \"drop\".";
                c.offerFindDrop = c.offerDismiss = true;
                r.hasCard = true;  r.card = c;
            }
        }
    }

    //--------------------------------------------------------------------------
    // SOUND CHECK: is it the right KIND of kick and bass? (advice only: mixing can't fix these)
    if (bothM && in.youM.kick.valid && in.refM.kick.valid)
    {
        auto& r = raw[(size_t) CheckId::kickType];
        const auto& yk = in.youM.kick;
        const auto& rk = in.refM.kick;
        const int lenD = std::abs (yk.lengthClass() - rk.lengthClass());
        const int attD = std::abs (yk.attackClass() - rk.attackClass());
        const int pitD = std::abs (yk.pitchClass() - rk.pitchClass());
        const int score = lenD + attD + pitD + (yk.clicky() != rk.clicky() ? 1 : 0);
        const float ratio = rk.decayMs > 0 ? yk.decayMs / rk.decayMs : 1.0f;
        r.metric = (float) score;
        r.value = score == 0 ? juce::String ("matches") : juce::String (score) + (score == 1 ? " trait off" : " traits off");
        r.st = (score <= 1 && ratio > 0.65f && ratio < 1.5f) ? Status::pass : (score <= 2 ? Status::near : Status::fail);
        if (r.st != Status::pass)
        {
            juce::StringArray want;
            if (rk.lengthClass() < yk.lengthClass()) want.add ("a shorter decay (rings about " + juce::String (juce::roundToInt (rk.decayMs)) + " ms)");
            if (rk.lengthClass() > yk.lengthClass()) want.add ("a longer, ringing tail (about " + juce::String (juce::roundToInt (rk.decayMs)) + " ms)");
            if (rk.attackClass() < yk.attackClass()) want.add ("a harder, faster transient");
            if (rk.attackClass() > yk.attackClass()) want.add ("a rounder, softer hit");
            if (rk.clicky() && ! yk.clicky()) want.add ("more click on top");
            if (! rk.clicky() && yk.clicky()) want.add ("less click on top");
            if (rk.pitchClass() < yk.pitchClass()) want.add ("less pitch sweep (a flatter, tuned kick)");
            if (rk.pitchClass() > yk.pitchClass()) want.add ("more pitch sweep from the hit");
            auto c = newCard (CheckId::kickType, r.st == Status::fail ? "SWAP THE KICK" : "KICK CHARACTER DIFFERS",
                              "yours: " + yk.label() + "  |  " + R + ": " + rk.label());
            c.see = R + "'s kick is " + rk.label() + " (rings about " + juce::String (juce::roundToInt (rk.decayMs)) + " ms, attack "
                    + juce::String (juce::roundToInt (rk.riseMs)) + " ms, pitch drop " + juce::String (rk.pitchDropSt, 0) + " st). Yours is "
                    + yk.label() + " (" + juce::String (juce::roundToInt (yk.decayMs)) + " ms, " + juce::String (juce::roundToInt (yk.riseMs))
                    + " ms, " + juce::String (yk.pitchDropSt, 0) + " st).";
            c.cause = r.st == Status::fail ? "Different kick types: EQ and compression can't turn one into the other."
                                           : "Close, but part of the kick's character differs.";
            if (r.st == Status::fail)
            {
                c.how.add ("Swap the kick for one with " + want.joinIntoString (", ") + ".");
                c.how.add ("Audition kicks in Drum Rack with this section playing, and watch this card.");
                c.how.add ("Meanwhile, the Correction panel's ATTACK and TAIL controls can get you part of the way.");
            }
            else
            {
                c.how.add ("Look for " + want.joinIntoString (", ") + ", or shape it: a transient shaper or the kick's amp envelope.");
                c.how.add ("Or use the Correction panel's ATTACK and TAIL controls.");
            }
            c.worked = "The Kick type check ticks.";
            c.adviceOnly = true;
            c.highlightMask = maskFor ({ 0 });
            r.hasCard = true;  r.card = c;
        }
    }

    if (bothM && in.youM.kick.valid && in.refM.kick.valid)
    {
        auto& r = raw[(size_t) CheckId::bassPattern];
        const auto yp = in.youM.bassPattern, rp = in.refM.bassPattern;
        if (yp == BassPattern::unknown || rp == BassPattern::unknown)
        {
            r.st = Status::info;
            r.value = "unclear";
        }
        else
        {
            r.metric = yp == rp ? 0.0f : 1.0f;
            r.value = yp == rp ? bassPatternName (yp) : juce::String ("differs");
            r.st = yp == rp ? Status::pass : Status::fail;
            if (r.st != Status::pass)
            {
                auto c = newCard (CheckId::bassPattern, "BASS PATTERN DIFFERS",
                                  R + ": " + bassPatternName (rp) + "  |  yours: " + bassPatternName (yp));
                c.see = R + "'s bass plays " + bassPatternName (rp) + "; yours plays " + bassPatternName (yp)
                        + ". Mixing can't change the rhythm.";
                c.cause = "The bass MIDI (or the bass loop) has a different rhythm.";
                switch (rp)
                {
                    case BassPattern::rolling16:
                        c.how.add ("In your bass MIDI, put a note on 16ths 2, 3 and 4 of every beat, each just under a 16th long. Leave 16th 1 for the kick.");
                        c.how.add ("Serum: a short amp envelope (decay about one 16th) so each note speaks.");
                        break;
                    case BassPattern::rolling8:
                        c.how.add ("Put one note on the offbeat 8th (16th 3) of every beat, held to the end of the beat.");
                        break;
                    case BassPattern::offbeat:
                        c.how.add ("Put one short note on the offbeat (16th 3) of every beat and leave the rest empty.");
                        break;
                    case BassPattern::gallop:
                        c.how.add ("Put notes on 16ths 3 and 4 of every beat.");
                        break;
                    case BassPattern::reverse:
                        c.how.add ("Use a reverse bass: a sub note that swells up into each kick (reverse a bass hit, or a slow-attack envelope ending on the beat).");
                        break;
                    case BassPattern::sustained:
                        c.how.add ("Use one held sub note instead of separate notes, and let the sidechain shape it.");
                        break;
                    case BassPattern::none:
                        c.how.add (R + " leaves the space between the kicks empty here: mute the bass in this section.");
                        break;
                    case BassPattern::unknown:
                    default: break;
                }
                c.how.add ("Then re-check: the beat-shape and sub cards only make sense once the rhythm matches.");
                c.worked = "The Bass pattern check ticks.";
                c.adviceOnly = true;
                c.highlightMask = maskFor ({ 1, 2, 3 });
                c.beatMask = beatBits ({ 1, 2, 3 });
                c.beatGraph = true;
                r.hasCard = true;  r.card = c;
            }
        }
    }

    // NOTE CHECK: bass notes against your key and the reference's movement (advice only)
    if (bothM && in.youM.bassNotes.count > 0)
    {
        auto& r = raw[(size_t) CheckId::bassNotes];
        const auto& yn = in.youM.bassNotes;
        const auto& rn = in.refM.bassNotes;
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        auto nm = [] (int midi) { return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1); };
        auto pc = [] (int midi) { return juce::String (names[((midi % 12) + 12) % 12]); };
        const bool keyKnown = in.youM.keyIndex >= 0 && in.youM.keyConf >= 0.1f;    // only a clear key reading
        int outNote = -1;
        float outW = 0;
        if (keyKnown)
            for (int i = 0; i < yn.count; ++i)
                if (yn.weight[(size_t) i] >= 0.25f && ! scaleContains (in.youM.keyIndex, in.youM.keyMinor, yn.midi[(size_t) i]))
                { outNote = yn.midi[(size_t) i]; outW = yn.weight[(size_t) i]; break; }
        int yDistinct = 0, rDistinct = 0;
        for (int i = 0; i < yn.count; ++i) if (yn.weight[(size_t) i] >= 0.2f) ++yDistinct;
        for (int i = 0; i < rn.count; ++i) if (rn.weight[(size_t) i] >= 0.2f) ++rDistinct;
        juce::StringArray yParts;
        for (int i = 0; i < juce::jmin (2, yn.count); ++i) yParts.add (nm (yn.midi[(size_t) i]));
        r.value = yParts.joinIntoString ("/");
        r.metric = outNote >= 0 ? 2.0f : (rDistinct >= 2 && yDistinct == 1 ? 1.0f : 0.0f);
        r.st = outNote >= 0 ? Status::near : (rDistinct >= 2 && yDistinct == 1 ? Status::near : Status::pass);
        if (outNote >= 0)
        {
            int down = outNote - 1, up = outNote + 1;
            while (! scaleContains (in.youM.keyIndex, in.youM.keyMinor, down)) --down;
            while (! scaleContains (in.youM.keyIndex, in.youM.keyMinor, up)) ++up;
            auto c = newCard (CheckId::bassNotes, "BASS NOTE OUT OF KEY", nm (outNote) + " isn't in " + keyName (in.youM.keyIndex, in.youM.keyMinor));
            c.see = nm (outNote) + " (about " + juce::String (juce::roundToInt (outW * 100.0f)) + "% of your bass here) isn't in "
                    + keyName (in.youM.keyIndex, in.youM.keyMinor) + ", the key the rest of your track reads in. Your bass plays "
                    + yn.describe() + ".";
            c.cause = "A wrong note in the bass MIDI, or a bass patch tuned off.";
            c.how.add ("Move it to " + pc (down) + " or " + pc (up) + ", the nearest notes in the key.");
            c.how.add ("If it's on purpose (a passing note), ignore this card: it's a reading of the finished mix.");
            c.worked = "The Bass notes check ticks.";
            c.adviceOnly = true;
            r.hasCard = true;  r.card = c;
        }
        else if (r.st == Status::near)
        {
            juce::StringArray rParts;
            for (int i = 0; i < rn.count; ++i) if (rn.weight[(size_t) i] >= 0.2f) rParts.add (nm (rn.midi[(size_t) i]));
            auto c = newCard (CheckId::bassNotes, "YOUR BASS STAYS ON ONE NOTE",
                              R + " moves between " + rParts.joinIntoString (" and ") + "; yours stays on " + nm (yn.midi[0]));
            c.see = R + "'s bass moves between " + rParts.joinIntoString (" and ") + " here; yours stays on " + nm (yn.midi[0]) + ".";
            c.cause = "A one-note bass line where the reference has movement.";
            if (keyKnown)
            {
                const int root = in.youM.keyIndex;
                const int third = root + (in.youM.keyMinor ? 3 : 4), fifth = root + 7;
                c.how.add ("Add movement in the bass MIDI: try your key's 3rd (" + juce::String (names[third % 12]) + ") or 5th ("
                           + juce::String (names[fifth % 12]) + ") every other bar, or on the last beat of the bar.");
            }
            else
                c.how.add ("Add movement in the bass MIDI: a second note every other bar, or on the last beat of the bar.");
            c.how.add ("Optional: a one-note bass can be a choice. This card is a reading, not a rule.");
            c.worked = "The Bass notes check ticks.";
            c.adviceOnly = true;
            r.hasCard = true;  r.card = c;
        }
    }

    //--------------------------------------------------------------------------
    // 2. Mono below 120 Hz
    if (youOk && in.youM.valid)
    {
        auto& r = raw[(size_t) CheckId::mono];
        const float corr = in.youM.monoCorr;
        r.metric = corr;
        r.value = juce::String (corr, 2);
        r.st = corr >= 0.90f ? Status::pass : (corr >= 0.75f ? Status::near : Status::fail);
        if (r.st != Status::pass)
        {
            auto c = newCard (CheckId::mono, "LOW END ISN'T MONO", "correlation " + juce::String (corr, 2) + " below 120 Hz");
            c.see = "Below 120 Hz your left and right channels only line up " + juce::String (juce::roundToInt (corr * 100.0f))
                    + "%" + (in.refM.valid ? " (" + R + ": " + juce::String (juce::roundToInt (in.refM.monoCorr * 100.0f)) + "%)" : juce::String())
                    + ". On a mono club sub, part of your low end cancels out.";
            c.cause = "A wide or chorused sub layer, stereo unison on the bass oscillator, or a stereo reverb or delay reaching below 120 Hz.";
            c.how.add ("Put Ableton's Utility on the bass: turn on Bass Mono and set it to 120 Hz.");
            c.how.add ("Do the same on any other wide low layer (sub pads, reverb returns), or once on the master before the limiter.");
            c.how.add ("On the bass synth, turn off stereo unison or chorus for the sub oscillator.");
            c.worked = "The Mono check ticks (correlation 0.90 or higher).";
            c.tooFar = "You can't overdo this one.";
            c.watch = "The sub level can shift slightly once it's mono.";
            r.hasCard = true;  r.card = c;
        }
    }

    //--------------------------------------------------------------------------
    // 3. Kick tuning (an estimate from the finished mix: yellow at most, never red)
    if (bothM)
    {
        auto& r = raw[(size_t) CheckId::tuning];
        const auto& y = in.youM;
        const auto& f = in.refM;
        const bool clear = y.bassConf >= 5 && y.kickConf >= 4 && f.bassConf >= 5 && f.kickConf >= 4;
        if (! clear)
        {
            r.st = Status::info;
            r.value = "unclear";
        }
        else
        {
            const float yourRel = wrapSemis (semitonesBetween (y.bassHz, y.kickHz));
            const float refRel = wrapSemis (semitonesBetween (f.bassHz, f.kickHz));
            const float dev = wrapSemis (yourRel - refRel);
            r.metric = dev;
            r.value = (dev >= 0 ? "+" : "-") + juce::String (std::abs (dev), 1) + " st";
            r.st = std::abs (dev) < 0.6f ? Status::pass : Status::near;
            if (r.st != Status::pass)
            {
                const int move = -juce::roundToInt (dev);
                auto c = newCard (CheckId::tuning, "KICK MAY NOT BE TUNED TO YOUR BASS",
                                  "about " + juce::String (std::abs (dev), 1) + " semitones off");
                c.see = "Your kick seems to settle near " + noteName (y.kickHz) + " (" + juce::String (y.kickHz, 0)
                        + " Hz) over a bass on " + noteName (y.bassHz) + " (" + juce::String (y.bassHz, 0) + " Hz). "
                        + "Compared with how " + R + "'s kick sits on its bass, yours is about "
                        + juce::String (std::abs (dev), 1) + " semitones " + (dev > 0 ? "sharp." : "flat.");
                c.cause = "The kick sample's pitch doesn't follow the bass note.";
                c.how.add ("Tune the kick about " + juce::String (move > 0 ? "+" : "") + juce::String (move == 0 ? (dev > 0 ? -1 : 1) : move)
                           + " semitones (Simpler or Sampler: Transpose, or your kick synth's pitch).");
                if (y.keyIndex >= 0 && y.keyConf >= 0.05f)
                    c.how.add ("Your track reads in " + keyName (y.keyIndex, y.keyMinor) + ": a kick that settles on its root or 5th sits best.");
                c.how.add ("Listen with the bass: the kick's tail should blend into the bass note, not beat against it.");
                c.how.add ("This is an estimate from the finished mix. If your ears disagree, trust them.");
                c.worked = "The Kick tuning check ticks (within about half a semitone).";
                c.tooFar = "If the kick starts to sound thin or boomy, go back halfway.";
                c.watch = "A retuned kick can make a new clash on a different note.";
                r.hasCard = true;  r.card = c;
            }
        }
    }

    //--------------------------------------------------------------------------
    // 4 + 5. Kick/bass on the kick 16th, and the beat shape (the sidechain)
    const auto bs = beatShape (in.you, in.ref);
    if (both && bs.valid)
    {
        {
            auto& r = raw[(size_t) CheckId::kickBass];
            const float s0 = bs.diff[0];
            r.metric = s0;
            r.value = signedDb (s0);
            r.st = grade (std::abs (s0), 2.0f, 3.0f);
            if (r.st != Status::pass)
            {
                const int n = round5 (juce::jlimit (5.0f, 30.0f, 4.0f * std::abs (s0)));
                FixCard c;
                if (s0 > 0)
                {
                    c = newCard (CheckId::kickBass, "KICK AND BASS PILE UP ON THE KICK", signedDb (s0) + " on the 1st 16th");
                    c.see = "On the 1st 16th of every beat your sub is " + absDb (s0) + " fuller than " + R
                            + "'s (levels lined up). The bass is still sounding when the kick hits.";
                    c.cause = "The Kickstart 2 duck isn't deep enough right at the kick, or the kick's tail rings into the bass.";
                    c.how.add ("Kickstart 2: raise MIX about " + juce::String (n) + "% for a deeper duck at the kick.");
                    c.how.add ("Or pick a shape that starts right at the bottom on its left edge.");
                    c.how.add ("Or shorten the kick's decay so its tail ends before the bass comes back.");
                    c.tooFar = "The bars on 1, 5, 9 and 13 drop below " + R + "'s: lower MIX about 5% at a time.";
                }
                else
                {
                    c = newCard (CheckId::kickBass, "KICK 16th IS THIN", signedDb (s0) + " on the 1st 16th");
                    c.see = "On the 1st 16th of every beat your sub is " + absDb (s0) + " lighter than " + R + "'s (levels lined up).";
                    c.cause = "The duck is too deep right at the kick, or the bass is out of phase with the kick's tail and cancels it.";
                    c.how.add ("Kickstart 2: lower MIX about " + juce::String (n) + "%.");
                    c.how.add ("Flip the bass polarity (Utility: Phase L and R). Keep whichever makes the 1st-16th bars fuller.");
                    c.how.add ("Or let the kick's tail run a little longer.");
                    c.tooFar = "The bars on 1, 5, 9 and 13 grow taller than " + R + "'s: raise MIX about 5%.";
                }
                c.worked = "The 1st-16th bars (1, 5, 9, 13) line up with " + R + "'s.";
                c.watch = "A deeper or shallower duck also moves the 2nd 16th: watch Beat shape.";
                c.highlightMask = maskFor ({ 0 });
                c.beatMask = beatBits ({ 0 });
                c.beatGraph = true;
                r.hasCard = true;  r.card = c;
            }
        }
        {
            auto& r = raw[(size_t) CheckId::beatShape];
            const float s1 = bs.diff[1], s3 = bs.diff[3];
            const float lump = (bs.you[1] - bs.you[2]) - (bs.ref[1] - bs.ref[2]);
            struct Cand { int kind; float mag; float signedVal; };
            std::vector<Cand> cands {
                { 0, s3, s3 },          // too full before the kick
                { 1, -s3, s3 },         // ducking more than the reference
                { 2, -s1, s1 },         // coming back too late
                { 3, std::abs (lump) * 0.8f, lump } };   // a lumpy roll matters a little less
            auto worst = *std::max_element (cands.begin(), cands.end(), [] (auto& a, auto& b) { return a.mag < b.mag; });
            r.metric = worst.signedVal;
            r.st = grade (std::max (0.0f, worst.mag), 2.0f, 3.0f);
            r.value = r.st == Status::pass ? juce::String ("matches")
                                           : signedDb (worst.signedVal) + (worst.kind <= 1 ? " 4th" : (worst.kind == 2 ? " 2nd" : " roll"));
            if (r.st != Status::pass)
            {
                FixCard c;
                const float beatMs = (float) (60000.0 / bpm);
                if (worst.kind == 0)
                {
                    const float depth = juce::jlimit (3.0f, 26.0f, 2.0f * s3);
                    const int mix = juce::jlimit (50, 100, round5 (100.0f * (1.0f - std::pow (10.0f, -depth / 20.0f))));
                    c = newCard (CheckId::beatShape, "BASS RUNS INTO THE KICK", signedDb (s3) + " on the 4th 16th");
                    c.see = "Your bass is full right before every kick; " + R + " takes a breath there ("
                            + absDb (s3) + " lower, levels lined up).";
                    c.cause = "Kickstart 2 opens and stays open until the next kick.";
                    c.how.add ("Keep 1/4 and AUDIO.");
                    c.how.add ("Pick a shape whose right edge drops before the end (row 2, 3rd shape).");
                    c.how.add ("Set MIX to about " + juce::String (mix) + "% (about -" + juce::String (juce::roundToInt (depth))
                               + " dB at the bottom of the duck). Already on that shape? Raise MIX about 10%.");
                    c.how.add ("MIDI option: end each bass note at about 75% of the beat (" + juce::String (juce::roundToInt (beatMs * 0.75f))
                               + " ms after the kick at " + juce::String (bpm, 0) + " BPM).");
                    c.worked = "The ^ on 16ths 4, 8, 12 and 16 disappears and the 4th-16th bars match " + R + "'s.";
                    c.tooFar = "They turn into v: lower MIX about 10% at a time.";
                    c.highlightMask = maskFor ({ 3 });
                    c.beatMask = beatBits ({ 3 });
                }
                else if (worst.kind == 1)
                {
                    const int n = round5 (juce::jlimit (5.0f, 30.0f, 5.0f * std::abs (s3)));
                    c = newCard (CheckId::beatShape, "YOU'RE DUCKING MORE THAN THE REFERENCE", signedDb (s3) + " on the 4th 16th");
                    c.see = "Your 4th 16ths are " + absDb (s3) + " lighter than " + R
                            + "'s (levels lined up): you leave a gap before the kick that " + R + " doesn't.";
                    c.cause = "The Kickstart 2 shape drops too early or too deep before the kick.";
                    c.how.add ("Kickstart 2: lower MIX about " + juce::String (n) + "%.");
                    c.how.add ("Or pick a shape whose right edge drops later (closer to the end).");
                    c.how.add ("If the bass notes are short, lengthen them to the end of the beat.");
                    c.worked = "The v on 16ths 4, 8, 12 and 16 disappears.";
                    c.tooFar = "They turn into ^: raise MIX about 5%.";
                    c.highlightMask = maskFor ({ 3 });
                    c.beatMask = beatBits ({ 3 });
                }
                else if (worst.kind == 2)
                {
                    const int n = round5 (juce::jlimit (5.0f, 25.0f, 4.0f * std::abs (s1)));
                    c = newCard (CheckId::beatShape, "BASS COMES BACK TOO LATE AFTER THE KICK", signedDb (s1) + " on the 2nd 16th");
                    c.see = "Your 2nd 16ths are " + absDb (s1) + " lighter than " + R
                            + "'s (levels lined up): your bass is still ducked when " + R + " is already rolling.";
                    c.cause = "Kickstart 2's curve rises too slowly, or MIX is set very deep.";
                    c.how.add ("Kickstart 2: pick a shape with a steeper left edge (a faster rise).");
                    c.how.add ("Or lower MIX about " + juce::String (n) + "%.");
                    c.how.add ("Check that the bass notes start right on the 2nd 16th, not later.");
                    c.worked = "The 2nd-16th bars (2, 6, 10, 14) match " + R + "'s.";
                    c.tooFar = "The kick 16th starts to pile up (Kick / bass check): use a slightly slower rise.";
                    c.highlightMask = maskFor ({ 1 });
                    c.beatMask = beatBits ({ 1 });
                }
                else
                {
                    const float yourGap = bs.you[1] - bs.you[2], refGap = bs.ref[1] - bs.ref[2];
                    c = newCard (CheckId::beatShape, "YOUR ROLL IS LUMPY", "2nd vs 3rd 16th off by " + absDb (lump));
                    c.see = "In your roll the 2nd 16th sits " + absDb (yourGap) + (yourGap >= 0 ? " above" : " below")
                            + " the 3rd; in " + R + " it sits " + absDb (refGap) + (refGap >= 0 ? " above" : " below")
                            + ". The roll has a different weight from the reference's.";
                    c.cause = "The bass notes themselves: velocity, note length or envelope differ between the 16ths. "
                              "Not Kickstart: its curve is open on both.";
                    c.how.add ("Even out velocities: lower the " + juce::String (lump > 0 ? "2nd" : "3rd")
                               + "-16th bass notes by about " + absDb (lump * 0.7f) + " (or raise the other one).");
                    c.how.add ("Or give the bass gentle compression (about 2:1, fast attack) to even the roll.");
                    c.how.add ("Check the note lengths: a longer note on one 16th sounds louder.");
                    c.worked = "The 2nd- and 3rd-16th bars sit like " + R + "'s.";
                    c.tooFar = "The other 16th becomes the loud one: go back halfway.";
                    c.highlightMask = maskFor ({ 1, 2 });
                    c.beatMask = beatBits ({ 1, 2 });
                }
                c.beatGraph = true;
                c.watch = "Changing the duck moves the overall sub level: fix this before Sub level.";
                r.hasCard = true;  r.card = c;
            }
        }

        // one beat in each bar different (a fill or arrangement difference): information only
        {
            std::array<float, 4> e {};
            for (int beat = 0; beat < 4; ++beat)
            {
                double py = 0, pr = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const auto s = (size_t) (beat * 4 + k);
                    py += in.you.feel[s] + kPunchWeight * in.you.punch[s];
                    pr += in.ref.feel[s] + kPunchWeight * in.ref.punch[s];
                }
                e[(size_t) beat] = powerToDb ((float) py / in.you.fullTotal) - powerToDb ((float) pr / in.ref.fullTotal);
            }
            const float mean = (e[0] + e[1] + e[2] + e[3]) / 4.0f;
            int odd = -1, calm = 0;
            for (int b = 0; b < 4; ++b)
            {
                const float x = std::abs (e[(size_t) b] - mean);
                if (x >= 3.0f) odd = (odd == -1 ? b : 99);
                else if (x <= 1.5f) ++calm;
            }
            if (odd >= 0 && odd < 4 && calm == 3)
            {
                auto c = newCard (CheckId::oneBeat, "BEAT " + juce::String (odd + 1) + " OF EACH BAR DIFFERS",
                                  signedDb (e[(size_t) odd] - mean) + " on beat " + juce::String (odd + 1));
                c.severity = Status::info;
                c.see = "Beat " + juce::String (odd + 1) + " carries " + absDb (e[(size_t) odd] - mean)
                        + (e[(size_t) odd] > mean ? " more" : " less") + " sub than " + R + "'s, while the other beats match.";
                c.cause = "Usually a fill, a drum edit or an arrangement difference, not a mix problem.";
                c.how.add ("Nothing to fix unless it sounds wrong. Listen to beat " + juce::String (odd + 1) + " with A/B.");
                c.highlightMask = 0xF << (odd * 4);
                info.push_back (c);
            }
        }
    }

    //--------------------------------------------------------------------------
    // 6. Sub level
    if (both && bd)
    {
        // Breakdown mode: the reference has (almost) no sub here. Compare loosely, and say HOW the
        // reference muted its kick and bass, from its band shares.
        auto& r = raw[(size_t) CheckId::subLevel];
        const float d = in.you.shakeDb() - in.ref.shakeDb();
        r.metric = d;
        r.value = signedDb (d);
        const bool bothNone = in.you.shakeDb() <= -30.0f && in.ref.shakeDb() <= -25.0f;
        r.st = (bothNone || std::abs (d) <= 3.0f) ? Status::pass : (std::abs (d) <= 6.0f ? Status::near : Status::fail);
        if (r.st != Status::pass)
        {
            const bool haveBands = in.youM.valid && in.refM.valid;
            const auto& ys = in.youM.bandDb;
            const auto& rs = in.refM.bandDb;
            using B = Measurements::Band;
            const bool refHP = haveBands && rs[B::bSub] <= -30.0f && rs[B::bBody] - rs[B::bSub] >= 20.0f;
            const bool youLP = haveBands && ys[B::bHigh] - rs[B::bHigh] <= -6.0f && ys[B::bSub] - rs[B::bSub] >= 10.0f;
            // how deep the reference's low cut goes: compare its punch zone with its body
            const juce::String cut = ! haveBands                             ? "about 120-150 Hz"
                                   : rs[B::bPunch] >= rs[B::bBody] - 10.0f ? "about 60-80 Hz"
                                   : rs[B::bPunch] >= rs[B::bBody] - 15.0f ? "about 120-150 Hz"
                                                                           : "about 150-200 Hz";
            auto bandText = [] (float v) { return juce::String (juce::roundToInt (v)) + " dB"; };
            FixCard c;
            if (d > 0)
            {
                if (youLP && refHP)
                {
                    c = newCard (CheckId::subLevel, "YOU'RE LOW-PASSING; THE REFERENCE HIGH-PASSES", signedDb (d) + " sub vs " + R + "'s breakdown");
                    c.see = R + "'s breakdown keeps almost no sub (20-60 Hz: " + bandText (rs[B::bSub]) + " of the mix) and puts its weight at 120-300 Hz ("
                            + bandText (rs[B::bBody]) + "). Yours keeps its sub (" + bandText (ys[B::bSub]) + ") and loses its highs (2-10 kHz: "
                            + bandText (ys[B::bHigh]) + " vs " + bandText (rs[B::bHigh]) + "): that's a low-pass. The more you close a low-pass, "
                              "the more of what's left is sub, so this reading goes up.";
                    c.cause = "A low-pass muffles by removing the top, which leaves the sub in charge. " + R + " mutes the other way: it removes the bottom.";
                    c.how.add ("On the kick and the rolling bass, swap the low-pass for a high-pass: EQ Eight low cut, 24 dB/oct, at " + cut + ".");
                    c.how.add ("Want some muffle as well? Add a gentle high cut around 3 kHz, or use Auto Filter in band-pass.");
                    c.how.add ("Automate the filter off at the drop, so the full sub comes back at once.");
                    c.how.add ("Turning the fader down won't fix this: the sub's share of the mix stays the same.");
                }
                else if (refHP)
                {
                    c = newCard (CheckId::subLevel, "YOUR MUTED KICK AND BASS STILL CARRY SUB", signedDb (d) + " sub vs " + R + "'s breakdown");
                    c.see = R + "'s breakdown keeps almost no sub (20-60 Hz: " + bandText (rs[B::bSub]) + " of the mix) and puts its weight at 120-300 Hz ("
                            + bandText (rs[B::bBody]) + "). Yours still has sub at " + bandText (ys[B::bSub]) + ".";
                    c.cause = R + " mutes its kick and bass with a high-pass: the thump stays, the sub goes.";
                    c.how.add ("High-pass the kick and the rolling bass in the breakdown: EQ Eight low cut, 24 dB/oct, at " + cut + ".");
                    c.how.add ("Still reading over? Raise the cutoff a little, or use the steeper 48 dB/oct slope.");
                    c.how.add ("Automate it off at the drop, so the full sub comes back at once.");
                    c.how.add ("Lowering the fader helps less than a filter: the sub's share of the mix barely changes.");
                }
                else
                {
                    c = newCard (CheckId::subLevel, "TOO MUCH SUB FOR THE BREAKDOWN", signedDb (d) + " sub vs " + R + "'s breakdown");
                    c.see = "Your breakdown carries " + absDb (d) + " more sub than " + R + "'s, as a share of the mix."
                            + whoIsIt (in.you, in.ref, true);
                    c.cause = R + " pulls its kick and bass back further in this section.";
                    c.how.add ("Lower the kick and bass in the breakdown about " + absDb (d) + ", or high-pass them (EQ Eight low cut around 100 Hz).");
                    c.how.add ("Automate it back at the drop.");
                }
                c.tooFar = "The breakdown sounds like it has no kick at all: bring the cutoff down a little.";
            }
            else
            {
                c = newCard (CheckId::subLevel, "THE REFERENCE KEEPS MORE SUB IN ITS BREAKDOWN", signedDb (d) + " sub vs " + R + "'s breakdown");
                c.see = "Your breakdown has " + absDb (d) + " less sub than " + R + "'s, as a share of the mix." + whoIsIt (in.you, in.ref, false);
                c.cause = "Your breakdown filter or level takes out more low end than " + R + "'s.";
                c.how.add ("Open the filter on the kick and bass a little (lower the low-cut frequency), or bring them up about " + absDb (d) + ".");
                c.tooFar = "The breakdown loses its contrast with the drop: go back halfway.";
            }
            c.worked = "Sub level reads within 3 dB of " + R + "'s breakdown.";
            c.watch = "Check the drop afterwards: the filter must be fully off there.";
            r.hasCard = true;  r.card = c;
        }
    }
    else if (both)
    {
        auto& r = raw[(size_t) CheckId::subLevel];
        const float d = in.you.shakeDb() - in.ref.shakeDb();
        r.metric = d;
        r.value = signedDb (d);
        r.st = std::abs (d) <= tol ? Status::pass : (std::abs (d) <= 3.0f ? Status::near : Status::fail);
        if (r.st != Status::pass)
        {
            FixCard c;
            if (d > 0)
            {
                c = newCard (CheckId::subLevel, "SUB TOO LOUD", signedDb (d) + " vs " + R);
                c.see = "Your sub is " + absDb (d) + " over " + R + "'s across the whole loop, as a share of the mix, so loudness can't fool it."
                        + whoIsIt (in.you, in.ref, true);
                c.cause = "The bass or sub layer is turned up too far, or the kick's low end is heavy.";
                c.how.add ("Bass fader about " + signedDb (-d) + " (or the sub layer, if you have one).");
                c.how.add ("Or Kickstart 2: raise MIX about 5-10% to duck a little more.");
                c.how.add ("If the 1st-16th bars are what's tall, lower the kick instead.");
            }
            else
            {
                c = newCard (CheckId::subLevel, "SUB TOO QUIET", signedDb (d) + " vs " + R);
                c.see = "Your sub is " + absDb (d) + " under " + R + "'s across the whole loop, as a share of the mix, so loudness can't fool it."
                        + whoIsIt (in.you, in.ref, false);
                c.cause = "The bass or sub layer is too low, or the duck takes out too much.";
                c.how.add ("Bass fader about " + signedDb (-d) + " (or the sub layer, if you have one).");
                c.how.add ("Or Kickstart 2: lower MIX about 5-10% to duck a little less.");
                c.how.add ("Check the step grid for dim bars (gaps): filling them adds sub without more level.");
                c.watch = "Raising the bass can add limiter squash and mud: watch those checks.";
            }
            c.worked = "Your foot turns green (within " + juce::String (tol, 1) + " dB).";
            c.tooFar = "It flips to the other side by more than " + juce::String (tol, 1) + " dB: go back halfway.";
            r.hasCard = true;  r.card = c;
        }
        else if (std::abs (d) > 0.5f)
        {
            const int n = juce::jlimit (2, 10, juce::roundToInt (std::abs (d) * 5.0f));
            auto c = newCard (CheckId::fineTune, "FINE-TUNE THE LAST dB", signedDb (d) + " vs " + R);
            c.severity = Status::info;
            c.see = "You're inside the green zone, " + absDb (d) + (d > 0 ? " over " : " under ") + R + ".";
            c.cause = "Optional: closing the last fraction.";
            c.how.add ("Bass fader about " + signedDb (-d) + ", or Kickstart 2 MIX about " + juce::String (d > 0 ? "+" : "-")
                       + juce::String (n) + "%.");
            c.how.add ("Let the loop play two or three passes after each move before you read it.");
            c.how.add ("Finer than 0.5 dB is below the loop-to-loop variation: no need to chase it.");
            info.push_back (c);
        }
    }

    //--------------------------------------------------------------------------
    // 7. Kick punch (the punch zone)
    if (both && bd)
    {
        auto& r = raw[(size_t) CheckId::punch];
        const float d = in.you.punchDb() - in.ref.punchDb();
        r.metric = d;
        r.value = signedDb (d);
        r.st = (std::abs (d) <= 3.0f || (in.you.punchDb() <= -25.0f && in.ref.punchDb() <= -20.0f)) ? Status::pass
             : (std::abs (d) <= 6.0f ? Status::near : Status::fail);
        // the Sub level card already explains the filter; no second card
    }
    else if (both)
    {
        auto& r = raw[(size_t) CheckId::punch];
        const float d = in.you.punchDb() - in.ref.punchDb();
        const float thr = std::max (1.5f, tol * 1.5f);
        r.metric = d;
        r.value = signedDb (d);
        r.st = std::abs (d) <= thr ? Status::pass : (std::abs (d) <= thr + 2.0f ? Status::near : Status::fail);
        if (r.st != Status::pass)
        {
            auto c = newCard (CheckId::punch, d > 0 ? "KICK PUNCH TOO LOUD" : "KICK PUNCH TOO QUIET", signedDb (d) + " in the punch zone");
            c.see = "The punch zone (kick thump and bass body) is " + absDb (d) + (d > 0 ? " over " : " under ") + R
                    + "'s, as a share of the mix.";
            c.cause = d > 0 ? "A heavy kick body, or a bass with a lot of 60-120 Hz." : "A thin kick, or a bass low-passed too far.";
            if (d > 0)
            {
                c.how.add ("Lower the kick about " + absDb (d) + ", or cut about " + absDb (d) + " around 70-90 Hz on the kick.");
                c.how.add ("If the bass carries a lot of 60-120 Hz, cut there on the bass instead.");
            }
            else
            {
                c.how.add ("Raise the kick about " + absDb (d) + ", or boost about " + absDb (d) + " around 70-90 Hz on the kick.");
                c.how.add ("Or let more of the bass's body through: open its low-pass a little.");
            }
            c.worked = "The Kick punch check ticks.";
            c.tooFar = "It reads the other way by more than " + juce::String (thr, 1) + " dB: go back halfway.";
            c.watch = "Kick changes also move Kick / bass and Sub level.";
            c.highlightMask = maskFor ({ 0 });
            r.hasCard = true;  r.card = c;
        }
    }

    //--------------------------------------------------------------------------
    // 8. Low-mid mud
    if (both)
    {
        auto& r = raw[(size_t) CheckId::mud];
        const float dm = in.you.mudDb() - in.ref.mudDb();
        r.metric = dm;
        r.value = signedDb (dm);
        r.st = dm <= 2.0f ? Status::pass : (dm <= 4.0f ? Status::near : Status::fail);
        if (r.st != Status::pass)
        {
            std::array<std::pair<float, int>, kStepsPerBar> per {};
            for (int s = 0; s < kStepsPerBar; ++s)
                per[(size_t) s] = { in.you.stepMudDb (s) - in.ref.stepMudDb (s), s };
            std::sort (per.begin(), per.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            int mask = 0;
            juce::StringArray where;
            for (int i = 0; i < 3; ++i) { mask |= 1 << per[(size_t) i].second; where.add (juce::String (per[(size_t) i].second + 1)); }
            auto c = newCard (CheckId::mud, "LOW-MID MUD", signedDb (dm) + " vs " + R);
            c.see = "Your 150-400 Hz low-mids are " + absDb (dm) + " over " + R + "'s, as a share of the mix (worst on 16ths "
                    + where.joinIntoString (", ") + "). Too much low-mid makes even a big sub sound woolly.";
            c.cause = "Bass harmonics, kick body, pads, reverb tails and plucks piling up around 200-300 Hz.";
            c.how.add ("Cut 2-3 dB around 250 Hz on the bass or pads, not the master.");
            c.how.add ("High-pass reverb and delay returns at about 200 Hz.");
            c.how.add ("Solo tracks to find the one that fills 150-400 Hz: Ground Shake hears only the finished master.");
            c.worked = "The Mud check ticks (within 2 dB of " + R + ").";
            c.tooFar = "The bass vanishes on small speakers: give a little back.";
            c.watch = "Cutting mud can tempt a sub boost: check Limiter squash after.";
            c.highlightMask = mask;
            r.hasCard = true;  r.card = c;
        }
        else if (dm <= -4.0f)
        {
            auto c = newCard (CheckId::thinMud, "THINNER LOW-MIDS THAN THE REFERENCE", signedDb (dm) + " vs " + R);
            c.severity = Status::info;
            c.see = "Your 150-400 Hz low-mids are " + absDb (dm) + " under " + R + "'s. That's clean, but the bass may sound smaller on small speakers.";
            c.cause = "Information only: a choice, not a mistake.";
            c.how.add ("If the bass disappears on laptop speakers, add a little 150-300 Hz to the bass (saturation or a gentle boost).");
            info.push_back (c);
        }
    }

    //--------------------------------------------------------------------------
    // 9. Limiter squash (sub crest compared with the reference)
    if (bothM)
    {
        auto& r = raw[(size_t) CheckId::squash];
        const float sq = in.refM.subCrestDb - in.youM.subCrestDb;
        r.metric = sq;
        r.value = sq > 0 ? signedDb (-sq) : juce::String ("OK");
        r.st = grade (sq, 2.0f, 4.0f);
        if (r.st != Status::pass)
        {
            auto c = newCard (CheckId::squash, "LIMITER IS FLATTENING THE SUB", "sub peaks " + absDb (sq) + " flatter than " + R);
            c.see = "Your sub hits stand " + absDb (sq) + " less above their average than " + R
                    + "'s: the limiter is squashing the kick and bass peaks.";
            c.cause = "Too much gain into the limiter, or a fast limiter catching every kick.";
            c.how.add ("Lower the limiter's input gain about " + absDb (sq) + ".");
            c.how.add ("Or lower the sub about 1-2 dB so the limiter works less.");
            c.how.add ("Or put a gentle clipper on the kick before the limiter so it takes the peaks instead.");
            c.worked = "The Limiter squash check ticks (within 2 dB of " + R + "'s punch).";
            c.tooFar = "Too little limiting makes you quieter than " + R + ": see the loudness card.";
            c.watch = "Less limiting means less loudness: rekordbox will turn you up more (DJ playback).";
            r.hasCard = true;  r.card = c;
        }
    }

    //--------------------------------------------------------------------------
    // 10. DJ playback (rekordbox Auto Gain), plus the loudness gap
    if (youOk && in.youM.valid && in.youM.lufs > -60.0f)
    {
        auto& r = raw[(size_t) CheckId::djPlayback];
        const float gain = in.djRefLevel - in.youM.lufs;
        const float over = in.youM.peakDb + gain;
        r.metric = over;
        r.value = over > 0 ? signedDb (over) + " over" : juce::String ("OK");
        r.st = grade (over, 1.0f, 3.0f);
        if (r.st != Status::pass)
        {
            auto c = newCard (CheckId::djPlayback, "DJ PLAYBACK: REKORDBOX WILL SQUASH YOUR SUB",
                              "peaks " + signedDb (over) + " over 0 dBFS");
            c.see = "rekordbox Auto Gain (reference " + juce::String (in.djRefLevel, 0) + " dB) would boost your track about "
                    + signedDb (gain) + ", pushing your peaks about " + absDb (over) + " over 0 dBFS, where its limiter flattens the kick.";
            c.cause = "Your track measures quieter (" + juce::String (in.youM.lufs, 1) + " LUFS) than Auto Gain's target, but its peaks already sit near 0 dB.";
            c.how.add ("Get loudness with less sub peak: lower the sub about 1-2 dB, then push the limiter input about " + absDb (gain) + ".");
            c.how.add ("Or lower the sub about " + absDb (over) + ".");
            c.how.add ("Estimate only: rekordbox's analysis differs a little from LUFS.");
            c.worked = "The DJ playback check ticks (under 1 dB over).";
            c.watch = "Pushing the limiter adds squash: watch Limiter squash.";
            r.hasCard = true;  r.card = c;
        }

        if (in.refM.valid && in.refM.lufs > -60.0f)
        {
            const float gap = in.refM.lufs - in.youM.lufs;        // > 0: you're quieter
            if (std::abs (gap) >= 1.0f)
            {
                auto c = newCard (CheckId::loudnessGap, gap > 0 ? "QUIETER THAN THE REFERENCE" : "LOUDER THAN THE REFERENCE",
                                  absDb (gap) + (gap > 0 ? " quieter" : " louder") + " than " + R);
                c.severity = Status::info;
                c.see = "Your loop measures " + juce::String (in.youM.lufs, 1) + " LUFS; " + R + "'s window measures "
                        + juce::String (in.refM.lufs, 1) + " LUFS. rekordbox Auto Gain will turn yours "
                        + (gap > 0 ? "up" : "down") + " about " + absDb (gap) + " more than " + R
                        + ": that's the extra trim you'd need.";
                c.cause = "Loudness, not the sub: the sub reading is level-matched and ignores this on purpose.";
                if (gap > 0)
                {
                    c.how.add ("To close it at the source, raise the limiter's input gain about " + absDb (gap) + ".");
                    c.how.add ("Watch Limiter squash and DJ playback while you push.");
                }
                else
                {
                    c.how.add ("If you want to sit level with " + R + ", lower the limiter's input about " + absDb (gap) + ".");
                    c.how.add ("Less limiting also gives the sub its punch back (Limiter squash). Louder is fine if Squash and DJ playback stay green.");
                }
                info.push_back (c);
            }
        }
    }

    // key: information only
    if (bothM && in.youM.keyIndex >= 0 && in.refM.keyIndex >= 0 && in.youM.keyConf >= 0.05f && in.refM.keyConf >= 0.05f
        && (in.youM.keyIndex != in.refM.keyIndex || in.youM.keyMinor != in.refM.keyMinor))
    {
        auto c = newCard (CheckId::keyInfo, "DIFFERENT KEY FROM THE REFERENCE",
                          keyName (in.youM.keyIndex, in.youM.keyMinor) + " vs " + keyName (in.refM.keyIndex, in.refM.keyMinor));
        c.severity = Status::info;
        c.see = "You: " + keyName (in.youM.keyIndex, in.youM.keyMinor) + ". " + R + ": " + keyName (in.refM.keyIndex, in.refM.keyMinor) + ".";
        c.cause = "Information only: tracks in different keys can still match their low end.";
        c.how.add ("Nothing to fix. A different bass note does change how deep the sub feels, which the Range knob's Auto setting allows for.");
        info.push_back (c);
    }

    // Breakdown mode: ducking, kick tuning and limiter-squash advice don't apply to a section
    // that's meant to have no sub
    if (bd)
        for (auto id : { CheckId::kickType, CheckId::bassNotes, CheckId::tuning, CheckId::kickBass, CheckId::beatShape,
                         CheckId::squash, CheckId::djPlayback })
        {
            auto& r = raw[(size_t) id];
            if (r.st == Status::waiting) continue;
            r.st = Status::info;
            r.value = "breakdown";
            r.hasCard = false;
        }

    //--------------------------------------------------------------------------
    // hold-for-one-loop ticking, progress, rows and cards
    float scoreSum = 0;
    int scored = 0;
    bool firstSet = false;
    for (int i = 0; i < kNumChecks; ++i)
    {
        auto& r = raw[(size_t) i];
        auto& t = tracks[(size_t) i];
        const auto id = (CheckId) i;
        Status shown = r.st;
        t.now = r.metric;

        if (r.st == Status::pass)
        {
            if (t.passSince < 0) t.passSince = nowSec;
            t.ticked = t.ticked || ! holdForLoop || (nowSec - t.passSince) >= loopSec;
            shown = t.ticked ? Status::pass : Status::holding;
        }
        else
        {
            t.passSince = -1.0;
            t.ticked = false;
        }
        if ((r.st == Status::near || r.st == Status::fail) && ! t.hasStart)
        {
            t.hasStart = true;
            t.start = r.metric;
        }

        rows.push_back ({ id, checkName (id), shown, r.value });

        if (shown == Status::pass || shown == Status::waiting || shown == Status::info) {}
        else
        {
            FixCard c;
            if (r.hasCard)
            {
                c = r.card;
                c.severity = r.st;
                lastCard[(size_t) i] = c;
                hasLastCard[(size_t) i] = true;
            }
            else if (shown == Status::holding && hasLastCard[(size_t) i])
            {
                c = lastCard[(size_t) i];
                c.severity = Status::holding;
            }
            else
                c.id = CheckId::numChecks;      // nothing to show

            if (c.id != CheckId::numChecks)
            {
            // progress: snapshot (or where this card started) -> now
            {
                const bool useSnap = snapValid && snapHas[(size_t) i];
                const float base = useSnap ? snapValues[(size_t) i] : t.start;
                auto fmt = [id] (float v)
                {
                    if (id == CheckId::mono) return juce::String (v, 2);
                    if (id == CheckId::tuning) return (v >= 0 ? "+" : "-") + juce::String (std::abs (v), 1) + " st";
                    return signedDb (v);
                };
                // how far from the target a reading is, so "moving the wrong way" can be spotted
                auto badness = [id] (float v)
                {
                    if (id == CheckId::mono) return (1.0f - v) * 20.0f;
                    if (id == CheckId::section) return -v;
                    if (id == CheckId::squash || id == CheckId::djPlayback) return std::max (0.0f, v);
                    return std::abs (v);
                };
                if (t.hasStart && badness (r.metric) > badness (t.start) + 1.0f)
                    c.worse = "Getting worse since this card appeared (" + fmt (t.start) + arrow + fmt (r.metric)
                              + "): undo the last change, or try the other direction.";
                if ((t.hasStart || useSnap) && std::abs (base - r.metric) >= 0.05f)
                    c.progress = (useSnap ? "snapshot " : juce::String()) + fmt (base) + arrow + fmt (r.metric)
                                 + (shown == Status::holding ? U (" \xe2\x9c\x93 holding for one loop pass") : juce::String());
            }
            if (! firstSet && shown != Status::holding)
            {
                c.fixFirst = true;
                firstSet = true;
            }
            fixCards.push_back (c);
            }
        }
        switch (shown)
        {
            case Status::pass:    scoreSum += 1.0f; ++scored; break;
            case Status::holding: scoreSum += 0.9f; ++scored; break;
            case Status::near:    scoreSum += 0.5f; ++scored; break;
            case Status::fail:    ++scored; break;
            case Status::waiting:
            case Status::info:
            default: break;
        }
    }
    // comparing against the wrong section: every other card would be misleading, so only that one shows
    if (raw[(size_t) CheckId::section].st == Status::near || raw[(size_t) CheckId::section].st == Status::fail)
        fixCards.erase (std::remove_if (fixCards.begin(), fixCards.end(),
                                        [] (const FixCard& c) { return c.id != CheckId::section; }),
                        fixCards.end());
    else
        for (auto& c : info)
        {
            // a breakdown loop's loudness and thin low-mids are meant to differ from the track's
            if (bd && (c.id == CheckId::loudnessGap || c.id == CheckId::thinMud)) continue;
            fixCards.push_back (c);
        }
    percent = scored >= 3 ? 100.0f * scoreSum / (float) scored : -1.0f;

    // one-line status for the header
    if (! refOk)          status = "Load a reference track (or drop it here), then play your loop.";
    else if (! youOk)     status = "Play your loop in Ableton. Ground Shake listens after the limiter.";
    else
    {
        const FixCard* top = nullptr;
        for (auto& c : fixCards) if (c.fixFirst) { top = &c; break; }
        if (top != nullptr)
            status = "Fix this first: " + top->title.substring (0, 1) + top->title.substring (1).toLowerCase()
                     + " (" + top->headline + ").";
        else
        {
            bool anyHolding = false;
            for (auto& rw : rows) anyHolding = anyHolding || rw.status == Status::holding;
            status = anyHolding ? "Looking good: holding each fix for one loop pass before it ticks."
                                : "Your feet shake like the reference. Locked in.";
        }
        if (bd && raw[(size_t) CheckId::section].st == Status::pass) status = "Breakdown mode. " + status;
    }
}
} // namespace gs
