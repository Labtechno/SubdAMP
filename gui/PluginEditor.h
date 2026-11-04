#pragma once

#include "gui/FileSlot.h"
#include "gui/LevelMeter.h"
#include "gui/ModuleCard.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace subdamp::gui
{

/**
    Interface du plugin : entrée, chaîne de modules (gate → [PRE] → ampli → cab → [POST]),
    sortie. Les cartes des modules de pitch se déplacent avant ou après l'ampli selon leur
    paramètre de position.

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
    std::vector<ModuleCard*> chainOrder();

    static constexpr int baseWidth = 1400;
    static constexpr int baseHeight = 500;

    SubdAmpProcessor& subdAmp;
    std::unique_ptr<LookAndFeel> lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Slider inputKnob, outputKnob;
    juce::Label inputLabel, outputLabel;
    juce::TextButton bypassButton;
    LevelMeter inputMeter, outputMeter;

    FileSlot ampFile, cabFile;
    ModuleCard gateCard, subCard, ampCard, cabCard, shifterCard;

    // Après les composants : détruites en premier.
    juce::AudioProcessorValueTreeState::SliderAttachment inputAttachment, outputAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    std::atomic<float>& subPosition;
    std::atomic<float>& shifterPosition;
    std::pair<bool, bool> layoutPositions { false, false };

    int displayedLatency = 0;
    double lastTimerSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubdAmpEditor)
};

} // namespace subdamp::gui
