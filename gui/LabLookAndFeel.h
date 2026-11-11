#pragma once

#include "gui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace subdamp::gui
{

/**
    Apparence « matériel de labo » de tous les composants JUCE de SubdAmp.

      - potards : bouton moleté sur graduation sérigraphiée, arc ambre de la valeur (parti du
        centre pour les réglages bipolaires), index blanc cassé, valeur dans une fenêtre ;
      - faders verticaux : gorge, échelle en dB, repère 0 dB, curseur métallique strié ;
      - listes : fenêtre d'écran, chevron ambre ; menus : plaque sombre, sélection ambre ;
      - touches : plaques mécaniques. La propriété "lab-style" d'un bouton choisit une variante :
        "bypass" (témoin rouge), "latch" (témoin ambre : FIGER), sinon touche simple.

    Aucune logique ici : uniquement du dessin. Les composants restent reliés à leurs paramètres
    par les attachements JUCE habituels.
*/
class LabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LabLookAndFeel();

    static constexpr const char* styleProperty = "lab-style";

    // Potards et faders
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    // Textes
    juce::Font getLabelFont (juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    // Listes et menus
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;

    // Touches
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Divers
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

private:
    theme::SharedResources resources; // garde polices et textures chargées tant que l'apparence vit
};

} // namespace subdamp::gui
