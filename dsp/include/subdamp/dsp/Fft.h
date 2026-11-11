#pragma once

#include <complex>
#include <vector>

namespace subdamp::dsp
{

/**
    FFT complexe radix 2, en place. Tables (rotations, permutation) calculées dans prepare() :
    forward() et inverse() n'allouent rien.

    inverse() inclut la normalisation 1/N : inverse (forward (x)) == x.
*/
class Fft
{
public:
    /** size : puissance de 2. Peut allouer. */
    void prepare (int size);

    int size() const noexcept { return n; }

    void forward (std::complex<float>* data) const noexcept;
    void inverse (std::complex<float>* data) const noexcept;

private:
    void transform (std::complex<float>* data, bool isInverse) const noexcept;

    int n = 0;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitReversed;
};

} // namespace subdamp::dsp
