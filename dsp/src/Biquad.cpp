#include "subdamp/dsp/Biquad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

namespace
{
    struct Prototype
    {
        double cosW, alpha;
    };

    Prototype prototype (double sampleRate, double frequency, double q)
    {
        const auto w = 2.0 * std::numbers::pi * frequency / sampleRate;
        return { std::cos (w), std::sin (w) / (2.0 * q) };
    }

    BiquadCoefficients normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { static_cast<float> (b0 / a0), static_cast<float> (b1 / a0), static_cast<float> (b2 / a0),
                 static_cast<float> (a1 / a0), static_cast<float> (a2 / a0) };
    }

    BiquadCoefficients shelf (double sampleRate, double frequency, double gainDb, bool isLow)
    {
        // Pente de plateau S = 1 (la plus raide sans bosse).
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w = 2.0 * std::numbers::pi * frequency / sampleRate;
        const auto cosW = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
        const auto twoSqrtAAlpha = 2.0 * std::sqrt (a) * alpha;
        const auto sign = isLow ? 1.0 : -1.0;

        return normalise (a * ((a + 1.0) - sign * (a - 1.0) * cosW + twoSqrtAAlpha),
                          sign * 2.0 * a * ((a - 1.0) - sign * (a + 1.0) * cosW),
                          a * ((a + 1.0) - sign * (a - 1.0) * cosW - twoSqrtAAlpha),
                          (a + 1.0) + sign * (a - 1.0) * cosW + twoSqrtAAlpha,
                          -sign * 2.0 * ((a - 1.0) + sign * (a + 1.0) * cosW),
                          (a + 1.0) + sign * (a - 1.0) * cosW - twoSqrtAAlpha);
    }
}

BiquadCoefficients BiquadCoefficients::lowPass (double sampleRate, double frequency, double q)
{
    const auto [cosW, alpha] = prototype (sampleRate, frequency, q);
    return normalise ((1.0 - cosW) / 2.0, 1.0 - cosW, (1.0 - cosW) / 2.0,
                      1.0 + alpha, -2.0 * cosW, 1.0 - alpha);
}

BiquadCoefficients BiquadCoefficients::highPass (double sampleRate, double frequency, double q)
{
    const auto [cosW, alpha] = prototype (sampleRate, frequency, q);
    return normalise ((1.0 + cosW) / 2.0, -(1.0 + cosW), (1.0 + cosW) / 2.0,
                      1.0 + alpha, -2.0 * cosW, 1.0 - alpha);
}

BiquadCoefficients BiquadCoefficients::peak (double sampleRate, double frequency, double q, double gainDb)
{
    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto [cosW, alpha] = prototype (sampleRate, frequency, q);
    return normalise (1.0 + alpha * a, -2.0 * cosW, 1.0 - alpha * a,
                      1.0 + alpha / a, -2.0 * cosW, 1.0 - alpha / a);
}

BiquadCoefficients BiquadCoefficients::lowShelf (double sampleRate, double frequency, double gainDb)
{
    return shelf (sampleRate, frequency, gainDb, true);
}

BiquadCoefficients BiquadCoefficients::highShelf (double sampleRate, double frequency, double gainDb)
{
    return shelf (sampleRate, frequency, gainDb, false);
}

double BiquadCoefficients::magnitudeDecibels (double frequency, double sampleRate) const noexcept
{
    // |H(e^jw)| = |b0 + b1 z^-1 + b2 z^-2| / |1 + a1 z^-1 + a2 z^-2|, z = e^jw
    const auto w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);

    const auto numeratorRe = b0 + b1 * c1 + b2 * c2;
    const auto numeratorIm = -(b1 * s1 + b2 * s2);
    const auto denominatorRe = 1.0 + a1 * c1 + a2 * c2;
    const auto denominatorIm = -(a1 * s1 + a2 * s2);

    const auto power = (numeratorRe * numeratorRe + numeratorIm * numeratorIm)
                     / std::max (1.0e-30, denominatorRe * denominatorRe + denominatorIm * denominatorIm);
    return 10.0 * std::log10 (std::max (1.0e-30, power));
}

} // namespace subdamp::dsp
