#include "plugin/PluginProcessor.h"

#include "gui/PluginEditor.h"
#include "plugin/StateVersioning.h"
#include "subdamp/dsp/PeakMeter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace subdamp
{

namespace
{
    const juce::Identifier stateType { "SubdAmpState" };
    const juce::Identifier ampModelPathProperty { "ampModelPath" };
    const juce::Identifier cabIrPathProperty { "cabIrPath" };

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

    bool isOn (const std::atomic<float>& value) noexcept
    {
        return value.load (std::memory_order_relaxed) > 0.5f;
    }

    float load (const std::atomic<float>& value) noexcept
    {
        return value.load (std::memory_order_relaxed);
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

    /** Chemin portable : les accents (dossier utilisateur « Jérémy »...) restent intacts sous Windows. */
    std::filesystem::path toPath (const juce::File& file)
    {
        const auto utf8 = file.getFullPathName().toStdString();
        return std::filesystem::path (std::u8string (utf8.begin(), utf8.end()));
    }

    bool isReadableAudioFile (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)) != nullptr;
    }
}

//==============================================================================
SubdAmpProcessor::SubdAmpProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Guitare", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createLayout()),
      raw {
          rawParameter (parameters, params::id::inputGain),
          rawParameter (parameters, params::id::outputGain),
          rawParameter (parameters, params::id::gateThreshold),
          rawParameter (parameters, params::id::gateRelease),
          rawParameter (parameters, params::id::ampGain),
          rawParameter (parameters, params::id::ampBass),
          rawParameter (parameters, params::id::ampMid),
          rawParameter (parameters, params::id::ampTreble),
          rawParameter (parameters, params::id::ampLevel),
          rawParameter (parameters, params::id::subDown1),
          rawParameter (parameters, params::id::subDown2),
          rawParameter (parameters, params::id::subUp),
          rawParameter (parameters, params::id::subTone),
          rawParameter (parameters, params::id::fsShift),
          rawParameter (parameters, params::id::fsFeedback),
          rawParameter (parameters, params::id::whInterval),
          rawParameter (parameters, params::id::whMode),
          rawParameter (parameters, params::id::whGlide),
          rawParameter (parameters, params::id::whSensitivity),
          rawParameter (parameters, params::id::hmKey),
          rawParameter (parameters, params::id::hmScale),
          rawParameter (parameters, params::id::hmVoice1),
          rawParameter (parameters, params::id::hmLevel1),
          rawParameter (parameters, params::id::hmVoice2),
          rawParameter (parameters, params::id::hmLevel2),
          rawParameter (parameters, params::id::hmFormant) },
      textureRaw {
          rawParameter (parameters, params::id::shInterval),
          rawParameter (parameters, params::id::shDecay),
          rawParameter (parameters, params::id::shAmount),
          rawParameter (parameters, params::id::shTone),
          rawParameter (parameters, params::id::poInterval1),
          rawParameter (parameters, params::id::poLevel1),
          rawParameter (parameters, params::id::poInterval2),
          rawParameter (parameters, params::id::poLevel2),
          rawParameter (parameters, params::id::poFormant),
          rawParameter (parameters, params::id::poWidth),
          rawParameter (parameters, params::id::frHold),
          rawParameter (parameters, params::id::frAttack),
          rawParameter (parameters, params::id::frRelease),
          rawParameter (parameters, params::id::frTone),
          rawParameter (parameters, params::id::grMode),
          rawParameter (parameters, params::id::grSize),
          rawParameter (parameters, params::id::grDensity),
          rawParameter (parameters, params::id::grPitch),
          rawParameter (parameters, params::id::grSpread),
          rawParameter (parameters, params::id::grReverse),
          rawParameter (parameters, params::id::grDivision),
          rawParameter (parameters, params::id::grRepeats),
          rawParameter (parameters, params::id::tempo) },
      bypassParameter (boolParameter (parameters, params::id::bypass))
{
    for (const auto& info : moduleInfos)
    {
        enabledValues[indexOf (info.id)] = &rawParameter (parameters, info.enabledParameter);
        mixValues[indexOf (info.id)] = info.mixParameter != nullptr ? &rawParameter (parameters, info.mixParameter) : nullptr;
    }

    // FREEZE : la nappe s'ajoute au jeu (MIX = son niveau), qui n'est jamais atténué.
    slotFor (ModuleId::freeze).setWetAddsToDry (true);

    parameters.state.setProperty (state::chainOrderProperty, juce::String (formatChainOrder (defaultChainOrder)), nullptr);

    // Ramasse-miettes des captures remplacées et suivi de la latence, sur le thread de l'interface.
    startTimerHz (4);
}

SubdAmpProcessor::~SubdAmpProcessor()
{
    stopTimer();
    loaderThread.removeAllJobs (true, -1);
}

//==============================================================================
void SubdAmpProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const dsp::ProcessSpec spec { sampleRate, maximumExpectedSamplesPerBlock };

    inputGain.prepare (spec);
    outputGain.prepare (spec);
    bypass.prepare (spec);

    for (auto& slot : slots)
        slot.prepare (spec);

    dryBuffer.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    monoRight.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    preparedBlockSize = maximumExpectedSamplesPerBlock;
    preparedSampleRate.store (sampleRate, std::memory_order_relaxed);

    scope.prepare (sampleRate);
    loadMeasurer.reset (sampleRate, maximumExpectedSamplesPerBlock);

    // L'ordre demandé s'applique directement, sans fondu.
    activePackedOrder = requestedOrder.load (std::memory_order_acquire);
    activeOrder = unpackChainOrder (activePackedOrder);
    reorderFade.prepare (sampleRate, 0.01);
    reorderFade.setCurrentAndTarget (1.0f);

    // Démarre directement sur les valeurs courantes, sans rampe depuis zéro.
    updateTargetsFromParameters();
    reset();

    setLatencySamples (amp.getModel().getLatencySamples());
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

    for (auto& slot : slots)
        slot.reset();
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

double SubdAmpProcessor::getTailLengthSeconds() const
{
    // Queue la plus longue parmi les modules allumés : le DAW continue de calculer jusque-là
    // à l'export (réverbe du SHIMMER, relâche du FREEZE). Le FREQ SHIFT et le cab : 2 s suffisent.
    auto seconds = 2.0;

    if (isModuleEnabled (ModuleId::shimmer))
        seconds = std::max (seconds, 1.5 * load (textureRaw.shDecay));

    if (isModuleEnabled (ModuleId::freeze))
        seconds = std::max (seconds, 0.001 * load (textureRaw.frRelease) + 1.0);

    return seconds;
}

//==============================================================================
void SubdAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // Charge mesurée sur le vrai calcul (sans verrou bloquant : essai de verrou côté JUCE).
    const juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer (loadMeasurer, numSamples);

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
    updateTransport();

    // Le signal guitare (canal 0) devient la sortie gauche ; la droite est calculée à part.
    // Sortie mono : les deux sont ensuite réunies.
    const auto stereoOut = getTotalNumOutputChannels() > 1 && buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer (0);
    auto* right = stereoOut ? buffer.getWritePointer (1) : nullptr;
    const auto beatsPerSample = currentTempo / 60.0 / preparedSampleRate.load (std::memory_order_relaxed);

    // Certains hôtes envoient parfois plus d'échantillons qu'annoncé dans prepareToPlay :
    // on découpe pour ne jamais dépasser la taille des tampons préalloués.
    for (int start = 0; start < numSamples; start += preparedBlockSize)
    {
        const auto chunk = std::min (preparedBlockSize, numSamples - start);
        auto* chunkRight = right != nullptr ? right + start : monoRight.getWritePointer (0);

        granular.setTransportPosition (blockStartBeats.has_value()
                                           ? std::optional<double> (*blockStartBeats + start * beatsPerSample)
                                           : std::nullopt);

        updateChainOrder();
        processChunk (left + start, chunkRight, chunk);

        if (right == nullptr)
            for (int i = 0; i < chunk; ++i)
                left[start + i] = 0.5f * (left[start + i] + chunkRight[i]);
    }

    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);
}

void SubdAmpProcessor::processChunk (float* leftSamples, float* rightSamples, int numSamples) noexcept
{
    const auto size = static_cast<std::size_t> (numSamples);
    const std::span<float> left { leftSamples, size };
    const std::span<float> right { rightSamples, size };
    const std::span<float> dry { dryBuffer.getWritePointer (0), size };

    publishPeak (meters.inputPeak, dsp::blockPeak (left));

    std::copy (left.begin(), left.end(), dry.begin());

    // Bypass complètement engagé : rien à calculer, le fondu recopie le signal sec.
    if (! bypass.isFullyBypassed())
    {
        inputGain.process (left);
        std::copy (left.begin(), left.end(), right.begin());

        // Chaîne en série, dans l'ordre choisi. Gauche et droite restent identiques tant
        // qu'aucune texture ne les a séparées.
        for (const auto module : activeOrder)
            slotFor (module).process (left, right);

        // Changement d'ordre en cours : fondu de sortie (voir updateChainOrder).
        if (reorderFade.isSmoothing() || reorderFade.getCurrent() < 1.0f)
        {
            for (std::size_t i = 0; i < size; ++i)
            {
                const auto gain = reorderFade.getNext();
                left[i] *= gain;
                right[i] *= gain;
            }
        }

        outputGain.processStereo (left, right);
    }

    bypass.processStereo (dry, left, right);

    scope.push (left, right);
    publishTelemetry();

    publishPeak (meters.outputPeak, std::max (dsp::blockPeak (left), dsp::blockPeak (right)));
}

void SubdAmpProcessor::publishTelemetry() noexcept
{
    const auto running = [this] (ModuleId module)
    {
        return ! bypass.isFullyBypassed() && isOn (*enabledValues[indexOf (module)]);
    };

    // Voyant du GATE : il s'allume quand le gate coupe (relâche, puis silence entre les notes).
    // Arrêté ou contourné, le gate ne coupe rien : voyant éteint.
    const auto gateRunning = running (ModuleId::gate);
    telemetry.gateCutting.store (gateRunning ? 1.0f - gate.getOpening() : 0.0f, std::memory_order_relaxed);
    telemetry.gateLevel.store (gateRunning ? gate.getDetectorLevel() : 0.0f, std::memory_order_relaxed);

    telemetry.whammySemitones.store (running (ModuleId::whammy) ? whammy.getCurrentSemitones() : 0.0f, std::memory_order_relaxed);

    const auto harmonyRunning = running (ModuleId::harmonizer);
    telemetry.harmonyNote.store (harmonyRunning ? harmonizer.getDetectedNote() : -1, std::memory_order_relaxed);
    telemetry.harmonyShift1.store (harmonizer.getVoiceShiftSemitones (0), std::memory_order_relaxed);
    telemetry.harmonyShift2.store (harmonizer.getVoiceShiftSemitones (1), std::memory_order_relaxed);

    telemetry.freezeHeld.store (running (ModuleId::freeze) && freeze.isFrozen(), std::memory_order_relaxed);
}

void SubdAmpProcessor::updateChainOrder() noexcept
{
    const auto requested = requestedOrder.load (std::memory_order_acquire);

    if (requested == activePackedOrder)
    {
        reorderFade.setTarget (1.0f);
        return;
    }

    // Nouvel ordre : la sortie descend en 10 ms, l'ordre change dans le silence, puis remonte.
    // Rebrancher des pédales en jouant ferait un « pop » : ici, un creux de 20 ms au plus.
    if (reorderFade.getCurrent() <= 0.0f && ! reorderFade.isSmoothing())
    {
        activePackedOrder = requested;
        activeOrder = unpackChainOrder (requested);
        reorderFade.setTarget (1.0f);
    }
    else
    {
        reorderFade.setTarget (0.0f);
    }
}

void SubdAmpProcessor::updateTransport() noexcept
{
    // Tempo et position du DAW s'il en fournit ; sinon (standalone), le tempo réglé.
    currentTempo = load (textureRaw.tempo);
    blockStartBeats.reset();

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                currentTempo = *bpm;

            if (position->getIsPlaying())
                if (const auto beats = position->getPpqPosition(); beats.hasValue())
                    blockStartBeats = *beats;
        }
    }

    granular.setTempo (currentTempo);
    displayedTempo.store (currentTempo, std::memory_order_relaxed);
}

void SubdAmpProcessor::updateTargetsFromParameters() noexcept
{
    inputGain.setGainDecibels (load (raw.inputGain));
    outputGain.setGainDecibels (load (raw.outputGain));
    bypass.setBypassed (bypassParameter.get());

    // Interrupteurs et MIX. Le WHAMMY et le FREQ SHIFT mélangent eux-mêmes (fondu vers le son
    // direct à l'unisson, réinjection) : leur emplacement reste à 100 %.
    for (const auto& info : moduleInfos)
    {
        auto& slot = slotFor (info.id);
        slot.setEnabled (isOn (*enabledValues[indexOf (info.id)]));

        const auto mixesInternally = info.id == ModuleId::whammy || info.id == ModuleId::freqShift;
        slot.setMix (mixesInternally ? 1.0f : getModuleMix (info.id));
    }

    whammy.setMix (load (*mixValues[indexOf (ModuleId::whammy)]) * 0.01f);
    shifter.setMix (load (*mixValues[indexOf (ModuleId::freqShift)]) * 0.01f);

    gate.setThresholdDecibels (load (raw.gateThreshold));
    gate.setReleaseMilliseconds (load (raw.gateRelease));

    amp.setGainDecibels (load (raw.ampGain));
    amp.setToneGains (load (raw.ampBass), load (raw.ampMid), load (raw.ampTreble));
    amp.setLevelDecibels (load (raw.ampLevel));

    // Capture rééchantillonnée : le sec est retardé d'autant, pour qu'un MIX partiel de
    // l'ampli ne filtre pas en peigne.
    slotFor (ModuleId::amp).setDryDelaySamples (amp.getModel().getLatencySamples());

    // SUB et HARMONIZER : le traité ne contient que les voix, le MIX dose le son direct.
    sub.setLevels (0.0f, load (raw.subDown1) * 0.01f, load (raw.subDown2) * 0.01f, load (raw.subUp) * 0.01f);
    sub.setToneFrequency (load (raw.subTone));

    whammy.setInterval (load (raw.whInterval));
    whammy.setMode (static_cast<dsp::Whammy::Mode> (juce::roundToInt (load (raw.whMode))));
    whammy.setGlideMilliseconds (load (raw.whGlide));
    whammy.setSensitivity (load (raw.whSensitivity) * 0.01f);

    harmonizer.setKey (juce::roundToInt (load (raw.hmKey)));
    harmonizer.setScale (static_cast<dsp::Scale> (juce::jlimit (0, dsp::numScales - 1, juce::roundToInt (load (raw.hmScale)))));
    harmonizer.setVoiceInterval (0, params::harmonyVoiceSteps (juce::roundToInt (load (raw.hmVoice1))));
    harmonizer.setVoiceInterval (1, params::harmonyVoiceSteps (juce::roundToInt (load (raw.hmVoice2))));
    harmonizer.setVoiceLevel (0, load (raw.hmLevel1) * 0.01f);
    harmonizer.setVoiceLevel (1, load (raw.hmLevel2) * 0.01f);
    harmonizer.setFormantSemitones (load (raw.hmFormant));
    harmonizer.setDirectLevel (0.0f);

    shifter.setShiftHz (load (raw.fsShift));
    shifter.setFeedback (load (raw.fsFeedback) * 0.01f);

    shimmer.setInterval (params::shimmerIntervalSemitones (juce::roundToInt (load (textureRaw.shInterval))));
    shimmer.setDecaySeconds (load (textureRaw.shDecay));
    shimmer.setShimmer (load (textureRaw.shAmount) * 0.01f);
    shimmer.setToneHz (load (textureRaw.shTone));

    poly.setInterval (0, load (textureRaw.poInterval1));
    poly.setInterval (1, load (textureRaw.poInterval2));
    poly.setVoiceLevel (0, load (textureRaw.poLevel1) * 0.01f);
    poly.setVoiceLevel (1, load (textureRaw.poLevel2) * 0.01f);
    poly.setFormantSemitones (load (textureRaw.poFormant));
    poly.setWidth (load (textureRaw.poWidth) * 0.01f);

    // Arrêter le module libère aussi la nappe : elle s'éteint avec son fondu de relâche.
    freeze.setHold (isOn (textureRaw.frHold) && isOn (*enabledValues[indexOf (ModuleId::freeze)]));
    freeze.setAttackMilliseconds (load (textureRaw.frAttack));
    freeze.setReleaseMilliseconds (load (textureRaw.frRelease));
    freeze.setToneHz (load (textureRaw.frTone));

    granular.setMode (load (textureRaw.grMode) > 0.5f ? dsp::GranularCloud::Mode::stutter : dsp::GranularCloud::Mode::cloud);
    granular.setGrainMilliseconds (load (textureRaw.grSize));
    granular.setDensity (load (textureRaw.grDensity));
    granular.setPitchSemitones (load (textureRaw.grPitch));
    granular.setSpread (load (textureRaw.grSpread) * 0.01f);
    granular.setReverseProbability (load (textureRaw.grReverse) * 0.01f);
    granular.setDivisionBeats (params::granularDivisionBeats (juce::roundToInt (load (textureRaw.grDivision))));
    granular.setRepeats (juce::roundToInt (load (textureRaw.grRepeats)));
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
    state::removeUnknownParameters (loaded, defaults);

    parameters.replaceState (loaded);
    restoreChainOrderFromState();
    restoreFilesFromState();
}

void SubdAmpProcessor::restoreChainOrderFromState()
{
    // Lecture tolérante : un ordre absent ou abîmé donne un ordre complet et valide.
    const auto text = parameters.state.getProperty (state::chainOrderProperty).toString().toStdString();
    setChainOrder (parseChainOrder (text));
}

//==============================================================================
ChainOrder SubdAmpProcessor::getChainOrder() const noexcept
{
    return unpackChainOrder (requestedOrder.load (std::memory_order_acquire));
}

void SubdAmpProcessor::setChainOrder (const ChainOrder& newOrder)
{
    // Repasse par le texte : garantit un ordre complet (chaque module une fois).
    const auto order = parseChainOrder (formatChainOrder (newOrder));
    parameters.state.setProperty (state::chainOrderProperty, juce::String (formatChainOrder (order)), nullptr);
    requestedOrder.store (packChainOrder (order), std::memory_order_release);
}

void SubdAmpProcessor::moveModule (ModuleId module, int newIndex)
{
    setChainOrder (withModuleMoved (getChainOrder(), module, static_cast<std::size_t> (std::max (0, newIndex))));
}

void SubdAmpProcessor::restoreFilesFromState()
{
    const auto ampPath = parameters.state.getProperty (ampModelPathProperty).toString();
    const auto cabPath = parameters.state.getProperty (cabIrPathProperty).toString();

    if (ampPath.isNotEmpty())
        loadAmpModel (juce::File (ampPath));
    else
        clearAmpModel();

    if (cabPath.isNotEmpty())
        loadCabImpulseResponse (juce::File (cabPath));
    else
        clearCabImpulseResponse();
}

//==============================================================================
void SubdAmpProcessor::loadAmpModel (const juce::File& file)
{
    // Le chemin est mémorisé même si le fichier est introuvable : en le remettant en place
    // (disque externe rebranché...), la session retrouvera sa capture.
    parameters.state.setProperty (ampModelPathProperty, file.getFullPathName(), nullptr);

    if (! file.existsAsFile())
    {
        setStatus (ampStatus, { file.getFileName(), juce::String::fromUTF8 ("Fichier introuvable"), false, true });
        return;
    }

    setStatus (ampStatus, { file.getFileName(), juce::String::fromUTF8 ("Chargement…"), true, false });

    loaderThread.addJob ([this, file]
    {
        const auto result = amp.getModel().load (toPath (file));

        setStatus (ampStatus, result.succeeded ? FileSlotStatus { file.getFileName(), {}, false, false }
                                               : FileSlotStatus { file.getFileName(), juce::String::fromUTF8 (result.error.c_str()), false, true });
    });
}

void SubdAmpProcessor::clearAmpModel()
{
    parameters.state.removeProperty (ampModelPathProperty, nullptr);
    amp.getModel().unload();
    setStatus (ampStatus, {});
}

void SubdAmpProcessor::loadCabImpulseResponse (const juce::File& file)
{
    parameters.state.setProperty (cabIrPathProperty, file.getFullPathName(), nullptr);

    if (! file.existsAsFile())
    {
        setStatus (cabStatus, { file.getFileName(), juce::String::fromUTF8 ("Fichier introuvable"), false, true });
        return;
    }

    if (! isReadableAudioFile (file))
    {
        setStatus (cabStatus, { file.getFileName(), juce::String::fromUTF8 ("Format audio non reconnu (WAV ou AIFF attendu)"), false, true });
        return;
    }

    cab.loadImpulseResponse (file);
    setStatus (cabStatus, { file.getFileName(), {}, false, false });
}

void SubdAmpProcessor::clearCabImpulseResponse()
{
    parameters.state.removeProperty (cabIrPathProperty, nullptr);
    cab.clearImpulseResponse();
    setStatus (cabStatus, {});
}

void SubdAmpProcessor::setStatus (FileSlotStatus& slot, FileSlotStatus newStatus)
{
    {
        const juce::ScopedLock lock (statusLock);
        slot = std::move (newStatus);
    }

    // Peut être appelé depuis le thread de chargement : la notification passe par le thread de l'interface.
    triggerAsyncUpdate();
}

FileSlotStatus SubdAmpProcessor::getAmpStatus() const
{
    const juce::ScopedLock lock (statusLock);
    return ampStatus;
}

FileSlotStatus SubdAmpProcessor::getCabStatus() const
{
    const juce::ScopedLock lock (statusLock);
    return cabStatus;
}

juce::File SubdAmpProcessor::getAmpModelFile() const
{
    return juce::File (parameters.state.getProperty (ampModelPathProperty).toString());
}

juce::File SubdAmpProcessor::getCabImpulseResponseFile() const
{
    return juce::File (parameters.state.getProperty (cabIrPathProperty).toString());
}

void SubdAmpProcessor::handleAsyncUpdate()
{
    fileStatusBroadcaster.sendChangeMessage();
}

void SubdAmpProcessor::timerCallback()
{
    amp.getModel().collectGarbage();

    // Le rééchantillonnage d'une capture ajoute un peu de latence : on la déclare au DAW
    // pour qu'il compense (alignement des pistes à l'export).
    const auto modelLatency = amp.getModel().getLatencySamples();

    if (modelLatency != getLatencySamples())
        setLatencySamples (modelLatency);
}

//==============================================================================
bool SubdAmpProcessor::isModuleEnabled (ModuleId module) const noexcept
{
    return isOn (*enabledValues[indexOf (module)]);
}

float SubdAmpProcessor::getModuleMix (ModuleId module) const noexcept
{
    const auto* value = mixValues[indexOf (module)];
    return value != nullptr ? load (*value) * 0.01f : 1.0f;
}

double SubdAmpProcessor::getModuleLatencyMs (ModuleId module) const noexcept
{
    const auto sampleRate = preparedSampleRate.load (std::memory_order_relaxed);

    if (sampleRate <= 0.0)
        return 0.0;

    switch (module)
    {
        case ModuleId::whammy:     return 1000.0 * whammy.getLatencySamples() / sampleRate;
        case ModuleId::harmonizer: return 1000.0 * harmonizer.getLatencySamples() / sampleRate;
        case ModuleId::amp:        return 1000.0 * amp.getModel().getLatencySamples() / sampleRate;
        case ModuleId::poly:       return 1000.0 * poly.latencySamples() / sampleRate;
        case ModuleId::gate:
        case ModuleId::sub:
        case ModuleId::cab:
        case ModuleId::freqShift:
        case ModuleId::shimmer:
        case ModuleId::freeze:
        case ModuleId::granular:   break;
    }

    return 0.0;
}

LatencyStatus SubdAmpProcessor::getLatencyStatus() const noexcept
{
    LatencyStatus status;

    if (bypassParameter.get())
        return status;

    for (const auto module : getChainOrder())
    {
        const auto& info = infoFor (module);
        const auto mix = getModuleMix (module);

        // Module arrêté, ou mélangé à moins de 1 % : il ne change rien à ce qu'on entend.
        if (! isModuleEnabled (module) || mix < 0.01f)
            continue;

        // Le WHAMMY fixe à l'unisson rend le son direct, sans retard.
        if (module == ModuleId::whammy
            && std::abs (load (raw.whInterval)) < dsp::Whammy::unisonFadeSemitones && juce::roundToInt (load (raw.whMode)) == 0)
            continue;

        const auto latency = getModuleLatencyMs (module);

        // Texture mélangée : le son direct passe sans l'attendre, sa latence est à part.
        if (info.isTexture && mix < 0.995f)
        {
            status.texturesActive = true;
            status.textureMilliseconds = std::max (status.textureMilliseconds, latency);
            continue;
        }

        // En série : chaque module mélangé retarde ce qui suit.
        status.tier = std::max (status.tier, info.tier);
        status.milliseconds += latency;
    }

    return status;
}

//==============================================================================
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
