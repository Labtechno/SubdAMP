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
ModuleCard::ModuleCard (juce::AudioProcessorValueTreeState& parametersToUse,
                        const juce::String& cardTitle,
                        dsp::LatencyTier latencyTier,
                        const char* enabledParameterId,
                        const char* positionParameterId)
    : parameters (parametersToUse),
      title (cardTitle),
      tier (latencyTier)
{
    powerButton.setTooltip (juce::String::fromUTF8 ("Marche / arrêt du module"));
    powerButton.onStateChange = [this] { repaint(); };
    addAndMakeVisible (powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, enabledParameterId, powerButton);

    if (positionParameterId != nullptr)
    {
        positionButton.setClickingTogglesState (true);
        positionButton.setTooltip (juce::String::fromUTF8 ("Place le module avant ou après l'ampli"));
        positionButton.setColour (juce::TextButton::buttonOnColourId, theme::panelRaised);
        positionButton.onStateChange = [this] { updatePositionText(); };
        addAndMakeVisible (positionButton);
        positionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, positionParameterId, positionButton);
        updatePositionText();
    }
}

ModuleCard::~ModuleCard() = default;

void ModuleCard::addKnob (const char* parameterId, const juce::String& labelText)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (knob->slider);

    knob->label.setText (labelText, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, theme::textDim);
    knob->label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (knob->label);

    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, parameterId, knob->slider);

    // Double-clic : retour à la valeur par défaut du paramètre.
    if (auto* parameter = parameters.getParameter (parameterId))
        knob->slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    knobs.push_back (std::move (knob));
}

void ModuleCard::setFooter (juce::Component* component, int baseHeight)
{
    footer = component;
    footerBaseHeight = baseHeight;
    addAndMakeVisible (footer);
}

void ModuleCard::setScale (float newScale)
{
    scale = newScale;
    resized();
    repaint();
}

void ModuleCard::updatePositionText()
{
    positionButton.setButtonText (positionButton.getToggleState() ? juce::String::fromUTF8 ("APRÈS L'AMPLI")
                                                                  : juce::String::fromUTF8 ("AVANT L'AMPLI"));
}

//==============================================================================
void ModuleCard::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto corner = 10.0f * scale;

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (powerButton.getToggleState() ? theme::panelOutline.brighter (0.15f) : theme::panelOutline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f * scale);

    auto header = bounds.reduced (12.0f * scale, 0.0f).withHeight (36.0f * scale).withY (6.0f * scale);

    const auto badge = header.removeFromRight (58.0f * scale).withSizeKeepingCentre (58.0f * scale, 18.0f * scale);
    theme::drawLatencyBadge (g, badge, tier, {}, scale);

    g.setColour (powerButton.getToggleState() ? theme::text : theme::textDim);
    g.setFont (juce::FontOptions (15.0f * scale, juce::Font::bold));
    g.drawText (title, header, juce::Justification::centredLeft);
}

void ModuleCard::paintOverChildren (juce::Graphics& g)
{
    // Module à l'arrêt : potards grisés, mais toujours réglables.
    if (powerButton.getToggleState())
        return;

    const auto top = 76.0f * scale;
    g.setColour (theme::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (top).reduced (2.0f), 8.0f * scale);
}

void ModuleCard::resized()
{
    const auto px = [this] (float value) { return juce::roundToInt (value * scale); };

    auto area = getLocalBounds().reduced (px (12.0f), 0);
    area.removeFromTop (px (42.0f)); // en-tête (titre + badge)

    auto controls = area.removeFromTop (px (26.0f));
    powerButton.setBounds (controls.removeFromLeft (px (26.0f)));

    if (positionButton.isVisible())
        positionButton.setBounds (controls.removeFromRight (px (118.0f)).reduced (0, px (2.0f)));

    area.removeFromTop (px (8.0f));

    if (footer != nullptr)
    {
        const auto footerHeight = px (static_cast<float> (footerBaseHeight));

        // Sans potards (cab), le sélecteur de fichier se place en haut de la carte.
        if (knobs.empty())
        {
            footer->setBounds (area.removeFromTop (footerHeight));
            return;
        }

        area.removeFromBottom (px (12.0f));
        footer->setBounds (area.removeFromBottom (footerHeight));
        area.removeFromBottom (px (6.0f));
    }

    if (knobs.empty())
        return;

    const auto columns = juce::jmin (knobColumns, static_cast<int> (knobs.size()));
    const auto cellWidth = area.getWidth() / columns;
    const auto cellHeight = px (106.0f);

    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        auto cell = juce::Rectangle<int> (area.getX() + column * cellWidth, area.getY() + row * cellHeight, cellWidth, cellHeight);

        auto& knob = *knobs[i];
        knob.label.setFont (juce::FontOptions (11.0f * scale, juce::Font::bold));
        knob.label.setBounds (cell.removeFromTop (px (16.0f)));
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, cell.getWidth(), px (18.0f));
        knob.slider.setBounds (cell.reduced (px (4.0f), 0).withTrimmedBottom (px (6.0f)));
    }
}

} // namespace subdamp::gui
