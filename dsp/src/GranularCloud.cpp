#include "subdamp/dsp/GranularCloud.h"

#include "subdamp/dsp/Interpolation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

void GranularCloud::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    history.assign (std::bit_ceil (static_cast<std::size_t> (8.0 * sampleRate) + 16), 0.0f);
    historyMask = history.size() - 1;

    for (int i = 0; i <= hannSize; ++i)
        hann[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / hannSize));

    reset();
}

void GranularCloud::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    written = 0;

    for (auto& grain : grains)
        grain.active = false;

    samplesToNextGrain = 0.0;
    freeRunPhase = 0.0;
    lastSlot = -1;
    repeatsLeft = 0;
    started = 0;
    captured = 0;
    lastTrigger = -1;
}

float GranularCloud::random() noexcept
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<float> (randomState) / 4294967296.0f;
}

GranularCloud::Grain* GranularCloud::freeGrain() noexcept
{
    for (auto& grain : grains)
        if (! grain.active)
            return &grain;

    return nullptr; // tous occupés : ce grain est simplement sauté
}

void GranularCloud::setPan (Grain& grain, float pan) const noexcept
{
    const auto angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * std::numbers::pi_v<float>;
    grain.gainLeft = std::cos (angle);
    grain.gainRight = std::sin (angle);
}

float GranularCloud::window (const Grain& grain) const noexcept
{
    if (grain.fade > 0)
    {
        const auto edge = std::min (grain.age, grain.length - 1 - grain.age);
        return std::min (1.0f, static_cast<float> (edge) / static_cast<float> (grain.fade));
    }

    const auto position = static_cast<float> (grain.age) / static_cast<float> (grain.length) * hannSize;
    const auto index = std::min (static_cast<int> (position), hannSize - 1);
    const auto fraction = position - static_cast<float> (index);
    return hann[static_cast<std::size_t> (index)] + fraction * (hann[static_cast<std::size_t> (index) + 1] - hann[static_cast<std::size_t> (index)]);
}

void GranularCloud::spawnCloudGrain() noexcept
{
    auto* grain = freeGrain();

    if (grain == nullptr)
        return;

    const auto length = std::max (64, static_cast<int> (grainMs * 0.001f * static_cast<float> (sampleRate)
                                                         * (1.0f + spread * (random() - 0.5f))));

    // Hauteur : base + dispersion quantifiée au demi-ton (jusqu'à ± 1 octave à 100 %).
    const auto jitter = std::round ((random() * 2.0f - 1.0f) * spread * 12.0f);
    const auto ratio = static_cast<double> (semitonesToRatio (pitch + jitter));
    const auto reversed = random() < reverseProbability;

    // Position : dans le passé, jusqu'à 1 s en arrière selon la dispersion. Un grain lu à
    // l'endroit et transposé vers l'aigu doit partir assez loin pour ne pas rattraper le présent.
    const auto scatter = static_cast<double> (random() * spread) * sampleRate;
    const auto newest = static_cast<double> (written - 1);

    grain->length = length;
    grain->age = 0;
    grain->fade = 0;
    grain->step = reversed ? -ratio : ratio;
    grain->position = reversed ? newest - 2.0 - scatter
                               : newest - 2.0 - scatter - ratio * length;
    setPan (*grain, (random() * 2.0f - 1.0f) * spread);
    grain->active = true;
    ++started;
}

void GranularCloud::triggerStutter() noexcept
{
    const auto divisionSamples = std::max (64, static_cast<int> (divisionBeats * 60.0 / tempo * sampleRate));
    lastTrigger = written - 1; // index de l'échantillon en cours

    // Nouvelle tranche : la dernière division jouée. Elle est prise assez longue pour qu'une
    // lecture transposée vers l'aigu (dispersion comprise) ne rattrape jamais le présent.
    const auto maxRatio = std::max (1.0, static_cast<double> (semitonesToRatio (pitch + std::round (spread * 12.0f))));

    if (repeatsLeft <= 0)
    {
        sliceLength = divisionSamples;
        sliceStart = written - 2 - static_cast<std::int64_t> (std::ceil (maxRatio * divisionSamples));
        repeatsLeft = std::max (1, repeats);
        ++captured;
    }

    --repeatsLeft;

    auto* grain = freeGrain();

    if (grain == nullptr)
        return;

    const auto jitter = std::round ((random() * 2.0f - 1.0f) * spread * 12.0f);
    const auto grainRatio = static_cast<double> (semitonesToRatio (pitch + jitter));
    const auto reversed = random() < reverseProbability;
    const auto span = grainRatio * sliceLength;

    grain->length = sliceLength;
    grain->age = 0;
    grain->fade = static_cast<int> (0.003 * sampleRate); // découpes nettes mais sans clic
    grain->step = reversed ? -grainRatio : grainRatio;
    grain->position = reversed ? static_cast<double> (sliceStart) + span - 1.0 : static_cast<double> (sliceStart);
    setPan (*grain, (random() * 2.0f - 1.0f) * spread);
    grain->active = true;
    ++started;
}

void GranularCloud::process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept
{
    const auto beatsPerSample = tempo / 60.0 / sampleRate;

    // Nombre moyen de grains superposés : le niveau est compensé pour rester stable.
    const auto overlapCount = std::max (1.0f, density * grainMs * 0.001f);
    const auto cloudGain = mode == Mode::cloud ? 1.0f / std::sqrt (overlapCount) : 1.0f;

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        history[static_cast<std::size_t> (written) & historyMask] = input[i];
        ++written;

        if (mode == Mode::cloud)
        {
            samplesToNextGrain -= 1.0;

            if (samplesToNextGrain <= 0.0)
            {
                spawnCloudGrain();
                samplesToNextGrain += sampleRate / std::max (0.5f, density) * (1.0 + spread * (random() - 0.5));
            }
        }
        else
        {
            // Sur la grille du DAW quand il joue, sinon en roue libre au tempo réglé.
            if (transport.has_value())
            {
                const auto beat = *transport + static_cast<double> (i) * beatsPerSample;
                const auto slot = static_cast<std::int64_t> (std::floor (beat / divisionBeats));

                if (slot != lastSlot)
                {
                    lastSlot = slot;
                    triggerStutter();
                }
            }
            else
            {
                freeRunPhase += beatsPerSample / divisionBeats;

                if (freeRunPhase >= 1.0 || lastTrigger < 0)
                {
                    freeRunPhase -= std::floor (freeRunPhase);
                    triggerStutter();
                }
            }
        }

        float outLeft = 0.0f, outRight = 0.0f;

        for (auto& grain : grains)
        {
            if (! grain.active)
                continue;

            const auto sample = window (grain) * readHermite (history.data(), historyMask, grain.position);
            outLeft += grain.gainLeft * sample;
            outRight += grain.gainRight * sample;

            grain.position += grain.step;

            if (++grain.age >= grain.length)
                grain.active = false;
        }

        left[i] = outLeft * cloudGain;
        right[i] = outRight * cloudGain;
    }

    if (! transport.has_value())
        lastSlot = -1; // reprise de la grille au prochain démarrage du DAW
}

} // namespace subdamp::dsp
