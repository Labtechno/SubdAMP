#pragma once

#include "plugin/CabSection.h"
#include "plugin/Modules.h"
#include "plugin/Parameters.h"
#include "subdamp/dsp/AmpSection.h"
#include "subdamp/dsp/BypassCrossfade.h"
#include "subdamp/dsp/FrequencyShifter.h"
#include "subdamp/dsp/GainStage.h"
#include "subdamp/dsp/Harmonizer.h"
#include "subdamp/dsp/NoiseGate.h"
#include "subdamp/dsp/OctaveDivider.h"
#include "subdamp/dsp/SwitchableEffect.h"
#include "subdamp/dsp/GranularCloud.h"
#include "subdamp/dsp/PolyShifter.h"
#include "subdamp/dsp/ShimmerReverb.h"
#include "subdamp/dsp/SpectralFreeze.h"
#include "subdamp/dsp/StereoTexture.h"
#include "subdamp/dsp/Whammy.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <optional>

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

/** Latence perçue (thread de l'interface). */
struct LatencyStatus
{
    // Son principal (LIVE ou JOUABLE)
    dsp::LatencyTier tier = dsp::LatencyTier::live;
    double milliseconds = 0.0;

    // Bus de textures, en parallèle
    bool texturesActive = false;
    double textureMilliseconds = 0.0;

    bool operator== (const LatencyStatus&) const = default;
};

/**
    Processeur principal de SubdAmp.

    Chaîne mono : entrée → gate → [slot PRE] → ampli → cab → [slot POST] → sortie, puis
    recopie en stéréo. Les modules de pitch (SUB, WHAMMY, HARMONIZER, FREQ SHIFT) se placent
    dans le slot PRE ou POST selon leur paramètre de position. Les entrées stéréo sont acceptées pour les DAW
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

    //==============================================================================
    // État pour l'interface (thread de l'interface)
    bool isModuleEnabled (ModuleId module) const noexcept;
    bool isModuleAfterAmp (ModuleId module) const noexcept;

    /** Retard propre à un module, en millisecondes. */
    double getModuleLatencyMs (ModuleId module) const noexcept;

    /** Latence du son entendu : catégorie la plus lente parmi les modules actifs, et retard total. */
    LatencyStatus getLatencyStatus() const noexcept;

    /** Tempo utilisé par les textures (celui du DAW s'il en donne un, sinon le réglage). */
    double getTempo() const noexcept { return displayedTempo.load (std::memory_order_relaxed); }

    /** Module ouvert dans l'interface (conservé quand on ferme et rouvre la fenêtre). */
    ModuleId openModule = ModuleId::amp;

private:
    juce::AudioProcessorEditor* createEditor() override;

    void handleAsyncUpdate() override;
    void timerCallback() override;

    void processChunk (float* left, float* right, int numSamples) noexcept;
    void updateTransport() noexcept;
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
        std::atomic<float>& whEnabled;
        std::atomic<float>& whPosition;
        std::atomic<float>& whInterval;
        std::atomic<float>& whMode;
        std::atomic<float>& whGlide;
        std::atomic<float>& whSensitivity;
        std::atomic<float>& whMix;
        std::atomic<float>& hmEnabled;
        std::atomic<float>& hmPosition;
        std::atomic<float>& hmKey;
        std::atomic<float>& hmScale;
        std::atomic<float>& hmVoice1;
        std::atomic<float>& hmLevel1;
        std::atomic<float>& hmVoice2;
        std::atomic<float>& hmLevel2;
        std::atomic<float>& hmFormant;
        std::atomic<float>& hmDirect;
    };

    /** Paramètres des textures, mis en cache de la même façon. */
    struct TextureParameters
    {
        std::atomic<float>& shEnabled;
        std::atomic<float>& shSource;
        std::atomic<float>& shInterval;
        std::atomic<float>& shDecay;
        std::atomic<float>& shAmount;
        std::atomic<float>& shTone;
        std::atomic<float>& shLevel;
        std::atomic<float>& poEnabled;
        std::atomic<float>& poSource;
        std::atomic<float>& poInterval1;
        std::atomic<float>& poLevel1;
        std::atomic<float>& poInterval2;
        std::atomic<float>& poLevel2;
        std::atomic<float>& poFormant;
        std::atomic<float>& poWidth;
        std::atomic<float>& frEnabled;
        std::atomic<float>& frSource;
        std::atomic<float>& frHold;
        std::atomic<float>& frAttack;
        std::atomic<float>& frRelease;
        std::atomic<float>& frTone;
        std::atomic<float>& frLevel;
        std::atomic<float>& grEnabled;
        std::atomic<float>& grSource;
        std::atomic<float>& grMode;
        std::atomic<float>& grSize;
        std::atomic<float>& grDensity;
        std::atomic<float>& grPitch;
        std::atomic<float>& grSpread;
        std::atomic<float>& grReverse;
        std::atomic<float>& grDivision;
        std::atomic<float>& grRepeats;
        std::atomic<float>& grLevel;
        std::atomic<float>& tempo;
    };

    Parameters raw;
    TextureParameters textureRaw;
    juce::AudioParameterBool& bypassParameter;

    //==============================================================================
    // Chaîne audio
    dsp::GainStage inputGain, outputGain;
    dsp::BypassCrossfade bypass;

    dsp::NoiseGate gate;
    dsp::OctaveDivider sub;
    dsp::Whammy whammy;
    dsp::Harmonizer harmonizer;
    dsp::FrequencyShifter shifter;
    dsp::AmpSection amp;
    CabSection cab;

    dsp::SwitchableEffect gateSlot { gate }, subSlot { sub }, whammySlot { whammy }, harmonizerSlot { harmonizer },
                          shifterSlot { shifter }, ampSlot { amp }, cabSlot { cab };

    std::array<dsp::SwitchableEffect*, 7> allSlots { &gateSlot, &subSlot, &whammySlot, &harmonizerSlot,
                                                     &shifterSlot, &ampSlot, &cabSlot };

    /** Module de pitch déplaçable avant ou après l'ampli. */
    struct MovableModule
    {
        dsp::SwitchableEffect& slot;
        std::atomic<float>& enabled;
        std::atomic<float>& position;
        params::Position activePosition;
    };

    // Ordre de traitement à l'intérieur d'un même slot : celui de movableOrder (Modules.h).
    std::array<MovableModule, 4> movableModules;

    //==============================================================================
    // Bus de textures (parallèle, stéréo)
    dsp::ShimmerReverb shimmer;
    dsp::PolyShifter poly;
    dsp::SpectralFreeze freeze;
    dsp::GranularCloud granular;

    dsp::TextureSlot shimmerSlot { shimmer }, polySlot { poly }, freezeSlot { freeze }, granularSlot { granular };

    /** Texture et la prise où elle écoute (avant l'ampli ou fin de chaîne). */
    struct TextureRoute
    {
        dsp::TextureSlot& slot;
        std::atomic<float>& source;
    };

    std::array<TextureRoute, 4> textureRoutes;

    juce::AudioBuffer<float> preAmpTap, endTap, monoRight; // préalloués dans prepareToPlay

    // Transport (thread audio) : tempo courant et position du DAW au début du bloc
    double currentTempo = 120.0;
    std::optional<double> blockStartBeats;
    std::atomic<double> displayedTempo { 120.0 }; // pour l'interface

    juce::AudioBuffer<float> dryBuffer; // copie du signal d'entrée, pour le bypass
    int preparedBlockSize = 0;
    std::atomic<double> preparedSampleRate { 0.0 }; // lu par l'interface pour convertir les latences

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
