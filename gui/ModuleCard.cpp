#include "gui/ModuleCard.h"

#include "gui/LabLookAndFeel.h"
#include "gui/Theme.h"

namespace subdamp::gui
{

//==============================================================================
void PowerButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto area = getLocalBounds().toFloat().reduced (3.0f);
    const auto scale = area.getHeight() / 22.0f;

    // Bague de l'interrupteur, plus claire au survol
    g.setColour (theme::seam);
    g.fillEllipse (area.expanded (2.0f));
    g.setColour (isHighlighted ? theme::amber.withAlpha (0.6f) : theme::metalMid);
    g.drawEllipse (area.expanded (1.0f), 1.2f);

    theme::drawLed (g, area.reduced (isDown ? 4.0f : 3.0f), theme::amber, getToggleState() ? 1.0f : 0.0f, scale);
}

//==============================================================================
StatusLamp::StatusLamp (std::function<float()> levelProvider, juce::Colour litColour,
                        const juce::String& lit, const juce::String& unlit)
    : level (std::move (levelProvider)), colour (litColour), litText (lit), unlitText (unlit)
{
    setInterceptsMouseClicks (true, false); // pour l'infobulle
}

void StatusLamp::refresh()
{
    const auto value = juce::jlimit (0.0f, 1.0f, level != nullptr ? level() : 0.0f);

    // Redessin seulement si la différence se voit.
    if (std::abs (value - shownLevel) > 0.02f || (value <= 0.0f) != (shownLevel <= 0.0f))
    {
        shownLevel = value;
        repaint();
    }
}

void StatusLamp::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto textHeight = 20.0f * scale;
    const auto lampArea = bounds.withTrimmedBottom (textHeight + 6.0f * scale);
    const auto diameter = juce::jmin (30.0f * scale, lampArea.getWidth(), lampArea.getHeight());

    theme::drawLed (g, juce::Rectangle<float> (diameter, diameter).withCentre (lampArea.getCentre()), colour, shownLevel, scale);

    const auto isLit = shownLevel >= 0.5f;
    g.setColour (isLit ? colour : theme::textDim);
    g.setFont (theme::labelFont (13.0f * scale));
    g.drawText (isLit ? litText : unlitText, bounds.removeFromBottom (textHeight), juce::Justification::centred);
}

//==============================================================================
namespace
{
    constexpr int headerHeight = 58;
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

void ModuleCard::styleLabel (juce::Label& label, juce::Colour colour)
{
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, colour);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

ModuleCard::Control& ModuleCard::addControl (const juce::String& labelText, int baseWidth)
{
    auto control = std::make_unique<Control>();
    control->baseWidth = baseWidth;
    control->label.setText (labelText, juce::dontSendNotification);
    styleLabel (control->label, theme::textDim);

    controls.push_back (std::move (control));
    return *controls.back();
}

std::unique_ptr<ModuleCard::Control> ModuleCard::makeKnob (const char* parameterId, const juce::String& labelText)
{
    auto control = std::make_unique<Control>();
    control->baseWidth = 100;
    control->label.setText (labelText, juce::dontSendNotification);
    styleLabel (control->label, theme::textDim);

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
    mixControl->label.setColour (juce::Label::textColourId, theme::amber);
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

void ModuleCard::addToggle (const char* parameterId, const juce::String& labelText, int baseWidth)
{
    auto& control = addControl ({}, baseWidth);

    control.toggle = std::make_unique<juce::TextButton> (labelText);
    auto& button = *control.toggle;
    button.setClickingTogglesState (true);
    button.getProperties().set (LabLookAndFeel::styleProperty, "latch");
    addAndMakeVisible (button);

    control.toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, parameterId, button);
    updateEnabledLook();
}

StatusLamp& ModuleCard::addLamp (const juce::String& labelText, std::function<float()> level, juce::Colour litColour,
                                 const juce::String& litText, const juce::String& unlitText, int baseWidth)
{
    auto& control = addControl (labelText, baseWidth);
    control.lamp = std::make_unique<StatusLamp> (std::move (level), litColour, litText, unlitText);
    addAndMakeVisible (*control.lamp);
    updateEnabledLook();
    return *control.lamp;
}

void ModuleCard::refreshLamps()
{
    for (auto& control : controls)
        if (control->lamp != nullptr)
            control->lamp->refresh();
}

void ModuleCard::setIdentity (const juce::String& moduleCode, int newPosition, int count)
{
    if (moduleCode != code || newPosition != position || count != positionCount)
    {
        code = moduleCode;
        position = newPosition;
        positionCount = count;
        repaint();
    }
}

void ModuleCard::setDisplay (juce::Component* newDisplay)
{
    display = newDisplay;

    if (display != nullptr)
        addAndMakeVisible (display);

    resized();
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
    // L'étiquette porte déjà la catégorie : on n'en garde que la valeur (« 15 ms »).
    const auto text = (latencyText != nullptr ? latencyText() : juce::String()).trimCharactersAtStart (juce::String::fromUTF8 (" ·"));

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

void ModuleCard::updateEnabledLook()
{
    // Module à l'arrêt : réglages grisés, mais toujours modifiables (on prépare un son avant de l'allumer).
    const auto alpha = powerButton.getToggleState() ? 1.0f : 0.45f;

    const auto apply = [alpha] (Control& control)
    {
        control.label.setAlpha (alpha);

        for (juce::Component* component : { static_cast<juce::Component*> (control.slider.get()), static_cast<juce::Component*> (control.combo.get()),
                                            static_cast<juce::Component*> (control.toggle.get()), static_cast<juce::Component*> (control.lamp.get()) })
            if (component != nullptr)
                component->setAlpha (alpha);
    };

    for (auto& control : controls)
        apply (*control);

    if (mixControl != nullptr)
        apply (*mixControl);

    repaint();
}

//==============================================================================
void ModuleCard::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto bounds = getLocalBounds().toFloat();
    const auto isOn = powerButton.getToggleState();

    // Façade vissée
    drawPlate (g, bounds.reduced (1.0f), scale, anthracite.brighter (0.06f), 7.0f * scale);

    for (const auto corner : { bounds.getTopLeft(), bounds.getTopRight(), bounds.getBottomLeft(), bounds.getBottomRight() })
        drawScrew (g, corner + juce::Point<float> (corner.x < bounds.getCentreX() ? 11.0f : -11.0f, corner.y < bounds.getCentreY() ? 11.0f : -11.0f) * scale,
                   3.6f * scale, 0.4f + corner.x * 0.01f);

    // Bandeau : gravure de séparation
    const auto lineY = headerArea.getBottom();
    g.setColour (seam);
    g.drawLine (bounds.getX() + 22.0f * scale, lineY, bounds.getRight() - 22.0f * scale, lineY, 1.5f * scale);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (bounds.getX() + 22.0f * scale, lineY + 1.5f * scale, bounds.getRight() - 22.0f * scale, lineY + 1.5f * scale, 1.0f * scale);

    auto header = headerArea.reduced (22.0f * scale, 0.0f);
    header.removeFromLeft (44.0f * scale); // interrupteur

    // Plaque d'identification : code du module et place dans la chaîne
    if (code.isNotEmpty())
    {
        const auto plateArea = header.removeFromLeft (74.0f * scale).withSizeKeepingCentre (74.0f * scale, 36.0f * scale);
        g.setColour (seam);
        g.fillPath (chamfered (plateArea, 4.0f * scale));
        g.setColour (hairline.brighter (0.2f));
        g.strokePath (chamfered (plateArea.reduced (0.5f * scale), 4.0f * scale), juce::PathStrokeType (1.0f * scale));

        auto inner = plateArea.reduced (6.0f * scale, 4.0f * scale);
        g.setColour (isOn ? amber : textDim);
        g.setFont (monoFont (12.5f * scale));
        g.drawText (code, inner.removeFromTop (inner.getHeight() * 0.55f), juce::Justification::centred, false);
        g.setColour (textDim);
        g.setFont (monoFont (9.0f * scale));
        g.drawText ("POS " + juce::String (position).paddedLeft ('0', 2) + "/" + juce::String (positionCount),
                    inner, juce::Justification::centred, false);

        header.removeFromLeft (14.0f * scale);
    }

    // Étiquette de latence, à droite
    const auto tagHeight = 24.0f * scale;
    const auto tagWidth = latencyTagWidth (tier, shownLatency, tagHeight);
    drawLatencyTag (g, header.removeFromRight (tagWidth).withSizeKeepingCentre (tagWidth, tagHeight), tier, shownLatency, scale);
    header.removeFromRight (18.0f * scale);

    // Nom, puis description sur deux lignes
    const auto titleFontOptions = titleFont (24.0f * scale);
    const auto titleWidth = juce::GlyphArrangement::getStringWidth (titleFontOptions, title) + 18.0f * scale;
    drawSilkscreen (g, title, header.removeFromLeft (titleWidth), titleFontOptions, juce::Justification::centredLeft, isOn ? text : textDim);

    g.setColour (textDim);
    g.setFont (monoFont (11.0f * scale));
    g.drawFittedText (description, header.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);

    // MIX : filet gravé qui le sépare des autres réglages
    if (! mixArea.isEmpty())
    {
        const auto x = mixArea.getX() - 10.0f * scale;
        g.setColour (seam);
        g.drawLine (x, mixArea.getY() + 12.0f * scale, x, mixArea.getBottom() - 12.0f * scale, 1.5f * scale);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawLine (x + 1.5f * scale, mixArea.getY() + 12.0f * scale, x + 1.5f * scale, mixArea.getBottom() - 12.0f * scale, 1.0f * scale);
    }
}

void ModuleCard::layoutControl (Control& control, juce::Rectangle<int> cell)
{
    const auto px = [this] (float value) { return juce::roundToInt (value * scale); };
    cell = cell.reduced (px (5.0f), 0);

    control.label.setFont (theme::labelFont (13.0f * scale));
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
        control.toggle->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), px (58.0f)).translated (0, -px (10.0f)));
    }
    else if (control.lamp != nullptr)
    {
        control.lamp->setScale (scale);
        control.lamp->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), px (76.0f)).translated (0, -px (6.0f)));
    }
}

void ModuleCard::layoutRow (std::span<Control* const> row, juce::Rectangle<int> area)
{
    float totalWidth = 0.0f;

    for (const auto* control : row)
        totalWidth += static_cast<float> (control->baseWidth) * scale;

    const auto squeeze = totalWidth > 0.0f ? juce::jmin (1.0f, static_cast<float> (area.getWidth()) / totalWidth) : 1.0f;
    auto x = static_cast<float> (area.getX()) + juce::jmax (0.0f, (static_cast<float> (area.getWidth()) - totalWidth * squeeze) * 0.5f);
    const auto height = static_cast<float> (controlHeight) * scale;
    const auto y = static_cast<float> (area.getY()) + juce::jmax (0.0f, (static_cast<float> (area.getHeight()) - height) * 0.5f);

    for (auto* control : row)
    {
        const auto width = static_cast<float> (control->baseWidth) * scale * squeeze;
        layoutControl (*control, juce::Rectangle<float> (x, y, width, height).toNearestInt());
        x += width;
    }
}

void ModuleCard::resized()
{
    const auto px = [this] (float value) { return juce::roundToInt (value * scale); };

    auto area = getLocalBounds();
    headerArea = area.removeFromTop (px (static_cast<float> (headerHeight))).toFloat();

    auto header = headerArea.toNearestInt().reduced (px (22.0f), 0);
    powerButton.setBounds (header.removeFromLeft (px (32.0f)).withSizeKeepingCentre (px (30.0f), px (30.0f)));

    auto body = area.reduced (px (22.0f), px (16.0f));

    // Écran, à gauche
    if (display != nullptr)
    {
        const auto width = juce::jmin (px (330.0f), body.getWidth() * 32 / 100);
        display->setBounds (body.removeFromLeft (width));
        body.removeFromLeft (px (20.0f));
    }

    // MIX, à droite
    mixArea = {};

    if (mixControl != nullptr)
    {
        auto column = body.removeFromRight (px (112.0f));
        mixArea = column.toFloat();
        layoutControl (*mixControl, column.withSizeKeepingCentre (px (100.0f), px (static_cast<float> (controlHeight))));
        body.removeFromRight (px (20.0f));
    }

    controlsArea = body.toFloat();

    // Composant libre (fichier) : sous les réglages, ou centré s'il n'y a pas de réglages.
    if (footer != nullptr)
    {
        const auto width = juce::jmin (px (static_cast<float> (footerBaseWidth)), body.getWidth());
        const auto height = px (static_cast<float> (footerBaseHeight));

        if (controls.empty())
        {
            footer->setBounds (body.withSizeKeepingCentre (width, height));
            return;
        }

        footer->setBounds (body.removeFromBottom (height).withSizeKeepingCentre (width, height));
        body.removeFromBottom (px (8.0f));
    }

    std::vector<Control*> all;

    for (auto& control : controls)
        all.push_back (control.get());

    if (all.empty())
        return;

    // Une rangée si elle tient (quitte à resserrer un peu), sinon deux rangées équilibrées.
    float totalWidth = 0.0f;

    for (const auto* control : all)
        totalWidth += static_cast<float> (control->baseWidth) * scale;

    const auto rowHeight = px (static_cast<float> (controlHeight));
    const auto twoRowsFit = body.getHeight() >= 2 * rowHeight + px (8.0f);

    if (totalWidth <= static_cast<float> (body.getWidth()) * 1.1f || all.size() < 2 || ! twoRowsFit)
    {
        layoutRow (all, body);
        return;
    }

    std::size_t split = 0;
    float accumulated = 0.0f;

    while (split < all.size() && accumulated + static_cast<float> (all[split]->baseWidth) * scale * 0.5f < totalWidth * 0.5f)
        accumulated += static_cast<float> (all[split++]->baseWidth) * scale;

    split = juce::jlimit<std::size_t> (1, all.size() - 1, split);

    auto block = body.withSizeKeepingCentre (body.getWidth(), 2 * rowHeight + px (8.0f));
    const std::span<Control* const> everything (all);
    layoutRow (everything.first (split), block.removeFromTop (rowHeight));
    block.removeFromTop (px (8.0f));
    layoutRow (everything.subspan (split), block);
}

} // namespace subdamp::gui
