#pragma once

#include "gui/LevelMeter.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace subdamp::gui
{

/**
    Interface du plugin (étape 1 : entrée, sortie, bypass, vumètres).

    L'interface ne lit l'audio qu'à travers des atomics (paramètres et LevelMeters) :
    aucun échange bloquant avec le thread audio.
*/
class SubdAmpEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit SubdAmpEditor (SubdAmpProcessor& processorToEdit);
    ~SubdAmpEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class LookAndFeel;

    void timerCallback() override;
    float scale() const noexcept;

    static constexpr int baseWidth = 760;
    static constexpr int baseHeight = 420;

    SubdAmpProcessor& subdAmp;
    std::unique_ptr<LookAndFeel> lookAndFeel;

    juce::Slider inputKnob, outputKnob;
    juce::Label inputLabel, outputLabel;
    juce::TextButton bypassButton;
    LevelMeter inputMeter, outputMeter;

    // Après les composants : détruites en premier.
    juce::AudioProcessorValueTreeState::SliderAttachment inputAttachment, outputAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    double lastTimerSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubdAmpEditor)
};

} // namespace subdamp::gui
