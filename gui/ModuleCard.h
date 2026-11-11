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
    Voyant façon LED de pédale : sa luminosité suit un niveau de 0 à 1, relu par refresh()
    (minuteur de l'interface). Un texte sous la lampe dit l'état en clair.
*/
class StatusLamp final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    StatusLamp (std::function<float()> levelProvider, juce::Colour litColour,
                const juce::String& litText, const juce::String& unlitText);

    void refresh();
    void setScale (float newScale) { scale = newScale; repaint(); }

    float getShownLevel() const noexcept { return shownLevel; }

    void paint (juce::Graphics& g) override;

private:
    std::function<float()> level;
    juce::Colour colour;
    juce::String litText, unlitText;
    float shownLevel = 0.0f;
    float scale = 1.0f;
};

/**
    Panneau des réglages d'un module, affiché en grand sous le pédalier : titre, description,
    badge de latence, interrupteur, potards et listes déroulantes, potard MIX à part à droite,
    et un composant libre (sélecteur de fichier de l'ampli et du cab).

    Tout est relié aux paramètres par des attachements JUCE : l'automation du DAW et
    l'interface restent synchronisées sans code supplémentaire.
*/
class ModuleCard final : public juce::Component
{
public:
    ModuleCard (juce::AudioProcessorValueTreeState& parameters,
                const juce::String& title,
                dsp::LatencyTier tier,
                const char* enabledParameterId);

    ~ModuleCard() override;

    void addKnob (const char* parameterId, const juce::String& label);
    void addChoice (const char* parameterId, const juce::String& label, int baseWidth = 170);

    /** Gros bouton à bascule (FIGER du FREEZE), relié à un paramètre booléen. */
    void addToggle (const char* parameterId, const juce::String& label, int baseWidth = 150);

    /** Potard MIX (dry / wet), isolé à droite des réglages. */
    void setMixKnob (const char* parameterId);

    /** Voyant d'état (coupure du GATE...), placé avec les réglages. */
    StatusLamp& addLamp (const juce::String& label, std::function<float()> level, juce::Colour litColour,
                         const juce::String& litText, const juce::String& unlitText, int baseWidth = 110);

    /** Relit les voyants (minuteur de l'interface). */
    void refreshLamps();

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
        std::unique_ptr<juce::TextButton> toggle;
        std::unique_ptr<StatusLamp> lamp;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
        int baseWidth = 100;
    };

    Control& addControl (const juce::String& label, int baseWidth);
    std::unique_ptr<Control> makeKnob (const char* parameterId, const juce::String& label);
    void layoutControl (Control& control, juce::Rectangle<int> cell);
    void updateEnabledLook();

    juce::AudioProcessorValueTreeState& parameters;
    juce::String title, description;
    dsp::LatencyTier tier;

    PowerButton powerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    std::vector<std::unique_ptr<Control>> controls;
    std::unique_ptr<Control> mixControl;
    int mixDividerX = -1;

    juce::Component* footer = nullptr;
    int footerBaseWidth = 0, footerBaseHeight = 0;

    std::function<juce::String()> latencyText;
    juce::String shownLatency;

    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleCard)
};

} // namespace subdamp::gui
