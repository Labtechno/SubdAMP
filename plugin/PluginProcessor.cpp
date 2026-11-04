#include "plugin/PluginProcessor.h"

#include "gui/PluginEditor.h"
#include "plugin/Parameters.h"
#include "plugin/StateVersioning.h"
#include "subdamp/dsp/PeakMeter.h"

#include <algorithm>
#include <vector>

namespace subdamp
{

namespace
{
    const juce::Identifier stateType { "SubdAmpState" };

    std::atomic<float>& rawParameter (juce::AudioProcessorValueTreeState& apvts, const char* parameterId)
    {
        auto* value = apvts.getRawParameterValue (parameterId);
        jassert (value != nullptr);
        return *value;
    }

    juce::AudioParameterBool& boolParameter (juce::AudioProcessorValueTreeState& apvts, const char* parameterId)
    {
        auto* parameter = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (parameterId));
        jassert (parameter != nullptr);
        return *parameter;
    }

    /** Seul le thread audio écrit ; l'interface lit puis remet à zéro. */
    void publishPeak (std::atomic<float>& meter, float peak) noexcept
    {
        if (peak > meter.load (std::memory_order_relaxed))
            meter.store (peak, std::memory_order_relaxed);
    }

    std::vector<state::ParameterDefault> collectDefaults (const juce::AudioProcessor& processor)
    {
        std::vector<state::ParameterDefault> defaults;

        for (auto* parameter : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                defaults.push_back ({ ranged->getParameterID(),
                                      ranged->convertFrom0to1 (ranged->getDefaultValue()) });

        return defaults;
    }
}

SubdAmpProcessor::SubdAmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Guitare", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createLayout()),
      inputGainDb (rawParameter (parameters, params::id::inputGain)),
      outputGainDb (rawParameter (parameters, params::id::outputGain)),
      bypassParameter (boolParameter (parameters, params::id::bypass))
{
}

//==============================================================================
void SubdAmpProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const dsp::ProcessSpec spec { sampleRate, maximumExpectedSamplesPerBlock };

    inputGain.prepare (spec);
    outputGain.prepare (spec);
    bypass.prepare (spec);

    dryBuffer.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    preparedBlockSize = maximumExpectedSamplesPerBlock;

    // Démarre directement sur les valeurs courantes, sans rampe depuis zéro.
    updateTargetsFromParameters();
    reset();
}

void SubdAmpProcessor::releaseResources()
{
    dryBuffer.setSize (0, 0);
    preparedBlockSize = 0;
}

void SubdAmpProcessor::reset()
{
    inputGain.reset();
    outputGain.reset();
    bypass.reset();
}

bool SubdAmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto isMonoOrStereo = [] (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };

    return isMonoOrStereo (layouts.getMainInputChannelSet())
        && isMonoOrStereo (layouts.getMainOutputChannelSet());
}

//==============================================================================
void SubdAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0 || buffer.getNumChannels() == 0)
        return;

    if (preparedBlockSize <= 0)
    {
        jassertfalse; // processBlock appelé sans prepareToPlay
        buffer.clear();
        return;
    }

    // Sans entrée active, le canal 0 contient des données arbitraires.
    if (getTotalNumInputChannels() == 0)
        buffer.clear (0, 0, numSamples);

    updateTargetsFromParameters();

    // Certains hôtes envoient parfois plus d'échantillons qu'annoncé dans prepareToPlay :
    // on découpe pour ne jamais dépasser la taille des tampons préalloués.
    auto* mono = buffer.getWritePointer (0);

    for (int start = 0; start < numSamples; start += preparedBlockSize)
        processMonoChunk (mono + start, std::min (preparedBlockSize, numSamples - start));

    for (int channel = 1; channel < getTotalNumOutputChannels(); ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples);
}

void SubdAmpProcessor::processMonoChunk (float* samples, int numSamples) noexcept
{
    const auto size = static_cast<std::size_t> (numSamples);
    const std::span<float> block { samples, size };
    const std::span<float> dry { dryBuffer.getWritePointer (0), size };

    publishPeak (meters.inputPeak, dsp::blockPeak (block));

    std::copy (block.begin(), block.end(), dry.begin());

    // Bypass complètement engagé : rien à calculer, le fondu recopie le signal sec.
    if (! bypass.isFullyBypassed())
    {
        inputGain.process (block);
        // La chaîne de modules (gate, slots pitch, ampli, cab) s'insérera ici.
        outputGain.process (block);
    }

    bypass.process (dry, block);

    publishPeak (meters.outputPeak, dsp::blockPeak (block));
}

void SubdAmpProcessor::updateTargetsFromParameters() noexcept
{
    inputGain.setGainDecibels (inputGainDb.load (std::memory_order_relaxed));
    outputGain.setGainDecibels (outputGainDb.load (std::memory_order_relaxed));
    bypass.setBypassed (bypassParameter.get());
}

//==============================================================================
void SubdAmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (state::versionProperty, state::currentVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SubdAmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto loaded = juce::ValueTree::fromXml (*xml);

    if (! loaded.hasType (stateType))
        return;

    // Un état venant d'une version plus récente n'est pas compris : on garde le réglage courant
    // plutôt que de charger un son faux.
    if (state::migrateToCurrentVersion (loaded) == state::MigrationResult::fromNewerVersion)
        return;

    const auto defaults = collectDefaults (*this);
    state::addMissingParameters (loaded, defaults);

    parameters.replaceState (loaded);
}

juce::AudioProcessorParameter* SubdAmpProcessor::getBypassParameter() const
{
    return &bypassParameter;
}

juce::AudioProcessorEditor* SubdAmpProcessor::createEditor()
{
    return new gui::SubdAmpEditor (*this);
}

} // namespace subdamp

//==============================================================================
// Point d'entrée exigé par les wrappers de plugin JUCE.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new subdamp::SubdAmpProcessor();
}
