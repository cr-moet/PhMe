#include "PluginProcessor.h"
#include "PluginEditor.h"

PhMeProcessor::PhMeProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PhMe", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PhMeProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const auto seconds = AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction (
        [] (float v, int) { return v < 1.0f ? String (juce::roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; });
    const auto decibels = AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
        [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; });
    const auto percent = AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
        [] (float v, int) { return String (juce::roundToInt (v)) + " %"; });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::average, 1 }, "Average",
                                                       NormalisableRange<float> (0.05f, 5.0f, 0.0f, 0.4f), 0.5f, seconds));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::resolution, 1 }, "Resolution",
                                                        StringArray { "1/3 OCT", "1/6 OCT", "1/12 OCT", "1/24 OCT", "1/48 OCT" }, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::hold, 1 }, "Hold", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::zoom, 1 }, "Zoom",
                                                       NormalisableRange<float> (-12.0f, 36.0f, 0.1f), 0.0f, decibels));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoGain, 1 }, "Auto Gain", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::persist, 1 }, "Persistence",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, percent));
    return layout;
}

void PhMeProcessor::prepareToPlay (double sampleRate, int)
{
    analysisRate.store (sampleRate);
}

bool PhMeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void PhMeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    // Copy into the FIFO; when the editor is closed it fills up and the rest is dropped
    const float* left  = buffer.getReadPointer (0);
    const float* right = buffer.getReadPointer (juce::jmin (1, buffer.getNumChannels() - 1));
    const auto scope = fifo.write (juce::jmin (n, fifo.getFreeSpace()));
    for (auto [start, size, offset] : { std::tuple { scope.startIndex1, scope.blockSize1, 0 },
                                        std::tuple { scope.startIndex2, scope.blockSize2, scope.blockSize1 } })
        if (size > 0)
        {
            fifoBuffer.copyFrom (0, start, left + offset, size);
            fifoBuffer.copyFrom (1, start, right + offset, size);
        }
}

int PhMeProcessor::readSamples (float* left, float* right, int maxSamples) noexcept
{
    const auto scope = fifo.read (juce::jmin (maxSamples, fifo.getNumReady()));
    for (auto [start, size, offset] : { std::tuple { scope.startIndex1, scope.blockSize1, 0 },
                                        std::tuple { scope.startIndex2, scope.blockSize2, scope.blockSize1 } })
        if (size > 0)
        {
            std::copy_n (fifoBuffer.getReadPointer (0, start), size, left + offset);
            std::copy_n (fifoBuffer.getReadPointer (1, start), size, right + offset);
        }
    return scope.blockSize1 + scope.blockSize2;
}

juce::AudioProcessorEditor* PhMeProcessor::createEditor()
{
    return new PhMeEditor (*this);
}

void PhMeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PhMeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhMeProcessor();
}
