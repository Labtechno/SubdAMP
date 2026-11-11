#include "subdamp/dsp/PitchTracker.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace subdamp::dsp
{

void PitchTracker::prepare (double newSampleRate, double minHz, double maxHz)
{
    sampleRate = newSampleRate;
    decimation = std::max (1, static_cast<int> (std::lround (sampleRate / 12000.0)));

    const auto decimatedRate = sampleRate / decimation;

    if (decimation > 1)
    {
        // Anti-repliement avant décimation : la fondamentale ne dépasse pas 1,4 kHz,
        // on garde de la marge pour les premiers harmoniques.
        const auto cutoff = 0.35 * decimatedRate;
        antiAlias[0].setCoefficients (BiquadCoefficients::lowPass (sampleRate, cutoff, butterworth4Q1));
        antiAlias[1].setCoefficients (BiquadCoefficients::lowPass (sampleRate, cutoff, butterworth4Q2));
    }

    tauMin = std::max (2, static_cast<int> (std::floor (decimatedRate / maxHz)));
    tauMax = static_cast<int> (std::ceil (decimatedRate / minHz));
    window = tauMax;
    hop = std::max (1, static_cast<int> (std::lround (decimatedRate * 0.004)));
    reacquireFramesAfterLoss = static_cast<int> (std::lround (reacquireSeconds / 0.004));

    decimated.assign (std::bit_ceil (static_cast<std::size_t> (window + tauMax + 4)), 0.0f);
    decimatedMask = decimated.size() - 1;
    frame.assign (static_cast<std::size_t> (window + tauMax + 2), 0.0f);
    difference.assign (static_cast<std::size_t> (tauMax + 2), 0.0f);
    normalised.assign (static_cast<std::size_t> (tauMax + 2), 1.0f);

    fullTauMax = static_cast<int> (std::ceil (sampleRate / minHz)) + decimation + 2;
    full.assign (std::bit_ceil (static_cast<std::size_t> (3 * fullTauMax + 8)), 0.0f);
    fullMask = full.size() - 1;
    fullFrame.assign (static_cast<std::size_t> (3 * fullTauMax + 8), 0.0f);

    reset();
}

void PitchTracker::reset() noexcept
{
    for (auto& filter : antiAlias)
        filter.reset();

    std::fill (decimated.begin(), decimated.end(), 0.0f);
    std::fill (full.begin(), full.end(), 0.0f);
    decimatedWritten = 0;
    fullWritten = 0;
    decimationCounter = 0;
    hopCounter = 0;
    voiced = false;
    hasNewEstimate = false;
    period = 0.0f;
    reacquireFrames = 0;
    pendingPeriod = 0.0f;
    lastGoodPeriod = 0.0f;
}

int PitchTracker::localMinimumNear (int tau) const noexcept
{
    // Creux de la fonction normalisée le plus proche de tau (± 3 échantillons décimés).
    auto best = std::clamp (tau, tauMin, tauMax - 1);

    for (int t = std::max (tauMin, tau - 3); t <= std::min (tauMax - 1, tau + 3); ++t)
        if (normalised[static_cast<std::size_t> (t)] < normalised[static_cast<std::size_t> (best)])
            best = t;

    return best;
}

bool PitchTracker::takeNewEstimate() noexcept
{
    const auto result = hasNewEstimate;
    hasNewEstimate = false;
    return result;
}

void PitchTracker::push (float sample) noexcept
{
    full[static_cast<std::size_t> (fullWritten) & fullMask] = sample;
    ++fullWritten;

    auto filtered = sample;

    if (decimation > 1)
        filtered = antiAlias[1].processSample (antiAlias[0].processSample (sample));

    if (++decimationCounter < decimation)
        return;

    decimationCounter = 0;
    decimated[static_cast<std::size_t> (decimatedWritten) & decimatedMask] = filtered;
    ++decimatedWritten;

    if (++hopCounter < hop)
        return;

    hopCounter = 0;

    if (decimatedWritten >= window + tauMax)
        analyse();
}

void PitchTracker::analyse() noexcept
{
    hasNewEstimate = true;

    // Copie linéaire de la trame : les boucles ci-dessous se vectorisent.
    const auto frameLength = static_cast<std::size_t> (window + tauMax);
    const auto start = decimatedWritten - static_cast<std::int64_t> (frameLength);
    double energy = 0.0;

    for (std::size_t j = 0; j < frameLength; ++j)
    {
        frame[j] = decimated[static_cast<std::size_t> (start + static_cast<std::int64_t> (j)) & decimatedMask];
        energy += static_cast<double> (frame[j]) * frame[j];
    }

    // Une note vient d'être perdue : on décompte la fenêtre de raccrochage facile.
    if (voiced)
        reacquireFrames = reacquireFramesAfterLoss;
    else if (reacquireFrames > 0)
        --reacquireFrames;

    if (std::sqrt (energy / static_cast<double> (frameLength)) < silenceRms)
    {
        voiced = false;
        reacquireFrames = 0; // silence : la prochaine note repart du seuil strict
        pendingPeriod = 0.0f;
        lastGoodPeriod = 0.0f;
        return;
    }

    // 1. Fonction de différence : d(τ) = Σ (x[j] − x[j+τ])²
    const auto w = static_cast<std::size_t> (window);

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const auto* a = frame.data();
        const auto* b = frame.data() + tau;
        float sum = 0.0f;

        for (std::size_t j = 0; j < w; ++j)
        {
            const auto d = a[j] - b[j];
            sum += d * d;
        }

        difference[static_cast<std::size_t> (tau)] = sum;
    }

    // 2. Normalisation par la moyenne cumulée : d'(τ) = d(τ) · τ / Σ d(1..τ)
    double running = 0.0;
    normalised[0] = 1.0f;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += difference[static_cast<std::size_t> (tau)];
        normalised[static_cast<std::size_t> (tau)] = running > 0.0
            ? static_cast<float> (static_cast<double> (difference[static_cast<std::size_t> (tau)]) * tau / running)
            : 1.0f;
    }

    // 3. Premier creux sous le seuil (évite les erreurs d'octave vers le grave), sinon minimum global.
    int best = -1;

    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (normalised[static_cast<std::size_t> (tau)] < firstDipThreshold)
        {
            while (tau + 1 < tauMax && normalised[static_cast<std::size_t> (tau + 1)] < normalised[static_cast<std::size_t> (tau)])
                ++tau;

            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = tauMin;

        for (int tau = tauMin + 1; tau < tauMax; ++tau)
            if (normalised[static_cast<std::size_t> (tau)] < normalised[static_cast<std::size_t> (best)])
                best = tau;
    }

    const auto continuing = voiced || reacquireFrames > 0;

    // Erreur d'octave vers le grave : l'estimation est un multiple de la période suivie et
    // celle-ci colle encore au signal → c'est toujours la note en cours.
    if (continuing && lastGoodPeriod > 0.0f)
    {
        const auto previous = lastGoodPeriod / static_cast<float> (decimation);
        const auto multiple = static_cast<float> (best) / previous;
        const auto nearest = std::round (multiple);

        if (nearest >= 2.0f && nearest <= 4.0f && std::abs (multiple - nearest) < 0.04f * nearest)
        {
            const auto candidate = localMinimumNear (static_cast<int> (std::lround (previous)));

            if (normalised[static_cast<std::size_t> (candidate)] < stayThreshold)
                best = candidate;
        }
    }

    const auto quality = normalised[static_cast<std::size_t> (best)];

    if (quality >= (continuing ? stayThreshold : enterThreshold))
    {
        voiced = false;
        pendingPeriod = 0.0f;
        return;
    }

    // 4. Interpolation parabolique autour du creux
    auto refined = static_cast<float> (best);

    if (best > 1 && best < tauMax)
    {
        const auto a = normalised[static_cast<std::size_t> (best - 1)];
        const auto b = normalised[static_cast<std::size_t> (best)];
        const auto c = normalised[static_cast<std::size_t> (best + 1)];
        const auto denominator = a - 2.0f * b + c;

        if (denominator > 0.0f)
            refined += 0.5f * (a - c) / denominator;
    }

    // 5. Affinage à pleine résolution
    const auto estimate = refineAtFullRate (refined * static_cast<float> (decimation));

    // 6. Confirmation d'un changement de note. Première note (après un silence) : adoptée tout
    //    de suite au seuil strict. Sinon, une période nouvelle doit se répéter sur l'analyse
    //    suivante ; entre-temps, la période précédente est conservée.
    const auto differs = [] (float a, float b) { return std::abs (a - b) > 0.04f * b; };

    if (lastGoodPeriod > 0.0f && continuing && differs (estimate, lastGoodPeriod))
    {
        if (pendingPeriod > 0.0f && ! differs (estimate, pendingPeriod))
        {
            pendingPeriod = 0.0f; // confirmée
        }
        else
        {
            pendingPeriod = estimate;

            // Note en cours conservée pendant l'attente ; si on l'avait perdue, on attend.
            if (! voiced)
                return;

            period = lastGoodPeriod;
            return;
        }
    }
    else
    {
        pendingPeriod = 0.0f;
    }

    period = estimate;
    lastGoodPeriod = estimate;
    voiced = true;
}

float PitchTracker::refineAtFullRate (float coarsePeriod) noexcept
{
    const auto centre = static_cast<int> (std::lround (coarsePeriod));
    const auto lowest = std::max (2, centre - decimation - 1);
    const auto highest = std::min (fullTauMax, centre + decimation + 1);

    if (highest <= lowest + 1)
        return coarsePeriod;

    // Fenêtre de deux périodes, prise sur les échantillons les plus récents.
    const auto length = std::min (2 * centre, static_cast<int> (fullFrame.size()) - highest - 1);
    const auto frameLength = static_cast<std::int64_t> (length + highest + 1);

    if (length < 8 || fullWritten < frameLength)
        return coarsePeriod;

    const auto start = fullWritten - frameLength;

    for (std::int64_t j = 0; j < frameLength; ++j)
        fullFrame[static_cast<std::size_t> (j)] = full[static_cast<std::size_t> (start + j) & fullMask];

    const auto differenceAt = [this, length] (int tau)
    {
        const auto* a = fullFrame.data();
        const auto* b = fullFrame.data() + tau;
        float sum = 0.0f;

        for (int j = 0; j < length; ++j)
        {
            const auto d = a[j] - b[j];
            sum += d * d;
        }

        return sum;
    };

    auto best = lowest;
    auto bestValue = differenceAt (lowest);
    auto previousValue = bestValue;
    float beforeBest = bestValue, afterBest = bestValue;

    for (int tau = lowest + 1; tau <= highest; ++tau)
    {
        const auto value = differenceAt (tau);

        if (value < bestValue)
        {
            best = tau;
            bestValue = value;
            beforeBest = previousValue;
            afterBest = bestValue;
        }
        else if (tau == best + 1)
        {
            afterBest = value;
        }

        previousValue = value;
    }

    // Minimum en bord de plage : l'estimation grossière était décalée, on la garde.
    if (best == lowest || best == highest)
        return coarsePeriod;

    const auto denominator = beforeBest - 2.0f * bestValue + afterBest;
    auto result = static_cast<float> (best);

    if (denominator > 0.0f)
        result += 0.5f * (beforeBest - afterBest) / denominator;

    return result;
}

} // namespace subdamp::dsp
