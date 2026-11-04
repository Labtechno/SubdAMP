#pragma once

#include "subdamp/dsp/MonoEffect.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace subdamp::gui
{

/** Interrupteur rond façon pédale : anneau éteint, disque ambre allumé. */
class PowerButton final : public juce::Button
{
public:
    PowerButton() : juce::Button ("power") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;
};

/**
    Carte d'un module de la chaîne : titre, badge de latence, interrupteur, éventuel
    sélecteur de position (avant / après l'ampli), potards, et un composant libre en bas
    (sélecteur de fichier pour l'ampli et le cab).

    Tout est relié aux paramètres par des attachements JUCE : l'automation du DAW et
    l'interface restent synchronisées sans code supplémentaire.
*/
class ModuleCard final : public juce::Component
{
public:
    ModuleCard (juce::AudioProcessorValueTreeState& parameters,
                const juce::String& title,
                dsp::LatencyTier tier,
                const char* enabledParameterId,
                const char* positionParameterId = nullptr);

    ~ModuleCard() override;

    void addKnob (const char* parameterId, const juce::String& label);
    void setKnobColumns (int columns) { knobColumns = columns; }

    /** Composant affiché sous les potards (non possédé), avec sa hauteur à l'échelle 1. */
    void setFooter (juce::Component* component, int baseHeight);

    /** Largeur de la carte à l'échelle 1, pour la mise en page de la chaîne. */
    int getBaseWidth() const noexcept { return baseWidth; }
    void setBaseWidth (int width) noexcept { baseWidth = width; }

    void setScale (float newScale);

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void updatePositionText();

    juce::AudioProcessorValueTreeState& parameters;
    juce::String title;
    dsp::LatencyTier tier;

    PowerButton powerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    juce::TextButton positionButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> positionAttachment;

    std::vector<std::unique_ptr<Knob>> knobs;
    int knobColumns = 3;

    juce::Component* footer = nullptr;
    int footerBaseHeight = 0;

    int baseWidth = 200;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleCard)
};

} // namespace subdamp::gui
