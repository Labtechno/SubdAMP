#pragma once

#include "gui/ModuleCard.h"
#include "plugin/Modules.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <optional>

namespace subdamp::gui
{

/**
    Pédalier : les 11 modules en une rangée, dans l'ordre réel de la chaîne en série.

      - clic sur une pédale : ses réglages s'ouvrent en grand ;
      - clic sur sa LED : marche / arrêt ;
      - clic maintenu et glissé : la pédale se déplace, les autres s'écartent pour montrer où
        elle va tomber ; au relâchement, la chaîne prend ce nouvel ordre.

    Chaque module est une plaque vissée : bande de couleur de sa catégorie de latence (vert
    LIVE, orange JOUABLE, bleu acier TEXTURE), référence, nom, MIX quand il n'est pas à 100 %,
    témoin de marche. Un câble les relie dans l'ordre du signal.
*/
class PedalBoard final : public juce::Component
{
public:
    PedalBoard (juce::AudioProcessorValueTreeState& parameters,
                std::function<void (ModuleId)> onSelect,
                std::function<void (ModuleId, int)> onMove);
    ~PedalBoard() override;

    void setOrder (const ChainOrder& newOrder);
    void setSelected (ModuleId module);
    void setScale (float newScale);

    /** Redessine les pédales dont le MIX a changé (appelé par le minuteur de l'interface). */
    void refreshMixes();

    bool isDragging() const noexcept { return dragged.has_value(); }

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int pedalWidth = 80;
    static constexpr int pedalHeight = 130;
    static constexpr int gap = 12;
    static constexpr int hintHeight = 20;

private:
    class Pedal;

    Pedal& pedalFor (ModuleId module);
    juce::Rectangle<int> slotBounds (std::size_t index) const;
    std::size_t slotIndexAt (float x) const;
    void layoutPedals();

    void startDrag (Pedal& pedal);
    void dragTo (float x);
    void endDrag();

    std::function<void (ModuleId)> onSelect;
    std::function<void (ModuleId, int)> onMove;

    std::array<std::unique_ptr<Pedal>, numModules> pedals;
    ChainOrder order = defaultChainOrder;
    ModuleId selected = ModuleId::amp;
    float scale = 1.0f;

    // Glisser-déposer en cours
    std::optional<ModuleId> dragged;
    std::size_t dropIndex = 0;
    float dragCentreX = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalBoard)
};

} // namespace subdamp::gui
