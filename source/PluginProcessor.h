#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
// PhMe - phase meter. The audio passes unchanged; the processor only copies the
// samples into a lock-free FIFO, all analysis runs in the editor (message thread).
//==============================================================================
namespace ParamIDs
{
    inline constexpr const char* average    = "average";      // s, time averaging of the correlation
    inline constexpr const char* resolution = "resolution";   // smoothing over frequency (octave fraction)
    inline constexpr const char* hold       = "hold";         // show the minimum since the last reset
    inline constexpr const char* zoom       = "zoom";         // dB, scope gain (when auto gain is off)
    inline constexpr const char* autoGain   = "autoGain";     // scopes follow the level
    inline constexpr const char* persist    = "persist";      // %, afterglow of the scopes
}

class PhMeProcessor final : public juce::AudioProcessor
{
public:
    PhMeProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Reader side of the FIFO (one reader: the editor). Returns the number of samples copied.
    int readSamples (float* left, float* right, int maxSamples) noexcept;
    double getAnalysisSampleRate() const noexcept       { return analysisRate.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int fifoSize = 1 << 17;   // about 2.7 s at 48 kHz
    juce::AbstractFifo fifo { fifoSize };
    juce::AudioBuffer<float> fifoBuffer { 2, fifoSize };
    std::atomic<double> analysisRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhMeProcessor)
};
