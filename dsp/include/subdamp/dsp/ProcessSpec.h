#pragma once

namespace subdamp::dsp
{

/** Contexte de traitement transmis à chaque bloc DSP lors de sa préparation. */
struct ProcessSpec
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
};

} // namespace subdamp::dsp
