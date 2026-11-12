#include "PluginEditor.h"

PhMeEditor::PhMeEditor (PhMeProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = processorRef.apvts;
    average    = apvts.getRawParameterValue (ParamIDs::average);
    resolution = apvts.getRawParameterValue (ParamIDs::resolution);
    hold       = apvts.getRawParameterValue (ParamIDs::hold);
    zoom       = apvts.getRawParameterValue (ParamIDs::zoom);
    autoGain   = apvts.getRawParameterValue (ParamIDs::autoGain);
    persist    = apvts.getRawParameterValue (ParamIDs::persist);

    for (auto* c : std::initializer_list<juce::Component*> { &correlationView, &correlationMeter, &polarSample, &polarLevel, &lissajous })
        addAndMakeVisible (c);
    correlationView.onReset = [this] { correlation.resetMinimum(); };
    correlationView.setTooltip ("Click to reset the minimum (HOLD)");

    const auto setUpKnob = [this, &apvts] (LabelledKnob& k, const char* id, const juce::String& text)
    {
        k.knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
        k.knob.setColour (juce::Slider::textBoxBackgroundColourId, Colours::text);   // LED-style value display
        k.knob.setColour (juce::Slider::textBoxTextColourId, Colours::accent);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, k.knob);
        k.label.setText (text, juce::dontSendNotification);
        k.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        k.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (k.knob);
        addAndMakeVisible (k.label);
    };
    setUpKnob (averageKnob, ParamIDs::average, "AVERAGE");
    setUpKnob (zoomKnob, ParamIDs::zoom, "ZOOM");
    setUpKnob (persistKnob, ParamIDs::persist, "PERSISTENCE");

    resolutionLabel.setText ("RESOLUTION", juce::dontSendNotification);
    resolutionLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    resolutionLabel.setJustificationType (juce::Justification::centred);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::resolution)))
        resolutionBox.addItemList (choice->choices, 1);
    resolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, ParamIDs::resolution, resolutionBox);
    resolutionBox.setTooltip ("Smoothing over frequency");

    for (auto* b : { &holdButton, &autoGainButton })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("led", true);
    }
    holdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ParamIDs::hold, holdButton);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ParamIDs::autoGain, autoGainButton);
    holdButton.setTooltip ("Show the lowest correlation per frequency since the last reset (dashed red line)");
    resetButton.setTooltip ("Reset the minimum");
    resetButton.onClick = [this] { correlation.resetMinimum(); };
    autoGainButton.setTooltip ("The scopes follow the level; off: ZOOM");
    autoGainButton.onStateChange = [this] { zoomKnob.knob.setEnabled (! autoGainButton.getToggleState()); };
    autoGainButton.onStateChange();
    for (auto* c : std::initializer_list<juce::Component*> { &resolutionLabel, &resolutionBox, &holdButton, &resetButton, &autoGainButton })
        addAndMakeVisible (c);

    // Samples left in the FIFO from before the editor was open are old: drop them
    readL.resize (16384);
    readR.resize (16384);
    while (processorRef.readSamples (readL.data(), readR.data(), (int) readL.size()) > 0) {}
    correlation.prepare (processorRef.getAnalysisSampleRate());

    setResizable (true, true);
    setResizeLimits (1040, 700, 2400, 1600);
    setSize (1240, 800);
    sendLookAndFeelChange();   // the value boxes of the knobs were created before the look was set
    startTimerHz (60);
}

PhMeEditor::~PhMeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

float PhMeEditor::scopeGain()
{
    // 1 = the edge of the scopes; manual: a full scale mono signal touches the edge at 0 dB
    constexpr float sqrtHalf = 0.70710678f;
    const float target = autoGain->load() > 0.5f
                             ? juce::jlimit (0.5f, 250.0f, 0.9f / juce::jmax (1.0e-6f, broadband.getPeak()))
                             : juce::Decibels::decibelsToGain (zoom->load()) * sqrtHalf;
    shownGain *= std::pow (target / shownGain, 0.15f);   // glide (in dB)
    return shownGain;
}

void PhMeEditor::timerCallback()
{
    if (! juce::approximatelyEqual (correlation.getSampleRate(), processorRef.getAnalysisSampleRate()))
        correlation.prepare (processorRef.getAnalysisSampleRate());
    const double sampleRate = correlation.getSampleRate();
    const float seconds = average->load(), persistence = persist->load() * 0.01f;

    bool analysed = false;
    int total = 0;
    for (int n; (n = processorRef.readSamples (readL.data() + total, readR.data() + total, (int) readL.size() - total)) > 0;)
    {
        analysed = correlation.push (readL.data() + total, readR.data() + total, n, seconds) || analysed;
        broadband.push (readL.data() + total, readR.data() + total, n, seconds, sampleRate);
        total += n;
        if (total == (int) readL.size())   // a long gap: the scopes only need the newest samples
            total = 0;
    }

    // Scopes: what arrived since the last frame (at most 1/10 s)
    const float gain = scopeGain();
    const int shown = juce::jmin (total, (int) (sampleRate * 0.1));
    const float* l = readL.data() + total - shown;
    const float* r = readR.data() + total - shown;
    const auto gainText = (autoGain->load() > 0.5f ? "AUTO " : "") + Plot::signedValue (juce::Decibels::gainToDecibels (gain * 1.41421356f), 1) + " dB";
    for (Scope* scope : std::initializer_list<Scope*> { &polarSample, &polarLevel, &lissajous })
    {
        scope->addSamples (l, r, shown, gain, persistence);
        scope->setGainText (gainText);
    }

    if (analysed)
    {
        static constexpr float octaves[] { 1.0f / 3.0f, 1.0f / 6.0f, 1.0f / 12.0f, 1.0f / 24.0f };
        correlation.evaluate (curve, octaves[(size_t) juce::jlimit (0, 3, juce::roundToInt (resolution->load()))]);
        correlationView.setCurve (curve, hold->load() > 0.5f);
    }
    correlationMeter.setValue (broadband.correlation());
}

void PhMeEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Title: filled box with the name in the background colour
    const juce::Rectangle<float> box (14.0f, 10.0f, 80.0f, 34.0f);
    g.setColour (Colours::text);
    g.fillRect (box);
    g.setColour (Colours::background);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("PhMe", box, juce::Justification::centred);

    g.setColour (Colours::text.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText ("PHASE METER", juce::Rectangle<float> (108.0f, 10.0f, 300.0f, 34.0f), juce::Justification::centredLeft);

    g.setColour (Colours::text.withAlpha (0.2f));
    g.drawHorizontalLine (headerHeight + 4, 0.0f, (float) getWidth());

    for (auto r : { correlationPanel, scopePanel })
        Plot::drawScrewedPanel (g, r.toFloat());

    g.setColour (Colours::text);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    for (const auto& [r, text] : titles)
        g.drawText (text, r, juce::Justification::centredLeft);
}

void PhMeEditor::resized()
{
    auto area = getLocalBounds().reduced (14, 0);
    area.removeFromTop (headerHeight + 14);
    area.removeFromBottom (14);

    auto controls = area.removeFromBottom (150);
    area.removeFromBottom (14);

    const int scopeWidth = juce::jlimit (340, 560, area.getWidth() * 3 / 10);
    auto scopes = area.removeFromRight (scopeWidth);
    area.removeFromRight (18);

    // Correlation over frequency and the broadband correlation
    titles[0] = { area.removeFromTop (18).withTrimmedLeft (34), "PHASE CORRELATION OVER FREQUENCY" };
    auto meter = area.removeFromBottom (64);
    titles[1] = { meter.removeFromTop (18).withTrimmedLeft (34), "CORRELATION" };
    correlationMeter.setBounds (meter);
    area.removeFromBottom (8);
    correlationView.setBounds (area);

    // The three scopes
    const int gap = 10, each = (scopes.getHeight() - 2 * gap) / 3;
    int index = 2;
    for (auto [scope, text] : { std::pair<Scope*, const char*> { &polarSample, "POLAR SAMPLE" },
                                std::pair<Scope*, const char*> { &polarLevel, "POLAR LEVEL" },
                                std::pair<Scope*, const char*> { &lissajous, "LISSAJOUS" } })
    {
        auto cell = scopes.removeFromTop (each);
        scopes.removeFromTop (gap);
        titles[(size_t) index++] = { cell.removeFromTop (18), text };
        scope->setBounds (cell);
    }

    // Controls
    scopePanel = controls.removeFromRight (scopeWidth);
    controls.removeFromRight (18);
    correlationPanel = controls;

    const auto inner = [] (juce::Rectangle<int> r) { return r.reduced (30, 14); };
    const auto place = [] (LabelledKnob& k, juce::Rectangle<int> cell)
    {
        k.label.setBounds (cell.removeFromTop (16));
        k.knob.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 100), cell.getHeight()));
    };

    auto c = inner (correlationPanel);
    place (averageKnob, c.removeFromLeft (120));
    c.removeFromLeft (24);
    auto column = c.removeFromLeft (150).withSizeKeepingCentre (150, 100);
    resolutionLabel.setBounds (column.removeFromTop (18));
    resolutionBox.setBounds (column.removeFromTop (30));
    column.removeFromTop (12);
    holdButton.setBounds (column.removeFromLeft (72).withHeight (38));
    column.removeFromLeft (6);
    resetButton.setBounds (column.withHeight (38));

    auto s = inner (scopePanel);
    const int knobWidth = juce::jmin (110, (s.getWidth() - 100) / 2);
    place (zoomKnob, s.removeFromLeft (knobWidth));
    place (persistKnob, s.removeFromLeft (knobWidth));
    autoGainButton.setBounds (s.withSizeKeepingCentre (juce::jmin (s.getWidth(), 100), 44));
}
