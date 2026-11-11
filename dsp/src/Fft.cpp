#include "subdamp/dsp/Fft.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

namespace subdamp::dsp
{

void Fft::prepare (int size)
{
    assert (size > 1 && std::has_single_bit (static_cast<unsigned> (size)));
    n = size;

    twiddles.resize (static_cast<std::size_t> (n / 2));

    for (int k = 0; k < n / 2; ++k)
    {
        const auto angle = -2.0 * std::numbers::pi * k / n;
        twiddles[static_cast<std::size_t> (k)] = { static_cast<float> (std::cos (angle)), static_cast<float> (std::sin (angle)) };
    }

    const auto bits = std::countr_zero (static_cast<unsigned> (n));
    bitReversed.resize (static_cast<std::size_t> (n));

    for (int i = 0; i < n; ++i)
    {
        int reversed = 0;

        for (int b = 0; b < bits; ++b)
            reversed |= ((i >> b) & 1) << (bits - 1 - b);

        bitReversed[static_cast<std::size_t> (i)] = reversed;
    }
}

void Fft::forward (std::complex<float>* data) const noexcept
{
    transform (data, false);
}

void Fft::inverse (std::complex<float>* data) const noexcept
{
    transform (data, true);

    const auto scale = 1.0f / static_cast<float> (n);

    for (int i = 0; i < n; ++i)
        data[i] *= scale;
}

void Fft::transform (std::complex<float>* data, bool isInverse) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto j = bitReversed[static_cast<std::size_t> (i)];

        if (j > i)
            std::swap (data[i], data[j]);
    }

    for (int length = 2; length <= n; length <<= 1)
    {
        const auto half = length / 2;
        const auto stride = n / length;

        for (int start = 0; start < n; start += length)
        {
            for (int k = 0; k < half; ++k)
            {
                auto w = twiddles[static_cast<std::size_t> (k * stride)];

                if (isInverse)
                    w = std::conj (w);

                // Produit complexe écrit à la main : l'opérateur * de std::complex gère les
                // infinis et NaN (appel de fonction), inutilement lent ici.
                const auto a = data[start + k];
                const auto x = data[start + k + half];
                const std::complex<float> b { x.real() * w.real() - x.imag() * w.imag(),
                                              x.real() * w.imag() + x.imag() * w.real() };
                data[start + k] = a + b;
                data[start + k + half] = a - b;
            }
        }
    }
}

} // namespace subdamp::dsp
