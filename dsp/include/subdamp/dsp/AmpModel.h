#pragma once

#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/MonoEffect.h"
#include "subdamp/dsp/RealtimeHandoff.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace subdamp::dsp
{

/**
    Capture d'ampli Neural Amp Modeler (.nam).

    Chargement (lecture du fichier, construction du réseau, préchauffage) hors du thread audio,
    puis transmission au thread audio par RealtimeHandoff : changer de capture en jouant ne
    provoque ni allocation ni verrou côté audio, juste un court fondu d'entrée.

    Si la fréquence de l'hôte diffère de celle de la capture (48 kHz en général), le modèle
    tourne quand même à sa fréquence d'origine grâce à un RateBridge (latence < 1 ms).

    Le volume de sortie est normalisé d'après la sonie mesurée à l'entraînement (quand le
    fichier la fournit) : passer d'une capture à l'autre ne fait pas de saut de volume.
    Sans capture chargée, le module laisse passer le signal tel quel.
*/
class AmpModel final : public MonoEffect
{
public:
    AmpModel();
    ~AmpModel() override;

    /** Hors thread audio, audio arrêté (prepareToPlay). */
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

    struct LoadResult
    {
        bool succeeded = false;
        std::string error;
    };

    /** Hors thread audio (thread de chargement). Ne lève pas d'exception. */
    LoadResult load (const std::filesystem::path& file);

    /** Hors thread audio : retire la capture (le module redevient transparent). */
    void unload();

    /** Hors thread audio : libère la capture précédente, rendue par le thread audio. */
    void collectGarbage();

    /** Latence ajoutée par le rééchantillonnage éventuel, en échantillons hôte. */
    int getLatencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

    /** Sonie cible de la normalisation (dBFS), comme le plugin NAM officiel. */
    static constexpr double targetLoudnessDb = -18.0;

private:
    struct Instance;

    void prepareInstance (Instance& instance);

    std::mutex specMutex; // jamais pris par le thread audio
    ProcessSpec spec;
    bool isPrepared = false;

    RealtimeHandoff<Instance> handoff;
    unsigned seenGeneration = 0;
    LinearSmoother fadeIn;
    std::atomic<int> latency { 0 };
};

} // namespace subdamp::dsp
