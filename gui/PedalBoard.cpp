#include "gui/PedalBoard.h"

#include "gui/Theme.h"

namespace subdamp::gui
{

//==============================================================================
class PedalBoard::Pedal final : public juce::Component,
                                 public juce::SettableTooltipClient
{
public:
    Pedal (PedalBoard& ownerBoard, const ModuleInfo& moduleInfo, juce::AudioProcessorValueTreeState& parameters)
        : owner (ownerBoard), info (moduleInfo)
    {
        setTooltip (juce::String::fromUTF8 ("Clic : ouvrir les réglages — LED : marche / arrêt"));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        led.setTooltip (juce::String::fromUTF8 ("Marche / arrêt"));
        led.onStateChange = [this] { repaint(); };
        addAndMakeVisible (led);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, info.enabledParameter, led);
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

    void setScale (float newScale) { scale = newScale; resized(); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto isOn = led.getToggleState();
        auto body = getLocalBounds().toFloat().reduced (2.0f * scale);
        const auto corner = 9.0f * scale;

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

        g.setColour (selected ? theme::accent : theme::panelOutline.brighter (isOn ? 0.25f : 0.0f));
        g.drawRoundedRectangle (body, corner, (selected ? 2.0f : 1.0f) * scale);

        // Nom
        g.setColour (isOn ? theme::text : theme::textDim);
        g.setFont (juce::FontOptions (14.0f * scale, juce::Font::bold));
        g.drawText (info.pedalName, body.withTrimmedTop (12.0f * scale).withHeight (26.0f * scale), juce::Justification::centred);

        // Vis de façade, pour l'esprit « boîtier »
        g.setColour (theme::background.withAlpha (0.7f));

        for (const auto x : { body.getX() + 10.0f * scale, body.getRight() - 10.0f * scale })
            g.fillEllipse (juce::Rectangle<float> (4.0f * scale, 4.0f * scale).withCentre ({ x, body.getBottom() - 10.0f * scale }));
    }

    void resized() override
    {
        const auto size = juce::roundToInt (28.0f * scale);
        led.setBounds (getLocalBounds().withSizeKeepingCentre (size, size).withY (getHeight() - size - juce::roundToInt (18.0f * scale)));
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (event.mouseWasClicked())
            owner.onSelect (info.id);
    }

private:
    PedalBoard& owner;
    const ModuleInfo& info;
    PowerButton led;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    bool selected = false;
    float scale = 1.0f;
};

//==============================================================================
PedalBoard::PedalBoard (juce::AudioProcessorValueTreeState& parameters, std::function<void (ModuleId)> selectCallback)
    : onSelect (std::move (selectCallback))
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
    return *pedals[static_cast<std::size_t> (module)];
}

void PedalBoard::setOrder (const std::vector<ModuleId>& newOrder)
{
    if (newOrder != order)
    {
        order = newOrder;
        resized();
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

void PedalBoard::paint (juce::Graphics& g)
{
    if (order.empty())
        return;

    const auto drawChevron = [&] (float x, float y)
    {
        juce::Path chevron;
        chevron.startNewSubPath (x - 3.0f * scale, y - 5.0f * scale);
        chevron.lineTo (x + 3.0f * scale, y);
        chevron.lineTo (x - 3.0f * scale, y + 5.0f * scale);
        g.setColour (theme::accent.withAlpha (0.8f));
        g.strokePath (chevron, juce::PathStrokeType (2.0f * scale, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    };

    // Rangée principale : câble d'un bout à l'autre, chevron entre deux pédales.
    const auto mainY = static_cast<float> (pedalFor (order.front()).getBounds().getCentreY());
    const auto cable = theme::panelOutline.brighter (0.2f);
    g.setColour (cable);
    g.drawLine (0.0f, mainY, static_cast<float> (getWidth()), mainY, 2.0f * scale);

    for (std::size_t i = 1; i < order.size(); ++i)
        drawChevron ((static_cast<float> (pedalFor (order[i - 1]).getRight()) + static_cast<float> (pedalFor (order[i]).getX())) * 0.5f, mainY);

    // Rangée des textures : branchée en parallèle, du début à la fin de la chaîne.
    const auto& first = pedalFor (textureOrder.front());
    const auto& last = pedalFor (textureOrder.back());
    const auto textureY = static_cast<float> (first.getBounds().getCentreY());
    const auto branchLeft = static_cast<float> (first.getX()) - 0.5f * gap * scale;
    const auto branchRight = static_cast<float> (last.getRight()) + 0.5f * gap * scale;

    g.setColour (theme::latencyTexture.withAlpha (0.5f));
    g.drawLine (branchLeft, mainY, branchLeft, textureY, 2.0f * scale);
    g.drawLine (branchRight, mainY, branchRight, textureY, 2.0f * scale);
    g.drawLine (branchLeft, textureY, branchRight, textureY, 2.0f * scale);

    g.setColour (theme::latencyTexture);
    g.setFont (juce::FontOptions (12.0f * scale, juce::Font::bold));
    g.drawFittedText (juce::String::fromUTF8 ("TEXTURES\nen parallèle"),
                      juce::Rectangle<float> (0.0f, textureY - 20.0f * scale, branchLeft - 10.0f * scale, 40.0f * scale).toNearestInt(),
                      juce::Justification::centredRight, 2);
}

void PedalBoard::resized()
{
    const auto width = juce::roundToInt (pedalWidth * scale);
    const auto height = juce::roundToInt (pedalHeight * scale);
    const auto spacing = juce::roundToInt (gap * scale);
    const auto rowSpacing = juce::roundToInt (rowGap * scale);

    const auto layoutRow = [&] (const auto& modules, int y)
    {
        const auto count = static_cast<int> (modules.size());
        const auto total = count * width + juce::jmax (0, count - 1) * spacing;
        auto x = juce::jmax (0, (getWidth() - total) / 2);

        for (const auto module : modules)
        {
            pedalFor (module).setBounds (x, y, width, height);
            x += width + spacing;
        }
    };

    const auto top = (getHeight() - 2 * height - rowSpacing) / 2;
    layoutRow (order, top);
    layoutRow (textureOrder, top + height + rowSpacing);
}

} // namespace subdamp::gui
