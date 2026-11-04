#include "standalone/StandaloneHost.h"

#include <algorithm>
#include <cmath>

namespace subdamp::standalone
{

namespace
{
    constexpr const char* pluginStateKey = "pluginState";
    constexpr const char* deviceStateKey = "audioDeviceState";

    std::unique_ptr<juce::AudioProcessor> createStandaloneProcessor()
    {
        // Même séquence que le wrapper standalone de JUCE : le processeur doit savoir
        // qu'il tourne en standalone (wrapperType) dès sa construction.
        juce::PluginHostType::jucePlugInClientCurrentWrapperType = juce::AudioProcessor::wrapperType_Standalone;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        std::unique_ptr<juce::AudioProcessor> instance (createPluginFilter());
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);

        jassert (instance != nullptr);
        return instance;
    }

    /**
        Choisit le canal guitare d'un périphérique qu'on n'a jamais configuré.

        Priorité aux noms explicites (Inst, Guitar, Hi-Z). Sinon, cas des Focusrite à deux
        entrées (Scarlett Solo) : l'entrée instrument est la 2. Sinon, la première entrée.
    */
    int guessGuitarChannel (const juce::String& deviceName, const juce::StringArray& inputNames)
    {
        if (inputNames.isEmpty())
            return -1;

        for (int i = 0; i < inputNames.size(); ++i)
        {
            const auto name = inputNames[i].toLowerCase();

            if (name.contains ("inst") || name.contains ("guit") || name.contains ("hi-z") || name.contains ("hiz"))
                return i;
        }

        if (deviceName.containsIgnoreCase ("focusrite") && inputNames.size() == 2)
            return 1;

        return 0;
    }
}

//==============================================================================
StandaloneHost::StandaloneHost()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = JucePlugin_Name;
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
   #if JUCE_LINUX || JUCE_BSD
    options.folderName          = "~/.config";
   #endif
    properties.setStorageParameters (options);

    processor = createStandaloneProcessor();

    auto* settings = properties.getUserSettings();

    juce::MemoryBlock pluginState;

    if (pluginState.fromBase64Encoding (settings->getValue (pluginStateKey)) && pluginState.getSize() > 0)
        processor->setStateInformation (pluginState.getData(), static_cast<int> (pluginState.getSize()));

    const auto savedDeviceState = settings->getXmlValue (deviceStateKey);
    firstLaunch = savedDeviceState == nullptr;

    deviceManager.initialise (1, 2, savedDeviceState.get(), true);

    if (firstLaunch)
    {
        preferAsioOnFirstLaunch();
        selectDefaultChannels();
    }

    deviceManager.addAudioCallback (this);
    deviceManager.addChangeListener (this);
}

StandaloneHost::~StandaloneHost()
{
    deviceManager.removeChangeListener (this);
    saveSettings();

    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void StandaloneHost::saveSettings()
{
    auto* settings = properties.getUserSettings();

    juce::MemoryBlock pluginState;
    processor->getStateInformation (pluginState);
    settings->setValue (pluginStateKey, pluginState.toBase64Encoding());

    if (const auto deviceState = deviceManager.createStateXml())
        settings->setValue (deviceStateKey, deviceState.get());

    properties.saveIfNeeded();
}

void StandaloneHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Chaque changement de carte son est sauvegardé tout de suite : rien n'est perdu en cas de plantage.
    saveSettings();
}

//==============================================================================
void StandaloneHost::preferAsioOnFirstLaunch()
{
   #if JUCE_WINDOWS
    // Le pilote par défaut sous Windows (WASAPI partagé) ajoute beaucoup de latence.
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != "ASIO")
            continue;

        type->scanForDevices();

        if (! type->getDeviceNames().isEmpty())
            deviceManager.setCurrentAudioDeviceType (type->getTypeName(), true);

        return;
    }
   #endif
}

juce::String StandaloneHost::applySetup (juce::AudioDeviceManager::AudioDeviceSetup setup)
{
    return deviceManager.setAudioDeviceSetup (setup, true);
}

juce::String StandaloneHost::selectDefaultChannels()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return {};

    const auto guitarChannel = guessGuitarChannel (device->getName(), device->getInputChannelNames());

    if (const auto error = selectGuitarInput (guitarChannel); error.isNotEmpty())
        return error;

    return selectOutputChannels (0);
}

juce::String StandaloneHost::selectDriver (const juce::String& typeName)
{
    deviceManager.setCurrentAudioDeviceType (typeName, true);
    return selectDefaultChannels();
}

juce::String StandaloneHost::selectDevices (const juce::String& inputDeviceName, const juce::String& outputDeviceName)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.inputDeviceName = inputDeviceName;
    setup.outputDeviceName = outputDeviceName;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;

    if (const auto error = applySetup (setup); error.isNotEmpty())
        return error;

    return selectDefaultChannels();
}

juce::String StandaloneHost::selectGuitarInput (int channelIndex)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();

    if (channelIndex >= 0)
        setup.inputChannels.setBit (channelIndex);

    return applySetup (setup);
}

juce::String StandaloneHost::selectOutputChannels (int firstChannelIndex)
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr || firstChannelIndex < 0)
        return {};

    const auto numOutputs = device->getOutputChannelNames().size();

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setBit (firstChannelIndex);

    if (firstChannelIndex + 1 < numOutputs)
        setup.outputChannels.setBit (firstChannelIndex + 1);

    return applySetup (setup);
}

juce::String StandaloneHost::selectSampleRate (double sampleRate)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = sampleRate;
    return applySetup (setup);
}

juce::String StandaloneHost::selectBufferSize (int bufferSizeSamples)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = bufferSizeSamples;
    return applySetup (setup);
}

int StandaloneHost::getGuitarInputChannel() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getActiveInputChannels().findNextSetBit (0);

    return -1;
}

int StandaloneHost::getFirstOutputChannel() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getActiveOutputChannels().findNextSetBit (0);

    return -1;
}

double StandaloneHost::getRoundTripLatencyMs() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr || device->getCurrentSampleRate() <= 0.0)
        return 0.0;

    auto driverLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    // Certains pilotes ne rapportent rien : on compte au minimum un buffer à l'aller et un au retour.
    if (driverLatency <= 0)
        driverLatency = 2 * device->getCurrentBufferSizeSamples();

    const auto total = driverLatency + processor->getLatencySamples();
    return 1000.0 * total / device->getCurrentSampleRate();
}

juce::String StandaloneHost::describeSetup() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return juce::String::fromUTF8 ("Aucune carte son ouverte");

    const auto guitarChannel = getGuitarInputChannel();
    const auto inputName = guitarChannel >= 0 ? device->getInputChannelNames()[guitarChannel]
                                              : juce::String::fromUTF8 ("aucune entrée");

    return device->getName()
         + juce::String::fromUTF8 ("  ·  Guitare : ") + inputName
         + juce::String::fromUTF8 ("  ·  ") + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz"
         + juce::String::fromUTF8 ("  ·  ") + juce::String (device->getCurrentBufferSizeSamples())
         + juce::String::fromUTF8 (" éch.  ·  ≈ ") + juce::String (getRoundTripLatencyMs(), 1) + " ms";
}

//==============================================================================
void StandaloneHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sampleRate = device->getCurrentSampleRate();
    const auto bufferSize = device->getCurrentBufferSizeSamples();

    processBuffer.setSize (2, bufferSize, false, true, false);

    processor->setRateAndBufferSizeDetails (sampleRate, bufferSize);
    processor->prepareToPlay (sampleRate, bufferSize);
}

void StandaloneHost::audioDeviceStopped()
{
    processor->releaseResources();
}

void StandaloneHost::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                       float* const* outputChannelData, int numOutputChannels,
                                                       int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const auto capacity = processBuffer.getNumSamples();

    if (capacity == 0)
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

        return;
    }

    const auto* guitar = numInputChannels > 0 ? inputChannelData[0] : nullptr;

    // Le pilote peut exceptionnellement livrer plus d'échantillons que la taille annoncée.
    for (int start = 0; start < numSamples; start += capacity)
    {
        const auto chunk = std::min (capacity, numSamples - start);
        auto* left = processBuffer.getWritePointer (0);

        if (guitar != nullptr)
        {
            juce::FloatVectorOperations::copy (left, guitar + start, chunk);

            const auto range = juce::FloatVectorOperations::findMinAndMax (left, chunk);
            const auto peak = std::max (std::abs (range.getStart()), std::abs (range.getEnd()));

            if (peak > inputPeak.load (std::memory_order_relaxed))
                inputPeak.store (peak, std::memory_order_relaxed);
        }
        else
        {
            juce::FloatVectorOperations::clear (left, chunk);
        }

        processBuffer.clear (1, 0, chunk);

        // Vue sur les données préallouées : pas d'allocation (moins de 32 canaux).
        juce::AudioBuffer<float> block (processBuffer.getArrayOfWritePointers(), 2, chunk);
        processor->processBlock (block, emptyMidi);

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (channel < 2)
                juce::FloatVectorOperations::copy (outputChannelData[channel] + start, block.getReadPointer (channel), chunk);
            else
                juce::FloatVectorOperations::clear (outputChannelData[channel] + start, chunk);
        }
    }
}

} // namespace subdamp::standalone
