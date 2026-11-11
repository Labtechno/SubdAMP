#pragma once

#include "subdamp/dsp/MonoEffect.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace subdamp::gui
{

/** Interrupteur rond façon LED de pédale : anneau éteint, disque ambre allumé. */
class PowerButton final : public juce::Button
{
public:
    PowerButton() : juce::Button ("power") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;
};

/**
    Panneau des réglages d'un module, affiché en grand sous le pédalier : titre, description,
    badge de latence, interrupteur, éventuel sélecteur de position (avant / après l'ampli),
    potards et listes déroulantes, et un composant libre (sélecteur de fichier de l'ampli et du cab).

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
    void addChoice (const char* parameterId, const juce::String& label, int baseWidth = 170);

    void setDescription (const juce::String& text) { description = text; repaint(); }

    /** Composant affiché à droite des réglages (non possédé), avec sa taille à l'échelle 1. */
    void setFooter (juce::Component* component, int baseWidth, int baseHeight);

    /** Texte ajouté au badge de latence (« · 15 ms »), relu par refreshLatency(). */
    void setLatencyText (std::function<juce::String()> provider) { latencyText = std::move (provider); }
    void refreshLatency();

    void setScale (float newScale);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Control
    {
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        int baseWidth = 100;
    };

    Control& addControl (const juce::String& label, int baseWidth);
    void updatePositionText();
    void updateEnabledLook();

    juce::AudioProcessorValueTreeState& parameters;
    juce::String title, description;
    dsp::LatencyTier tier;

    PowerButton powerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    juce::TextButton positionButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> positionAttachment;

    std::vector<std::unique_ptr<Control>> controls;

    juce::Component* footer = nullptr;
    int footerBaseWidth = 0, footerBaseHeight = 0;

    std::function<juce::String()> latencyText;
    juce::String shownLatency;

    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleCard)
};

} // namespace subdamp::gui
