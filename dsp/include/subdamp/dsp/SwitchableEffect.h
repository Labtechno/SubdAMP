#pragma once

#include "subdamp/dsp/BypassCrossfade.h"
#include "subdamp/dsp/MonoEffect.h"

#include <vector>

namespace subdamp::dsp
{

/**
    Met un module en marche / à l'arrêt sans clic (fondu de 10 ms).

    À l'arrêt complet, le module n'est pas calculé du tout (aucun coût CPU). À la remise en
    marche, il repart d'un état remis à zéro plutôt que d'un état périmé.
*/
class SwitchableEffect
{
public:
    explicit SwitchableEffect (MonoEffect& effectToSwitch) noexcept : effect (effectToSwitch) {}

    void prepare (const ProcessSpec& spec);
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    bool isFullyOff() const noexcept { return fade.isFullyBypassed(); }

    void process (std::span<float> block) noexcept;

private:
    MonoEffect& effect;
    BypassCrossfade fade;
    std::vector<float> dry;
    bool wasFullyOff = true;
};

} // namespace subdamp::dsp
