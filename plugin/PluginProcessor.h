#pragma once

#include "subdamp/dsp/BypassCrossfade.h"
#include "subdamp/dsp/GainStage.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace subdamp
{

/**
    Niveaux crête publiés par le thread audio pour les vumètres de l'interface.

    Le thread audio ne fait que monter la valeur ; l'interface la lit et la remet à zéro
    (exchange), ce qui donne la crête depuis la dernière lecture sans aucun verrou.
*/
struct LevelMeters
{
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };

    static_assert (std::atomic<float>::is_always_lock_free);
};

/**
    Processeur principal de SubdAmp.

    Le signal guitare est traité en mono (canal 0 de l'entrée), puis recopié sur toutes
    les sorties. Les entrées stéréo sont acceptées pour les DAW qui n'ont que des pistes
    stéréo : seul le canal gauche est alors utilisé.
*/
class SubdAmpProcessor final : public juce::AudioProcessor
{
public:
    SubdAmpProcessor();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeters& getMeters() noexcept { return meters; }

private:
    juce::AudioProcessorEditor* createEditor() override;

    void processMonoChunk (float* samples, int numSamples) noexcept;
    void updateTargetsFromParameters() noexcept;

    juce::AudioProcessorValueTreeState parameters;

    // Pointeurs mis en cache à la construction : lecture atomique, sans recherche par nom dans le thread audio.
    std::atomic<float>& inputGainDb;
    std::atomic<float>& outputGainDb;
    juce::AudioParameterBool& bypassParameter;

    dsp::GainStage inputGain;
    dsp::GainStage outputGain;
    dsp::BypassCrossfade bypass;

    juce::AudioBuffer<float> dryBuffer; // copie du signal d'entrée, pour le bypass
    int preparedBlockSize = 0;

    LevelMeters meters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubdAmpProcessor)
};

} // namespace subdamp
