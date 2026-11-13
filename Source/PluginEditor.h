#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace gsui
{
    // shiny black + red LED palette
    const juce::Colour bg        { 0xff07070a };
    const juce::Colour panelTop  { 0xff17171c };
    const juce::Colour panelBot  { 0xff0c0c10 };
    const juce::Colour red       { 0xffff2b2b };
    const juce::Colour redDim    { 0xff6e1212 };
    const juce::Colour text      { 0xfff1f1f3 };
    const juce::Colour muted     { 0xff8b8f99 };
    const juce::Colour green     { 0xff35e07a };
    const juce::Colour yellow    { 0xffffd23f };
    const juce::Colour blue      { 0xff3fa9ff };
    const juce::Colour over      { 0xffff3b3b };
    const juce::Colour grey      { 0xff6d717b };

    void ledPanel (juce::Graphics&, juce::Rectangle<float>, float radius = 10.0f, float glow = 1.0f);

    // match state of YOUR foot against the reference
    enum class Match { none, matched, slightlyOff, wellUnder, wellOver };
    Match classify (float diffDb, float tolerance = 1.0f);
    juce::Colour colourFor (Match);
    juce::String labelFor (Match);
    juce::Colour colourFor (gs::Status);

    class LedLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LedLookAndFeel();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int h) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle,
                               float endAngle, juce::Slider&) override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                            int thumbStart, int thumbSize, bool over, bool down) override;
    };
}

//==============================================================================
class FeetView : public juce::Component
{
public:
    explicit FeetView (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick (double timeSec);

private:
    void drawFoot (juce::Graphics&, juce::Rectangle<float> area, bool leftFoot, juce::Colour, float shake, double t);
    GroundShakeProcessor& proc;
    double time = 0;
    float youShake = 0, refShake = 0;          // smoothed 0..1
};

//==============================================================================
class StepGridView : public juce::Component
{
public:
    explicit StepGridView (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void setHighlight (int mask) { if (mask != highlight) { highlight = mask; repaint(); } }

private:
    GroundShakeProcessor& proc;
    int highlight = 0;
};

//==============================================================================
class ReferenceView : public juce::Component
{
public:
    explicit ReferenceView (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    float xForTime (double sec) const;
    double timeForX (float x) const;
    juce::Rectangle<float> waveArea() const;
    GroundShakeProcessor& proc;
    int dragGrabBarOffset = 0;
};

//==============================================================================
// Mono, kick/bass, notes, squash, mud and DJ readings in one row of tiles
class DiagnosticsStrip : public juce::Component
{
public:
    explicit DiagnosticsStrip (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    GroundShakeProcessor& proc;
};

//==============================================================================
// The live checklist with the overall match %
class ChecklistView : public juce::Component
{
public:
    explicit ChecklistView (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    GroundShakeProcessor& proc;
};

//==============================================================================
// The fixes list: one card per problem, in fix order. Click a card to open it.
class FixListView : public juce::Component
{
public:
    explicit FixListView (GroundShakeProcessor& p) : proc (p) {}
    void refresh (int viewWidth);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int expandedHighlight() const;             // grid steps of the open card
    std::function<void()> onLayoutChanged;

private:
    float layoutCard (juce::Graphics*, const gs::FixCard&, bool expanded, juce::Rectangle<float> area, int index);
    bool isExpanded (const gs::FixCard&) const;
    GroundShakeProcessor& proc;
    std::vector<gs::FixCard> cards;
    std::vector<juce::Rectangle<float>> cardRects;
    struct Hit { juce::Rectangle<float> r; int action; };   // 1 = find the drop, 2 = that's intended
    std::vector<Hit> hits;
    int userExpanded = -1;                     // CheckId the user opened (-1 = follow "fix this first")
    bool userCollapsed = false;
};

//==============================================================================
class GroundShakeContent : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    static constexpr int baseW = 1400, baseH = 820;
    explicit GroundShakeContent (GroundShakeProcessor&);
    ~GroundShakeContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (double t);

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void refreshReferenceControls();
    void refreshMatchControls();
    void chooseFile();
    juce::String statusText() const;

    GroundShakeProcessor& proc;
    gsui::LedLookAndFeel lnf;
    FeetView feet;
    StepGridView grid;
    ReferenceView refView;
    DiagnosticsStrip strip;
    ChecklistView checklist;
    FixListView fixList;
    juce::Viewport fixViewport;
    juce::TextButton aButton { "A: YOUR MIX" }, bButton { "B: REFERENCE" };
    juce::TextButton loadButton { "LOAD" }, x2Button { "x2" }, halfButton { "/2" },
                     nudgeLeft { "<" }, nudgeRight { ">" }, dropButton { "FIND THE DROP" },
                     breakdownButton { "FIND BREAKDOWN" };
    juce::TextButton bars1 { "1" }, bars2 { "2" }, bars4 { "4" }, bars8 { "8" }, loopButton { "LOOP" };
    juce::Label bpmLabel, bpmTitle, detectedLabel, offsetLabel;
    // controls row
    juce::Slider rangeKnob, toleranceKnob;
    juce::TextButton autoButton { "AUTO" }, snapshotButton { "SNAPSHOT" }, resetButton { "RESET" },
                     reanalyzeButton { "RE-ANALYZE REF" };
    juce::TextButton dj6 { "-6" }, dj9 { "-9" }, dj12 { "-12" };
    // match panel
    juce::TextButton matchButton { "MATCH" }, previewButton { "PREVIEW A/B" }, applyButton { "APPLY" },
                     revertButton { "REVERT" }, weightButton { "WEIGHT" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> headerR, feetR, gridR, refR, stripR, controlsR, checklistR, fixesR, matchR;
    int tickCount = 0;
};

//==============================================================================
class GroundShakeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GroundShakeEditor (GroundShakeProcessor&);
    ~GroundShakeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    GroundShakeProcessor& proc;
    GroundShakeContent content;
    double startTime = juce::Time::getMillisecondCounterHiRes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroundShakeEditor)
};
