#pragma once

#include "gui/Displays.h"
#include "gui/FileSlot.h"
#include "gui/LabLookAndFeel.h"
#include "gui/LevelMeter.h"
#include "gui/ModuleCard.h"
#include "gui/PedalBoard.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace subdamp::gui
{

/**
    Interface du plugin, comme un appareil de laboratoire :
      - bandeau : plaque de marque, oscilloscope de la sortie, latence, charge du traitement, bypass ;
      - flancs : faders d'entrée et de sortie avec leurs échelles de témoins ;
      - au centre, le pédalier dans l'ordre réel de la chaîne (glisser pour le changer) ;
      - en bas, la façade du module sélectionné : écran, réglages, MIX.

    L'interface ne lit l'audio qu'à travers des atomics (paramètres, mesures, prise d'oscilloscope) :
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
    void timerCallback() override;
    float scale() const noexcept;

    void selectModule (ModuleId module);
    void moveModule (ModuleId module, int newIndex);
    ModuleCard& cardFor (ModuleId module);
    void buildCards();
    void updateIdentities();
    void updateReadouts();

    static constexpr int baseWidth = 1280;
    static constexpr int baseHeight = 720;

    SubdAmpProcessor& subdAmp;

    // Polices et textures, puis l'apparence : avant tous les composants (détruits après eux).
    theme::SharedResources resources;
    std::unique_ptr<LabLookAndFeel> lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Slider inputFader, outputFader;
    juce::TextButton bypassButton;
    LevelMeter inputMeter, outputMeter;

    Oscilloscope scope;
    SegmentReadout latencyReadout, textureReadout, cpuReadout;

    FileSlot ampFile, cabFile;
    PedalBoard pedalBoard;
    std::array<std::unique_ptr<ModuleCard>, numModules> cards;
    std::array<std::unique_ptr<ModuleDisplay>, numModules> displays;

    // Après les composants : détruites en premier.
    juce::AudioProcessorValueTreeState::SliderAttachment inputAttachment, outputAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    // Zones calculées dans resized(), dessinées dans paint()
    juce::Rectangle<float> headerArea, brandArea, inputStrip, outputStrip, bayArea;

    ChainOrder shownOrder = defaultChainOrder;
    LatencyStatus shownLatency;
    double lastTimerSeconds = 0.0;
    double smoothedCpu = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubdAmpEditor)
};

} // namespace subdamp::gui
