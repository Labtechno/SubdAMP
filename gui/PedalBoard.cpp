#include "gui/PedalBoard.h"

#include "gui/Theme.h"

#include <algorithm>
#include <limits>

namespace subdamp::gui
{

//==============================================================================
class PedalBoard::Pedal final : public juce::Component,
                                 public juce::SettableTooltipClient
{
public:
    Pedal (PedalBoard& ownerBoard, const ModuleInfo& moduleInfo, juce::AudioProcessorValueTreeState& parameters)
        : owner (ownerBoard),
          info (moduleInfo),
          mixValue (moduleInfo.mixParameter != nullptr ? parameters.getRawParameterValue (moduleInfo.mixParameter) : nullptr)
    {
        setTooltip (juce::String::fromUTF8 ("Clic : réglages — glisser : changer de place — LED : marche / arrêt"));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        led.setTooltip (juce::String::fromUTF8 ("Marche / arrêt"));
        led.onStateChange = [this] { repaint(); };
        addAndMakeVisible (led);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, info.enabledParameter, led);

        shownMix = currentMix();
    }

    ModuleId id() const noexcept { return info.id; }

    void setSelected (bool shouldBeSelected)
    {
        if (selected != shouldBeSelected)
        {
            selected = shouldBeSelected;
            repaint();
        }
    }

    void setLifted (bool shouldBeLifted)
    {
        lifted = shouldBeLifted;
        setMouseCursor (lifted ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }

    void setScale (float newScale) { scale = newScale; resized(); repaint(); }

    void refreshMix()
    {
        if (const auto mix = currentMix(); std::abs (mix - shownMix) > 0.001f)
        {
            shownMix = mix;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto isOn = led.getToggleState();
        auto body = getLocalBounds().toFloat().reduced (2.0f * scale);
        const auto corner = 9.0f * scale;

        // Pédale soulevée : ombre portée.
        if (lifted)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (body.translated (0.0f, 5.0f * scale), corner);
        }

        if (selected)
        {
            g.setColour (theme::accent.withAlpha (0.18f));
            g.fillRoundedRectangle (body.expanded (2.0f * scale), corner + 2.0f * scale);
        }

        g.setColour (isOn ? theme::panelRaised : theme::panel);
        g.fillRoundedRectangle (body, corner);

        // Bande de couleur de la catégorie de latence
        const auto tierColour = theme::colourFor (info.tier);
        g.setColour (isOn ? tierColour : tierColour.withAlpha (0.35f));
        g.fillRoundedRectangle (body.withHeight (6.0f * scale), 3.0f * scale);

        g.setColour (selected || lifted ? theme::accent : theme::panelOutline.brighter (isOn ? 0.25f : 0.0f));
        g.drawRoundedRectangle (body, corner, (selected || lifted ? 2.0f : 1.0f) * scale);

        // Nom
        g.setColour (isOn ? theme::text : theme::textDim);
        g.setFont (juce::FontOptions (12.5f * scale, juce::Font::bold));
        g.drawFittedText (info.pedalName, body.withTrimmedTop (12.0f * scale).withHeight (22.0f * scale).toNearestInt(),
                          juce::Justification::centred, 1, 0.8f);

        // MIX, seulement quand il n'est pas à fond : on repère d'un coup d'œil les effets dosés.
        if (shownMix < 0.995f)
        {
            g.setColour ((isOn ? theme::accent : theme::textDim).withAlpha (isOn ? 0.9f : 0.6f));
            g.setFont (juce::FontOptions (10.5f * scale, juce::Font::bold));
            g.drawText ("MIX " + juce::String (juce::roundToInt (shownMix * 100.0f)) + " %",
                        body.withTrimmedTop (33.0f * scale).withHeight (14.0f * scale), juce::Justification::centred);
        }

        // Vis de façade, pour l'esprit « boîtier »
        g.setColour (theme::background.withAlpha (0.7f));

        for (const auto x : { body.getX() + 9.0f * scale, body.getRight() - 9.0f * scale })
            g.fillEllipse (juce::Rectangle<float> (4.0f * scale, 4.0f * scale).withCentre ({ x, body.getBottom() - 9.0f * scale }));
    }

    void resized() override
    {
        const auto size = juce::roundToInt (26.0f * scale);
        led.setBounds (getLocalBounds().withSizeKeepingCentre (size, size).withY (getHeight() - size - juce::roundToInt (18.0f * scale)));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        // Petit seuil : un clic un peu tremblé reste un clic.
        if (! dragging && event.getDistanceFromDragStart() > juce::roundToInt (6.0f * scale))
        {
            dragging = true;
            owner.startDrag (*this);
        }

        if (dragging)
            owner.dragTo (event.getEventRelativeTo (&owner).position.x);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (dragging)
        {
            dragging = false;
            owner.endDrag();
        }
        else if (event.mouseWasClicked())
        {
            owner.onSelect (info.id);
        }
    }

private:
    float currentMix() const noexcept
    {
        return mixValue != nullptr ? mixValue->load (std::memory_order_relaxed) * 0.01f : 1.0f;
    }

    PedalBoard& owner;
    const ModuleInfo& info;
    std::atomic<float>* mixValue;
    PowerButton led;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    bool selected = false, lifted = false, dragging = false;
    float scale = 1.0f;
    float shownMix = 1.0f;
};

//==============================================================================
PedalBoard::PedalBoard (juce::AudioProcessorValueTreeState& parameters,
                        std::function<void (ModuleId)> selectCallback,
                        std::function<void (ModuleId, int)> moveCallback)
    : onSelect (std::move (selectCallback)),
      onMove (std::move (moveCallback))
{
    for (std::size_t i = 0; i < moduleInfos.size(); ++i)
    {
        pedals[i] = std::make_unique<Pedal> (*this, moduleInfos[i], parameters);
        addAndMakeVisible (*pedals[i]);
    }
}

PedalBoard::~PedalBoard() = default;

PedalBoard::Pedal& PedalBoard::pedalFor (ModuleId module)
{
    return *pedals[indexOf (module)];
}

void PedalBoard::setOrder (const ChainOrder& newOrder)
{
    // Pendant un glisser, l'ordre affiché est celui de l'aperçu : on n'y touche pas.
    if (newOrder != order && ! dragged.has_value())
    {
        order = newOrder;
        layoutPedals();
        repaint();
    }
}

void PedalBoard::setSelected (ModuleId module)
{
    selected = module;

    for (auto& pedal : pedals)
        pedal->setSelected (pedal->id() == module);
}

void PedalBoard::setScale (float newScale)
{
    scale = newScale;

    for (auto& pedal : pedals)
        pedal->setScale (scale);

    resized();
}

void PedalBoard::refreshMixes()
{
    for (auto& pedal : pedals)
        pedal->refreshMix();
}

//==============================================================================
juce::Rectangle<int> PedalBoard::slotBounds (std::size_t index) const
{
    const auto width = static_cast<float> (pedalWidth) * scale;
    const auto height = static_cast<float> (pedalHeight) * scale;
    const auto spacing = static_cast<float> (gap) * scale;
    const auto total = static_cast<float> (numModules) * width + static_cast<float> (numModules - 1) * spacing;

    const auto left = juce::jmax (0.0f, (static_cast<float> (getWidth()) - total) * 0.5f);
    const auto top = (static_cast<float> (getHeight()) - height - static_cast<float> (hintHeight) * scale) * 0.5f;

    return juce::Rectangle<float> (left + static_cast<float> (index) * (width + spacing), top, width, height).toNearestInt();
}

std::size_t PedalBoard::slotIndexAt (float x) const
{
    std::size_t nearest = 0;
    auto bestDistance = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < numModules; ++i)
    {
        const auto distance = std::abs (static_cast<float> (slotBounds (i).getCentreX()) - x);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            nearest = i;
        }
    }

    return nearest;
}

void PedalBoard::layoutPedals()
{
    // Pendant un glisser : les autres pédales prennent leur place dans l'ordre d'arrivée,
    // la case de destination reste libre, la pédale tenue suit la souris.
    const auto shown = dragged.has_value() ? withModuleMoved (order, *dragged, dropIndex) : order;

    for (std::size_t i = 0; i < numModules; ++i)
    {
        if (dragged.has_value() && shown[i] == *dragged)
            continue;

        pedalFor (shown[i]).setBounds (slotBounds (i));
    }

    if (dragged.has_value())
    {
        auto& pedal = pedalFor (*dragged);
        const auto bounds = slotBounds (dropIndex);
        pedal.setBounds (bounds.withCentre ({ juce::roundToInt (dragCentreX), bounds.getCentreY() - juce::roundToInt (6.0f * scale) }));
        pedal.toFront (false);
    }
}

void PedalBoard::startDrag (Pedal& pedal)
{
    dragged = pedal.id();
    dropIndex = static_cast<std::size_t> (std::find (order.begin(), order.end(), pedal.id()) - order.begin());
    dragCentreX = static_cast<float> (pedal.getBounds().getCentreX());
    pedal.setLifted (true);
    layoutPedals();
    repaint();
}

void PedalBoard::dragTo (float x)
{
    if (! dragged.has_value())
        return;

    const auto half = static_cast<float> (pedalWidth) * scale * 0.5f;
    dragCentreX = juce::jlimit (half, static_cast<float> (getWidth()) - half, x);
    dropIndex = slotIndexAt (dragCentreX);
    layoutPedals();
    repaint();
}

void PedalBoard::endDrag()
{
    if (! dragged.has_value())
        return;

    const auto module = *dragged;
    const auto from = static_cast<std::size_t> (std::find (order.begin(), order.end(), module) - order.begin());

    order = withModuleMoved (order, module, dropIndex);
    dragged.reset();
    pedalFor (module).setLifted (false);
    layoutPedals();
    repaint();

    if (dropIndex != from)
        onMove (module, static_cast<int> (dropIndex));
}

//==============================================================================
void PedalBoard::paint (juce::Graphics& g)
{
    const auto first = slotBounds (0);
    const auto last = slotBounds (numModules - 1);
    const auto cableY = static_cast<float> (first.getCentreY());

    // Câble d'un bout à l'autre, chevron entre deux pédales : le sens du signal.
    g.setColour (theme::panelOutline.brighter (0.2f));
    g.drawLine (0.0f, cableY, static_cast<float> (getWidth()), cableY, 2.0f * scale);

    for (std::size_t i = 1; i < numModules; ++i)
    {
        const auto x = (static_cast<float> (slotBounds (i - 1).getRight()) + static_cast<float> (slotBounds (i).getX())) * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (x - 2.5f * scale, cableY - 4.5f * scale);
        chevron.lineTo (x + 2.5f * scale, cableY);
        chevron.lineTo (x - 2.5f * scale, cableY + 4.5f * scale);
        g.setColour (theme::accent.withAlpha (0.8f));
        g.strokePath (chevron, juce::PathStrokeType (2.0f * scale, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    // Case de destination pendant un glisser
    if (dragged.has_value())
    {
        const auto target = slotBounds (dropIndex).toFloat().reduced (2.0f * scale);
        g.setColour (theme::background);
        g.fillRoundedRectangle (target, 9.0f * scale);

        juce::Path outline;
        outline.addRoundedRectangle (target, 9.0f * scale);
        juce::Path dashed;
        const float dashes[] { 6.0f * scale, 4.0f * scale };
        juce::PathStrokeType (1.5f * scale).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (theme::accent.withAlpha (0.8f));
        g.fillPath (dashed);
    }

    // Aide, sous la rangée
    g.setColour (theme::textDim.withAlpha (0.8f));
    g.setFont (juce::FontOptions (11.5f * scale));
    g.drawText (juce::String::fromUTF8 ("Clic : réglages   ·   Glisser une pédale : changer l'ordre de la chaîne   ·   LED : marche / arrêt"),
                juce::Rectangle<int> (first.getX(), last.getBottom() + juce::roundToInt (6.0f * scale),
                                      last.getRight() - first.getX(), juce::roundToInt (static_cast<float> (hintHeight) * scale)),
                juce::Justification::centred);
}

void PedalBoard::resized()
{
    layoutPedals();
}

} // namespace subdamp::gui
