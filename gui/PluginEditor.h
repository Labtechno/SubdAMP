#pragma once

#include "gui/FileSlot.h"
#include "gui/LevelMeter.h"
#include "gui/ModuleCard.h"
#include "gui/PedalBoard.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace subdamp::gui
{

/**
    Interface du plugin, façon pédalier :
      - en haut, l'entrée, le pédalier dans l'ordre réel de la chaîne, la sortie ;
      - en dessous, les réglages complets du module sélectionné.

    L'interface ne lit l'audio qu'à travers des atomics (paramètres, vumètres, latences) :
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

    void selectModule (ModuleId module);
    std::vector<ModuleId> chainOrder() const;
    ModuleCard& cardFor (ModuleId module);
    void buildCards();

    static constexpr int baseWidth = 1200;
    static constexpr int baseHeight = 560;

    SubdAmpProcessor& subdAmp;
    std::unique_ptr<LookAndFeel> lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Slider inputKnob, outputKnob;
    juce::Label inputLabel, outputLabel;
    juce::TextButton bypassButton;
    LevelMeter inputMeter, outputMeter;

    FileSlot ampFile, cabFile;
    PedalBoard pedalBoard;
    std::array<std::unique_ptr<ModuleCard>, moduleInfos.size()> cards;

    // Après les composants : détruites en premier.
    juce::AudioProcessorValueTreeState::SliderAttachment inputAttachment, outputAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    std::vector<ModuleId> shownOrder;
    LatencyStatus shownLatency;
    double lastTimerSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubdAmpEditor)
};

} // namespace subdamp::gui
