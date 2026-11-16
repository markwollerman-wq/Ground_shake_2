#include "PluginEditor.h"

using namespace gsui;

//==============================================================================
//  Palette helpers
//==============================================================================
void gsui::ledPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius, float glow)
{
    // glossy black body
    g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    // soft gloss on the top third
    auto gloss = r.withHeight (r.getHeight() * 0.35f).reduced (2.0f, 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), gloss.getX(), gloss.getY(),
                                             juce::Colours::white.withAlpha (0.0f), gloss.getX(), gloss.getBottom(), false));
    g.fillRoundedRectangle (gloss, radius - 2.0f);
    // red LED border with a glow
    for (auto [w, a] : { std::pair { 7.0f, 0.06f }, { 4.0f, 0.14f }, { 2.0f, 0.35f } })
    {
        g.setColour (red.withAlpha (a * glow));
        g.drawRoundedRectangle (r, radius, w);
    }
    g.setColour (red.withAlpha (0.95f * glow));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

gsui::Match gsui::classify (float d, float tol)
{
    if (std::abs (d) <= tol) return Match::matched;
    if (std::abs (d) <= 3.0f) return Match::slightlyOff;
    return d < 0 ? Match::wellUnder : Match::wellOver;
}

juce::Colour gsui::colourFor (Match m)
{
    switch (m)
    {
        case Match::matched:     return green;
        case Match::slightlyOff: return yellow;
        case Match::wellUnder:   return blue;
        case Match::wellOver:    return over;
        case Match::none:
        default:                 return muted;
    }
}

juce::String gsui::labelFor (Match m)
{
    switch (m)
    {
        case Match::matched:     return "MATCHED";
        case Match::slightlyOff: return "SLIGHTLY OFF";
        case Match::wellUnder:   return "WELL UNDER";
        case Match::wellOver:    return "WELL OVER";
        case Match::none:
        default:                 return "WAITING";
    }
}

juce::Colour gsui::colourFor (gs::Status s)
{
    switch (s)
    {
        case gs::Status::pass:    return green;
        case gs::Status::holding: return green.withAlpha (0.7f);
        case gs::Status::near:    return yellow;
        case gs::Status::fail:    return over;
        case gs::Status::info:
        case gs::Status::waiting:
        default:                  return grey;
    }
}

void LedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1,
                                       juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (size, size);
    const float ang = a0 + pos * (a1 - a0);
    const float arcR = size * 0.5f - 3.0f;
    // track + glowing value arc
    juce::Path track;
    track.addCentredArc (c.getCentreX(), c.getCentreY(), arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (juce::Colour (0xff222228));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path arc;
    arc.addCentredArc (c.getCentreX(), c.getCentreY(), arcR, arcR, 0.0f, a0, ang, true);
    const bool on = s.isEnabled();
    for (auto [wd, al] : { std::pair { 8.0f, 0.10f }, { 5.0f, 0.22f } })
    {
        g.setColour (red.withAlpha (on ? al : al * 0.4f));
        g.strokePath (arc, juce::PathStrokeType (wd, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (on ? red : redDim);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // glossy black knob
    auto knob = c.reduced (size * 0.2f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a31), knob.getCentreX(), knob.getY(),
                                             juce::Colour (0xff060608), knob.getCentreX(), knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (knob, 1.0f);
    // pointer
    const float pr = knob.getWidth() * 0.5f;
    const juce::Point<float> p0 (c.getCentreX() + std::sin (ang) * pr * 0.35f, c.getCentreY() - std::cos (ang) * pr * 0.35f);
    const juce::Point<float> p1 (c.getCentreX() + std::sin (ang) * pr * 0.85f, c.getCentreY() - std::cos (ang) * pr * 0.85f);
    g.setColour (red.withAlpha (0.35f));
    g.drawLine ({ p0, p1 }, 5.0f);
    g.setColour (on ? red.brighter (0.3f) : muted);
    g.drawLine ({ p0, p1 }, 2.0f);
}

void LedLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                    int thumbStart, int thumbSize, bool over, bool)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    g.setColour (juce::Colour (0xff121216));
    g.fillRoundedRectangle (r, 3.0f);
    auto t = vertical ? juce::Rectangle<float> (r.getX(), (float) thumbStart, r.getWidth(), (float) thumbSize)
                      : juce::Rectangle<float> ((float) thumbStart, r.getY(), (float) thumbSize, r.getHeight());
    g.setColour (red.withAlpha (over ? 0.8f : 0.5f));
    g.fillRoundedRectangle (t.reduced (1.0f), 3.0f);
}

LedLookAndFeel::LedLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::black);
    setColour (juce::Label::outlineWhenEditingColourId, red);
    setColour (juce::TextEditor::highlightColourId, redDim);
    setColour (juce::CaretComponent::caretColourId, red);
}

void LedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xff3a0a0a) : panelTop, r.getX(), r.getY(),
                                             on ? juce::Colour (0xff1a0303) : panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);
    const float glow = on ? 1.0f : (over ? 0.6f : 0.35f);
    if (on)
        for (auto [w, a] : { std::pair { 6.0f, 0.10f }, { 3.0f, 0.25f } })
        {
            g.setColour (red.withAlpha (a));
            g.drawRoundedRectangle (r, 6.0f, w);
        }
    g.setColour (red.withAlpha (glow * (down ? 1.0f : 0.9f)));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
}

void LedLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? juce::Colours::white : (b.isEnabled() ? text : muted.withAlpha (0.5f)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Font LedLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions (juce::jmin (15.0f, h * 0.48f), juce::Font::bold));
}

//==============================================================================
//  Feet
//==============================================================================
namespace
{
    // A right foot, sole view, toes up, in a 0..1 box
    juce::Path makeFoot()
    {
        juce::Path p;
        p.startNewSubPath (0.50f, 0.98f);
        p.cubicTo (0.27f, 0.98f, 0.21f, 0.82f, 0.25f, 0.64f);
        p.cubicTo (0.29f, 0.47f, 0.17f, 0.36f, 0.21f, 0.25f);
        p.cubicTo (0.25f, 0.14f, 0.70f, 0.12f, 0.79f, 0.26f);
        p.cubicTo (0.86f, 0.38f, 0.75f, 0.50f, 0.75f, 0.64f);
        p.cubicTo (0.75f, 0.82f, 0.73f, 0.98f, 0.50f, 0.98f);
        p.closeSubPath();
        const float toes[5][3] = { { 0.30f, 0.085f, 0.090f }, { 0.47f, 0.045f, 0.064f }, { 0.60f, 0.062f, 0.056f },
                                   { 0.71f, 0.105f, 0.048f }, { 0.795f, 0.170f, 0.042f } };
        for (auto& t : toes)
            p.addEllipse (t[0] - t[2], t[1] - t[2], t[2] * 2.0f, t[2] * 2.0f);
        return p;
    }

    float shakeFromDb (float db)                     // sub share in dB -> 0..1 shake
    {
        return juce::jlimit (0.0f, 1.0f, (db + 24.0f) / 18.0f);
    }
}

void FeetView::tick (double t)
{
    time = t;
    const bool youOn = proc.liveHasData() || ! proc.isHostPlaying();
    const float yt = youOn ? shakeFromDb (proc.getLiveSubDbNow()) : 0.0f;
    const float rt = proc.hasReference() ? shakeFromDb (proc.getRefSubDbNow()) : 0.0f;
    auto follow = [] (float cur, float target) { return cur + (target - cur) * (target > cur ? 0.6f : 0.12f); };
    youShake = follow (youShake, yt);
    refShake = follow (refShake, rt);
    repaint();
}

void FeetView::drawFoot (juce::Graphics& g, juce::Rectangle<float> area, bool leftFoot, juce::Colour c, float shake, double t)
{
    const float h = area.getHeight(), w = h * 0.62f;
    auto box = juce::Rectangle<float> (w, h).withCentre (area.getCentre());
    // the shake: fast jitter whose size follows the sub
    const float amp = shake * h * 0.035f;
    const float dx = amp * (float) std::sin (t * 2.0 * juce::MathConstants<double>::pi * 31.0)
                   + amp * 0.5f * (float) std::sin (t * 2.0 * juce::MathConstants<double>::pi * 47.0);
    const float dy = amp * 0.6f * (float) std::cos (t * 2.0 * juce::MathConstants<double>::pi * 37.0);
    const float rot = shake * 0.03f * (float) std::sin (t * 2.0 * juce::MathConstants<double>::pi * 23.0);

    auto path = makeFoot();
    auto tr = juce::AffineTransform::scale (leftFoot ? -1.0f : 1.0f, 1.0f).translated (leftFoot ? 1.0f : 0.0f, 0.0f)
                  .scaled (w, h)
                  .rotated (rot, w * 0.5f, h * 0.6f)
                  .translated (box.getX() + dx, box.getY() + dy);
    path.applyTransform (tr);

    // ground under the foot: vibration lines
    auto ground = juce::Rectangle<float> (area.getWidth() * 0.9f, h * 0.12f)
                      .withCentre ({ area.getCentreX(), area.getBottom() + h * 0.07f });
    for (int line = 0; line < 3; ++line)
    {
        juce::Path wave;
        const float y = ground.getY() + line * ground.getHeight() * 0.45f;
        const int pts = 48;
        for (int i = 0; i <= pts; ++i)
        {
            const float x = ground.getX() + ground.getWidth() * i / (float) pts;
            const float ph = (float) (t * (18.0 + line * 5.0)) + i * 0.7f;
            const float yy = y + std::sin (ph) * shake * (6.0f - line * 1.5f);
            i == 0 ? wave.startNewSubPath (x, yy) : wave.lineTo (x, yy);
        }
        g.setColour (c.withAlpha (0.15f + 0.5f * shake * (1.0f - line * 0.3f)));
        g.strokePath (wave, juce::PathStrokeType (1.4f));
    }

    juce::DropShadow (c.withAlpha (0.35f + 0.5f * shake), (int) (14 + 26 * shake), {}).drawForPath (g, path);
    g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), box.getCentreX(), box.getY(),
                                             c.darker (0.6f), box.getCentreX(), box.getBottom(), false));
    g.fillPath (path);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (path, juce::PathStrokeType (1.2f));
}

void FeetView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledPanel (g, r, 12.0f);

    const auto live = proc.getDisplayYouProfile();
    const auto& ref = proc.getDisplayRefProfile();
    const bool haveYou = live.valid, haveRef = proc.hasReference() && ref.valid;
    const float diff = haveYou && haveRef ? live.shakeDb() - ref.shakeDb() : 0.0f;
    const float tol = proc.getTolerance();
    const auto match = haveYou && haveRef ? classify (diff, tol) : Match::none;

    const float colW = r.getWidth() * 0.30f;
    auto leftCol = r.withWidth (colW).reduced (12.0f, 14.0f);
    auto rightCol = r.withTrimmedLeft (r.getWidth() - colW).reduced (12.0f, 14.0f);
    auto mid = r.reduced (colW, 14.0f);

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (muted);
    g.drawText ("YOUR MIX", leftCol.removeFromTop (18.0f), juce::Justification::centred);
    g.drawText ("REFERENCE", rightCol.removeFromTop (18.0f), juce::Justification::centred);

    const auto youColour = haveYou && haveRef ? colourFor (match) : (haveYou ? red : redDim);
    drawFoot (g, leftCol.reduced (10.0f, 8.0f).withTrimmedBottom (26.0f), true, youColour, youShake, time);
    drawFoot (g, rightCol.reduced (10.0f, 8.0f).withTrimmedBottom (26.0f), false, haveRef ? red : redDim, refShake, time);

    // centre read-out
    g.setColour (muted);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("MATCH", mid.removeFromTop (22.0f), juce::Justification::centred);

    auto big = mid.removeFromTop (mid.getHeight() * 0.42f);
    g.setFont (juce::FontOptions (54.0f, juce::Font::bold));
    if (match == Match::none)
    {
        g.setColour (muted.withAlpha (0.6f));
        g.drawText ("--", big, juce::Justification::centred);
    }
    else
    {
        g.setColour (colourFor (match));
        g.drawText (juce::String (diff >= 0 ? "+" : "") + juce::String (diff, 1) + " dB", big, juce::Justification::centred);
    }

    auto pill = mid.removeFromTop (34.0f).withSizeKeepingCentre (200.0f, 30.0f);
    const auto mc = colourFor (match);
    g.setColour (mc.withAlpha (0.15f));
    g.fillRoundedRectangle (pill, 15.0f);
    g.setColour (mc.withAlpha (0.9f));
    g.drawRoundedRectangle (pill, 15.0f, 1.4f);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (labelFor (match), pill, juce::Justification::centred);

    mid.removeFromTop (12.0f);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (muted);
    auto line = [] (bool ok, float v) { return ok ? juce::String (v, 1) + " dB" : juce::String ("--"); };
    g.drawText ("you " + line (haveYou, live.shakeDb()) + "    ref " + line (haveRef, ref.shakeDb()),
                mid.removeFromTop (20.0f), juce::Justification::centred);
    g.setFont (juce::FontOptions (11.5f));
    g.setColour (muted.withAlpha (0.8f));
    g.drawText ("sub share of the whole mix, level-matched  |  green = within " + juce::String (tol, tol < 1.0f ? 2 : 1) + " dB",
                mid.removeFromTop (18.0f), juce::Justification::centred);
}

//==============================================================================
//  16th step grid
//==============================================================================
void StepGridView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledPanel (g, r, 12.0f);
    auto inner = r.reduced (16.0f, 10.0f);

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (muted);
    auto title = inner.removeFromTop (20.0f);
    g.drawText ("16th STEP GRID", title, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("sub on every 16th, averaged over the loop   |   dim = gap / dropout   |   ^ / v = 3 dB+ above / below ref",
                title, juce::Justification::centredRight);

    const auto live = proc.getDisplayYouProfile();
    const auto& ref = proc.getDisplayRefProfile();
    const bool haveYou = live.valid, haveRef = proc.hasReference() && ref.valid;
    const auto& snap = proc.getSnapshotProfile();

    const float labelW = 46.0f;
    auto rows = inner.reduced (0.0f, 4.0f);
    const float rowH = rows.getHeight() / 2.0f;
    const float colW = (rows.getWidth() - labelW) / (float) gs::kStepsPerBar;
    const int current = (int) std::floor (std::fmod (proc.getHostPositionSteps(), 16.0));

    auto rowMax = [] (const gs::StepProfile& p)
    {
        float m = -100.0f;
        for (int s = 0; s < gs::kStepsPerBar; ++s) m = std::max (m, p.stepDb (s));
        return m;
    };
    const float youMax = haveYou ? rowMax (live) : 0.0f, refMax = haveRef ? rowMax (ref) : 0.0f;
    // both rows share one scale (the loudest step of either, 10 dB down to the floor) so you
    // can compare heights directly; quiet steps and gaps stand out
    const float top = std::max (haveYou ? youMax : -100.0f, haveRef ? refMax : -100.0f);
    const float range = 10.0f;

    for (int row = 0; row < 2; ++row)
    {
        const bool isYou = row == 0;
        const bool have = isYou ? haveYou : haveRef;
        const auto& p = isYou ? live : ref;
        auto rr = juce::Rectangle<float> (rows.getX(), rows.getY() + row * rowH, rows.getWidth(), rowH).reduced (0, 3.0f);
        g.setColour (isYou ? text : muted);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (isYou ? "YOU" : "REF", rr.withWidth (labelW), juce::Justification::centredLeft);

        for (int s = 0; s < gs::kStepsPerBar; ++s)
        {
            auto cell = juce::Rectangle<float> (rr.getX() + labelW + s * colW, rr.getY(), colW, rr.getHeight()).reduced (3.0f, 1.0f);
            g.setColour (juce::Colour (0xff111116));
            g.fillRoundedRectangle (cell, 3.0f);
            if ((highlight >> s) & 1)                       // "show steps" from the open fix card
            {
                g.setColour (yellow.withAlpha (0.12f));
                g.fillRoundedRectangle (cell.expanded (1.5f), 4.0f);
                g.setColour (yellow.withAlpha (0.55f));
                g.drawRoundedRectangle (cell.expanded (1.5f), 4.0f, 1.2f);
            }
            if (s == current && (proc.isHostPlaying() || ! haveYou))
            {
                g.setColour (red.withAlpha (0.25f));
                g.drawRoundedRectangle (cell.expanded (1.5f), 4.0f, 2.0f);
            }
            if (! have) continue;

            const float db = p.stepDb (s);
            const float lvl = juce::jlimit (0.05f, 1.0f, (db - (top - range)) / range);
            const bool gap = db < (isYou ? youMax : refMax) - 9.0f;
            auto bar = cell.withTrimmedTop (cell.getHeight() * (1.0f - lvl));
            const auto c = gap ? redDim : (s % 4 == 0 ? red.brighter (0.25f) : red);
            g.setColour (c.withAlpha (gap ? 0.5f : 0.9f));
            g.fillRoundedRectangle (bar, 2.5f);
            if (! gap)
            {
                g.setColour (c.withAlpha (0.25f));
                g.drawRoundedRectangle (bar.expanded (1.0f), 3.0f, 2.0f);
            }

            if (isYou && proc.hasSnapshot() && snap.valid)   // where this step was at the snapshot
            {
                const float sl = juce::jlimit (0.05f, 1.0f, (snap.stepDb (s) - (top - range)) / range);
                const float yy = cell.getBottom() - cell.getHeight() * sl;
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawLine (cell.getX() + 2.0f, yy, cell.getRight() - 2.0f, yy, 1.4f);
            }

            if (isYou && haveRef)
            {
                const float d = db - ref.stepDb (s);
                if (std::abs (d) >= 3.0f)
                {
                    g.setColour (d > 0 ? over : blue);
                    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                    g.drawText (d > 0 ? "^" : "v", cell.withHeight (14.0f), juce::Justification::centred);
                }
            }
        }
    }

    // beat numbers + beat lines
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int beat = 0; beat < 4; ++beat)
    {
        const float x = rows.getX() + labelW + beat * 4 * colW;
        g.setColour (muted.withAlpha (0.8f));
        g.drawText (juce::String (beat + 1), juce::Rectangle<float> (x, rows.getBottom() - 2.0f, colW, 12.0f),
                    juce::Justification::centred);
        if (beat > 0)
        {
            g.setColour (red.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, rows.getY(), rows.getBottom() - 2.0f);
        }
    }
}

//==============================================================================
//  Arrangement: the reference on top, your learned track below, pairs in colour
//==============================================================================
juce::Colour ArrangementView::pairColour (int index)
{
    static const juce::uint32 c[] = { 0xff3fd0ff, 0xffffb13f, 0xffb48cff, 0xff35e07a, 0xffff6fb5, 0xff2fe0c8, 0xffc6f04a, 0xffff7a3d };
    return juce::Colour (c[((index % 8) + 8) % 8]);
}

float ArrangementView::xForTime (double sec) const
{
    auto* ref = proc.getReference();
    auto a = refLane();
    if (ref == nullptr) return a.getX();
    return a.getX() + (float) (sec / ref->lengthSeconds()) * a.getWidth();
}

double ArrangementView::timeForX (float x) const
{
    auto* ref = proc.getReference();
    auto a = refLane();
    if (ref == nullptr) return 0.0;
    return (x - a.getX()) / a.getWidth() * ref->lengthSeconds();
}

int ArrangementView::songBarsShown() const
{
    int n = juce::jmax (16, proc.getLearnedBars());
    for (auto& p : proc.getPairs()) n = juce::jmax (n, p.songEnd);
    return n;
}

float ArrangementView::xForBar (double bar) const
{
    auto a = songLane();
    return a.getX() + (float) (bar / songBarsShown()) * a.getWidth();
}

double ArrangementView::barForX (float x) const
{
    auto a = songLane();
    return (x - a.getX()) / a.getWidth() * songBarsShown();
}

int ArrangementView::pairAt (float x, int& edge) const
{
    const auto& ps = proc.getPairs();
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        const float x0 = xForBar (ps[(size_t) i].songStart), x1 = xForBar (ps[(size_t) i].songEnd);
        if (std::abs (x - x0) <= 5.0f) { edge = -1; return i; }
        if (std::abs (x - x1) <= 5.0f) { edge = 1; return i; }
        if (x > x0 && x < x1) { edge = 0; return i; }
    }
    edge = 0;
    return -1;
}

bool ArrangementView::getDraft (int& a, int& b) const
{
    if (draftA < 0 || draftB <= draftA) return false;
    a = draftA;  b = draftB;
    return true;
}

void ArrangementView::paint (juce::Graphics& g)
{
    // ---- title row
    auto head = getLocalBounds().toFloat().reduced (16.0f, 0.0f).withHeight (28.0f);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (muted);
    g.drawText ("REFERENCE", head, juce::Justification::centredLeft);
    auto* ref = proc.getReference();
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (ref != nullptr ? ref->name : juce::String ("--"), head.withTrimmedLeft (160.0f).withWidth (210.0f),
                juce::Justification::centredLeft, true);
    g.setColour (muted);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("BARS", juce::Rectangle<float> (378.0f, head.getY(), 40.0f, head.getHeight()), juce::Justification::centredRight);

    // ---- reference lane
    auto a = refLane();
    g.setColour (juce::Colour (0xff0d0d11));
    g.fillRoundedRectangle (a, 6.0f);
    const int sel = proc.getSelectedPair();
    const auto& pairs = proc.getPairs();
    float selWx0 = -1, selWx1 = -1;
    if (ref == nullptr || proc.isLoading())
    {
        g.setColour (muted);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (proc.isLoading() ? "Analyzing reference..." : "Drop a WAV, AIFF or MP3 here, or press LOAD",
                    a, juce::Justification::centred);
    }
    else
    {
        const double bpm = proc.getBpm(), barSec = 240.0 / bpm;
        const int barsAvail = proc.getNumReferenceBars();
        const int bars = juce::jlimit (1, proc.getWindowBars(), juce::jmax (1, barsAvail));
        const double winStart = proc.getGridStartSec() + proc.getWindowStartBar() * barSec;
        const float wx0 = xForTime (winStart), wx1 = xForTime (winStart + bars * barSec);
        selWx0 = wx0;  selWx1 = wx1;

        const auto n = ref->overviewMin.size();
        float peak = 1.0e-6f;
        for (size_t i = 0; i < n; ++i) peak = std::max ({ peak, ref->overviewMax[i], -ref->overviewMin[i] });
        const float mid = a.getCentreY(), half = a.getHeight() * 0.44f / peak;
        for (size_t i = 0; i < n; ++i)
        {
            const float x = a.getX() + a.getWidth() * (float) i / (float) n;
            g.setColour (x >= wx0 && x <= wx1 ? red.withAlpha (0.9f) : redDim.withAlpha (0.8f));
            g.drawVerticalLine ((int) x, mid - ref->overviewMax[i] * half, mid - ref->overviewMin[i] * half + 1.0f);
        }
        for (int b = 0;; ++b)
        {
            const double t = proc.getGridStartSec() + b * barSec;
            if (t > ref->lengthSeconds()) break;
            if (t < 0) continue;
            g.setColour (juce::Colours::white.withAlpha (b % 4 == 0 ? 0.16f : 0.05f));
            g.drawVerticalLine ((int) xForTime (t), a.getY(), a.getBottom());
        }
        // where each pair (on this reference) points
        for (int i = 0; i < (int) pairs.size(); ++i)
        {
            const auto& p = pairs[(size_t) i];
            if (p.slot != proc.getActiveSlot()) continue;
            const double t0 = proc.getGridStartSec() + p.refStart * barSec;
            auto band = juce::Rectangle<float> (xForTime (t0), a.getBottom() - 5.0f, xForTime (t0 + p.refBars * barSec) - xForTime (t0), 4.0f);
            g.setColour (pairColour (p.colour).withAlpha (i == sel ? 1.0f : 0.6f));
            g.fillRect (band);
        }
        auto win = juce::Rectangle<float> (wx0, a.getY(), wx1 - wx0, a.getHeight());
        g.setColour (red.withAlpha (0.10f));
        g.fillRoundedRectangle (win, 4.0f);
        g.setColour (red.withAlpha (0.25f));
        g.drawRoundedRectangle (win, 4.0f, 3.0f);
        g.setColour (sel >= 0 && sel < (int) pairs.size() ? pairColour (pairs[(size_t) sel].colour) : red);
        g.drawRoundedRectangle (win, 4.0f, 1.4f);
        const double ph = proc.getRefPlayPositionSec();
        if (ph >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawVerticalLine ((int) xForTime (ph), a.getY(), a.getBottom());
        }
        auto marker = [&] (int bar, const juce::String& label, juce::Colour c)
        {
            if (bar < 0 || barsAvail <= gs::kBars) return;
            const float x = xForTime (proc.getGridStartSec() + bar * barSec);
            g.setColour (c.withAlpha (0.8f));
            g.fillRect (x - 1.0f, a.getY(), 2.0f, 8.0f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (label, juce::Rectangle<float> (x + 3.0f, a.getY() + 1.0f, 80.0f, 11.0f), juce::Justification::centredLeft);
        };
        marker (proc.getDropBar(), "DROP", yellow);
        marker (proc.getBreakdownBar(), "BREAKDOWN", blue);

        g.setColour (red);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        const int first = proc.getWindowStartBar() + 1;
        const juce::String tag = sel >= 0 ? "PAIR " + juce::String (sel + 1) + "  "
                               : proc.isBreakdownMode() ? "BREAKDOWN  "
                               : (proc.getWindowStartBar() == proc.getDropBar() ? "DROP  " : juce::String());
        g.drawText (tag + "bars " + juce::String (first) + "-" + juce::String (first + bars - 1) + " of " + juce::String (barsAvail)
                        + "  |  drag to move",
                    head, juce::Justification::centredRight);
    }

    // ---- your learned track
    auto s = songLane();
    g.setColour (juce::Colour (0xff0d0d11));
    g.fillRoundedRectangle (s, 6.0f);
    const auto& ovMn = proc.getLearnedOverviewMin();
    const auto& ovMx = proc.getLearnedOverviewMax();
    const int per = gs::SongRecorder::kOverviewPerBar, trim = proc.getTrimBars();
    const int shown = songBarsShown();
    const int startBar = proc.getEffectiveStartBar();
    if (proc.getLearnedBars() <= 0 && ovMn.empty())
    {
        g.setColour (muted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("YOUR TRACK: press LEARN, then play your song in Ableton from bar " + juce::String (startBar > 0 ? startBar : 1)
                        + " to the end", s, juce::Justification::centred);
    }
    else
    {
        float peak = 1.0e-6f;
        for (size_t i = (size_t) trim * per; i < ovMn.size(); ++i) peak = std::max ({ peak, ovMx[i], -ovMn[i] });
        const float mid = s.getCentreY(), half = s.getHeight() * 0.42f / peak;
        const int total = juce::jmin ((int) ovMn.size() / per - trim, shown);
        for (int b = 0; b < total; ++b)
            for (int q = 0; q < per; ++q)
            {
                const size_t idx = (size_t) (b + trim) * per + q;
                const float x0 = xForBar (b + q / (double) per), x1 = xForBar (b + (q + 1) / (double) per);
                g.setColour (juce::Colour (0xffd8d8de).withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, mid - ovMx[idx] * half, juce::jmax (x0 + 0.8f, x1),
                                                                         mid - ovMn[idx] * half + 1.0f));
            }
    }
    // bar lines + Ableton bar numbers
    g.setFont (juce::FontOptions (9.5f));
    for (int b = 0; b <= shown; b += 4)
    {
        const float x = xForBar (b);
        g.setColour (juce::Colours::white.withAlpha (b % 16 == 0 ? 0.16f : 0.06f));
        g.drawVerticalLine ((int) x, s.getY(), s.getBottom());
        if (b % 16 == 0 && b < shown)
        {
            g.setColour (muted.withAlpha (0.9f));
            g.drawText (juce::String (startBar + b), juce::Rectangle<float> (x + 2.0f, s.getBottom() - 12.0f, 40.0f, 11.0f),
                        juce::Justification::centredLeft);
        }
    }
    // pairs
    for (int i = 0; i < (int) pairs.size(); ++i)
    {
        const auto& p = pairs[(size_t) i];
        int a0 = p.songStart, a1 = p.songEnd;
        if ((drag == Drag::resizeStart || drag == Drag::resizeEnd) && dragPair == i) { a0 = tempA; a1 = tempB; }
        auto block = juce::Rectangle<float>::leftTopRightBottom (xForBar (a0), s.getY() + 1.0f, xForBar (a1), s.getBottom() - 1.0f);
        const auto c = pairColour (p.colour);
        g.setColour (c.withAlpha (i == sel ? 0.28f : 0.14f));
        g.fillRoundedRectangle (block, 4.0f);
        g.setColour (c.withAlpha (i == sel ? 1.0f : 0.7f));
        g.drawRoundedRectangle (block, 4.0f, i == sel ? 2.0f : 1.0f);
        auto tag = juce::Rectangle<float> (block.getX() + 4.0f, block.getY() + 3.0f, 32.0f, 14.0f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1) + (p.slot == 1 ? "B" : ""), tag.withTrimmedLeft (12.0f), juce::Justification::centredLeft);
        const auto light = p.status == 1 ? green : p.status == 2 ? yellow : p.status == 3 ? over : grey;
        g.setColour (light.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ tag.getX() + 5.0f, tag.getCentreY() }));
        g.setColour (light);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ tag.getX() + 5.0f, tag.getCentreY() }));
        if (p.needsLearn)
        {
            g.setColour (yellow);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("RE-LEARN", block.reduced (4.0f, 2.0f), juce::Justification::bottomLeft);
        }
    }
    // the stretch you're dragging out
    if (draftA >= 0 && draftB > draftA)
    {
        auto d = juce::Rectangle<float>::leftTopRightBottom (xForBar (draftA), s.getY(), xForBar (draftB), s.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (d, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawRoundedRectangle (d, 4.0f, 1.2f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("bars " + juce::String (startBar + draftA) + "-" + juce::String (startBar + draftB - 1) + "  (+ PAIR)",
                    d.reduced (4.0f, 2.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
    }
    // the playhead, and the learn pass
    const double nowBar = proc.getSongBarNow();
    if (proc.isHostPlaying() && nowBar >= 0 && nowBar < shown)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine ((int) xForBar (nowBar), s.getY(), s.getBottom());
    }
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (muted);
    g.drawText ("YOUR TRACK", s.reduced (6.0f, 2.0f).withHeight (13.0f), juce::Justification::topRight);
    if (proc.getLearnState() != GroundShakeProcessor::LearnState::off)
    {
        g.setColour (red);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("LEARNING  bar " + juce::String (startBar + (int) juce::jmax (0.0, nowBar)), s.reduced (8.0f, 4.0f).withTrimmedTop (12.0f),
                    juce::Justification::topRight);
    }
    // connect the selected pair to where it points in the reference
    if (sel >= 0 && sel < (int) pairs.size() && selWx0 >= 0 && pairs[(size_t) sel].slot == proc.getActiveSlot())
    {
        const auto& p = pairs[(size_t) sel];
        const float xa = (selWx0 + selWx1) * 0.5f, xb = (xForBar (p.songStart) + xForBar (p.songEnd)) * 0.5f;
        juce::Path link;
        link.startNewSubPath (xa, a.getBottom());
        link.cubicTo (xa, a.getBottom() + 5.0f, xb, s.getY() - 5.0f, xb, s.getY());
        g.setColour (pairColour (p.colour).withAlpha (0.85f));
        g.strokePath (link, juce::PathStrokeType (1.6f));
    }
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    int edge = 0;
    const bool inSong = songLane().contains (e.position);
    const int i = inSong ? pairAt (e.position.x, edge) : -1;
    setMouseCursor (i >= 0 && edge != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    if (refLane().expanded (0.0f, 4.0f).contains (e.position))
    {
        if (proc.getReference() == nullptr) return;
        drag = Drag::refWindow;
        const double barSec = 240.0 / proc.getBpm();
        const int barUnder = (int) std::floor ((timeForX (e.position.x) - proc.getGridStartSec()) / barSec);
        const int start = proc.getWindowStartBar();
        const int bars = juce::jlimit (1, proc.getWindowBars(), juce::jmax (1, proc.getNumReferenceBars()));
        if (barUnder >= start && barUnder < start + bars)
            dragGrabBarOffset = barUnder - start;
        else
        {
            dragGrabBarOffset = bars / 2;
            proc.setWindowStartBar (barUnder - dragGrabBarOffset);
        }
        return;
    }
    if (! songLane().contains (e.position)) return;
    int edge = 0;
    const int i = pairAt (e.position.x, edge);
    if (i >= 0 && edge != 0)
    {
        drag = edge < 0 ? Drag::resizeStart : Drag::resizeEnd;
        dragPair = i;
        tempA = proc.getPairs()[(size_t) i].songStart;
        tempB = proc.getPairs()[(size_t) i].songEnd;
        return;
    }
    if (i >= 0)
    {
        proc.selectPair (i);              // clicking a pair turns FOLLOW off
        clearDraft();
        if (onDraftChanged) onDraftChanged();
        return;
    }
    // starting a new stretch: let go of the selected pair, so moving the reference window
    // next doesn't re-point that pair
    if (proc.getSelectedPair() >= 0) proc.selectPair (-1);
    drag = Drag::draft;
    draftAnchor = juce::jmax (0, (int) std::floor (barForX (e.position.x)));
    draftA = draftB = -1;
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::refWindow)
    {
        const double barSec = 240.0 / proc.getBpm();
        const int barUnder = (int) std::floor ((timeForX (e.position.x) - proc.getGridStartSec()) / barSec);
        const int target = barUnder - dragGrabBarOffset;
        if (target != proc.getWindowStartBar()) proc.setWindowStartBar (target);
        return;
    }
    const int bar = juce::jlimit (0, songBarsShown(), (int) std::round (barForX (e.position.x)));
    if (drag == Drag::draft)
    {
        const int cur = juce::jmax (0, (int) std::floor (barForX (e.position.x)));
        draftA = juce::jmin (draftAnchor, cur);
        draftB = juce::jmax (draftAnchor, cur) + 1;
        repaint();
        if (onDraftChanged) onDraftChanged();
    }
    else if (drag == Drag::resizeStart) { tempA = juce::jmin (bar, tempB - 1); repaint(); }
    else if (drag == Drag::resizeEnd)   { tempB = juce::jmax (bar, tempA + 1); repaint(); }
}

void ArrangementView::mouseUp (const juce::MouseEvent& e)
{
    if ((drag == Drag::resizeStart || drag == Drag::resizeEnd) && dragPair >= 0)
        proc.setPairRange (dragPair, tempA, tempB);
    else if (drag == Drag::draft && e.mouseWasClicked())
    {
        clearDraft();                      // a plain click clears the stretch
        if (onDraftChanged) onDraftChanged();
    }
    drag = Drag::none;
    dragPair = -1;
    repaint();
}

//==============================================================================
//  Text helpers
//==============================================================================
namespace
{
    juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::TextLayout layoutText (const juce::AttributedString& a, float width)
    {
        juce::TextLayout tl;
        tl.createLayout (a, juce::jmax (10.0f, width));
        return tl;
    }

    juce::AttributedString attr (const juce::String& s, const juce::Font& f, juce::Colour c)
    {
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        a.append (s, f, c);
        return a;
    }

    // "Label: text" with a bold label, wrapped. Draws when g != nullptr. Returns the height used.
    float para (juce::Graphics* g, const juce::String& label, const juce::String& body, float x, float y, float w,
                juce::Colour bodyColour = gsui::text, float size = 12.0f)
    {
        if (body.isEmpty()) return 0.0f;
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        a.setLineSpacing (1.0f);
        if (label.isNotEmpty()) a.append (label + "  ", font (size, true), gsui::muted);
        a.append (body, font (size), bodyColour);
        auto tl = layoutText (a, w);
        if (g != nullptr) tl.draw (*g, { x, y, w, tl.getHeight() });
        return tl.getHeight();
    }

    juce::String shortKey (int index, bool minor)
    {
        if (index < 0) return "--";
        static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (n[index % 12]) + (minor ? "m" : "");
    }

    void statusIcon (juce::Graphics& g, juce::Rectangle<float> r, gs::Status s)
    {
        using gs::Status;
        const auto c = gsui::colourFor (s);
        r = r.withSizeKeepingCentre (12.0f, 12.0f);
        switch (s)
        {
            case Status::pass:
            {
                g.setColour (c.withAlpha (0.25f));
                g.fillEllipse (r.expanded (2.0f));
                g.setColour (c);
                g.fillEllipse (r);
                juce::Path tick;
                tick.startNewSubPath (r.getX() + 3.0f, r.getCentreY());
                tick.lineTo (r.getX() + 5.2f, r.getBottom() - 3.2f);
                tick.lineTo (r.getRight() - 2.6f, r.getY() + 3.2f);
                g.setColour (juce::Colour (0xff04140a));
                g.strokePath (tick, juce::PathStrokeType (1.8f));
                break;
            }
            case Status::holding:
                g.setColour (c);
                g.drawEllipse (r, 1.6f);
                g.fillEllipse (r.reduced (3.5f));
                break;
            case Status::near:
            case Status::fail:
                g.setColour (c.withAlpha (0.2f));
                g.fillEllipse (r);
                g.setColour (c);
                g.drawEllipse (r, 1.6f);
                break;
            case Status::info:
                g.setColour (c);
                g.drawEllipse (r, 1.2f);
                g.setFont (font (9.0f, true));
                g.drawText ("i", r, juce::Justification::centred);
                break;
            case Status::waiting:
            default:
                g.setColour (c.withAlpha (0.6f));
                g.fillEllipse (r.reduced (4.0f));
                break;
        }
    }

    const gs::CheckRow* findRow (const gs::Coach& coach, gs::CheckId id)
    {
        for (auto& r : coach.checks()) if (r.id == id) return &r;
        return nullptr;
    }

    // one beat (four 16ths), you vs reference, levels lined up
    void drawBeatGraph (juce::Graphics& g, juce::Rectangle<float> r, const gs::BeatShape& bs, int beatMask, juce::Colour youColour)
    {
        g.setColour (juce::Colour (0xff0b0b0e));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (gsui::red.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        auto plot = r.reduced (12.0f, 8.0f).withTrimmedBottom (14.0f).withTrimmedTop (12.0f);
        const float colW = plot.getWidth() / 4.0f;
        for (int k = 0; k < 4; ++k)
            if ((beatMask >> k) & 1)
            {
                g.setColour (gsui::yellow.withAlpha (0.12f));
                g.fillRect (plot.getX() + k * colW, plot.getY() - 6.0f, colW, plot.getHeight() + 10.0f);
            }
        g.setFont (font (10.0f, true));
        const char* labels[] = { "1  kick", "2", "3", "4" };
        for (int k = 0; k < 4; ++k)
        {
            g.setColour (gsui::muted);
            g.drawText (labels[k], juce::Rectangle<float> (plot.getX() + k * colW, plot.getBottom() + 2.0f, colW, 12.0f),
                        juce::Justification::centred);
            if (k > 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawVerticalLine ((int) (plot.getX() + k * colW), plot.getY(), plot.getBottom());
            }
        }
        g.setFont (font (10.0f));
        g.setColour (gsui::muted);
        g.drawText ("one beat, levels lined up", r.reduced (10.0f, 3.0f).withHeight (12.0f), juce::Justification::centredLeft);
        g.setColour (gsui::red);
        g.drawText ("ref", r.reduced (10.0f, 3.0f).withHeight (12.0f), juce::Justification::centredRight);
        g.setColour (youColour);
        g.drawText ("you", r.reduced (10.0f, 3.0f).withHeight (12.0f).withTrimmedRight (30.0f), juce::Justification::centredRight);
        if (! bs.valid) return;

        float lo = 0, hi = 0;
        for (int k = 0; k < 4; ++k) { lo = std::min ({ lo, bs.you[(size_t) k], bs.ref[(size_t) k] }); hi = std::max ({ hi, bs.you[(size_t) k], bs.ref[(size_t) k] }); }
        lo = std::min (lo - 1.0f, -6.0f);  hi = std::max (hi + 1.0f, 3.0f);
        auto yFor = [&] (float db) { return plot.getBottom() - (db - lo) / (hi - lo) * plot.getHeight(); };
        auto curve = [&] (const std::array<float, 4>& v, juce::Colour c, float w)
        {
            juce::Path p;
            for (int k = 0; k < 4; ++k)
            {
                const float x = plot.getX() + (k + 0.5f) * colW, y = yFor (v[(size_t) k]);
                k == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
            }
            g.setColour (c.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (w + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            for (int k = 0; k < 4; ++k)
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ plot.getX() + (k + 0.5f) * colW, yFor (v[(size_t) k]) }));
        };
        curve (bs.ref, gsui::red, 2.0f);
        curve (bs.you, youColour, 2.4f);
    }
}

//==============================================================================
//  Diagnostics strip
//==============================================================================
void DiagnosticsStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledPanel (g, r, 12.0f);
    const auto& coach = proc.getDisplayCoach();
    const auto you = proc.getDisplayYouMeasurements();
    const auto& ref = proc.getDisplayRefMeasurements();
    const auto liveP = proc.getDisplayYouProfile();
    const auto& refP = proc.getDisplayRefProfile();

    struct Tile { juce::String title, big; juce::Colour colour; juce::String l1, l2; };
    auto rowOf = [&] (gs::CheckId id, juce::String& value, juce::Colour& c)
    {
        if (auto* row = findRow (coach, id))
        {
            value = row->status == gs::Status::waiting ? juce::String ("--") : row->value;
            c = row->status == gs::Status::waiting ? muted : colourFor (row->status);
        }
        else { value = "--"; c = muted; }
    };
    std::vector<Tile> tiles (6);
    {
        auto& t = tiles[0];
        t.title = "MONO < 120 Hz";
        rowOf (gs::CheckId::mono, t.big, t.colour);
        t.l1 = "ref " + (ref.valid ? juce::String (ref.monoCorr, 2) : juce::String ("--"));
        t.l2 = "L/R correlation";
    }
    {
        auto& t = tiles[1];
        t.title = "KICK / BASS";
        rowOf (gs::CheckId::kickBass, t.big, t.colour);
        static const char* lenS[] = { "short", "medium", "long" };
        static const char* attS[] = { "sharp", "medium", "soft" };
        auto kickShort = [&] (const gs::Measurements& m)
        { return m.valid && m.kick.valid ? juce::String (lenS[m.kick.lengthClass()]) + "/" + attS[m.kick.attackClass()] : juce::String ("--"); };
        auto bassShort = [] (const gs::Measurements& m)
        {
            switch (m.bassPattern)
            {
                case gs::BassPattern::rolling16: return juce::String ("16ths");
                case gs::BassPattern::rolling8:  return juce::String ("8ths");
                case gs::BassPattern::offbeat:   return juce::String ("offbeat");
                case gs::BassPattern::gallop:    return juce::String ("gallop");
                case gs::BassPattern::reverse:   return juce::String ("reverse");
                case gs::BassPattern::sustained: return juce::String ("held");
                case gs::BassPattern::none:      return juce::String ("none");
                case gs::BassPattern::unknown:
                default:                         return juce::String (m.valid ? "mixed" : "--");
            }
        };
        t.l1 = "kick " + kickShort (you) + " / " + kickShort (ref);
        t.l2 = "bass " + bassShort (you) + " / " + bassShort (ref);
    }
    {
        auto& t = tiles[2];
        t.title = "SUB NOTE  you / ref";
        t.big = (you.valid ? gs::noteName (you.bassHz) : juce::String ("--")) + " / " + (ref.valid ? gs::noteName (ref.bassHz) : juce::String ("--"));
        juce::String v;  juce::Colour c;
        rowOf (gs::CheckId::tuning, v, c);
        t.colour = text;
        t.l1 = "kick ~" + (you.valid ? gs::noteName (you.kickHz) : juce::String ("--")) + " / ~" + (ref.valid ? gs::noteName (ref.kickHz) : juce::String ("--"));
        t.l2 = "key " + (you.valid ? shortKey (you.keyIndex, you.keyMinor) : juce::String ("--")) + " / "
               + (ref.valid ? shortKey (ref.keyIndex, ref.keyMinor) : juce::String ("--"));
    }
    {
        auto& t = tiles[3];
        t.title = "LIMITER SQUASH";
        rowOf (gs::CheckId::squash, t.big, t.colour);
        t.l1 = "sub crest you " + (you.valid ? juce::String (you.subCrestDb, 1) : juce::String ("--"));
        t.l2 = "ref " + (ref.valid ? juce::String (ref.subCrestDb, 1) + " dB" : juce::String ("--"));
    }
    {
        auto& t = tiles[4];
        t.title = "MUD 150-400 Hz";
        rowOf (gs::CheckId::mud, t.big, t.colour);
        t.l1 = "vs ref, share of mix";
        if (liveP.valid && refP.valid)
        {
            const float cl = (liveP.shakeDb() - liveP.mudDb()) - (refP.shakeDb() - refP.mudDb());
            t.l2 = "clarity " + juce::String (cl >= 0 ? "+" : "") + juce::String (cl, 1) + " dB";
        }
        else t.l2 = "clarity --";
    }
    {
        auto& t = tiles[5];
        t.title = "DJ PLAYBACK";
        rowOf (gs::CheckId::djPlayback, t.big, t.colour);
        t.l1 = "LUFS " + (you.valid ? juce::String (you.lufs, 1) : juce::String ("--")) + " / "
               + (ref.valid ? juce::String (ref.lufs, 1) : juce::String ("--"));
        t.l2 = you.valid ? "Auto Gain " + juce::String (proc.getDjRefLevel() - you.lufs >= 0 ? "+" : "")
                               + juce::String (proc.getDjRefLevel() - you.lufs, 1) + " dB"
                         : juce::String ("Auto Gain --");
    }

    auto inner = r.reduced (10.0f, 8.0f);
    const float w = inner.getWidth() / (float) tiles.size();
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        auto t = juce::Rectangle<float> (inner.getX() + w * (float) i, inner.getY(), w, inner.getHeight()).reduced (6.0f, 0.0f);
        if (i > 0)
        {
            g.setColour (red.withAlpha (0.25f));
            g.drawVerticalLine ((int) (t.getX() - 6.0f), t.getY() + 4.0f, t.getBottom() - 4.0f);
        }
        const auto& tile = tiles[i];
        g.setFont (font (10.5f, true));
        g.setColour (muted);
        g.drawText (tile.title, t.removeFromTop (14.0f), juce::Justification::centredLeft, true);
        g.setFont (font (17.0f, true));
        g.setColour (tile.colour);
        g.drawText (tile.big, t.removeFromTop (26.0f), juce::Justification::centredLeft, true);
        g.setFont (font (10.5f));
        g.setColour (muted.brighter (0.2f));
        g.drawText (tile.l1, t.removeFromTop (14.0f), juce::Justification::centredLeft, true);
        g.drawText (tile.l2, t.removeFromTop (14.0f), juce::Justification::centredLeft, true);
    }
}

//==============================================================================
//  Checklist
//==============================================================================
void ChecklistView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledPanel (g, r, 12.0f);
    auto inner = r.reduced (16.0f, 10.0f);
    const auto& coach = proc.getDisplayCoach();

    auto head = inner.removeFromTop (20.0f);
    g.setFont (font (13.0f, true));
    g.setColour (muted);
    g.drawText ("CHECKLIST", head, juce::Justification::centredLeft);
    const float pct = coach.matchPercent();
    g.setColour (pct < 0 ? muted : (pct >= 90 ? green : (pct >= 60 ? yellow : over)));
    g.drawText (pct < 0 ? juce::String ("match: --") : "match: " + juce::String (juce::roundToInt (pct)) + "%", head,
                juce::Justification::centredRight);
    auto badge = [&] (const juce::String& txt, juce::Colour c, float cx)
    {
        auto b = juce::Rectangle<float> (cx - 52.0f, head.getY() + 1.0f, 104.0f, 18.0f);
        g.setColour (c.withAlpha (0.15f));
        g.fillRoundedRectangle (b, 9.0f);
        g.setColour (c);
        g.drawRoundedRectangle (b, 9.0f, 1.0f);
        g.setFont (font (10.5f, true));
        g.drawText (txt, b, juce::Justification::centred);
    };
    const int sel = proc.getSelectedPair();
    if (sel >= 0)
        badge ("PAIR " + juce::String (sel + 1) + (proc.isShowingSavedPair() ? " LEARNED" : " LIVE"),
               ArrangementView::pairColour (proc.getPairs()[(size_t) sel].colour), head.getX() + 142.0f);
    if (proc.isDisplayBreakdown())
        badge ("BREAKDOWN", blue, head.getX() + (sel >= 0 ? 252.0f : 142.0f));

    auto bar = inner.removeFromTop (12.0f).withSizeKeepingCentre (inner.getWidth(), 6.0f);
    g.setColour (juce::Colour (0xff1a1a20));
    g.fillRoundedRectangle (bar, 3.0f);
    if (pct > 0)
    {
        auto fill = bar.withWidth (bar.getWidth() * pct / 100.0f);
        const auto c = pct >= 90 ? green : (pct >= 60 ? yellow : over);
        g.setColour (c.withAlpha (0.25f));
        g.fillRoundedRectangle (fill.expanded (0.0f, 2.0f), 4.0f);
        g.setColour (c);
        g.fillRoundedRectangle (fill, 3.0f);
    }
    inner.removeFromTop (6.0f);

    const auto& rows = coach.checks();
    const int perCol = (int) (rows.size() + 1) / 2;
    const float colW = inner.getWidth() / 2.0f, rowH = inner.getHeight() / (float) juce::jmax (1, perCol);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int col = (int) i / perCol, row = (int) i % perCol;
        auto cell = juce::Rectangle<float> (inner.getX() + col * colW, inner.getY() + row * rowH, colW, rowH).reduced (2.0f, 1.0f);
        const auto& rw = rows[i];
        statusIcon (g, cell.removeFromLeft (16.0f), rw.status);
        cell.removeFromLeft (6.0f);
        g.setFont (font (12.0f, rw.status == gs::Status::fail || rw.status == gs::Status::near));
        g.setColour (rw.status == gs::Status::waiting ? muted : text);
        g.drawText (rw.name, cell, juce::Justification::centredLeft, true);
        g.setFont (font (10.5f));
        g.setColour (rw.status == gs::Status::waiting ? muted.withAlpha (0.6f) : colourFor (rw.status));
        g.drawText (rw.status == gs::Status::waiting ? juce::String ("waiting") : rw.value, cell.withTrimmedRight (8.0f),
                    juce::Justification::centredRight, true);
    }
    if (rows.empty())
    {
        g.setColour (muted);
        g.setFont (font (12.0f));
        g.drawText ("Waiting for a reference and your loop.", inner, juce::Justification::centred);
    }
}

//==============================================================================
//  Fixes list
//==============================================================================
bool FixListView::isExpanded (const gs::FixCard& c) const
{
    if (userExpanded >= 0) return (int) c.id == userExpanded;
    if (userCollapsed) return false;
    for (auto& k : cards)
        if (k.fixFirst) return &k == &c || k.id == c.id;
    return ! cards.empty() && cards.front().id == c.id;
}

int FixListView::expandedHighlight() const
{
    for (auto& c : cards) if (isExpanded (c)) return c.highlightMask;
    return 0;
}

void FixListView::refresh (int viewWidth)
{
    cards = proc.getDisplayCards();
    if (userExpanded >= 0)
    {
        bool found = false;
        for (auto& c : cards) found = found || (int) c.id == userExpanded;
        if (! found) { userExpanded = -1; userCollapsed = false; }
    }
    float y = 0.0f;
    const float w = (float) viewWidth;
    cardRects.clear();
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const float h = layoutCard (nullptr, cards[i], isExpanded (cards[i]), { 0.0f, y, w, 10000.0f }, (int) i);
        cardRects.push_back ({ 0.0f, y, w, h });
        y += h + 8.0f;
    }
    const int newH = juce::jmax (40, (int) y);
    if (getWidth() != viewWidth || getHeight() != newH)
    {
        setSize (viewWidth, newH);
        if (onLayoutChanged) onLayoutChanged();
    }
    repaint();
}

float FixListView::layoutCard (juce::Graphics* g, const gs::FixCard& c, bool expanded, juce::Rectangle<float> area, int)
{
    using gs::Status;
    const auto sevColour = colourFor (c.severity);
    const float x = area.getX() + 16.0f, w = area.getWidth() - 28.0f;
    float y = area.getY() + 10.0f;

    auto badge = [&] (const juce::String& s, juce::Colour col)
    {
        if (g != nullptr)
        {
            auto br = juce::Rectangle<float> (x, y, juce::GlyphArrangement::getStringWidth (font (10.0f, true), s) + 14.0f, 16.0f);
            g->setColour (col.withAlpha (0.18f));
            g->fillRoundedRectangle (br, 8.0f);
            g->setColour (col);
            g->drawRoundedRectangle (br, 8.0f, 1.0f);
            g->setFont (font (10.0f, true));
            g->drawText (s, br, juce::Justification::centred);
        }
        y += 22.0f;
    };
    if (c.fixFirst)                         badge ("FIX THIS FIRST", red);
    else if (c.severity == Status::holding) badge ("FIXED - HOLDING FOR ONE LOOP", green);
    else if (c.severity == Status::info)    badge ("INFORMATION", grey);
    if (c.adviceOnly && c.severity != Status::holding) badge ("SOUND / NOTE CHOICE: YOU CHANGE THIS", yellow);

    {
        auto tl = layoutText (attr (c.title, font (13.0f, true), text), w - 18.0f);
        if (g != nullptr) tl.draw (*g, { x, y, w - 18.0f, tl.getHeight() });
        if (g != nullptr)       // open / closed marker
        {
            juce::Path tri;
            const float cx = x + w - 6.0f, cy = y + 8.0f;
            if (expanded) tri.addTriangle (cx - 5.0f, cy - 2.5f, cx + 5.0f, cy - 2.5f, cx, cy + 3.5f);
            else          tri.addTriangle (cx - 2.5f, cy - 5.0f, cx - 2.5f, cy + 5.0f, cx + 3.5f, cy);
            g->setColour (muted);
            g->fillPath (tri);
        }
        y += tl.getHeight() + 2.0f;
    }
    {
        auto tl = layoutText (attr (c.headline, font (12.0f, true), sevColour), w);
        if (g != nullptr) tl.draw (*g, { x, y, w, tl.getHeight() });
        y += tl.getHeight() + 6.0f;
    }

    if (! expanded)
    {
        y += para (g, "", c.see, x, y, w, muted.brighter (0.25f), 11.5f);
        if (c.progress.isNotEmpty()) y += 4.0f + para (g, "Progress:", c.progress, x, y + 4.0f, w, text, 11.5f);
        if (c.worse.isNotEmpty()) y += 4.0f + para (g, "Careful:", c.worse, x, y + 4.0f, w, over.brighter (0.3f), 11.5f);
        if (c.fixed.isNotEmpty()) y += 4.0f + para (g, "Fixed:", c.fixed, x, y + 4.0f, w, blue.brighter (0.4f), 11.5f);
        return y + 10.0f - area.getY();
    }

    const float gap = 6.0f;
    y += para (g, "Comparing to:", c.comparing, x, y, w, text) + gap;
    if (c.fixed.isNotEmpty()) y += para (g, "Ground Shake:", c.fixed, x, y, w, blue.brighter (0.4f)) + gap;
    y += para (g, "What we see:", c.see, x, y, w, text) + gap;
    if (c.beatGraph)
    {
        if (g != nullptr)
        {
            const auto bs = gs::beatShape (proc.getDisplayYouProfile(), proc.getDisplayRefProfile());
            drawBeatGraph (*g, { x, y, w, 104.0f }, bs, c.beatMask, c.severity == Status::holding ? green : yellow);
        }
        y += 104.0f + gap + 2.0f;
    }
    y += para (g, "Likely cause:", c.cause, x, y, w, text) + gap;
    if (! c.how.isEmpty())
    {
        const bool kick = c.id == gs::CheckId::beatShape || c.id == gs::CheckId::kickBass;
        y += para (g, kick ? "Do it yourself (Kickstart 2):" : "Do it yourself:", " ", x, y, w, text);
        for (int i = 0; i < c.how.size(); ++i)
        {
            const float h = para (g, juce::String (i + 1) + ".", c.how[i], x + 10.0f, y + 2.0f, w - 10.0f, text);
            y += h + 3.0f;
        }
        y += gap - 2.0f;
    }
    if (c.worked.isNotEmpty()) y += para (g, "Worked when:", c.worked, x, y, w, green.brighter (0.3f)) + gap;
    if (c.tooFar.isNotEmpty()) y += para (g, "Too far if:", c.tooFar, x, y, w, yellow.brighter (0.3f)) + gap;
    if (c.watch.isNotEmpty())  y += para (g, "Watch:", c.watch, x, y, w, text) + gap;
    if (c.progress.isNotEmpty()) y += para (g, "Progress:", c.progress, x, y, w, text) + gap;
    if (c.worse.isNotEmpty())    y += para (g, "Careful:", c.worse, x, y, w, over.brighter (0.3f)) + gap;

    if (c.offerFindDrop || c.offerDismiss)
    {
        float bx = x;
        auto button = [&] (const juce::String& s, int action)
        {
            auto br = juce::Rectangle<float> (bx, y + 2.0f, juce::GlyphArrangement::getStringWidth (font (11.0f, true), s) + 24.0f, 26.0f);
            if (g != nullptr)
            {
                g->setColour (juce::Colour (0xff1c0606));
                g->fillRoundedRectangle (br, 6.0f);
                g->setColour (red);
                g->drawRoundedRectangle (br, 6.0f, 1.2f);
                g->setColour (text);
                g->setFont (font (11.0f, true));
                g->drawText (s, br, juce::Justification::centred);
                hits.push_back ({ br, action });
            }
            bx = br.getRight() + 8.0f;
        };
        if (c.offerFindDrop) button ("FIND THE DROP", 1);
        if (c.offerDismiss)  button (c.dismissLabel, 2);
        y += 34.0f;
    }
    return y + 6.0f - area.getY();
}

void FixListView::paint (juce::Graphics& g)
{
    hits.clear();
    if (cards.empty())
    {
        g.setColour (muted);
        g.setFont (font (13.0f));
        const bool ready = proc.hasReference() && proc.liveHasData();
        g.drawFittedText (ready ? "No fixes: your low end matches the reference."
                                : "Fixes appear here once a reference is loaded and your loop has played for two bars.",
                          getLocalBounds().reduced (10, 6), juce::Justification::centredTop, 3);
        return;
    }
    for (size_t i = 0; i < cards.size() && i < cardRects.size(); ++i)
    {
        const auto& c = cards[i];
        auto rr = cardRects[i];
        const bool open = isExpanded (c);
        const auto sev = colourFor (c.severity);
        g.setColour (juce::Colour (open ? 0xff15151a : 0xff111115));
        g.fillRoundedRectangle (rr, 8.0f);
        g.setColour ((open ? sev : sev.withAlpha (0.35f)).withMultipliedAlpha (c.severity == gs::Status::info ? 0.6f : 1.0f));
        g.drawRoundedRectangle (rr.reduced (0.5f), 8.0f, open ? 1.3f : 0.8f);
        g.fillRoundedRectangle (rr.withWidth (4.0f).reduced (0.0f, 6.0f).translated (4.0f, 0.0f), 2.0f);
        layoutCard (&g, c, open, rr, (int) i);
    }
}

void FixListView::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position;
    for (auto& h : hits)
        if (h.r.contains (p))
        {
            if (h.action == 1) proc.findTheDrop();
            if (h.action == 2) proc.dismissSectionWarning();
            return;
        }
    for (size_t i = 0; i < cardRects.size() && i < cards.size(); ++i)
        if (cardRects[i].contains (p))
        {
            if (isExpanded (cards[i])) { userExpanded = -1; userCollapsed = true; }
            else                       { userExpanded = (int) cards[i].id; userCollapsed = false; }
            refresh (getWidth());
            return;
        }
}

//==============================================================================
//  Main content (laid out at 1400 x 820, scaled as a whole when resized)
//==============================================================================
GroundShakeContent::GroundShakeContent (GroundShakeProcessor& p)
    : proc (p), feet (p), grid (p), arrangement (p), strip (p), checklist (p), fixList (p)
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<juce::Component*> {
             &slotA, &slotB, &learnButton, &learnPairButton, &addPairButton, &deletePairButton, &autoPairButton,
             &matchAllButton, &followButton, &startDown, &startUp, &feelKnob, &punchKnob, &attackKnob, &tailKnob,
             &mudKnob, &lowCutKnob, &duckKnobs[0], &duckKnobs[1], &duckKnobs[2], &duckKnobs[3], &monoButton,
             &feet, &grid, &arrangement, &strip, &checklist, &fixViewport, &aButton, &bButton, &loadButton, &x2Button,
             &halfButton, &nudgeLeft, &nudgeRight, &dropButton, &breakdownButton, &bars1, &bars2, &bars4, &bars8,
             &loopButton, &bpmLabel, &bpmTitle, &detectedLabel, &offsetLabel,
             &rangeKnob, &toleranceKnob, &autoButton, &snapshotButton, &resetButton, &reanalyzeButton, &dj6, &dj9, &dj12,
             &matchButton, &previewButton, &applyButton, &revertButton, &weightButton })
        addAndMakeVisible (c);

    fixViewport.setViewedComponent (&fixList, false);
    fixViewport.setScrollBarsShown (true, false);
    fixViewport.setScrollBarThickness (8);

    aButton.setClickingTogglesState (true);
    bButton.setClickingTogglesState (true);
    aButton.setRadioGroupId (1);
    bButton.setRadioGroupId (1);
    (proc.isListeningToReference() ? bButton : aButton).setToggleState (true, juce::dontSendNotification);
    aButton.onClick = [this] { proc.setListenToReference (bButton.getToggleState()); };
    bButton.onClick = [this] { proc.setListenToReference (bButton.getToggleState()); };

    loadButton.onClick = [this] { chooseFile(); };
    x2Button.onClick = [this] { proc.doubleBpm(); };
    halfButton.onClick = [this] { proc.halveBpm(); };
    dropButton.onClick = [this] { proc.findTheDrop(); };
    dropButton.setTooltip ("Jump the window to the reference's fullest, loudest section");
    breakdownButton.onClick = [this] { proc.findTheBreakdown(); };
    breakdownButton.setTooltip ("Jump the window to the reference's breakdown (the section with the least sub)");
    int n = 0;
    for (auto* b : { &bars1, &bars2, &bars4, &bars8 })
    {
        const int bars = 1 << n++;
        b->setClickingTogglesState (true);
        b->setRadioGroupId (3);
        b->onClick = [this, b, bars] { if (b->getToggleState()) proc.setWindowBars (bars); };
        b->setTooltip ("Compare " + juce::String (bars) + (bars == 1 ? " bar" : " bars"));
    }
    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this] { proc.setFollowLoop (loopButton.getToggleState()); };
    loopButton.setTooltip ("Follow the length of your Ableton loop (1, 2, 4 or 8 bars)");
    auto nudge = [this] (int dir)
    {
        // click = 10 ms, shift-click = one 16th
        const bool coarse = juce::ModifierKeys::currentModifiers.isShiftDown();
        proc.nudgeGrid (dir * (coarse ? 15.0 / juce::jmax (1.0, proc.getBpm()) : 0.010));
    };
    nudgeLeft.onClick = [nudge] { nudge (-1); };
    nudgeRight.onClick = [nudge] { nudge (1); };

    bpmTitle.setText ("BPM", juce::dontSendNotification);
    bpmTitle.setFont (font (13.0f, true));
    bpmTitle.setColour (juce::Label::textColourId, muted);
    bpmLabel.setEditable (true);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (font (17.0f, true));
    bpmLabel.setColour (juce::Label::textColourId, red);
    bpmLabel.setColour (juce::Label::outlineColourId, redDim);
    bpmLabel.onTextChange = [this]
    {
        const double v = bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue();
        if (v >= 40.0 && v <= 300.0) proc.setBpm (v);
        refreshReferenceControls();
    };
    for (auto* l : { &detectedLabel, &offsetLabel })
    {
        l->setFont (font (12.5f));
        l->setColour (juce::Label::textColourId, muted);
    }

    // knobs
    for (auto* k : { &rangeKnob, &toleranceKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }
    rangeKnob.setRange (45.0, 80.0, 1.0);
    rangeKnob.setValue (proc.getSplit(), juce::dontSendNotification);
    rangeKnob.setDoubleClickReturnValue (true, 60.0);
    auto applyRange = [this] { proc.setSplitManual ((float) rangeKnob.getValue()); repaint (controlsR.toNearestInt()); };
    rangeKnob.onDragEnd = applyRange;
    rangeKnob.onValueChange = [this, applyRange]
    {
        if (! rangeKnob.isMouseButtonDown()) applyRange();
        repaint (controlsR.toNearestInt());
    };
    toleranceKnob.setRange (0.25, 3.0, 0.25);
    toleranceKnob.setValue (proc.getTolerance(), juce::dontSendNotification);
    toleranceKnob.setDoubleClickReturnValue (true, 1.0);
    toleranceKnob.onValueChange = [this] { proc.setTolerance ((float) toleranceKnob.getValue()); repaint (controlsR.toNearestInt()); };
    autoButton.onClick = [this] { proc.setRangeAuto(); };
    snapshotButton.onClick = [this] { proc.takeSnapshot(); };
    resetButton.onClick = [this] { proc.resetMeasurements(); };
    reanalyzeButton.onClick = [this] { proc.reanalyzeReference(); };
    snapshotButton.setTooltip ("Freeze the current readings so the cards show before -> now");
    resetButton.setTooltip ("Clear all messages and history; measure fresh from the next bar");

    for (auto* b : { &dj6, &dj9, &dj12 })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2);
    }
    dj6.onClick = [this] { if (dj6.getToggleState()) proc.setDjRefLevel (-6.0f); };
    dj9.onClick = [this] { if (dj9.getToggleState()) proc.setDjRefLevel (-9.0f); };
    dj12.onClick = [this] { if (dj12.getToggleState()) proc.setDjRefLevel (-12.0f); };
    const float dj = proc.getDjRefLevel();
    (dj > -7.5f ? dj6 : (dj > -10.5f ? dj9 : dj12)).setToggleState (true, juce::dontSendNotification);

    // match panel
    matchButton.onClick = [this] { proc.computeMatch(); refreshMatchControls(); };
    previewButton.setClickingTogglesState (true);
    previewButton.onClick = [this] { proc.setMatchPreview (previewButton.getToggleState()); refreshMatchControls(); };
    applyButton.onClick = [this] { proc.applyMatch(); refreshMatchControls(); };
    revertButton.onClick = [this] { proc.revertMatch(); refreshMatchControls(); };
    weightButton.setClickingTogglesState (true);
    weightButton.onClick = [this] { proc.setMatchWeight (weightButton.getToggleState()); refreshMatchControls(); };
    weightButton.setTooltip ("Gentle sub saturation: adds harmonics so the sub reads on smaller systems");

    // ---- Phase 3: reference slots
    for (auto* b : { &slotA, &slotB })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4);
    }
    slotA.onClick = [this] { if (slotA.getToggleState()) proc.setActiveSlot (0); };
    slotB.onClick = [this] { if (slotB.getToggleState()) proc.setActiveSlot (1); };
    slotA.setTooltip ("Reference A. With a pair selected, switching moves that pair to this reference.");
    slotB.setTooltip ("Reference B: load a second reference for sections the first one doesn't have.");

    // ---- learn pass and pairs
    learnButton.onClick = [this]
    {
        if (proc.getLearnState() == GroundShakeProcessor::LearnState::off) proc.startLearn();
        else proc.stopLearn();
    };
    learnButton.setTooltip ("Press, then play your song in Ableton from the song start bar to the end. Press again to finish.");
    learnPairButton.onClick = [this] { proc.startLearnPair (proc.getSelectedPair()); };
    learnPairButton.setTooltip ("Re-learn just the selected pair after a mix change: press, then play that section.");
    addPairButton.onClick = [this]
    {
        int a = 0, b = 0;
        if (arrangement.getDraft (a, b)) { proc.addPair (a, b); arrangement.clearDraft(); }
    };
    addPairButton.setTooltip ("Pair the stretch you dragged on your track with the reference window above.");
    deletePairButton.onClick = [this] { proc.deletePair (proc.getSelectedPair()); };
    autoPairButton.onClick = [this] { proc.autoPair(); };
    autoPairButton.setTooltip ("First guess: pair your drops with the reference's drop and your breakdowns with its breakdown.");
    matchAllButton.onClick = [this] { proc.matchAllPairs(); refreshMatchControls(); };
    matchAllButton.setTooltip ("Work out and apply the correction for every pair.");
    followButton.setClickingTogglesState (true);
    followButton.onClick = [this] { proc.setFollowPlayhead (followButton.getToggleState()); };
    followButton.setTooltip ("The readout follows the pair that's playing. Clicking a pair turns this off.");
    startDown.onClick = [this] { proc.setSongStartBar (proc.getSongStartBar() - 1); };
    startUp.onClick = [this] { proc.setSongStartBar (proc.getSongStartBar() + 1); };
    startDown.setTooltip ("The Ableton bar your song starts on (below 1 = AUTO: the first bar with sound). Remembered for every project.");
    arrangement.onDraftChanged = [this] { refreshMatchControls(); };

    // ---- correction knobs
    auto setupKnob = [] (juce::Slider& k, double lo, double hi, double step, double def)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k.setRange (lo, hi, step);
        k.setDoubleClickReturnValue (true, def);
    };
    setupKnob (feelKnob, -6.0, 6.0, 0.1, 0.0);
    setupKnob (punchKnob, -6.0, 6.0, 0.1, 0.0);
    setupKnob (attackKnob, -6.0, 6.0, 0.5, 0.0);
    setupKnob (tailKnob, -12.0, 6.0, 0.5, 0.0);
    setupKnob (mudKnob, -9.0, 0.0, 0.5, 0.0);
    setupKnob (lowCutKnob, 0.0, 250.0, 5.0, 0.0);
    for (auto& d : duckKnobs) setupKnob (d, -12.0, 3.0, 0.5, 0.0);
    for (auto* k : { &feelKnob, &punchKnob, &attackKnob, &tailKnob, &mudKnob, &lowCutKnob, &duckKnobs[0], &duckKnobs[1],
                     &duckKnobs[2], &duckKnobs[3] })
        k->onValueChange = [this] { applyKnobs(); };
    monoButton.setClickingTogglesState (true);
    monoButton.onClick = [this]
    {
        auto c = proc.getEditedCorrection();
        c.mono = monoButton.getToggleState();
        proc.setEditedCorrection (c);
        refreshMatchControls();
    };
    monoButton.setTooltip ("Sub mono below 120 Hz");

    fixList.onLayoutChanged = [this] { fixList.repaint(); };
    proc.onReferenceChanged = [this] { refreshReferenceControls(); };
    refreshReferenceControls();
    refreshMatchControls();
    refreshKnobs();
}

void GroundShakeContent::applyKnobs()
{
    auto c = proc.getEditedCorrection();
    c.feelDb = (float) feelKnob.getValue();
    c.punchDb = (float) punchKnob.getValue();
    c.attackDb = (float) attackKnob.getValue();
    c.tailDb = (float) tailKnob.getValue();
    if (std::abs (c.tailDb) > 0.01f && c.tailStartMs <= 0) c.tailStartMs = 150.0f;
    c.mudDb = (float) mudKnob.getValue();
    c.lowCutHz = lowCutKnob.getValue() < 20.0 ? 0.0f : (float) lowCutKnob.getValue();
    for (size_t k = 0; k < 4; ++k) c.duckDb[k] = (float) duckKnobs[k].getValue();
    proc.setEditedCorrection (c);
    repaint (matchR.toNearestInt());
}

void GroundShakeContent::refreshKnobs()
{
    for (auto* k : { &feelKnob, &punchKnob, &attackKnob, &tailKnob, &mudKnob, &lowCutKnob, &duckKnobs[0], &duckKnobs[1],
                     &duckKnobs[2], &duckKnobs[3] })
        if (k->isMouseButtonDown()) return;
    const auto& c = proc.getEditedCorrection();
    feelKnob.setValue (c.feelDb, juce::dontSendNotification);
    punchKnob.setValue (c.punchDb, juce::dontSendNotification);
    attackKnob.setValue (c.attackDb, juce::dontSendNotification);
    tailKnob.setValue (c.tailDb, juce::dontSendNotification);
    mudKnob.setValue (c.mudDb, juce::dontSendNotification);
    lowCutKnob.setValue (c.lowCutHz, juce::dontSendNotification);
    for (size_t k = 0; k < 4; ++k) duckKnobs[k].setValue (c.duckDb[k], juce::dontSendNotification);
}

GroundShakeContent::~GroundShakeContent()
{
    proc.onReferenceChanged = nullptr;
    setLookAndFeel (nullptr);
}

void GroundShakeContent::refreshReferenceControls()
{
    const bool has = proc.hasReference();
    bpmLabel.setText (has ? juce::String (proc.getBpm(), 2) : juce::String ("--"), juce::dontSendNotification);
    detectedLabel.setText (has ? "detected " + juce::String (proc.getDetectedBpm(), 2) : juce::String(),
                           juce::dontSendNotification);
    const int ms = (int) std::round (proc.getGridOffsetSec() * 1000.0);
    offsetLabel.setText (has ? "grid " + juce::String (ms >= 0 ? "+" : "") + juce::String (ms) + " ms" : juce::String(),
                         juce::dontSendNotification);
    for (auto* b : { &x2Button, &halfButton, &nudgeLeft, &nudgeRight, &bButton, &dropButton, &reanalyzeButton })
        b->setEnabled (has);
    breakdownButton.setEnabled (has && proc.getBreakdownBar() >= 0);
    const int wb = proc.getWindowBars();
    bars1.setToggleState (wb == 1, juce::dontSendNotification);
    bars2.setToggleState (wb == 2, juce::dontSendNotification);
    bars4.setToggleState (wb == 4, juce::dontSendNotification);
    bars8.setToggleState (wb == 8, juce::dontSendNotification);
    loopButton.setToggleState (proc.isFollowingLoop(), juce::dontSendNotification);
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    followButton.setToggleState (proc.isFollowingPlayhead(), juce::dontSendNotification);
    bpmLabel.setEditable (has);
    if (! rangeKnob.isMouseButtonDown())
        rangeKnob.setValue (proc.getSplit(), juce::dontSendNotification);
    repaint();
}

void GroundShakeContent::refreshMatchControls()
{
    const auto st = proc.getEditedMatchState();
    const int sel = proc.getSelectedPair();
    const bool pairMode = ! proc.getPairs().empty();
    const bool editable = ! pairMode || sel >= 0;
    const bool selAnalyzed = sel >= 0 && proc.getPairs()[(size_t) sel].analyzed;
    matchButton.setEnabled (sel >= 0 ? selAnalyzed : (! pairMode && proc.hasReference() && proc.liveHasData()));
    previewButton.setEnabled (editable && (st == MatchState::ready || st == MatchState::preview));
    previewButton.setToggleState (st == MatchState::preview, juce::dontSendNotification);
    applyButton.setEnabled (editable && (st == MatchState::ready || st == MatchState::preview));
    revertButton.setEnabled (editable && st != MatchState::off);
    weightButton.setEnabled (editable);
    monoButton.setEnabled (editable);
    weightButton.setToggleState (proc.getEditedCorrection().weight > 0.0f, juce::dontSendNotification);
    monoButton.setToggleState (proc.getEditedCorrection().mono, juce::dontSendNotification);
    for (auto* k : { &feelKnob, &punchKnob, &attackKnob, &tailKnob, &mudKnob, &lowCutKnob, &duckKnobs[0], &duckKnobs[1],
                     &duckKnobs[2], &duckKnobs[3] })
        k->setEnabled (editable);

    // learn + pair buttons
    const bool learningNow = proc.getLearnState() != GroundShakeProcessor::LearnState::off;
    learnButton.setButtonText (learningNow ? "STOP" : "LEARN");
    learnButton.setToggleState (learningNow, juce::dontSendNotification);
    learnPairButton.setEnabled (! learningNow && sel >= 0);
    int a = 0, b = 0;
    addPairButton.setEnabled (! learningNow && arrangement.getDraft (a, b) && proc.hasReference() && proc.getLearnedBars() > 0);
    deletePairButton.setEnabled (! learningNow && sel >= 0);
    autoPairButton.setEnabled (! learningNow && proc.hasLearnedTrack() && proc.hasReference());
    matchAllButton.setEnabled (! learningNow && pairMode);
    followButton.setToggleState (proc.isFollowingPlayhead(), juce::dontSendNotification);
    repaint (matchR.toNearestInt());
}

void GroundShakeContent::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadReferenceAsync (f);
                          });
}

void GroundShakeContent::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& path : files)
    {
        juce::File f (path);
        if (f.hasFileExtension ("wav;aif;aiff;mp3"))
        {
            proc.loadReferenceAsync (f);
            return;
        }
    }
}

juce::String GroundShakeContent::statusText() const
{
    if (proc.isLoading()) return "Analyzing the reference: tempo, downbeat, drop and sub...";
    if (proc.getLoadError().isNotEmpty()) return proc.getLoadError();
    const auto msg = proc.getLearnMessage();
    if (msg.isNotEmpty() && (proc.getLearnState() != GroundShakeProcessor::LearnState::off
                             || juce::Time::getMillisecondCounterHiRes() / 1000.0 - learnMessageTime < 8.0))
        return msg;
    const int sel = proc.getSelectedPair();
    const auto line = proc.getDisplayCoach().statusLine();
    return sel >= 0 ? "Pair " + juce::String (sel + 1) + ": " + line : line;
}

void GroundShakeContent::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // subtle vignette
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff14141a), getWidth() * 0.5f, getHeight() * 0.3f,
                                             bg, 0.0f, (float) getHeight(), true));
    g.fillRect (getLocalBounds());

    // header
    ledPanel (g, headerR, 10.0f, 0.8f);
    auto h = headerR.reduced (18.0f, 0.0f);
    g.setColour (muted);
    g.setFont (font (12.0f, true));
    g.drawText ("WOLLER", h.withWidth (80.0f), juce::Justification::centredLeft);
    g.setColour (red);
    g.setFont (font (26.0f, true));
    g.drawText ("GROUND SHAKE", h.withTrimmedLeft (72.0f).withWidth (300.0f), juce::Justification::centredLeft);
    g.setColour (muted.withAlpha (0.7f));
    g.setFont (font (11.0f));
    g.drawText ("phase 3", h.withTrimmedLeft (306.0f).withWidth (70.0f), juce::Justification::centredLeft);
    {
        auto st = juce::Rectangle<float> (392.0f, headerR.getY() + 6.0f, 646.0f, headerR.getHeight() - 12.0f);
        g.setColour (juce::Colour (0xff0b0b0e));
        g.fillRoundedRectangle (st, 6.0f);
        g.setColour (red.withAlpha (0.25f));
        g.drawRoundedRectangle (st, 6.0f, 1.0f);
        g.setColour (text);
        g.setFont (font (13.5f));
        g.drawFittedText (statusText(), st.reduced (12.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
    }

    ledPanel (g, refR, 12.0f);
    ledPanel (g, controlsR, 12.0f);
    ledPanel (g, fixesR, 12.0f);
    ledPanel (g, matchR, 12.0f);

    // fixes title
    {
        auto t = fixesR.reduced (16.0f, 0.0f).withHeight (30.0f);
        g.setColour (muted);
        g.setFont (font (13.0f, true));
        g.drawText ("FIXES", t, juce::Justification::centredLeft);
        g.setFont (font (11.0f));
        g.drawText ("in fix order  |  click a card to open it", t, juce::Justification::centredRight);
    }

    // controls row labels
    {
        const float y0 = controlsR.getY();
        g.setFont (font (11.5f, true));
        g.setColour (muted);
        g.drawText ("RANGE", juce::Rectangle<float> (108.0f, y0 + 8.0f, 190.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (font (13.0f, true));
        const int sp = (int) std::round (proc.getSplit());
        g.drawText ("split " + juce::String (sp) + " Hz", juce::Rectangle<float> (108.0f, y0 + 24.0f, 190.0f, 18.0f), juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.setColour (muted.brighter (0.2f));
        g.drawText ("feel 20-" + juce::String (sp) + "  punch " + juce::String (sp) + "-" + juce::String (sp * 2),
                    juce::Rectangle<float> (108.0f, y0 + 42.0f, 200.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (proc.isRangeAuto() ? green : yellow);
        g.drawText (proc.isRangeAuto() ? "Auto (from reference)" : "Manual", juce::Rectangle<float> (176.0f, y0 + 60.0f, 130.0f, 18.0f),
                    juce::Justification::centredLeft);

        g.setFont (font (11.5f, true));
        g.setColour (muted);
        g.drawText ("TOLERANCE", juce::Rectangle<float> (404.0f, y0 + 8.0f, 120.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (font (13.0f, true));
        g.drawText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (proc.getTolerance(), 2) + " dB",
                    juce::Rectangle<float> (404.0f, y0 + 24.0f, 120.0f, 18.0f), juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.setColour (muted.brighter (0.2f));
        g.drawText ("how strict green is", juce::Rectangle<float> (404.0f, y0 + 42.0f, 120.0f, 14.0f), juce::Justification::centredLeft);

        g.setFont (font (11.5f, true));
        g.setColour (muted);
        g.drawText ("REKORDBOX REF", juce::Rectangle<float> (540.0f, y0 + 8.0f, 140.0f, 16.0f), juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.setColour (muted.brighter (0.2f));
        g.drawText ("Auto Gain level (dB)", juce::Rectangle<float> (540.0f, y0 + 24.0f, 140.0f, 14.0f), juce::Justification::centredLeft);
    }

    // song start (in the pairs row)
    {
        const float y = refR.getBottom() - 38.0f;
        g.setFont (font (10.5f, true));
        g.setColour (muted);
        g.drawText ("SONG START BAR", juce::Rectangle<float> (652.0f, y, 120.0f, 30.0f), juce::Justification::centredRight);
        g.setFont (font (14.0f, true));
        g.setColour (text);
        const int sb = proc.getSongStartBar();
        g.drawText (sb > 0 ? juce::String (sb) : juce::String ("AUTO"), juce::Rectangle<float> (802.0f, y, 42.0f, 30.0f),
                    juce::Justification::centred);
        if (proc.hasLearnedTrack() || proc.getLearnedBars() > 0)
        {
            g.setFont (font (10.0f));
            g.setColour (muted);
            g.drawText ("learned from bar " + juce::String (proc.getEffectiveStartBar()),
                        juce::Rectangle<float> (878.0f, y, 74.0f, 30.0f), juce::Justification::centredLeft, true);
        }
    }

    // correction panel text
    {
        auto t = matchR.reduced (16.0f, 0.0f).withHeight (28.0f);
        const int sel = proc.getSelectedPair();
        const bool pairMode = ! proc.getPairs().empty();
        g.setColour (muted);
        g.setFont (font (13.0f, true));
        g.drawText (sel >= 0 ? "CORRECTION  -  PAIR " + juce::String (sel + 1)
                             : (pairMode ? juce::String ("CORRECTION  -  select a pair") : juce::String ("CORRECTION  -  whole track")),
                    t, juce::Justification::centredLeft);
        const auto st = proc.getEditedMatchState();
        const juce::String stateText = pairMode && sel < 0 ? "unmarked parts are left alone"
                                     : st == MatchState::off     ? "off: audio untouched"
                                     : st == MatchState::ready   ? "ready: PREVIEW or APPLY"
                                     : st == MatchState::preview ? "previewing (level-matched)"
                                                                 : "ON";
        g.setColour (st == MatchState::applied ? green : (st == MatchState::off ? muted : yellow));
        g.setFont (font (11.5f, st == MatchState::applied));
        g.drawText (stateText, t, juce::Justification::centredRight);

        const auto& c = proc.getEditedCorrection();
        auto db = [] (float v) { return juce::String (v >= 0 ? "+" : "") + juce::String (v, 1); };
        const char* names[] = { "FEEL", "PUNCH", "ATTACK", "TAIL", "MUD", "LOW CUT" };
        const juce::String vals[] = { db (c.feelDb), db (c.punchDb), db (c.attackDb), db (c.tailDb), db (c.mudDb),
                                      c.lowCutHz > 0 ? juce::String (juce::roundToInt (c.lowCutHz)) + " Hz" : juce::String ("off") };
        for (int i = 0; i < 6; ++i)
        {
            auto r = juce::Rectangle<float> (976.0f + i * 66.0f, matchR.getY() + 114.0f, 66.0f, 13.0f);
            g.setFont (font (10.0f, true));
            g.setColour (muted);
            g.drawText (names[i], r, juce::Justification::centred);
            g.setFont (font (10.5f));
            g.setColour (text);
            g.drawText (vals[i], r.translated (0.0f, 13.0f), juce::Justification::centred);
        }
        g.setFont (font (10.0f, true));
        g.setColour (muted);
        g.drawText ("DUCK", juce::Rectangle<float> (980.0f, matchR.getY() + 150.0f, 64.0f, 14.0f), juce::Justification::centredLeft);
        g.setFont (font (9.5f));
        g.drawText ("built-in,", juce::Rectangle<float> (980.0f, matchR.getY() + 164.0f, 64.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("per 16th", juce::Rectangle<float> (980.0f, matchR.getY() + 176.0f, 64.0f, 12.0f), juce::Justification::centredLeft);
        for (int k = 0; k < 4; ++k)
        {
            auto r = juce::Rectangle<float> (1046.0f + k * 58.0f, matchR.getY() + 190.0f, 58.0f, 12.0f);
            g.setFont (font (9.5f, true));
            g.setColour (muted);
            g.drawText ("16th " + juce::String (k + 1), r, juce::Justification::centred);
            g.setFont (font (10.0f));
            g.setColour (text);
            g.drawText (db (c.duckDb[(size_t) k]), r.translated (0.0f, 12.0f), juce::Justification::centred);
        }
        g.setFont (font (10.5f));
        g.setColour (muted);
        g.drawText ("ceiling", juce::Rectangle<float> (1290.0f, matchR.getY() + 158.0f, 90.0f, 14.0f), juce::Justification::centred);
        g.setColour (text);
        g.drawText (juce::String (proc.getMatchCeilingDb(), 1) + " dB", juce::Rectangle<float> (1290.0f, matchR.getY() + 172.0f, 90.0f, 14.0f),
                    juce::Justification::centred);
    }
}

void GroundShakeContent::resized()
{
    headerR    = { 16.0f, 12.0f, 1368.0f, 56.0f };
    feetR      = { 16.0f, 80.0f, 940.0f, 170.0f };
    gridR      = { 16.0f, 262.0f, 940.0f, 116.0f };
    refR       = { 16.0f, 390.0f, 940.0f, 240.0f };
    stripR     = { 16.0f, 642.0f, 940.0f, 84.0f };
    controlsR  = { 16.0f, 738.0f, 940.0f, 86.0f };
    checklistR = { 968.0f, 80.0f, 416.0f, 192.0f };
    fixesR     = { 968.0f, 284.0f, 416.0f, 306.0f };
    matchR     = { 968.0f, 602.0f, 416.0f, 222.0f };

    feet.setBounds (feetR.toNearestInt());
    grid.setBounds (gridR.toNearestInt());
    arrangement.setBounds (refR.withHeight (162.0f).toNearestInt());
    strip.setBounds (stripR.toNearestInt());
    checklist.setBounds (checklistR.toNearestInt());
    fixViewport.setBounds (juce::Rectangle<int> (976, 314, 402, 268));
    fixList.refresh (fixViewport.getMaximumVisibleWidth());

    aButton.setBounds (juce::Rectangle<int> (1056, 22, 160, 36));
    bButton.setBounds (juce::Rectangle<int> (1222, 22, 156, 36));

    // reference title row: slots + window length
    const int ty = (int) refR.getY() + 4;
    slotA.setBounds (122, ty, 26, 22);
    slotB.setBounds (150, ty, 26, 22);
    bars1.setBounds (440, ty, 30, 22);
    bars2.setBounds (474, ty, 30, 22);
    bars4.setBounds (508, ty, 30, 22);
    bars8.setBounds (542, ty, 30, 22);
    loopButton.setBounds (580, ty, 54, 22);

    // reference controls row
    const int y = (int) refR.getY() + 164;
    loadButton.setBounds (32, y, 84, 30);
    bpmTitle.setBounds (126, y, 38, 30);
    bpmLabel.setBounds (162, y + 1, 78, 28);
    detectedLabel.setBounds (244, y, 108, 30);
    x2Button.setBounds (354, y, 44, 30);
    halfButton.setBounds (402, y, 44, 30);
    nudgeLeft.setBounds (452, y, 36, 30);
    nudgeRight.setBounds (492, y, 36, 30);
    offsetLabel.setBounds (530, y, 90, 30);
    dropButton.setBounds (622, y, 150, 30);
    breakdownButton.setBounds (778, y, 162, 30);

    // learn + pairs row
    const int y2 = (int) refR.getBottom() - 38;
    learnButton.setBounds (32, y2, 80, 30);
    learnPairButton.setBounds (116, y2, 96, 30);
    addPairButton.setBounds (216, y2, 70, 30);
    deletePairButton.setBounds (290, y2, 70, 30);
    autoPairButton.setBounds (364, y2, 92, 30);
    matchAllButton.setBounds (460, y2, 96, 30);
    followButton.setBounds (560, y2, 82, 30);
    startDown.setBounds (774, y2 + 2, 26, 26);
    startUp.setBounds (846, y2 + 2, 26, 26);

    // knobs + buttons row
    const int cy = (int) controlsR.getY();
    rangeKnob.setBounds (28, cy + 6, 76, 76);
    autoButton.setBounds (106, cy + 58, 64, 24);
    toleranceKnob.setBounds (324, cy + 6, 76, 76);
    dj6.setBounds (540, cy + 46, 44, 28);
    dj9.setBounds (588, cy + 46, 44, 28);
    dj12.setBounds (636, cy + 46, 48, 28);
    snapshotButton.setBounds (700, cy + 10, 122, 30);
    resetButton.setBounds (826, cy + 10, 120, 30);
    reanalyzeButton.setBounds (700, cy + 46, 246, 30);

    // correction panel
    const int my = (int) matchR.getY() + 30;
    matchButton.setBounds (980, my, 62, 28);
    previewButton.setBounds (1046, my, 86, 28);
    applyButton.setBounds (1136, my, 56, 28);
    revertButton.setBounds (1196, my, 60, 28);
    weightButton.setBounds (1260, my, 60, 28);
    monoButton.setBounds (1324, my, 52, 28);
    int i = 0;
    for (auto* k : { &feelKnob, &punchKnob, &attackKnob, &tailKnob, &mudKnob, &lowCutKnob })
        k->setBounds (984 + 66 * i++, (int) matchR.getY() + 62, 50, 50);
    for (int k = 0; k < 4; ++k)
        duckKnobs[(size_t) k].setBounds (1055 + 58 * k, (int) matchR.getY() + 148, 40, 40);
}

void GroundShakeContent::tick (double t)
{
    feet.tick (t);
    grid.repaint();
    const bool moving = proc.getRefPlayPositionSec() >= 0 || proc.isLoading() || proc.isHostPlaying()
                        || proc.getLearnState() != GroundShakeProcessor::LearnState::off;
    if (moving && tickCount % 2 == 0) arrangement.repaint();
    if (++tickCount % 6 == 0)                    // ~10 times a second
    {
        if (proc.getLearnMessage() != shownLearnMessage)
        {
            shownLearnMessage = proc.getLearnMessage();
            learnMessageTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        }
        checklist.repaint();
        strip.repaint();
        arrangement.repaint();
        fixList.refresh (fixViewport.getMaximumVisibleWidth());
        grid.setHighlight (fixList.expandedHighlight());
        refreshMatchControls();
        refreshKnobs();
        slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
        slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
        repaint (headerR.toNearestInt());
        repaint (refR.withTrimmedTop (200.0f).toNearestInt());
        if (! rangeKnob.isMouseButtonDown() && std::abs (rangeKnob.getValue() - proc.getSplit()) > 0.4)
        {
            rangeKnob.setValue (proc.getSplit(), juce::dontSendNotification);
            repaint (controlsR.toNearestInt());
        }
    }
}

//==============================================================================
//  Editor: hosts the content and scales it with the window
//==============================================================================
GroundShakeEditor::GroundShakeEditor (GroundShakeProcessor& p)
    : AudioProcessorEditor (&p), proc (p), content (p)
{
    addAndMakeVisible (content);
    content.setBounds (0, 0, GroundShakeContent::baseW, GroundShakeContent::baseH);
    setResizable (true, true);
    setResizeLimits (840, 504, 2100, 1260);
    getConstrainer()->setFixedAspectRatio ((double) GroundShakeContent::baseW / GroundShakeContent::baseH);
    setSize (1260, 756);
    startTimerHz (60);
}

GroundShakeEditor::~GroundShakeEditor() { stopTimer(); }

void GroundShakeEditor::paint (juce::Graphics& g) { g.fillAll (gsui::bg); }

void GroundShakeEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / GroundShakeContent::baseW));
}

void GroundShakeEditor::timerCallback()
{
    content.tick ((juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0);
}
