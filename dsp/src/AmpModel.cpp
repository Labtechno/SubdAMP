#include "subdamp/dsp/AmpModel.h"

#include "subdamp/dsp/Decibels.h"
#include "subdamp/dsp/RateBridge.h"

#include <NAM/container.h>
#include <NAM/convnet.h>
#include <NAM/dsp.h>
#include <NAM/get_dsp.h>
#include <NAM/linear.h>
#include <NAM/lstm.h>
#include <NAM/sequential.h>
#include <NAM/wavenet/model.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace subdamp::dsp
{

namespace
{
    /**
        NAM déclare ses architectures (WaveNet, LSTM...) par des initialiseurs statiques.
        Depuis une bibliothèque statique, l'éditeur de liens écarte les fichiers objets que
        rien ne référence, et leurs initialiseurs avec : le chargement échoue alors avec
        « No config parser registered for architecture ». On référence donc chacune d'elles.
    */
    void keepNamArchitecturesLinked() noexcept
    {
        using ConfigFactory = std::unique_ptr<nam::ModelConfig> (*) (const nlohmann::json&, double);

        static constexpr ConfigFactory architectures[] {
            &nam::wavenet::create_config,
            &nam::lstm::create_config,
            &nam::convnet::create_config,
            &nam::linear::create_config,
            &nam::container::create_config,
            &nam::sequential::create_config
        };

        for (const auto architecture : architectures)
        {
            volatile auto keep = architecture;
            (void) keep;
        }
    }
}

/** Une capture chargée et préparée pour une configuration audio donnée. */
struct AmpModel::Instance
{
    std::unique_ptr<nam::DSP> model; // nul : pas de capture, le signal passe tel quel

    bool useBridge = false;
    RateBridge bridge;
    std::vector<float> input, output; // brouillons pour l'appel direct (NAM ne traite pas sur place)

    float outputGain = 1.0f;
    int latency = 0;
};

AmpModel::AmpModel() = default;
AmpModel::~AmpModel() = default;

//==============================================================================
void AmpModel::prepareInstance (Instance& instance)
{
    instance.latency = 0;

    if (instance.model == nullptr)
        return;

    auto& model = *instance.model;

    // Les anciens fichiers (.nam 0.5.0) n'indiquent pas leur fréquence : l'entraîneur NAM
    // travaillait alors toujours à 48 kHz.
    const auto declaredRate = model.GetExpectedSampleRate();
    const auto modelRate = declaredRate > 0.0 ? declaredRate : 48000.0;

    instance.useBridge = std::abs (modelRate - spec.sampleRate) > 0.5
                      && ! model.SupportsArbitrarySampleRate();

    // Reset() alloue les tampons internes du réseau et le préchauffe : jamais sur le thread audio.
    if (instance.useBridge)
    {
        instance.bridge.prepare (spec.sampleRate, modelRate, spec.maxBlockSize);
        model.Reset (modelRate, instance.bridge.maxInnerBlock());
        instance.latency = instance.bridge.latencySamples();
    }
    else
    {
        model.Reset (spec.sampleRate, spec.maxBlockSize);
    }

    const auto size = static_cast<std::size_t> (spec.maxBlockSize);
    instance.input.assign (size, 0.0f);
    instance.output.assign (size, 0.0f);
}

void AmpModel::prepare (const ProcessSpec& newSpec)
{
    const std::lock_guard lock (specMutex);

    spec = newSpec;
    isPrepared = true;

    if (auto* current = handoff.getCurrent())
        prepareInstance (*current);

    if (auto* pending = handoff.getPendingWhileAudioStopped())
        prepareInstance (*pending);

    fadeIn.prepare (spec.sampleRate, 0.03);
    fadeIn.setCurrentAndTarget (1.0f);

    // Latence à déclarer : celle de la capture en attente si elle existe, car c'est elle qui
    // sera active dès le premier bloc.
    const auto* pending = handoff.getPendingWhileAudioStopped();
    const auto* active = pending != nullptr ? pending : handoff.getCurrent();
    latency.store (active != nullptr ? active->latency : 0, std::memory_order_relaxed);
}

void AmpModel::reset() noexcept
{
    if (auto* current = handoff.getCurrent())
        if (current->useBridge)
            current->bridge.reset();
}

//==============================================================================
AmpModel::LoadResult AmpModel::load (const std::filesystem::path& file)
{
    keepNamArchitecturesLinked();

    auto instance = std::make_unique<Instance>();

    try
    {
        // Pas de préchauffage au chargement : Reset() s'en charge, une fois la fréquence connue.
        nam::DspLoadOptions options;
        options.prewarm = false;
        instance->model = nam::get_dsp (file, options);
    }
    catch (const std::exception& e)
    {
        return { false, e.what() };
    }

    if (instance->model == nullptr)
        return { false, "Fichier de capture illisible." };

    if (instance->model->NumInputChannels() != 1 || instance->model->NumOutputChannels() != 1)
        return { false, "Seules les captures mono (une entrée, une sortie) sont prises en charge." };

    if (instance->model->HasLoudness())
        instance->outputGain = decibelsToGain (static_cast<float> (targetLoudnessDb - instance->model->GetLoudness()));

    const std::lock_guard lock (specMutex);

    try
    {
        if (isPrepared)
            prepareInstance (*instance);
    }
    catch (const std::exception& e)
    {
        return { false, e.what() };
    }

    handoff.publish (std::move (instance));
    return { true, {} };
}

void AmpModel::unload()
{
    const std::lock_guard lock (specMutex);
    handoff.publish (std::make_unique<Instance>());
}

void AmpModel::collectGarbage()
{
    handoff.collect();
}

//==============================================================================
void AmpModel::process (std::span<float> block) noexcept
{
    auto* instance = handoff.acquire();

    // Nouvelle capture (ou retrait) : on repart d'un fondu d'entrée pour éviter le clic.
    if (handoff.getGeneration() != seenGeneration)
    {
        seenGeneration = handoff.getGeneration();
        fadeIn.setCurrentAndTarget (0.0f);
        fadeIn.setTarget (1.0f);
        latency.store (instance != nullptr ? instance->latency : 0, std::memory_order_relaxed);
    }

    if (instance != nullptr && instance->model != nullptr)
    {
        auto& model = *instance->model;
        const auto numSamples = static_cast<int> (block.size());

        if (instance->useBridge)
        {
            instance->bridge.process (block, [&model] (float* in, float* out, int n)
            {
                if (n > 0)
                    model.process (&in, &out, n);
            });
        }
        else
        {
            auto* in = instance->input.data();
            auto* out = instance->output.data();
            std::copy (block.begin(), block.end(), in);
            model.process (&in, &out, numSamples);
            std::copy (out, out + numSamples, block.begin());
        }

        if (instance->outputGain != 1.0f)
            for (auto& sample : block)
                sample *= instance->outputGain;
    }

    if (fadeIn.isSmoothing())
        for (auto& sample : block)
            sample *= fadeIn.getNext();
}

} // namespace subdamp::dsp
