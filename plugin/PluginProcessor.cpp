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
          rawParameter (parameters, params::id::fsFeedback) },
      bypassParameter (boolParameter (parameters, params::id::bypass)),
      movableModules { MovableModule { subSlot, raw.subEnabled, raw.subPosition, positionOf (raw.subPosition) },
                       MovableModule { shifterSlot, raw.fsEnabled, raw.fsPosition, positionOf (raw.fsPosition) } }
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

    for (auto* slot : { &gateSlot, &subSlot, &shifterSlot, &ampSlot, &cabSlot })
        slot->prepare (spec);

    dryBuffer.setSize (1, maximumExpectedSamplesPerBlock, false, true, false);
    preparedBlockSize = maximumExpectedSamplesPerBlock;

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

    for (auto* slot : { &gateSlot, &subSlot, &shifterSlot, &ampSlot, &cabSlot })
        slot->reset();
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
        gateSlot.process (block);

        for (auto& module : movableModules)
            if (module.activePosition == params::Position::beforeAmp)
                module.slot.process (block);

        ampSlot.process (block);
        cabSlot.process (block);

        for (auto& module : movableModules)
            if (module.activePosition == params::Position::afterAmp)
                module.slot.process (block);

        outputGain.process (block);
    }

    bypass.process (dry, block);

    publishPeak (meters.outputPeak, dsp::blockPeak (block));
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
