#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace subdamp::standalone
{

/**
    Hôte audio de l'application standalone : carte son, routage des canaux, sauvegarde.

    Remplace StandalonePluginHolder de JUCE pour trois raisons :
      - l'entrée n'est jamais coupée (JUCE la coupe par défaut pour éviter le larsen) ;
      - un seul canal d'entrée est ouvert, celui de la guitare, choisi explicitement
        (sur une Scarlett Solo, la guitare est sur l'entrée 2, pas la 1) ;
      - le callback audio n'utilise ni verrou ni allocation.

    Le pilote ne transmet au callback que les canaux actifs : on en active exactement un
    en entrée (la guitare) et deux en sortie (ou un sur une sortie mono).
*/
class StandaloneHost final : private juce::AudioIODeviceCallback,
                             private juce::ChangeListener
{
public:
    StandaloneHost();
    ~StandaloneHost() override;

    juce::AudioProcessor& getProcessor() noexcept      { return *processor; }
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    /** Vrai si aucun réglage audio n'avait été sauvegardé (premier lancement). */
    bool isFirstLaunch() const noexcept { return firstLaunch; }

    /** Change de pilote (ASIO, CoreAudio, WASAPI...) et choisit des canaux par défaut. Renvoie une erreur éventuelle. */
    juce::String selectDriver (const juce::String& typeName);

    /** Change d'interface audio. Pour les pilotes sans entrées/sorties séparées (ASIO), passer le même nom deux fois. */
    juce::String selectDevices (const juce::String& inputDeviceName, const juce::String& outputDeviceName);

    juce::String selectGuitarInput (int channelIndex);
    juce::String selectOutputChannels (int firstChannelIndex);
    juce::String selectSampleRate (double sampleRate);
    juce::String selectBufferSize (int bufferSizeSamples);

    /** Index du canal d'entrée guitare dans la liste du périphérique, ou -1. */
    int getGuitarInputChannel() const;
    /** Index du premier canal de sortie utilisé, ou -1. */
    int getFirstOutputChannel() const;

    /** Latence aller-retour estimée (pilote d'entrée + sortie + plugin), en millisecondes. */
    double getRoundTripLatencyMs() const;

    /** Résumé lisible de la configuration (barre d'état). */
    juce::String describeSetup() const;

    /** Crête du signal guitare brut depuis le dernier appel (pour le vumètre des réglages). */
    float popInputPeak() noexcept { return inputPeak.exchange (0.0f, std::memory_order_relaxed); }

    void saveSettings();

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    juce::String applySetup (juce::AudioDeviceManager::AudioDeviceSetup setup);
    juce::String selectDefaultChannels();
    void preferAsioOnFirstLaunch();

    juce::ApplicationProperties properties;
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::AudioDeviceManager deviceManager;
    bool firstLaunch = false;

    // Préalloués dans audioDeviceAboutToStart, utilisés seulement par le thread audio.
    juce::AudioBuffer<float> processBuffer;
    juce::MidiBuffer emptyMidi;

    std::atomic<float> inputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneHost)
};

} // namespace subdamp::standalone
