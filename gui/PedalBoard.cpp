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
        setTooltip (juce::String::fromUTF8 ("Clic : réglages — glisser : changer de place — témoin : marche / arrêt"));
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
        using namespace theme;

        const auto isOn = led.getToggleState();
        auto body = getLocalBounds().toFloat().reduced (3.0f * scale);
        const auto chamfer = 5.0f * scale;

        // Module soulevé : ombre portée plus large
        if (lifted)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillPath (chamfered (body.translated (0.0f, 6.0f * scale).expanded (2.0f * scale), chamfer));
        }

        drawPlate (g, body, scale, isOn ? plateRaised : plate, chamfer);

        // Bande de la catégorie de latence, en haut
        const auto tierColour = colourFor (info.tier);
        g.setColour (isOn ? tierColour : tierColour.withAlpha (0.3f));
        g.fillRect (body.reduced (6.0f * scale, 0.0f).withHeight (3.0f * scale).translated (0.0f, 5.0f * scale));

        // Vis
        for (const auto x : { body.getX() + 8.0f * scale, body.getRight() - 8.0f * scale })
            for (const auto y : { body.getY() + 16.0f * scale, body.getBottom() - 9.0f * scale })
                drawScrew (g, { x, y }, 2.4f * scale, x * 0.05f);

        // Référence, nom, MIX
        g.setColour (textFaint);
        g.setFont (monoFont (8.5f * scale));
        g.drawText (info.code, body.withTrimmedTop (12.0f * scale).withHeight (10.0f * scale), juce::Justification::centred, false);

        g.setColour (isOn ? text : textDim);
        g.setFont (titleFont (14.0f * scale));
        g.drawFittedText (info.pedalName, body.withTrimmedTop (24.0f * scale).withHeight (20.0f * scale).reduced (4.0f * scale, 0.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.8f);

        if (shownMix < 0.995f)
        {
            g.setColour (isOn ? amber : textFaint);
            g.setFont (monoFont (10.0f * scale));
            g.drawText ("MIX " + juce::String (juce::roundToInt (shownMix * 100.0f)), body.withTrimmedTop (45.0f * scale).withHeight (13.0f * scale),
                        juce::Justification::centred, false);
        }

        // Sélection : équerres de repérage aux quatre coins
        if (selected || lifted)
        {
            const auto frame = body.expanded (2.0f * scale);
            const auto arm = 10.0f * scale;
            juce::Path marks;

            for (const auto corner : { frame.getTopLeft(), frame.getTopRight(), frame.getBottomLeft(), frame.getBottomRight() })
            {
                const auto dx = corner.x < frame.getCentreX() ? arm : -arm;
                const auto dy = corner.y < frame.getCentreY() ? arm : -arm;
                marks.startNewSubPath (corner.x + dx, corner.y);
                marks.lineTo (corner);
                marks.lineTo (corner.x, corner.y + dy);
            }

            g.setColour (amber);
            g.strokePath (marks, juce::PathStrokeType (2.0f * scale, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
        }
    }

    void resized() override
    {
        const auto size = juce::roundToInt (30.0f * scale);
        led.setBounds (getLocalBounds().withSizeKeepingCentre (size, size).withY (getHeight() - size - juce::roundToInt (22.0f * scale)));
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
    using namespace theme;

    const auto first = slotBounds (0);
    const auto last = slotBounds (numModules - 1);
    const auto cableY = static_cast<float> (first.getY()) + 74.0f * scale;

    // Câble de liaison, de l'entrée à la sortie, et sens du signal
    const auto drawCable = [&] (float x0, float x1)
    {
        g.setColour (seam);
        g.drawLine (x0, cableY, x1, cableY, 6.0f * scale);
        g.setColour (metalDark);
        g.drawLine (x0, cableY - 0.5f * scale, x1, cableY - 0.5f * scale, 3.5f * scale);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (x0, cableY - 1.5f * scale, x1, cableY - 1.5f * scale, 1.0f * scale);
    };

    drawCable (0.0f, static_cast<float> (getWidth()));

    for (std::size_t i = 0; i <= numModules; ++i)
    {
        // Jacks de part et d'autre de chaque module
        if (i < numModules)
        {
            const auto slot = slotBounds (i).toFloat();

            for (const auto x : { slot.getX() - 3.0f * scale, slot.getRight() - 3.0f * scale })
            {
                const auto jack = juce::Rectangle<float> (x, cableY - 5.0f * scale, 6.0f * scale, 10.0f * scale);
                g.setColour (metalMid);
                g.fillRect (jack);
                g.setColour (seam);
                g.drawRect (jack, 1.0f * scale);
            }
        }

        if (i > 0 && i < numModules)
        {
            const auto x = (static_cast<float> (slotBounds (i - 1).getRight()) + static_cast<float> (slotBounds (i).getX())) * 0.5f;
            juce::Path arrow;
            arrow.addTriangle (x - 2.5f * scale, cableY - 8.5f * scale, x - 2.5f * scale, cableY - 15.5f * scale, x + 3.0f * scale, cableY - 12.0f * scale);
            g.setColour (amber.withAlpha (0.85f));
            g.fillPath (arrow);
        }
    }

    g.setColour (textDim);
    g.setFont (labelFont (11.0f * scale));
    g.drawText ("IN", juce::Rectangle<float> (0.0f, cableY - 22.0f * scale, static_cast<float> (first.getX()) - 4.0f * scale, 12.0f * scale),
                juce::Justification::centredRight, false);
    g.drawText ("OUT", juce::Rectangle<float> (static_cast<float> (last.getRight()) + 4.0f * scale, cableY - 22.0f * scale, 40.0f * scale, 12.0f * scale),
                juce::Justification::centredLeft, false);

    // Case de destination pendant un glisser
    if (dragged.has_value())
    {
        const auto target = slotBounds (dropIndex).toFloat().reduced (3.0f * scale);
        g.setColour (seam);
        g.fillPath (chamfered (target, 5.0f * scale));

        juce::Path dashed;
        const float dashes[] { 6.0f * scale, 4.0f * scale };
        juce::PathStrokeType (1.5f * scale).createDashedStroke (dashed, chamfered (target, 5.0f * scale), dashes, 2);
        g.setColour (amber.withAlpha (0.8f));
        g.fillPath (dashed);
    }

    // Aide, sous la rangée
    g.setColour (textFaint);
    g.setFont (monoFont (10.5f * scale));
    g.drawText (juce::String::fromUTF8 ("CLIC : RÉGLAGES   ·   GLISSER : CHANGER L'ORDRE DE LA CHAÎNE   ·   TÉMOIN : MARCHE / ARRÊT"),
                juce::Rectangle<int> (first.getX(), last.getBottom() + juce::roundToInt (6.0f * scale),
                                      last.getRight() - first.getX(), juce::roundToInt (static_cast<float> (hintHeight) * scale)),
                juce::Justification::centred);
}

void PedalBoard::resized()
{
    layoutPedals();
}

} // namespace subdamp::gui
