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

    const auto live = proc.getLiveProfile();
    const auto& ref = proc.getReferenceProfile();
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

    const auto live = proc.getLiveProfile();
    const auto& ref = proc.getReferenceProfile();
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
//  Reference waveform + 8-bar window
//==============================================================================
juce::Rectangle<float> ReferenceView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (16.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (10.0f);
}

float ReferenceView::xForTime (double sec) const
{
    auto* ref = proc.getReference();
    auto a = waveArea();
    if (ref == nullptr) return a.getX();
    return a.getX() + (float) (sec / ref->lengthSeconds()) * a.getWidth();
}

double ReferenceView::timeForX (float x) const
{
    auto* ref = proc.getReference();
    auto a = waveArea();
    if (ref == nullptr) return 0.0;
    return (x - a.getX()) / a.getWidth() * ref->lengthSeconds();
}

void ReferenceView::paint (juce::Graphics& g)
{
    auto a = waveArea();                 // the panel itself is drawn by the parent (it wraps the controls too)
    g.setColour (juce::Colour (0xff0d0d11));
    g.fillRoundedRectangle (a, 6.0f);

    auto* ref = proc.getReference();
    if (ref == nullptr || proc.isLoading())
    {
        g.setColour (muted);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (proc.isLoading() ? "Analyzing reference..." : "Drop a WAV, AIFF or MP3 here, or press LOAD",
                    a, juce::Justification::centred);
        return;
    }

    const double bpm = proc.getBpm(), barSec = 240.0 / bpm;
    const int barsAvail = proc.getNumReferenceBars();
    const int bars = juce::jlimit (1, gs::kBars, barsAvail);
    const double winStart = proc.getGridStartSec() + proc.getWindowStartBar() * barSec;
    const double winEnd = winStart + bars * barSec;
    const float wx0 = xForTime (winStart), wx1 = xForTime (winEnd);

    // waveform
    const auto n = ref->overviewMin.size();
    float peak = 1.0e-6f;
    for (size_t i = 0; i < n; ++i) peak = std::max ({ peak, ref->overviewMax[i], -ref->overviewMin[i] });
    const float mid = a.getCentreY(), half = a.getHeight() * 0.44f / peak;
    for (size_t i = 0; i < n; ++i)
    {
        const float x = a.getX() + a.getWidth() * (float) i / (float) n;
        const bool inside = x >= wx0 && x <= wx1;
        g.setColour (inside ? red.withAlpha (0.9f) : redDim.withAlpha (0.8f));
        g.drawVerticalLine ((int) x, mid - ref->overviewMax[i] * half, mid - ref->overviewMin[i] * half + 1.0f);
    }

    // bar lines (every 4 bars brighter)
    for (int b = 0;; ++b)
    {
        const double t = proc.getGridStartSec() + b * barSec;
        if (t > ref->lengthSeconds()) break;
        if (t < 0) continue;
        g.setColour (juce::Colours::white.withAlpha (b % 4 == 0 ? 0.18f : 0.06f));
        g.drawVerticalLine ((int) xForTime (t), a.getY(), a.getBottom());
    }

    // the 8-bar window
    auto win = juce::Rectangle<float> (wx0, a.getY(), wx1 - wx0, a.getHeight());
    g.setColour (red.withAlpha (0.10f));
    g.fillRoundedRectangle (win, 4.0f);
    for (auto [w, al] : { std::pair { 6.0f, 0.10f }, { 3.0f, 0.25f } })
    {
        g.setColour (red.withAlpha (al));
        g.drawRoundedRectangle (win, 4.0f, w);
    }
    g.setColour (red);
    g.drawRoundedRectangle (win, 4.0f, 1.4f);

    // playhead while listening to B
    const double ph = proc.getRefPlayPositionSec();
    if (ph >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine ((int) xForTime (ph), a.getY(), a.getBottom());
    }

    // header text
    auto head = getLocalBounds().toFloat().reduced (16.0f, 0.0f).withHeight (32.0f);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (muted);
    g.drawText ("REFERENCE", head, juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (ref->name, head.withTrimmedLeft (90.0f).withTrimmedRight (240.0f), juce::Justification::centredLeft, true);
    g.setColour (red);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const int first = proc.getWindowStartBar() + 1;
    g.drawText (juce::String (bars) + " BARS  (bars " + juce::String (first) + "-" + juce::String (first + bars - 1)
                    + " of " + juce::String (barsAvail) + ")  - drag to move",
                head, juce::Justification::centredRight);
}

void ReferenceView::mouseDown (const juce::MouseEvent& e)
{
    if (proc.getReference() == nullptr) return;
    const double barSec = 240.0 / proc.getBpm();
    const int barUnder = (int) std::floor ((timeForX ((float) e.x) - proc.getGridStartSec()) / barSec);
    const int start = proc.getWindowStartBar();
    const int bars = juce::jlimit (1, gs::kBars, proc.getNumReferenceBars());
    if (barUnder >= start && barUnder < start + bars)
        dragGrabBarOffset = barUnder - start;
    else
    {
        dragGrabBarOffset = bars / 2;
        proc.setWindowStartBar (barUnder - dragGrabBarOffset);
    }
}

void ReferenceView::mouseDrag (const juce::MouseEvent& e)
{
    if (proc.getReference() == nullptr) return;
    const double barSec = 240.0 / proc.getBpm();
    const int barUnder = (int) std::floor ((timeForX ((float) e.x) - proc.getGridStartSec()) / barSec);
    const int target = barUnder - dragGrabBarOffset;
    if (target != proc.getWindowStartBar())
        proc.setWindowStartBar (target);              // snaps to whole bars
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
    const auto& coach = proc.getCoach();
    const auto you = proc.getLiveMeasurements();
    const auto& ref = proc.getReferenceMeasurements();
    const auto liveP = proc.getLiveProfile();
    const auto& refP = proc.getReferenceProfile();

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
        t.l1 = "1st 16th vs ref";
        t.l2 = "levels lined up";
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
    const auto& coach = proc.getCoach();

    auto head = inner.removeFromTop (20.0f);
    g.setFont (font (13.0f, true));
    g.setColour (muted);
    g.drawText ("CHECKLIST", head, juce::Justification::centredLeft);
    const float pct = coach.matchPercent();
    g.setColour (pct < 0 ? muted : (pct >= 90 ? green : (pct >= 60 ? yellow : over)));
    g.drawText (pct < 0 ? juce::String ("match: --") : "match: " + juce::String (juce::roundToInt (pct)) + "%", head,
                juce::Justification::centredRight);

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
    const float colW = inner.getWidth() / 2.0f, rowH = inner.getHeight() / 5.0f;
    for (size_t i = 0; i < rows.size() && i < 10; ++i)
    {
        const int col = (int) i / 5, row = (int) i % 5;
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
    cards = proc.getCoach().cards();
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
        return y + 10.0f - area.getY();
    }

    const float gap = 6.0f;
    y += para (g, "Comparing to:", c.comparing, x, y, w, text) + gap;
    y += para (g, "What we see:", c.see, x, y, w, text) + gap;
    if (c.beatGraph)
    {
        if (g != nullptr)
        {
            const auto bs = gs::beatShape (proc.getLiveProfile(), proc.getReferenceProfile());
            drawBeatGraph (*g, { x, y, w, 104.0f }, bs, c.beatMask, c.severity == Status::holding ? green : yellow);
        }
        y += 104.0f + gap + 2.0f;
    }
    y += para (g, "Likely cause:", c.cause, x, y, w, text) + gap;
    if (! c.how.isEmpty())
    {
        const bool kick = c.id == gs::CheckId::beatShape || c.id == gs::CheckId::kickBass;
        y += para (g, kick ? "How (Kickstart 2):" : "How:", " ", x, y, w, text);
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
        if (c.offerDismiss)  button ("THAT'S INTENDED", 2);
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
    : proc (p), feet (p), grid (p), refView (p), strip (p), checklist (p), fixList (p)
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<juce::Component*> {
             &feet, &grid, &refView, &strip, &checklist, &fixViewport, &aButton, &bButton, &loadButton, &x2Button,
             &halfButton, &nudgeLeft, &nudgeRight, &dropButton, &bpmLabel, &bpmTitle, &detectedLabel, &offsetLabel,
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
    dropButton.setTooltip ("Jump the 8-bar window to the reference's fullest, loudest section");
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

    fixList.onLayoutChanged = [this] { fixList.repaint(); };
    proc.onReferenceChanged = [this] { refreshReferenceControls(); };
    refreshReferenceControls();
    refreshMatchControls();
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
    bpmLabel.setEditable (has);
    if (! rangeKnob.isMouseButtonDown())
        rangeKnob.setValue (proc.getSplit(), juce::dontSendNotification);
    repaint();
}

void GroundShakeContent::refreshMatchControls()
{
    const auto st = proc.getMatchState();
    matchButton.setEnabled (proc.hasReference() && proc.liveHasData());
    previewButton.setEnabled (st == MatchState::ready || st == MatchState::preview);
    previewButton.setToggleState (st == MatchState::preview, juce::dontSendNotification);
    applyButton.setEnabled (st == MatchState::ready || st == MatchState::preview);
    revertButton.setEnabled (st != MatchState::off);
    weightButton.setToggleState (proc.getMatchSettings().weight > 0.0f, juce::dontSendNotification);
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
    return proc.getCoach().statusLine();
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
    g.drawText ("phase 2", h.withTrimmedLeft (306.0f).withWidth (60.0f), juce::Justification::centredLeft);
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
        g.setFont (font (11.5f, true));
        g.setColour (muted);
        g.drawText ("RANGE", juce::Rectangle<float> (108.0f, 730.0f, 190.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (font (13.0f, true));
        const int sp = (int) std::round (proc.getSplit());
        g.drawText ("split " + juce::String (sp) + " Hz", juce::Rectangle<float> (108.0f, 746.0f, 190.0f, 18.0f), juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.setColour (muted.brighter (0.2f));
        g.drawText ("feel 20-" + juce::String (sp) + "  punch " + juce::String (sp) + "-" + juce::String (sp * 2),
                    juce::Rectangle<float> (108.0f, 764.0f, 200.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (proc.isRangeAuto() ? green : yellow);
        g.drawText (proc.isRangeAuto() ? "Auto (from reference)" : "Manual", juce::Rectangle<float> (176.0f, 782.0f, 130.0f, 18.0f),
                    juce::Justification::centredLeft);

        g.setFont (font (11.5f, true));
        g.setColour (muted);
        g.drawText ("TOLERANCE", juce::Rectangle<float> (404.0f, 730.0f, 120.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (font (13.0f, true));
        g.drawText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (proc.getTolerance(), 2) + " dB",
                    juce::Rectangle<float> (404.0f, 746.0f, 120.0f, 18.0f), juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.setColour (muted.brighter (0.2f));
        g.drawText ("how strict green is", juce::Rectangle<float> (404.0f, 764.0f, 120.0f, 14.0f), juce::Justification::centredLeft);

        g.setFont (font (11.5f, true));
        g.setColour (muted);
        g.drawText ("REKORDBOX REF", juce::Rectangle<float> (540.0f, 730.0f, 140.0f, 16.0f), juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.setColour (muted.brighter (0.2f));
        g.drawText ("Auto Gain level (dB)", juce::Rectangle<float> (540.0f, 746.0f, 140.0f, 14.0f), juce::Justification::centredLeft);
    }

    // match panel text
    {
        auto t = matchR.reduced (16.0f, 0.0f).withHeight (28.0f);
        g.setColour (muted);
        g.setFont (font (13.0f, true));
        g.drawText ("MATCH", t, juce::Justification::centredLeft);
        const auto st = proc.getMatchState();
        const juce::String stateText = st == MatchState::off     ? "off: audio untouched"
                                     : st == MatchState::ready   ? "ready: press PREVIEW to hear it"
                                     : st == MatchState::preview ? "previewing (level-matched)"
                                                                 : "APPLIED: correction is on";
        g.setColour (st == MatchState::applied ? green : (st == MatchState::off ? muted : yellow));
        g.setFont (font (11.5f, st == MatchState::applied));
        g.drawText (stateText, t, juce::Justification::centredRight);

        const auto& m = proc.getMatchSettings();
        auto line = juce::Rectangle<float> (matchR.getX() + 16.0f, matchR.getBottom() - 30.0f, matchR.getWidth() - 32.0f, 22.0f);
        g.setFont (font (11.5f));
        g.setColour (st == MatchState::off ? muted : text);
        auto db = [] (float v) { return juce::String (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
        g.drawText ("feel " + db (m.feelDb) + "   punch " + db (m.punchDb) + "   mono " + (m.mono ? "on" : "off")
                        + "   ceiling " + juce::String (proc.getMatchCeilingDb(), 1) + " dB",
                    line, juce::Justification::centredLeft);
    }
}

void GroundShakeContent::resized()
{
    headerR    = { 16.0f, 12.0f, 1368.0f, 56.0f };
    feetR      = { 16.0f, 80.0f, 940.0f, 222.0f };
    gridR      = { 16.0f, 314.0f, 940.0f, 134.0f };
    refR       = { 16.0f, 460.0f, 940.0f, 150.0f };
    stripR     = { 16.0f, 622.0f, 940.0f, 88.0f };
    controlsR  = { 16.0f, 722.0f, 940.0f, 86.0f };
    checklistR = { 968.0f, 80.0f, 416.0f, 196.0f };
    fixesR     = { 968.0f, 288.0f, 416.0f, 410.0f };
    matchR     = { 968.0f, 710.0f, 416.0f, 98.0f };

    feet.setBounds (feetR.toNearestInt());
    grid.setBounds (gridR.toNearestInt());
    refView.setBounds (refR.withTrimmedBottom (48.0f).toNearestInt());
    strip.setBounds (stripR.toNearestInt());
    checklist.setBounds (checklistR.toNearestInt());
    fixViewport.setBounds (juce::Rectangle<int> (976, 318, 402, 372));
    fixList.refresh (fixViewport.getMaximumVisibleWidth());

    aButton.setBounds (juce::Rectangle<int> (1056, 22, 160, 36));
    bButton.setBounds (juce::Rectangle<int> (1222, 22, 156, 36));

    // controls row under the waveform
    const int y = (int) refR.getBottom() - 44;
    loadButton.setBounds (32, y, 90, 32);
    bpmTitle.setBounds (150, y, 40, 32);
    bpmLabel.setBounds (190, y + 1, 84, 30);
    detectedLabel.setBounds (280, y, 120, 32);
    x2Button.setBounds (404, y, 50, 32);
    halfButton.setBounds (458, y, 50, 32);
    nudgeLeft.setBounds (540, y, 40, 32);
    nudgeRight.setBounds (584, y, 40, 32);
    offsetLabel.setBounds (630, y, 110, 32);
    dropButton.setBounds (770, y, 170, 32);

    // knobs + buttons row
    rangeKnob.setBounds (28, 728, 76, 76);
    autoButton.setBounds (106, 780, 64, 24);
    toleranceKnob.setBounds (324, 728, 76, 76);
    dj6.setBounds (540, 768, 44, 28);
    dj9.setBounds (588, 768, 44, 28);
    dj12.setBounds (636, 768, 48, 28);
    snapshotButton.setBounds (700, 732, 122, 30);
    resetButton.setBounds (826, 732, 120, 30);
    reanalyzeButton.setBounds (700, 768, 246, 30);

    // match panel
    const int my = 742;
    matchButton.setBounds (980, my, 76, 30);
    previewButton.setBounds (1058, my, 104, 30);
    applyButton.setBounds (1164, my, 68, 30);
    revertButton.setBounds (1234, my, 72, 30);
    weightButton.setBounds (1308, my, 70, 30);
}

void GroundShakeContent::tick (double t)
{
    feet.tick (t);
    grid.repaint();
    if (proc.getRefPlayPositionSec() >= 0 || proc.isLoading()) refView.repaint();
    if (++tickCount % 6 == 0)                    // ~10 times a second
    {
        checklist.repaint();
        strip.repaint();
        fixList.refresh (fixViewport.getMaximumVisibleWidth());
        grid.setHighlight (fixList.expandedHighlight());
        refreshMatchControls();
        repaint (headerR.toNearestInt());
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
    setResizeLimits (840, 492, 2100, 1230);
    getConstrainer()->setFixedAspectRatio ((double) GroundShakeContent::baseW / GroundShakeContent::baseH);
    setSize (1260, 738);
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
