#include "plugin/CabSection.h"

namespace subdamp
{

void CabSection::prepare (const dsp::ProcessSpec& spec)
{
    convolution.prepare ({ spec.sampleRate, static_cast<juce::uint32> (spec.maxBlockSize), 1 });
    fallback.prepare (spec);
    scratch.assign (static_cast<std::size_t> (spec.maxBlockSize), 0.0f);
}

void CabSection::reset() noexcept
{
    convolution.reset();
    fallback.reset();
}

void CabSection::loadImpulseResponse (const juce::File& file)
{
    // Normalisation : toutes les IR sortent au même niveau, quel que soit leur enregistrement.
    convolution.loadImpulseResponse (file,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     0,
                                     juce::dsp::Convolution::Normalise::yes);
    useImpulseResponse = true;
}

void CabSection::clearImpulseResponse() noexcept
{
    useImpulseResponse = false;
}

void CabSection::process (std::span<float> block) noexcept
{
    if (useImpulseResponse.load (std::memory_order_relaxed))
    {
        // La convolution doit tourner en continu : c'est dans process() qu'elle transmet la
        // demande de chargement à son thread, puis installe la nouvelle IR une fois prête.
        std::copy (block.begin(), block.end(), scratch.begin());
        auto* channel = scratch.data();
        juce::dsp::AudioBlock<float> audio (&channel, 1, block.size());
        convolution.process (juce::dsp::ProcessContextReplacing<float> (audio));

        // Avant l'arrivée de l'IR, la convolution contient une impulsion unité (1 échantillon) :
        // on garde le cabinet de secours, jamais de son « sans cab » pendant un chargement.
        if (convolution.getCurrentIRSize() > 1)
        {
            std::copy (scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t> (block.size()), block.begin());
            return;
        }
    }

    fallback.process (block);
}

} // namespace subdamp
