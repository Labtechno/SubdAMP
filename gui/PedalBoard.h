#pragma once

#include "gui/ModuleCard.h"
#include "plugin/Modules.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <vector>

namespace subdamp::gui
{

/**
    Pédalier : une pédale par module. En haut, la chaîne principale dans son ordre réel ; en
    dessous, les textures, branchées en parallèle. Clic sur une pédale : ses réglages s'ouvrent
    en grand ; clic sur sa LED : marche / arrêt. La couleur du haut de la pédale indique la
    catégorie de latence (vert LIVE, orange JOUABLE, violet TEXTURE).
*/
class PedalBoard final : public juce::Component
{
public:
    PedalBoard (juce::AudioProcessorValueTreeState& parameters, std::function<void (ModuleId)> onSelect);
    ~PedalBoard() override;

    void setOrder (const std::vector<ModuleId>& newOrder);
    void setSelected (ModuleId module);
    void setScale (float newScale);

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int pedalWidth = 100;
    static constexpr int pedalHeight = 100;
    static constexpr int gap = 24;
    static constexpr int rowGap = 22;

private:
    class Pedal;

    Pedal& pedalFor (ModuleId module);

    std::function<void (ModuleId)> onSelect;
    std::array<std::unique_ptr<Pedal>, moduleInfos.size()> pedals;
    std::vector<ModuleId> order;
    ModuleId selected = ModuleId::amp;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalBoard)
};

} // namespace subdamp::gui
