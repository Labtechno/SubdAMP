#pragma once

#include "subdamp/dsp/FractionalResampler.h"

#include <algorithm>
#include <span>
#include <vector>

namespace subdamp::dsp
{

/**
    Fait tourner un traitement à une fréquence d'échantillonnage fixe (« interne ») quelle que
    soit celle de l'hôte : rééchantillonnage hôte → interne, traitement, puis interne → hôte.

    Sert aux captures NAM, entraînées à 48 kHz : dans un projet à 88,2 kHz, le modèle doit quand
    même voir du 48 kHz, sinon sa réponse en fréquence est décalée.

    Le nombre d'échantillons internes varie d'un bloc à l'autre ; une file en sortie, préremplie
    de latencySamples() zéros, garantit qu'on rend toujours exactement un bloc hôte complet.
    La latence ajoutée reste faible (moins d'1 ms avec les réglages par défaut).
*/
class RateBridge
{
public:
    /** Peut allouer. */
    void prepare (double hostRate, double innerRate, int maxHostBlock);
    void reset() noexcept;

    int latencySamples() const noexcept { return latency; }

    /** Taille maximale d'un bloc interne passé au traitement. */
    int maxInnerBlock() const noexcept { return innerCapacity; }

    /** Nombre de fois où la file de sortie a manqué d'échantillons (doit rester à 0). */
    int underflowCount() const noexcept { return underflows; }

    /**
        Traite un bloc hôte sur place. innerProcess (float* in, float* out, int n) est appelé
        une fois avec les échantillons internes disponibles (n peut valoir 0). Il doit écrire
        n échantillons dans out et peut se servir de in comme brouillon.
    */
    template <typename InnerProcess>
    void process (std::span<float> block, InnerProcess&& innerProcess) noexcept
    {
        const auto numHost = static_cast<int> (block.size());
        const auto numInner = down.process (block.data(), numHost, innerIn.data(), innerCapacity);

        innerProcess (innerIn.data(), innerOut.data(), numInner);

        const auto numUp = up.process (innerOut.data(), numInner, upScratch.data(), upCapacity);

        for (int i = 0; i < numUp; ++i)
            push (upScratch[static_cast<std::size_t> (i)]);

        for (auto& sample : block)
            sample = pop();
    }

private:
    void push (float sample) noexcept;
    float pop() noexcept;

    FractionalResampler down, up;

    std::vector<float> innerIn, innerOut, upScratch;
    int innerCapacity = 0, upCapacity = 0;

    std::vector<float> fifo;
    std::size_t fifoMask = 0, readIndex = 0, writeIndex = 0, fifoCount = 0;

    int latency = 0;
    int underflows = 0;
};

} // namespace subdamp::dsp
