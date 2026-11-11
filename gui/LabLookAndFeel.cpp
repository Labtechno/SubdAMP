#include "gui/LabLookAndFeel.h"

#include <cmath>

namespace subdamp::gui
{

namespace
{
    juce::String styleOf (const juce::Component& component)
    {
        return component.getProperties()[LabLookAndFeel::styleProperty].toString();
    }

    bool isSliderTextBox (const juce::Label& label)
    {
        return dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr;
    }
}

LabLookAndFeel::LabLookAndFeel()
{
    setDefaultSansSerifTypeface (resources->label);

    using namespace theme;

    setColour (juce::ResizableWindow::backgroundColourId, graphite);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::Label::textColourId, text);

    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, amber.withAlpha (0.35f));

    setColour (juce::TextButton::buttonColourId, metalDark);
    setColour (juce::TextButton::buttonOnColourId, metalDark);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);

    setColour (juce::ComboBox::backgroundColourId, screenGlass);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, amber);

    setColour (juce::PopupMenu::backgroundColourId, anthracite);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha (0.16f));
    setColour (juce::PopupMenu::highlightedTextColourId, amber);

    setColour (juce::TooltipWindow::backgroundColourId, anthracite);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, hairline);

    setColour (juce::ScrollBar::thumbColourId, metalMid);
    setColour (juce::ListBox::backgroundColourId, screenGlass);
    setColour (juce::TextEditor::backgroundColourId, screenGlass);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, hairline);
    setColour (juce::TextEditor::focusedOutlineColourId, amber);
    setColour (juce::CaretComponent::caretColourId, amber);
    setColour (juce::AlertWindow::backgroundColourId, anthracite);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, hairline);
}

//==============================================================================
void LabLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace theme;

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto radius = size * 0.5f - 1.0f;
    const auto unit = size / 100.0f;
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Réglages centrés sur zéro (gains, intervalles, décalage) : l'arc part du milieu.
    const auto range = slider.getRange();
    const auto isBipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const auto origin = isBipolar ? startAngle + static_cast<float> (slider.valueToProportionOfLength (0.0)) * (endAngle - startAngle)
                                  : startAngle;

    // Graduation sérigraphiée : 40 divisions, repères tous les quarts.
    constexpr int ticks = 41;

    for (int i = 0; i < ticks; ++i)
    {
        const auto proportion = static_cast<float> (i) / static_cast<float> (ticks - 1);
        const auto tickAngle = startAngle + proportion * (endAngle - startAngle);
        const auto isMajor = i % 10 == 0;
        const auto isMid = i % 5 == 0;
        const auto inner = radius * (isMajor ? 0.80f : (isMid ? 0.85f : 0.885f));

        g.setColour (isMajor ? textDim : textFaint);
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (inner, tickAngle),
                                       centre.getPointOnCircumference (radius, tickAngle)),
                    (isMajor ? 1.7f : 1.0f) * unit);
    }

    // Arc de la valeur, dans sa gorge
    const auto arcRadius = radius * 0.735f;
    const auto arcWidth = 3.2f * unit;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (seam);
    g.strokePath (track, juce::PathStrokeType (arcWidth + 1.5f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    if (std::abs (angle - origin) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (origin, angle), juce::jmax (origin, angle), true);
        g.setColour (amber.withAlpha (0.3f));
        g.strokePath (value, juce::PathStrokeType (arcWidth + 3.0f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setColour (amber);
        g.strokePath (value, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // Bouton : ombre, jupe moletée, chapeau usiné
    const auto knobRadius = radius * 0.6f;
    const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

    g.setColour (seam.withAlpha (0.8f));
    g.fillEllipse (knob.expanded (1.5f * unit).translated (0.0f, 3.0f * unit));

    g.setGradientFill (juce::ColourGradient (metalMid, knob.getX(), knob.getY(), seam, knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob);

    constexpr int ridges = 36;

    for (int i = 0; i < ridges; ++i)
    {
        const auto ridgeAngle = juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (ridges);
        g.setColour (i % 2 == 0 ? juce::Colours::white.withAlpha (0.10f) : juce::Colours::black.withAlpha (0.45f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (knobRadius * 0.84f, ridgeAngle),
                                       centre.getPointOnCircumference (knobRadius * 0.99f, ridgeAngle)),
                    1.2f * unit);
    }

    const auto capRadius = knobRadius * 0.78f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (metalLight, cap.getX(), cap.getY(), metalDark.darker (0.3f), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);

    // Usinage concentrique du chapeau
    for (auto ring = 0.25f; ring < 1.0f; ring += 0.22f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawEllipse (cap.withSizeKeepingCentre (cap.getWidth() * ring, cap.getHeight() * ring), 1.0f * unit);
    }

    g.setColour (seam.withAlpha (0.7f));
    g.drawEllipse (cap, 1.0f * unit);

    // Index : trait blanc cassé gravé dans le chapeau
    const auto pointerStart = centre.getPointOnCircumference (capRadius * 0.18f, angle);
    const auto pointerEnd = centre.getPointOnCircumference (knobRadius * 0.96f, angle);
    g.setColour (seam);
    g.drawLine (juce::Line<float> (pointerStart, pointerEnd), 4.2f * unit);
    g.setColour (text);
    g.drawLine (juce::Line<float> (pointerStart, pointerEnd), 2.4f * unit);
}

void LabLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                       juce::Slider& slider)
{
    // minSliderPos / maxSliderPos ne servent qu'aux curseurs doubles : ici, on recalcule la course.
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    using namespace theme;

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto unit = static_cast<float> (width) / 60.0f;
    const auto centreX = area.getCentreX() + 8.0f * unit;

    // Course du curseur : même calcul que JUCE (haut = maximum).
    const auto positionFor = [&] (float proportion) { return area.getY() + (1.0f - proportion) * area.getHeight(); };

    // Gorge du fader
    const auto groove = juce::Rectangle<float> (centreX - 3.0f * unit, area.getY() - 4.0f * unit, 6.0f * unit, area.getHeight() + 8.0f * unit);
    drawRecess (g, groove, unit, 0.0f);

    // Échelle en dB, à gauche : les repères usuels qui tombent dans la plage du fader.
    const auto range = slider.getRange();

    for (const auto decibels : { 12.0, 6.0, 0.0, -6.0, -12.0, -24.0, -48.0 })
    {
        if (decibels < range.getStart() || decibels > range.getEnd())
            continue;

        const auto proportion = static_cast<float> (slider.valueToProportionOfLength (decibels));
        const auto tickY = positionFor (proportion);
        const auto isUnity = std::abs (decibels) < 0.01;

        g.setColour (isUnity ? amber : textFaint);
        g.drawLine (groove.getX() - 9.0f * unit, tickY, groove.getX() - 3.0f * unit, tickY, (isUnity ? 1.6f : 1.0f) * unit);
        g.drawLine (groove.getRight() + 3.0f * unit, tickY, groove.getRight() + 7.0f * unit, tickY, 1.0f * unit);

        g.setColour (isUnity ? amber : textDim);
        g.setFont (monoFont (9.5f * unit));
        g.drawText (decibels > 0.0 ? "+" + juce::String (juce::roundToInt (decibels)) : juce::String (juce::roundToInt (decibels)),
                    juce::Rectangle<float> (area.getX(), tickY - 6.0f * unit, groove.getX() - 11.0f * unit - area.getX(), 12.0f * unit),
                    juce::Justification::centredRight, false);
    }

    // Curseur : plaque métallique striée, trait de lecture central
    const auto cap = juce::Rectangle<float> (30.0f * unit, 17.0f * unit).withCentre ({ centreX, sliderPos });
    drawPlate (g, cap, unit, metalMid, 2.0f * unit);

    for (const auto offset : { -5.5f, -3.0f, 3.0f, 5.5f })
    {
        g.setColour (seam.withAlpha (0.6f));
        g.drawLine (cap.getX() + 3.0f * unit, cap.getCentreY() + offset * unit, cap.getRight() - 3.0f * unit, cap.getCentreY() + offset * unit, 1.0f * unit);
    }

    g.setColour (text);
    g.fillRect (cap.getX() + 2.0f * unit, cap.getCentreY() - 0.75f * unit, cap.getWidth() - 4.0f * unit, 1.5f * unit);
}

juce::Label* LabLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, theme::text);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::textColourId, theme::text);
    label->setColour (juce::TextEditor::backgroundColourId, theme::screenGlass);
    label->setColour (juce::TextEditor::highlightColourId, theme::amber.withAlpha (0.35f));
    return label;
}

//==============================================================================
juce::Font LabLookAndFeel::getLabelFont (juce::Label& label)
{
    // Valeurs des potards : police à chasse fixe, les chiffres ne dansent pas.
    if (isSliderTextBox (label))
        return juce::Font (theme::monoFont (juce::jmax (9.0f, static_cast<float> (label.getHeight()) * 0.62f)));

    return label.getFont();
}

void LabLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (! isSliderTextBox (label) || label.isBeingEdited())
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    // Fenêtre de lecture de la valeur, creusée dans le panneau
    const auto font = getLabelFont (label);
    const auto area = label.getLocalBounds().toFloat();
    const auto unit = area.getHeight() / 20.0f;
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, label.getText()) + 14.0f * unit;
    const auto window = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), juce::jmax (textWidth, 46.0f * unit)),
                                                    area.getHeight() - 2.0f * unit);

    theme::drawRecess (g, window, unit, 2.0f * unit);
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    g.setFont (font);
    g.drawText (label.getText(), window, juce::Justification::centred, false);
}

//==============================================================================
void LabLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                   int, int, int, int, juce::ComboBox& box)
{
    using namespace theme;

    const auto area = juce::Rectangle<int> (width, height).toFloat();
    const auto unit = static_cast<float> (height) / 30.0f;

    drawRecess (g, area, unit, 3.0f * unit);
    g.setColour (isButtonDown ? screenGlass.brighter (0.08f) : screenGlass);
    g.fillPath (chamfered (area.reduced (2.0f * unit), 2.0f * unit));

    if (box.hasKeyboardFocus (false))
    {
        g.setColour (amber.withAlpha (0.5f));
        g.strokePath (chamfered (area.reduced (1.5f * unit), 2.5f * unit), juce::PathStrokeType (1.0f * unit));
    }

    // Chevron ambre, dans sa case
    const auto arrowZone = area.withLeft (area.getRight() - static_cast<float> (height) * 0.9f).reduced (2.0f * unit);
    g.setColour (hairline);
    g.drawLine (arrowZone.getX(), arrowZone.getY() + 3.0f * unit, arrowZone.getX(), arrowZone.getBottom() - 3.0f * unit, 1.0f * unit);

    juce::Path chevron;
    const auto c = arrowZone.getCentre();
    chevron.startNewSubPath (c.x - 5.0f * unit, c.y - 2.5f * unit);
    chevron.lineTo (c.x, c.y + 2.5f * unit);
    chevron.lineTo (c.x + 5.0f * unit, c.y - 2.5f * unit);
    g.setColour (box.isEnabled() ? amber : textFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.8f * unit, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}

juce::Font LabLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (theme::monoFont (static_cast<float> (box.getHeight()) * 0.48f));
}

void LabLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto unit = static_cast<float> (box.getHeight()) / 30.0f;
    label.setBounds (juce::roundToInt (6.0f * unit), 1, box.getWidth() - juce::roundToInt (static_cast<float> (box.getHeight()) * 0.9f + 8.0f * unit),
                     box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.75f);
}

void LabLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    using namespace theme;

    g.fillAll (anthracite);
    g.setColour (hairline);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (amber.withAlpha (0.7f));
    g.fillRect (0, 0, 2, height);
}

void LabLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                                        const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
{
    using namespace theme;

    if (isSeparator || icon != nullptr || hasSubMenu || shortcutKeyText.isNotEmpty())
    {
        LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu,
                                           itemText, shortcutKeyText, icon, textColour);
        return;
    }

    auto r = area.toFloat().reduced (4.0f, 1.0f);

    if (isHighlighted && isActive)
    {
        g.setColour (amber.withAlpha (0.14f));
        g.fillRect (r);
        g.setColour (amber);
        g.fillRect (r.withWidth (2.0f));
    }

    // Choix en cours : petit carré ambre, comme un témoin de position
    const auto markZone = r.removeFromLeft (r.getHeight());

    if (isTicked)
    {
        g.setColour (amber);
        g.fillRect (markZone.withSizeKeepingCentre (6.0f, 6.0f));
    }

    g.setColour (! isActive ? textFaint : (isHighlighted ? amber : (textColour != nullptr ? *textColour : text)));
    g.setFont (getPopupMenuFont());
    g.drawFittedText (itemText, r.toNearestInt(), juce::Justification::centredLeft, 1);
}

juce::Font LabLookAndFeel::getPopupMenuFont()
{
    return juce::Font (theme::monoFont (15.0f));
}

//==============================================================================
void LabLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool isHighlighted, bool isDown)
{
    using namespace theme;

    const auto area = button.getLocalBounds().toFloat().reduced (1.0f, 1.0f).withTrimmedBottom (2.0f);
    const auto unit = juce::jmax (0.6f, area.getHeight() / 30.0f);
    const auto style = styleOf (button);
    const auto isOn = button.getToggleState();

    // Touche mécanique : enfoncée quand on appuie (ou verrouillée), plus claire au survol.
    const auto base = isHighlighted && button.isEnabled() ? metalDark.brighter (0.15f) : metalDark;
    drawPlate (g, isDown ? area.translated (0.0f, 1.0f * unit) : area, unit, base, 3.0f * unit);

    if (isDown || (isOn && style.isNotEmpty()))
    {
        g.setColour (seam.withAlpha (0.35f));
        g.fillPath (chamfered (area, 3.0f * unit));
    }

    // Bypass : bandes de signalisation sur la tranche quand il est engagé.
    if (style == "bypass" && isOn)
        drawHazardStripes (g, area.withHeight (3.0f * unit).translated (0.0f, area.getHeight() - 3.0f * unit), unit, alert);

    if (style == "bypass" || style == "latch")
    {
        const auto ledSize = juce::jmin (12.0f * unit, area.getHeight() * 0.42f);
        const auto led = juce::Rectangle<float> (ledSize, ledSize)
                             .withCentre ({ area.getX() + area.getHeight() * 0.5f, area.getCentreY() });
        drawLed (g, led, style == "bypass" ? alert : amber, isOn ? 1.0f : 0.0f, unit);
    }
}

void LabLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool isDown)
{
    using namespace theme;

    const auto style = styleOf (button);
    const auto isOn = button.getToggleState();
    auto area = button.getLocalBounds().toFloat().withTrimmedBottom (2.0f);

    if (isDown)
        area.translate (0.0f, 1.0f);

    if (style == "bypass" || style == "latch")
        area.removeFromLeft (area.getHeight() * 0.85f);

    auto colour = text;

    if (style == "bypass" && isOn)
        colour = alert;
    else if (style == "latch" && isOn)
        colour = amber;

    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), area.reduced (4.0f, 0.0f).toNearestInt(),
                      style.isNotEmpty() ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.8f);
}

juce::Font LabLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (theme::labelFont (juce::jmin (26.0f, static_cast<float> (buttonHeight) * 0.42f)));
}

//==============================================================================
namespace
{
    juce::TextLayout tooltipLayout (const juce::String& text)
    {
        juce::AttributedString string;
        string.setJustification (juce::Justification::centredLeft);
        string.append (text, juce::Font (theme::monoFont (13.0f)), theme::text);

        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (string, 380.0f);
        return layout;
    }
}

void LabLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height)
{
    using namespace theme;

    const auto area = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (anthracite);
    g.fillRect (area);
    g.setColour (hairline);
    g.drawRect (area, 1.0f);
    g.setColour (amber);
    g.fillRect (area.withWidth (3.0f));

    tooltipLayout (tipText).draw (g, area.withTrimmedLeft (13.0f).withTrimmedRight (8.0f));
}

juce::Rectangle<int> LabLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                       juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout (tipText);
    const auto width = static_cast<int> (layout.getWidth() + 24.0f);
    const auto height = static_cast<int> (layout.getHeight() + 14.0f);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (width + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (height + 6) : screenPos.y + 6,
                                 width, height)
        .constrainedWithin (parentArea);
}

void LabLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    const auto colour = isMouseOver || isMouseDragging ? theme::amber : theme::metalMid;

    for (int i = 1; i <= 3; ++i)
    {
        const auto offset = static_cast<float> (i) * static_cast<float> (juce::jmin (w, h)) / 4.0f;
        g.setColour (theme::seam);
        g.drawLine (static_cast<float> (w) - offset, static_cast<float> (h), static_cast<float> (w), static_cast<float> (h) - offset, 2.5f);
        g.setColour (colour);
        g.drawLine (static_cast<float> (w) - offset, static_cast<float> (h) - 1.0f, static_cast<float> (w) - 1.0f, static_cast<float> (h) - offset, 1.0f);
    }
}

void LabLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                    bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    g.setColour (theme::seam);
    g.fillRect (x, y, width, height);

    const auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x + 2, thumbStartPosition, width - 4, thumbSize)
                                           : juce::Rectangle<int> (thumbStartPosition, y + 2, thumbSize, height - 4);
    g.setColour (isMouseOver || isMouseDown ? theme::amber.withAlpha (0.7f) : theme::metalMid);
    g.fillRect (thumb);
}

} // namespace subdamp::gui
