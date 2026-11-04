#pragma once

#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/ProcessSpec.h"

#include <span>

namespace subdamp::dsp
{

/**
    Bypass sans clic : fondu linéaire entre le signal sec (non traité) et le signal traité.

    Le fondu est linéaire (gain constant) et non à puissance constante : les deux signaux
    sont fortement corrélés, une loi à puissance constante produirait une bosse de volume
    au milieu du fondu.

    Quand le bypass est complètement engagé, le processeur hôte peut sauter tout le
    traitement pour économiser du CPU (voir isFullyBypassed()).
*/
class BypassCrossfade
{
public:
    void prepare (const ProcessSpec& spec, double fadeSeconds = 0.01) noexcept;

    /** Termine immédiatement un fondu en cours. */
    void reset() noexcept;

    void setBypassed (bool shouldBeBypassed) noexcept;

    /** Vrai quand le bypass est engagé et qu'aucun fondu n'est en cours. */
    bool isFullyBypassed() const noexcept;

    /**
        Mélange sur place : processed = processed × (1 − b) + dry × b, b allant de 0 (actif) à 1 (bypassé).
        Les deux blocs doivent avoir la même taille.
    */
    void process (std::span<const float> dry, std::span<float> processed) noexcept;

private:
    LinearSmoother bypassAmount;
};

} // namespace subdamp::dsp
