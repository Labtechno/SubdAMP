#include "subdamp/dsp/OctaveDivider.h"

#include <algorithm>
#include <cmath>

namespace subdamp::dsp
{

void OctaveDivider::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    trackingFilter[0].setCoefficients (BiquadCoefficients::lowPass (sampleRate, trackingHz, butterworth4Q1));
    trackingFilter[1].setCoefficients (BiquadCoefficients::lowPass (sampleRate, trackingHz, butterworth4Q2));
    octaveUpBand.setCoefficients (BiquadCoefficients::lowPass (sampleRate, octaveUpBandHz, butterworthQ));
    octaveUpDcBlock.setCoefficients (BiquadCoefficients::highPass (sampleRate, dcBlockHz, butterworthQ));

    appliedToneHz = 0.0f;
    updateToneFilter();

    for (auto* level : { &dryLevel, &downLevel, &twoDownLevel, &upLevel })
        level->prepare (sampleRate, 0.02);

    octaveGate.prepare (sampleRate, 0.01);

    envelopeAttack = static_cast<float> (std::exp (-1.0 / (0.001 * sampleRate)));
    envelopeRelease = static_cast<float> (std::exp (-1.0 / (0.05 * sampleRate)));

    reset();
}

void OctaveDivider::reset() noexcept
{
    for (auto& filter : trackingFilter)
        filter.reset();

    for (auto& filter : toneFilter)
        filter.reset();

    octaveUpBand.reset();
    octaveUpDcBlock.reset();
    octaveGate.setCurrentAndTarget (0.0f);

    envelope = 0.0f;
    previous = 0.0f;
    armed = false;
    flipFlop1 = 1.0f;
    flipFlop2 = 1.0f;
}

void OctaveDivider::setLevels (float dry, float octaveDown, float twoOctavesDown, float octaveUp) noexcept
{
    dryLevel.setTarget (dry);
    downLevel.setTarget (octaveDown);
    twoDownLevel.setTarget (twoOctavesDown);
    upLevel.setTarget (octaveUp);
}

void OctaveDivider::setToneFrequency (float hz) noexcept
{
    toneHz = hz;
}

void OctaveDivider::updateToneFilter() noexcept
{
    if (toneHz == appliedToneHz)
        return;

    appliedToneHz = toneHz;
    const auto cutoff = std::min (static_cast<double> (toneHz), sampleRate * 0.45);
    toneFilter[0].setCoefficients (BiquadCoefficients::lowPass (sampleRate, cutoff, butterworth4Q1));
    toneFilter[1].setCoefficients (BiquadCoefficients::lowPass (sampleRate, cutoff, butterworth4Q2));
}

void OctaveDivider::process (std::span<float> block) noexcept
{
    updateToneFilter();

    for (auto& sample : block)
    {
        const auto input = sample;

        // 1. Fondamentale isolée et enveloppe
        const auto fundamental = trackingFilter[1].processSample (trackingFilter[0].processSample (input));
        const auto level = std::fabs (fundamental);
        const auto envelopeCoefficient = level > envelope ? envelopeAttack : envelopeRelease;
        envelope = level + (envelope - level) * envelopeCoefficient;

        const auto audible = envelope > floorLevel;
        octaveGate.setTarget (audible ? 1.0f : 0.0f);

        // 2. Bascules, déclenchées au passage par zéro montant, une fois le signal
        //    passé sous le seuil négatif d'hystérésis (« armé »).
        if (audible)
        {
            if (fundamental < -hysteresisRatio * envelope)
                armed = true;

            if (armed && previous <= 0.0f && fundamental > 0.0f)
            {
                armed = false;
                flipFlop1 = -flipFlop1;

                if (flipFlop1 > 0.0f)
                    flipFlop2 = -flipFlop2;
            }
        }

        previous = fundamental;

        // 3. Octaves basses (polarité inversée un cycle sur deux) et 4. octave haute (redressement)
        const auto down = fundamental * flipFlop1;
        const auto twoDown = fundamental * flipFlop2;
        const auto up = octaveUpDcBlock.processSample (std::fabs (octaveUpBand.processSample (input)));

        const auto octaves = octaveGate.getNext()
                           * (down * downLevel.getNext() + twoDown * twoDownLevel.getNext() + up * upLevel.getNext());

        const auto shaped = toneFilter[1].processSample (toneFilter[0].processSample (octaves));

        sample = input * dryLevel.getNext() + shaped;
    }
}

} // namespace subdamp::dsp
