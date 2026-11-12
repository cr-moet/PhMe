#pragma once

#include "PluginProcessor.h"
#include "Views.h"

//==============================================================================
// Left: phase correlation over frequency with the broadband correlation below it.
// Right: three vectorscopes (polar sample, polar level, Lissajous).
// Below: the controls on two screwed panels.
//==============================================================================
class PhMeEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit PhMeEditor (PhMeProcessor&);
    ~PhMeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    float scopeGain();

    PhMeProcessor& processorRef;
    PhMeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    // Analysis (message thread)
    Analysis::Correlation correlation;
    Analysis::Broadband broadband;
    Analysis::Curve curve;
    std::vector<float> readL, readR;
    float shownGain = 1.0f;

    // Displays
    CorrelationView correlationView;
    CorrelationMeter correlationMeter;
    PolarSampleScope polarSample;
    PolarLevelScope polarLevel;
    LissajousScope lissajous;

    // Controls
    struct LabelledKnob
    {
        juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    LabelledKnob averageKnob, zoomKnob, persistKnob;
    juce::Label resolutionLabel;
    juce::ComboBox resolutionBox;
    juce::TextButton holdButton { "HOLD" }, resetButton { "RESET" }, autoGainButton { "AUTO GAIN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> resolutionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment, autoGainAttachment;

    // Raw parameter values for the timer
    std::atomic<float> *average = nullptr, *resolution = nullptr, *hold = nullptr, *zoom = nullptr, *autoGain = nullptr, *persist = nullptr;

    // Layout (for paint)
    static constexpr int headerHeight = 48;
    juce::Rectangle<int> correlationPanel, scopePanel;
    std::array<std::pair<juce::Rectangle<int>, const char*>, 5> titles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhMeEditor)
};
