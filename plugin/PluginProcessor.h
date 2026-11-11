#pragma once

#include "plugin/CabSection.h"
#include "plugin/Modules.h"
#include "plugin/Parameters.h"
#include "subdamp/dsp/AmpSection.h"
#include "subdamp/dsp/BypassCrossfade.h"
#include "subdamp/dsp/ChainSlot.h"
#include "subdamp/dsp/FrequencyShifter.h"
#include "subdamp/dsp/GainStage.h"
#include "subdamp/dsp/Harmonizer.h"
#include "subdamp/dsp/NoiseGate.h"
#include "subdamp/dsp/OctaveDivider.h"
#include "subdamp/dsp/GranularCloud.h"
#include "subdamp/dsp/PolyShifter.h"
#include "subdamp/dsp/ShimmerReverb.h"
#include "subdamp/dsp/SpectralFreeze.h"
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
    // Son le plus direct : modules allumés et mélangés, en série. Une texture à MIX 100 % n'a
    // plus de son direct : elle retarde tout ce qui la suit et passe ce badge en TEXTURE.
    dsp::LatencyTier tier = dsp::LatencyTier::live;
    double milliseconds = 0.0;

    // Textures mélangées au son direct (MIX < 100 %) : leur retard ne gêne pas le jeu.
    bool texturesActive = false;
    double textureMilliseconds = 0.0;

    bool operator== (const LatencyStatus&) const = default;
};

/**
    Processeur principal de SubdAmp.

    Chaîne en série, dans un ordre libre : entrée → 11 modules → sortie. Chaque module a son
    interrupteur et son MIX (sortie = sec × (1 − MIX) + traité × MIX, voir dsp::ChainSlot).
    Le signal est stéréo d'un bout à l'autre ; tant qu'aucune texture ne l'a ouvert, gauche et
    droite sont identiques. Les entrées stéréo sont acceptées pour les DAW qui n'ont que des
    pistes stéréo : seul le canal gauche (guitare) est alors utilisé.

    L'ordre n'est pas un paramètre automatisable : il est enregistré dans l'état (propriété
    « chainOrder ») et transmis au thread audio par un atomic de 64 bits. Un changement d'ordre
    passe par un court fondu de la sortie (10 ms vers le silence, puis retour) : pas de clic.
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
    // Ordre de la chaîne (thread de l'interface)
    ChainOrder getChainOrder() const noexcept;
    void setChainOrder (const ChainOrder& newOrder);

    /** Glisser-déposer : place le module à la position donnée (0 = début de chaîne). */
    void moveModule (ModuleId module, int newIndex);

    //==============================================================================
    // État pour l'interface (thread de l'interface)
    bool isModuleEnabled (ModuleId module) const noexcept;

    /** MIX du module, de 0 à 1. */
    float getModuleMix (ModuleId module) const noexcept;

    /** Retard propre à un module, en millisecondes. */
    double getModuleLatencyMs (ModuleId module) const noexcept;

    /** Latence du son entendu : catégorie la plus lente parmi les modules mélangés, retard cumulé. */
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
    void updateChainOrder() noexcept;
    void updateTransport() noexcept;
    void updateTargetsFromParameters() noexcept;
    void restoreFilesFromState();
    void restoreChainOrderFromState();
    void setStatus (FileSlotStatus& slot, FileSlotStatus newStatus);

    juce::AudioProcessorValueTreeState parameters;

    /** Valeurs brutes des paramètres, mises en cache : lecture atomique dans le thread audio. */
    struct Parameters
    {
        std::atomic<float>& inputGain;
        std::atomic<float>& outputGain;
        std::atomic<float>& gateThreshold;
        std::atomic<float>& gateRelease;
        std::atomic<float>& ampGain;
        std::atomic<float>& ampBass;
        std::atomic<float>& ampMid;
        std::atomic<float>& ampTreble;
        std::atomic<float>& ampLevel;
        std::atomic<float>& subDown1;
        std::atomic<float>& subDown2;
        std::atomic<float>& subUp;
        std::atomic<float>& subTone;
        std::atomic<float>& fsShift;
        std::atomic<float>& fsFeedback;
        std::atomic<float>& whInterval;
        std::atomic<float>& whMode;
        std::atomic<float>& whGlide;
        std::atomic<float>& whSensitivity;
        std::atomic<float>& hmKey;
        std::atomic<float>& hmScale;
        std::atomic<float>& hmVoice1;
        std::atomic<float>& hmLevel1;
        std::atomic<float>& hmVoice2;
        std::atomic<float>& hmLevel2;
        std::atomic<float>& hmFormant;
    };

    /** Paramètres des textures, mis en cache de la même façon. */
    struct TextureParameters
    {
        std::atomic<float>& shInterval;
        std::atomic<float>& shDecay;
        std::atomic<float>& shAmount;
        std::atomic<float>& shTone;
        std::atomic<float>& poInterval1;
        std::atomic<float>& poLevel1;
        std::atomic<float>& poInterval2;
        std::atomic<float>& poLevel2;
        std::atomic<float>& poFormant;
        std::atomic<float>& poWidth;
        std::atomic<float>& frHold;
        std::atomic<float>& frAttack;
        std::atomic<float>& frRelease;
        std::atomic<float>& frTone;
        std::atomic<float>& grMode;
        std::atomic<float>& grSize;
        std::atomic<float>& grDensity;
        std::atomic<float>& grPitch;
        std::atomic<float>& grSpread;
        std::atomic<float>& grReverse;
        std::atomic<float>& grDivision;
        std::atomic<float>& grRepeats;
        std::atomic<float>& tempo;
    };

    Parameters raw;
    TextureParameters textureRaw;
    juce::AudioParameterBool& bypassParameter;

    // Interrupteur et MIX de chaque module, dans l'ordre de ModuleId.
    std::array<std::atomic<float>*, numModules> enabledValues {}, mixValues {};

    //==============================================================================
    // Modules
    dsp::GainStage inputGain, outputGain;
    dsp::BypassCrossfade bypass;

    dsp::NoiseGate gate;
    dsp::OctaveDivider sub;
    dsp::Whammy whammy;
    dsp::Harmonizer harmonizer;
    dsp::AmpSection amp;
    CabSection cab;
    dsp::FrequencyShifter shifter;
    dsp::ShimmerReverb shimmer;
    dsp::PolyShifter poly;
    dsp::SpectralFreeze freeze;
    dsp::GranularCloud granular;

    // Un emplacement par module, dans l'ordre de ModuleId (et non de la chaîne).
    std::array<dsp::ChainSlot, numModules> slots {
        dsp::ChainSlot { gate }, dsp::ChainSlot { sub }, dsp::ChainSlot { whammy }, dsp::ChainSlot { harmonizer },
        dsp::ChainSlot { amp }, dsp::ChainSlot { cab }, dsp::ChainSlot { shifter }, dsp::ChainSlot { shimmer },
        dsp::ChainSlot { poly }, dsp::ChainSlot { freeze }, dsp::ChainSlot { granular }
    };

    dsp::ChainSlot& slotFor (ModuleId module) noexcept { return slots[indexOf (module)]; }

    //==============================================================================
    // Ordre de la chaîne : écrit par l'interface, lu par le thread audio (sans verrou).
    std::atomic<std::uint64_t> requestedOrder { packChainOrder (defaultChainOrder) };
    std::uint64_t activePackedOrder = packChainOrder (defaultChainOrder);
    ChainOrder activeOrder = defaultChainOrder; // thread audio uniquement
    dsp::LinearSmoother reorderFade;

    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

    juce::AudioBuffer<float> monoRight; // sortie droite d'une piste mono, préallouée dans prepareToPlay

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
