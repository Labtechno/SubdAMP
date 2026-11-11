#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

namespace subdamp
{

/**
    Prise d'oscilloscope : le thread audio y écrit ce qu'on entend (somme mono, décimée vers
    24 kHz), l'interface relit les derniers échantillons pour les écrans.

    Anneau d'atomics préalloué : ni verrou, ni allocation, aucune attente d'un côté ou de
    l'autre. Une lecture peut croiser une écriture (un échantillon plus récent que les autres) :
    sans importance pour un affichage.
*/
class ScopeTap
{
public:
    static constexpr std::size_t capacity = 8192; // puissance de 2

    void prepare (double sampleRate) noexcept
    {
        decimation = std::max (1, static_cast<int> (std::lround (sampleRate / 24000.0)));
        rate.store (sampleRate / decimation, std::memory_order_relaxed);
        counter = 0;
    }

    /** Thread audio. */
    void push (std::span<const float> left, std::span<const float> right) noexcept
    {
        auto index = written.load (std::memory_order_relaxed);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (++counter < decimation)
                continue;

            counter = 0;
            samples[index & mask].store (0.5f * (left[i] + right[i]), std::memory_order_relaxed);
            ++index;
        }

        written.store (index, std::memory_order_release);
    }

    /** Interface : copie les derniers échantillons, du plus ancien au plus récent. */
    void copyLatest (std::span<float> destination) const noexcept
    {
        const auto end = written.load (std::memory_order_acquire);
        const auto count = std::min<std::size_t> (destination.size(), capacity);

        for (std::size_t i = 0; i < count; ++i)
            destination[i] = samples[(end - count + i) & mask].load (std::memory_order_relaxed);
    }

    /** Fréquence des échantillons stockés (après décimation). */
    double getSampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

private:
    static constexpr std::uint64_t mask = capacity - 1;

    std::array<std::atomic<float>, capacity> samples {};
    std::atomic<std::uint64_t> written { 0 };
    std::atomic<double> rate { 24000.0 };
    int decimation = 2, counter = 0; // thread audio uniquement
};

} // namespace subdamp
