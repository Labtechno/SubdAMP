#include "plugin/Parameters.h"

namespace subdamp::params
{

namespace
{
    juce::String formatDecibels (float value, int /*maximumStringLength*/)
    {
        return juce::String (value, 1) + " dB";
    }

    std::unique_ptr<juce::AudioParameterFloat> makeGainParameter (const char* parameterId,
                                                                  const juce::String& name,
                                                                  float minDb,
                                                                  float maxDb)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { parameterId, addedInVersion1 },
            name,
            juce::NormalisableRange<float> { minDb, maxDb, 0.1f },
            0.0f,
            juce::AudioParameterFloatAttributes{}
                .withLabel ("dB")
                .withStringFromValueFunction (formatDecibels));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeGainParameter (id::inputGain, juce::String::fromUTF8 ("Gain d'entrée"), -24.0f, 24.0f));
    layout.add (makeGainParameter (id::outputGain, "Volume de sortie", -60.0f, 12.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, addedInVersion1 }, "Bypass", false));

    return layout;
}

} // namespace subdamp::params
