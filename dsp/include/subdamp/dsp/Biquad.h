#pragma once

namespace subdamp::dsp
{

/** Coefficients normalisés (a0 = 1) d'un filtre du second ordre. */
struct BiquadCoefficients
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;

    // Formules du « Audio EQ Cookbook » de Robert Bristow-Johnson.
    static BiquadCoefficients lowPass (double sampleRate, double frequency, double q);
    static BiquadCoefficients highPass (double sampleRate, double frequency, double q);
    static BiquadCoefficients peak (double sampleRate, double frequency, double q, double gainDb);
    static BiquadCoefficients lowShelf (double sampleRate, double frequency, double gainDb);
    static BiquadCoefficients highShelf (double sampleRate, double frequency, double gainDb);

    /** Gain du filtre à une fréquence, en dB (pour tracer sa vraie courbe dans l'interface). */
    double magnitudeDecibels (double frequency, double sampleRate) const noexcept;
};

/** Ordre 2 pour un filtre Butterworth (Q = 1/√2), et les deux étages d'un Butterworth d'ordre 4. */
inline constexpr double butterworthQ = 0.70710678118654752;
inline constexpr double butterworth4Q1 = 0.54119610014619698;
inline constexpr double butterworth4Q2 = 1.30656296487637652;

/**
    Filtre biquad en forme directe transposée II : bonne tenue numérique en float,
    deux variables d'état seulement. Les coefficients peuvent changer à chaque bloc.
*/
class Biquad
{
public:
    void setCoefficients (const BiquadCoefficients& newCoefficients) noexcept { c = newCoefficients; }
    void reset() noexcept { s1 = s2 = 0.0f; }

    float processSample (float x) noexcept
    {
        const auto y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }

private:
    BiquadCoefficients c;
    float s1 = 0.0f, s2 = 0.0f;
};

} // namespace subdamp::dsp
