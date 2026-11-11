#include "gui/ModuleCard.h"

#include "gui/Theme.h"

namespace subdamp::gui
{

//==============================================================================
void PowerButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre());
    const auto lineWidth = juce::jmax (1.5f, diameter * 0.1f);

    if (getToggleState())
    {
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillEllipse (circle.expanded (lineWidth));
        g.setColour (theme::accent);
        g.fillEllipse (circle);
    }
    else
    {
        g.setColour (isHighlighted ? theme::textDim : theme::bypassed);
        g.drawEllipse (circle.reduced (lineWidth * 0.5f), lineWidth);
    }
}

//==============================================================================
namespace
{
    constexpr int headerHeight = 64;
    constexpr int controlHeight = 132;
    constexpr int labelHeight = 18;
    constexpr int comboHeight = 30;
}

ModuleCard::ModuleCard (juce::AudioProcessorValueTreeState& parametersToUse,
                        const juce::String& cardTitle,
                        dsp::LatencyTier latencyTier,
                        const char* enabledParameterId)
    : parameters (parametersToUse),
      title (cardTitle),
      tier (latencyTier)
{
    powerButton.setTooltip (juce::String::fromUTF8 ("Marche / arrêt du module"));
    powerButton.onStateChange = [this] { updateEnabledLook(); };
    addAndMakeVisible (powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, enabledParameterId, powerButton);
}

ModuleCard::~ModuleCard() = default;

ModuleCard::Control& ModuleCard::addControl (const juce::String& labelText, int baseWidth)
{
    auto control = std::make_unique<Control>();
    control->baseWidth = baseWidth;
    control->label.setText (labelText, juce::dontSendNotification);
    control->label.setJustificationType (juce::Justification::centred);
    control->label.setColour (juce::Label::textColourId, theme::textDim);
    control->label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (control->label);

    controls.push_back (std::move (control));
    return *controls.back();
}

std::unique_ptr<ModuleCard::Control> ModuleCard::makeKnob (const char* parameterId, const juce::String& labelText)
{
    auto control = std::make_unique<Control>();
    control->baseWidth = 100;
    control->label.setText (labelText, juce::dontSendNotification);
    control->label.setJustificationType (juce::Justification::centred);
    control->label.setColour (juce::Label::textColourId, theme::textDim);
    control->label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (control->label);

    control->slider = std::make_unique<juce::Slider>();
    auto& slider = *control->slider;
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);

    control->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, parameterId, slider);

    // Double-clic : retour à la valeur par défaut du paramètre.
    if (auto* parameter = parameters.getParameter (parameterId))
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    return control;
}

void ModuleCard::addKnob (const char* parameterId, const juce::String& labelText)
{
    controls.push_back (makeKnob (parameterId, labelText));
    updateEnabledLook();
}

void ModuleCard::setMixKnob (const char* parameterId)
{
    mixControl = makeKnob (parameterId, "MIX");
    mixControl->label.setColour (juce::Label::textColourId, theme::accent);
    mixControl->slider->setTooltip (juce::String::fromUTF8 ("Dosage entre le son qui arrive (sec) et le son traité. Double-clic : valeur par défaut"));
    updateEnabledLook();
    resized();
}

void ModuleCard::addChoice (const char* parameterId, const juce::String& labelText, int baseWidth)
{
    auto& control = addControl (labelText, baseWidth);

    control.combo = std::make_unique<juce::ComboBox>();
    auto& combo = *control.combo;

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (parameterId)))
        combo.addItemList (choice->choices, 1);

    addAndMakeVisible (combo);
    control.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, parameterId, combo);

    updateEnabledLook();
}

void ModuleCard::setFooter (juce::Component* component, int baseWidth, int baseHeight)
{
    footer = component;
    footerBaseWidth = baseWidth;
    footerBaseHeight = baseHeight;
    addAndMakeVisible (footer);
}

void ModuleCard::refreshLatency()
{
    const auto text = latencyText != nullptr ? latencyText() : juce::String();

    if (text != shownLatency)
    {
        shownLatency = text;
        repaint();
    }
}

void ModuleCard::setScale (float newScale)
{
    scale = newScale;
    resized();
    repaint();
}

void ModuleCard::addToggle (const char* parameterId, const juce::String& labelText, int baseWidth)
{
    auto& control = addControl ({}, baseWidth);

    control.toggle = std::make_unique<juce::TextButton> (labelText);
    auto& button = *control.toggle;
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    button.setColour (juce::TextButton::buttonOnColourId, theme::latencyTexture);
    button.setColour (juce::TextButton::textColourOnId, theme::background);
    addAndMakeVisible (button);

    control.toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, parameterId, button);
    updateEnabledLook();
}

void ModuleCard::updateEnabledLook()
{
    // Module à l'arrêt : réglages grisés, mais toujours modifiables (on prépare un son avant de l'allumer).
    const auto alpha = powerButton.getToggleState() ? 1.0f : 0.45f;

    if (mixControl != nullptr)
    {
        mixControl->label.setAlpha (alpha);
        mixControl->slider->setAlpha (alpha);
    }

    for (auto& control : controls)
    {
        control->label.setAlpha (alpha);

        if (control->slider != nullptr)
            control->slider->setAlpha (alpha);

        if (control->combo != nullptr)
            control->combo->setAlpha (alpha);

        if (control->toggle != nullptr)
            control->toggle->setAlpha (alpha);
    }

    repaint();
}

//==============================================================================
void ModuleCard::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto corner = 12.0f * scale;
    const auto isOn = powerButton.getToggleState();

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (isOn ? theme::panelOutline.brighter (0.15f) : theme::panelOutline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f * scale);

    // En-tête : interrupteur, titre, description ; badge à droite.
    auto header = bounds.withHeight (headerHeight * scale).reduced (20.0f * scale, 0.0f);
    header.removeFromLeft (40.0f * scale); // interrupteur

    const auto badgeText = theme::labelFor (tier) + shownLatency;
    const auto badgeWidth = juce::GlyphArrangement::getStringWidth (juce::FontOptions (11.0f * scale, juce::Font::bold), badgeText)
                          + 26.0f * scale;
    const auto badge = header.removeFromRight (badgeWidth).withSizeKeepingCentre (badgeWidth, 22.0f * scale);
    theme::drawLatencyBadge (g, badge, tier, shownLatency, scale);
    header.removeFromRight (16.0f * scale);

    g.setColour (isOn ? theme::text : theme::textDim);
    g.setFont (juce::FontOptions (20.0f * scale, juce::Font::bold));
    const auto titleWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + 18.0f * scale;
    g.drawText (title, header.removeFromLeft (titleWidth), juce::Justification::centredLeft);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f * scale));
    g.drawFittedText (description, header.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);

    g.setColour (theme::panelOutline);
    g.drawHorizontalLine (juce::roundToInt (headerHeight * scale), bounds.getX() + 16.0f * scale, bounds.getRight() - 16.0f * scale);

    // Séparation du MIX : il dose tout le module, pas un réglage parmi d'autres.
    if (mixDividerX >= 0)
        g.drawVerticalLine (mixDividerX, (headerHeight + 24.0f) * scale, bounds.getBottom() - 24.0f * scale);
}

void ModuleCard::resized()
{
    const auto px = [this] (float value) { return juce::roundToInt (value * scale); };

    auto area = getLocalBounds().reduced (px (20.0f), 0);
    auto header = area.removeFromTop (px (headerHeight));

    powerButton.setBounds (header.removeFromLeft (px (30.0f)).withSizeKeepingCentre (px (28.0f), px (28.0f)));

    area.removeFromTop (px (12.0f));
    area.removeFromBottom (px (12.0f));

    const auto rowHeight = px (static_cast<float> (controlHeight));
    const auto rowY = area.getY() + juce::jmax (0, (area.getHeight() - rowHeight) / 2);

    // MIX : tout à droite, séparé par un trait.
    mixDividerX = -1;

    if (mixControl != nullptr)
    {
        const auto width = px (static_cast<float> (mixControl->baseWidth));
        layoutControl (*mixControl, juce::Rectangle<int> (area.getRight() - width, rowY, width, rowHeight));
        area.removeFromRight (width + px (12.0f));
        mixDividerX = area.getRight();
        area.removeFromRight (px (12.0f));
    }

    // Composant libre (fichier) : à droite des réglages, ou centré s'il n'y a pas de réglages.
    if (footer != nullptr)
    {
        const auto footerWidth = px (static_cast<float> (footerBaseWidth));
        const auto footerHeight = px (static_cast<float> (footerBaseHeight));

        if (controls.empty())
        {
            footer->setBounds (area.withSizeKeepingCentre (juce::jmin (footerWidth, area.getWidth()), footerHeight));
            return;
        }

        footer->setBounds (area.removeFromRight (footerWidth).withSizeKeepingCentre (footerWidth, footerHeight));
        area.removeFromRight (px (24.0f));
    }

    // Réglages sur une ligne, centrés ; resserrés s'ils sont trop nombreux pour la largeur.
    float totalWidth = 0.0f;

    for (auto& control : controls)
        totalWidth += static_cast<float> (control->baseWidth) * scale;

    const auto squeeze = totalWidth > 0.0f ? juce::jmin (1.0f, static_cast<float> (area.getWidth()) / totalWidth) : 1.0f;
    auto x = static_cast<float> (area.getX()) + juce::jmax (0.0f, (static_cast<float> (area.getWidth()) - totalWidth * squeeze) * 0.5f);

    for (auto& control : controls)
    {
        const auto width = static_cast<float> (control->baseWidth) * scale * squeeze;
        layoutControl (*control, juce::Rectangle<float> (x, static_cast<float> (rowY), width, static_cast<float> (rowHeight)).toNearestInt());
        x += width;
    }
}

void ModuleCard::layoutControl (Control& control, juce::Rectangle<int> cell)
{
    const auto px = [this] (float value) { return juce::roundToInt (value * scale); };
    cell = cell.reduced (px (6.0f), 0);

    control.label.setFont (juce::FontOptions (12.0f * scale, juce::Font::bold));
    control.label.setBounds (cell.removeFromTop (px (static_cast<float> (labelHeight))));

    if (control.slider != nullptr)
    {
        control.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, cell.getWidth(), px (20.0f));
        control.slider->setBounds (cell.withTrimmedTop (px (4.0f)));
    }
    else if (control.combo != nullptr)
    {
        control.combo->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), px (static_cast<float> (comboHeight)))
                                      .translated (0, -px (10.0f)));
    }
    else if (control.toggle != nullptr)
    {
        control.toggle->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), px (64.0f)).translated (0, -px (10.0f)));
    }
}

} // namespace subdamp::gui
