#include "subdamp/dsp/Whammy.h"

#include "subdamp/dsp/Interpolation.h"

#include <algorithm>
#include <cmath>

namespace subdamp::dsp
{

void Whammy::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    shifter.prepare (sampleRate);
    onset.prepare (sampleRate);
    interval.prepare (sampleRate, 0.03);
    mix.prepare (sampleRate, 0.02);

    setGlideMilliseconds (glideMs);
    reset();
}

void Whammy::reset() noexcept
{
    shifter.reset();
    onset.reset();
    interval.setCurrentAndTarget (interval.getTarget());
    mix.setCurrentAndTarget (mix.getTarget());
    glideProgress = 1.0f;
}

void Whammy::setInterval (float semitones) noexcept
{
    interval.setTarget (std::clamp (semitones, -24.0f, 24.0f));
}

void Whammy::setMode (Mode newMode) noexcept
{
    if (newMode == mode)
        return;

    mode = newMode;
    glideProgress = 1.0f; // en attendant la prochaine attaque : position de fin de glissement
}

void Whammy::setGlideMilliseconds (float milliseconds) noexcept
{
    glideMs = std::max (1.0f, milliseconds);
    glideStep = static_cast<float> (1000.0 / (static_cast<double> (glideMs) * sampleRate));
}

void Whammy::setMix (float amount) noexcept
{
    mix.setTarget (std::clamp (amount, 0.0f, 1.0f));
}

void Whammy::process (std::span<float> block) noexcept
{
    float cachedSemitones = 1.0e9f;
    float ratio = 1.0f;

    for (auto& sample : block)
    {
        const auto dry = sample;
        const auto target = interval.getNext();

        // Le détecteur tourne en permanence, pour être prêt quand on passe en mode « attaque ».
        if (onset.process (dry) && mode != Mode::fixed)
            glideProgress = 0.0f;

        if (mode == Mode::fixed)
        {
            currentSemitones = target;
        }
        else
        {
            glideProgress = std::min (1.0f, glideProgress + glideStep);

            const auto start = mode == Mode::attackToInterval ? 0.0f : target;
            const auto end = mode == Mode::attackToInterval ? target : 0.0f;
            currentSemitones = start + (end - start) * glideProgress;
        }

        // exp2 seulement quand la hauteur change (la plupart du temps, elle est stable).
        if (currentSemitones != cachedSemitones)
        {
            cachedSemitones = currentSemitones;
            ratio = semitonesToRatio (currentSemitones);
        }

        // Le shifter tourne toujours : sa mémoire reste à jour pour repartir sans trou.
        const auto shifted = shifter.process (dry, ratio);

        // Près de l'unisson, le transposé ressemble au sec : il s'efface (pas de peigne) et le
        // mélange est linéaire (pas de bosse). Ailleurs, puissance constante : à 50 %, le niveau
        // reste celui du sec.
        const auto away = std::min (1.0f, std::fabs (currentSemitones) / unisonFadeSemitones);
        const auto amount = mix.getNext() * away;
        const auto angle = amount * 1.57079632679f;
        const auto dryGain = (1.0f - away) * (1.0f - amount) + away * std::cos (angle);
        const auto wetGain = (1.0f - away) * amount + away * std::sin (angle);

        sample = dry * dryGain + shifted * wetGain;
    }
}

} // namespace subdamp::dsp
