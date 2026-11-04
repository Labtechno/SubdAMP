#pragma once

#include "plugin/CabSection.h"
#include "plugin/Parameters.h"
#include "subdamp/dsp/AmpSection.h"
#include "subdamp/dsp/BypassCrossfade.h"
#include "subdamp/dsp/FrequencyShifter.h"
#include "subdamp/dsp/GainStage.h"
#include "subdamp/dsp/NoiseGate.h"
#include "subdamp/dsp/OctaveDivider.h"
#include "subdamp/dsp/SwitchableEffect.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
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

/** État d'un fichier chargé (capture d'ampli ou IR), pour l'interface. */
struct FileSlotStatus
{
    juce::String fileName;  // vide : aucun fichier
    juce::String message;   // erreur, ou « Chargement… »
    bool isLoading = false;
    bool hasError = false;
};

/**
    Processeur principal de SubdAmp.

    Chaîne mono : entrée → gate → [slot PRE] → ampli → cab → [slot POST] → sortie, puis
    recopie en stéréo. Les modules de pitch (SUB, FREQ SHIFT) se placent dans le slot PRE
    ou POST selon leur paramètre de position. Les entrées stéréo sont acceptées pour les DAW
    qui n'ont que des pistes stéréo : seul le canal gauche est alors utilisé.
*/
class SubdAmpProcessor final : public juce::AudioProcessor,
                               private juce::AsyncUpdater,
                               private juce::Timer
{
public:
    SubdAmpProcessor();
    ~SubdAmpProcessor() override;

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
    double getTailLengthSeconds() const override;

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

    //==============================================================================
    // Fichiers (thread de l'interface)
    void loadAmpModel (const juce::File& file);
    void clearAmpModel();
    void loadCabImpulseResponse (const juce::File& file);
    void clearCabImpulseResponse();

    FileSlotStatus getAmpStatus() const;
    FileSlotStatus getCabStatus() const;

    /** Fichiers mémorisés dans l'état (chemin complet), pour ouvrir le sélecteur au bon endroit. */
    juce::File getAmpModelFile() const;
    juce::File getCabImpulseResponseFile() const;

    /** Prévient l'interface quand l'état d'un fichier change. */
    juce::ChangeBroadcaster& getFileStatusBroadcaster() noexcept { return fileStatusBroadcaster; }

private:
    juce::AudioProcessorEditor* createEditor() override;

    void handleAsyncUpdate() override;
    void timerCallback() override;

    void processMonoChunk (float* samples, int numSamples) noexcept;
    void updateTargetsFromParameters() noexcept;
    void restoreFilesFromState();
    void setStatus (FileSlotStatus& slot, FileSlotStatus newStatus);

    juce::AudioProcessorValueTreeState parameters;

    /** Valeurs brutes des paramètres, mises en cache : lecture atomique dans le thread audio. */
    struct Parameters
    {
        std::atomic<float>& inputGain;
        std::atomic<float>& outputGain;
        std::atomic<float>& gateEnabled;
        std::atomic<float>& gateThreshold;
        std::atomic<float>& gateRelease;
        std::atomic<float>& ampEnabled;
        std::atomic<float>& ampGain;
        std::atomic<float>& ampBass;
        std::atomic<float>& ampMid;
        std::atomic<float>& ampTreble;
        std::atomic<float>& ampLevel;
        std::atomic<float>& cabEnabled;
        std::atomic<float>& subEnabled;
        std::atomic<float>& subPosition;
        std::atomic<float>& subDry;
        std::atomic<float>& subDown1;
        std::atomic<float>& subDown2;
        std::atomic<float>& subUp;
        std::atomic<float>& subTone;
        std::atomic<float>& fsEnabled;
        std::atomic<float>& fsPosition;
        std::atomic<float>& fsShift;
        std::atomic<float>& fsMix;
        std::atomic<float>& fsFeedback;
    };

    Parameters raw;
    juce::AudioParameterBool& bypassParameter;

    //==============================================================================
    // Chaîne audio
    dsp::GainStage inputGain, outputGain;
    dsp::BypassCrossfade bypass;

    dsp::NoiseGate gate;
    dsp::OctaveDivider sub;
    dsp::FrequencyShifter shifter;
    dsp::AmpSection amp;
    CabSection cab;

    dsp::SwitchableEffect gateSlot { gate }, subSlot { sub }, shifterSlot { shifter }, ampSlot { amp }, cabSlot { cab };

    /** Module de pitch déplaçable avant ou après l'ampli. */
    struct MovableModule
    {
        dsp::SwitchableEffect& slot;
        std::atomic<float>& enabled;
        std::atomic<float>& position;
        params::Position activePosition;
    };

    // Ordre de traitement à l'intérieur d'un même slot : SUB puis FREQ SHIFT.
    std::array<MovableModule, 2> movableModules;

    juce::AudioBuffer<float> dryBuffer; // copie du signal d'entrée, pour le bypass
    int preparedBlockSize = 0;

    LevelMeters meters;

    //==============================================================================
    // Fichiers
    mutable juce::CriticalSection statusLock; // jamais pris par le thread audio
    FileSlotStatus ampStatus, cabStatus;
    juce::ChangeBroadcaster fileStatusBroadcaster;

    // En dernier : détruit en premier, il attend la fin d'un chargement en cours
    // avant que les membres utilisés par ce chargement ne disparaissent.
    juce::ThreadPool loaderThread { juce::ThreadPoolOptions{}.withThreadName ("SubdAmp loader").withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubdAmpProcessor)
};

} // namespace subdamp
