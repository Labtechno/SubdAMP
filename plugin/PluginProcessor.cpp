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

    params::Position positionOf (const std::atomic<float>& value) noexcept
    {
        return load (value) > 0.5f ? params::Position::afterAmp : params::Position::beforeAmp;
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
          rawParameter (parameters, params::id::gateEnabled),
          rawParameter (parameters, params::id::gateThreshold),
          rawParameter (parameters, params::id::gateRelease),
          rawParameter (parameters, params::id::ampEnabled),
          rawParameter (parameters, params::id::ampGain),
          rawParameter (parameters, params::id::ampBass),
          rawParameter (parameters, params::id::ampMid),
          rawParameter (parameters, params::id::ampTreble),
          rawParameter (parameters, params::id::ampLevel),
          rawParameter (parameters, params::id::cabEnabled),
          rawParameter (parameters, params::id::subEnabled),
          rawParameter (parameters, params::id::subPosition),
          rawParameter (parameters, params::id::subDry),
          rawParameter (parameters, params::id::subDown1),
          rawParameter (parameters, params::id::subDown2),
          rawParameter (parameters, params::id::subUp),
          rawParameter (parameters, params::id::subTone),
          rawParameter (parameters, params::id::fsEnabled),
          rawParameter (parameters, params::id::fsPosition),
          rawParameter (parameters, params::id::fsShift),
          rawParameter (parameters, params::id::fsMix),
          rawParameter (parameters, params::id::fsFeedback),
          rawParameter (parameters, params::id::whEnabled),
          rawParameter (parameters, params::id::whPosition),
          rawParameter (parameters, params::id::whInterval),
          rawParameter (parameters, params::id::whMode),
          rawParameter (parameters, params::id::whGlide),
          rawParameter (parameters, params::id::whSensitivity),
          rawParameter (parameters, params::id::whMix),
          rawParameter (parameters, params::id::hmEnabled),
          rawParameter (parameters, params::id::hmPosition),
          rawParameter (parameters, params::id::hmKey),
          rawParameter (parameters, params::id::hmScale),
          rawParameter (parameters, params::id::hmVoice1),
          rawParameter (parameters, params::id::hmLevel1),
          rawParameter (parameters, params::id::hmVoice2),
          rawParameter (parameters, params::id::hmLevel2),
          rawParameter (parameters, params::id::hmFormant),
          rawParameter (parameters, params::id::hmDirect) },
      textureRaw {
          rawParameter (parameters, params::id::shEnabled),
          rawParameter (parameters, params::id::shSource),
          rawParameter (parameters, params::id::shInterval),
          rawParameter (parameters, params::id::shDecay),
          rawParameter (parameters, params::id::shAmount),
          rawParameter (parameters, params::id::shTone),
          rawParameter (parameters, params::id::shLevel),
          rawParameter (parameters, params::id::poEnabled),
          rawParameter (parameters, params::id::poSource),
          rawParameter (parameters, params::id::poInterval1),
          rawParameter (parameters, params::id::poLevel1),
          rawParameter (parameters, params::id::poInterval2),
          rawParameter (parameters, params::id::poLevel2),
          rawParameter (parameters, params::id::poFormant),
          rawParameter (parameters, params::id::poWidth),
          rawParameter (parameters, params::id::frEnabled),
          rawParameter (parameters, params::id::frSource),
          rawParameter (parameters, params::id::frHold),
          rawParameter (parameters, params::id::frAttack),
          rawParameter (parameters, params::id::frRelease),
          rawParameter (parameters, params::id::frTone),
          rawParameter (parameters, params::id::frLevel),
          rawParameter (parameters, params::id::grEnabled),
          rawParameter (parameters, params::id::grSource),
          rawParameter (parameters, params::id::grMode),
          rawParameter (parameters, params::id::grSize),
          rawParameter (parameters, params::id::grDensity),
          rawParameter (parameters, params::id::grPitch),
          rawParameter (parameters, params::id::grSpread),
          rawParameter (parameters, params::id::grReverse),
          rawParameter (parameters, params::id::grDivision),
          rawParameter (parameters, params::id::grRepeats),
          rawParameter (parameters, params::id::grLevel),
          rawParameter (parameters, params::id::tempo) },
      bypassParameter (boolParameter (parameters, params::id::bypass)),
      movableModules { MovableModule { subSlot, raw.subEnabled, raw.subPosition, positionOf (raw.subPosition) },
                       MovableModule { whammySlot, raw.whEnabled, raw.whPosition, positionOf (raw.whPosition) },
                       MovableModule { harmonizerSlot, raw.hmEnabled, raw.hmPosition, positionOf (raw.hmPosition) },
                       MovableModule { shifterSlot, raw.fsEnabled, raw.fsPosition, positionOf (raw.fsPosition) } },
      textureRoutes { TextureRoute { shimmerSlot, textureRaw.shSource },
                      TextureRoute { polySlot, textureRaw.poSource },
                      TextureRoute { freezeSlot, textureRaw.frSource },
                      TextureRoute { granularSlot, textureRaw.grSource } }
{
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

    for (auto* slot : allSlots)
        slot->prepare (spec);

    for (auto& route : textureRoutes)
        route.slot.prepare (spec);

    dryBuffer.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    preAmpTap.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    endTap.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    monoRight.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    preparedBlockSize = maximumExpectedSamplesPerBlock;
    preparedSampleRate.store (sampleRate, std::memory_order_relaxed);

    for (auto& module : movableModules)
        module.activePosition = positionOf (module.position);

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

    for (auto* slot : allSlots)
        slot->reset();

    for (auto& route : textureRoutes)
        route.slot.reset();
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
    // La réinjection du FREQ SHIFT prolonge le son ; une IR de cab aussi, mais très peu.
    return 2.0;
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
        // Chaîne principale (mono)
        inputGain.process (left);
        gateSlot.process (left);

        for (auto& module : movableModules)
            if (module.activePosition == params::Position::beforeAmp)
                module.slot.process (left);

        const std::span<float> preAmp { preAmpTap.getWritePointer (0), size };
        std::copy (left.begin(), left.end(), preAmp.begin());

        ampSlot.process (left);
        cabSlot.process (left);

        for (auto& module : movableModules)
            if (module.activePosition == params::Position::afterAmp)
                module.slot.process (left);

        const std::span<float> end { endTap.getWritePointer (0), size };
        std::copy (left.begin(), left.end(), end.begin());
        std::copy (left.begin(), left.end(), right.begin());

        // Bus de textures : chacune écoute sa prise et s'ajoute en stéréo au son principal.
        for (auto& route : textureRoutes)
        {
            const auto fromPreAmp = route.source.load (std::memory_order_relaxed) < 0.5f;
            route.slot.process (fromPreAmp ? std::span<const float> { preAmp } : std::span<const float> { end }, left, right);
        }

        outputGain.processStereo (left, right);
    }

    bypass.processStereo (dry, left, right);

    publishPeak (meters.outputPeak, std::max (dsp::blockPeak (left), dsp::blockPeak (right)));
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

    gate.setThresholdDecibels (load (raw.gateThreshold));
    gate.setReleaseMilliseconds (load (raw.gateRelease));
    gateSlot.setEnabled (isOn (raw.gateEnabled));

    amp.setGainDecibels (load (raw.ampGain));
    amp.setToneGains (load (raw.ampBass), load (raw.ampMid), load (raw.ampTreble));
    amp.setLevelDecibels (load (raw.ampLevel));
    ampSlot.setEnabled (isOn (raw.ampEnabled));

    cabSlot.setEnabled (isOn (raw.cabEnabled));

    sub.setLevels (load (raw.subDry) * 0.01f, load (raw.subDown1) * 0.01f,
                   load (raw.subDown2) * 0.01f, load (raw.subUp) * 0.01f);
    sub.setToneFrequency (load (raw.subTone));

    whammy.setInterval (load (raw.whInterval));
    whammy.setMode (static_cast<dsp::Whammy::Mode> (juce::roundToInt (load (raw.whMode))));
    whammy.setGlideMilliseconds (load (raw.whGlide));
    whammy.setSensitivity (load (raw.whSensitivity) * 0.01f);
    whammy.setMix (load (raw.whMix) * 0.01f);

    harmonizer.setKey (juce::roundToInt (load (raw.hmKey)));
    harmonizer.setScale (static_cast<dsp::Scale> (juce::jlimit (0, dsp::numScales - 1, juce::roundToInt (load (raw.hmScale)))));
    harmonizer.setVoiceInterval (0, params::harmonyVoiceSteps (juce::roundToInt (load (raw.hmVoice1))));
    harmonizer.setVoiceInterval (1, params::harmonyVoiceSteps (juce::roundToInt (load (raw.hmVoice2))));
    harmonizer.setVoiceLevel (0, load (raw.hmLevel1) * 0.01f);
    harmonizer.setVoiceLevel (1, load (raw.hmLevel2) * 0.01f);
    harmonizer.setFormantSemitones (load (raw.hmFormant));
    harmonizer.setDirectLevel (load (raw.hmDirect) * 0.01f);

    shimmer.setInterval (params::shimmerIntervalSemitones (juce::roundToInt (load (textureRaw.shInterval))));
    shimmer.setDecaySeconds (load (textureRaw.shDecay));
    shimmer.setShimmer (load (textureRaw.shAmount) * 0.01f);
    shimmer.setToneHz (load (textureRaw.shTone));
    shimmerSlot.setLevel (load (textureRaw.shLevel) * 0.01f);
    shimmerSlot.setEnabled (isOn (textureRaw.shEnabled));

    poly.setInterval (0, load (textureRaw.poInterval1));
    poly.setInterval (1, load (textureRaw.poInterval2));
    poly.setVoiceLevel (0, load (textureRaw.poLevel1) * 0.01f);
    poly.setVoiceLevel (1, load (textureRaw.poLevel2) * 0.01f);
    poly.setFormantSemitones (load (textureRaw.poFormant));
    poly.setWidth (load (textureRaw.poWidth) * 0.01f);
    polySlot.setLevel (1.0f); // niveaux réglés voix par voix
    polySlot.setEnabled (isOn (textureRaw.poEnabled));

    // Arrêter le module libère aussi la nappe : elle s'éteint avec son fondu de relâche.
    freeze.setHold (isOn (textureRaw.frHold) && isOn (textureRaw.frEnabled));
    freeze.setAttackMilliseconds (load (textureRaw.frAttack));
    freeze.setReleaseMilliseconds (load (textureRaw.frRelease));
    freeze.setToneHz (load (textureRaw.frTone));
    freezeSlot.setLevel (load (textureRaw.frLevel) * 0.01f);
    freezeSlot.setEnabled (isOn (textureRaw.frEnabled));

    granular.setMode (load (textureRaw.grMode) > 0.5f ? dsp::GranularCloud::Mode::stutter : dsp::GranularCloud::Mode::cloud);
    granular.setGrainMilliseconds (load (textureRaw.grSize));
    granular.setDensity (load (textureRaw.grDensity));
    granular.setPitchSemitones (load (textureRaw.grPitch));
    granular.setSpread (load (textureRaw.grSpread) * 0.01f);
    granular.setReverseProbability (load (textureRaw.grReverse) * 0.01f);
    granular.setDivisionBeats (params::granularDivisionBeats (juce::roundToInt (load (textureRaw.grDivision))));
    granular.setRepeats (juce::roundToInt (load (textureRaw.grRepeats)));
    granularSlot.setLevel (load (textureRaw.grLevel) * 0.01f);
    granularSlot.setEnabled (isOn (textureRaw.grEnabled));

    shifter.setShiftHz (load (raw.fsShift));
    shifter.setMix (load (raw.fsMix) * 0.01f);
    shifter.setFeedback (load (raw.fsFeedback) * 0.01f);

    // Changement de position : le module s'éteint en fondu, change de place une fois
    // complètement arrêté, puis se rallume en fondu à son nouvel emplacement. Pas de clic.
    for (auto& module : movableModules)
    {
        const auto requested = positionOf (module.position);

        if (requested != module.activePosition)
        {
            module.slot.setEnabled (false);

            if (module.slot.isFullyOff())
                module.activePosition = requested;
        }
        else
        {
            module.slot.setEnabled (isOn (module.enabled));
        }
    }
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
    restoreFilesFromState();
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
    return isOn (*parameters.getRawParameterValue (infoFor (module).enabledParameter));
}

bool SubdAmpProcessor::isModuleAfterAmp (ModuleId module) const noexcept
{
    const auto* position = infoFor (module).positionParameter;
    return position != nullptr && positionOf (*parameters.getRawParameterValue (position)) == params::Position::afterAmp;
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

    for (const auto& info : moduleInfos)
    {
        if (! isModuleEnabled (info.id))
            continue;

        // Les textures sont en parallèle : elles ne retardent pas le son principal.
        if (info.isTexture)
        {
            status.texturesActive = true;
            status.textureMilliseconds = std::max (status.textureMilliseconds, getModuleLatencyMs (info.id));
            continue;
        }

        // Le WHAMMY ne retarde le son que lorsqu'il transpose et qu'il est mélangé.
        if (info.id == ModuleId::whammy
            && (load (raw.whMix) < 1.0f
                || (std::abs (load (raw.whInterval)) < dsp::Whammy::unisonFadeSemitones && juce::roundToInt (load (raw.whMode)) == 0)))
            continue;

        if (info.tier > status.tier)
            status.tier = info.tier;

        status.milliseconds += getModuleLatencyMs (info.id);
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
