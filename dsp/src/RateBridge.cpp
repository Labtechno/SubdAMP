#include "subdamp/dsp/RateBridge.h"

#include <bit>
#include <cmath>

namespace subdamp::dsp
{

void RateBridge::prepare (double hostRate, double innerRate, int maxHostBlock)
{
    const auto innerPerHost = innerRate / hostRate;

    innerCapacity = static_cast<int> (std::ceil (maxHostBlock * innerPerHost)) + 4;
    upCapacity = maxHostBlock + 8;

    down.prepare (hostRate, innerRate, maxHostBlock);
    up.prepare (innerRate, hostRate, innerCapacity);

    innerIn.assign (static_cast<std::size_t> (innerCapacity), 0.0f);
    innerOut.assign (static_cast<std::size_t> (innerCapacity), 0.0f);
    upScratch.assign (static_cast<std::size_t> (upCapacity), 0.0f);

    // Retard de disponibilité de la chaîne, en échantillons hôte : demi-noyau de chaque
    // rééchantillonneur, plus la granularité d'un échantillon interne et une marge d'arrondi.
    const auto hostPerInner = hostRate / innerRate;
    latency = static_cast<int> (std::ceil (down.halfWidth() + up.halfWidth() * hostPerInner + hostPerInner)) + 2;

    fifo.assign (std::bit_ceil (static_cast<std::size_t> (latency + 2 * maxHostBlock + 16)), 0.0f);
    fifoMask = fifo.size() - 1;

    reset();
}

void RateBridge::reset() noexcept
{
    down.reset();
    up.reset();

    std::fill (fifo.begin(), fifo.end(), 0.0f);
    readIndex = 0;
    writeIndex = static_cast<std::size_t> (latency);
    fifoCount = static_cast<std::size_t> (latency);
    underflows = 0;
}

void RateBridge::push (float sample) noexcept
{
    if (fifoCount > fifoMask)
        return; // file pleine : impossible avec les marges choisies

    fifo[writeIndex] = sample;
    writeIndex = (writeIndex + 1) & fifoMask;
    ++fifoCount;
}

float RateBridge::pop() noexcept
{
    if (fifoCount == 0)
    {
        ++underflows;
        return 0.0f;
    }

    const auto sample = fifo[readIndex];
    readIndex = (readIndex + 1) & fifoMask;
    --fifoCount;
    return sample;
}

} // namespace subdamp::dsp
