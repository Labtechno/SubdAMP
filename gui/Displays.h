#pragma once

#include "plugin/PluginProcessor.h"
#include "subdamp/dsp/Fft.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <complex>
#include <vector>

namespace subdamp::gui
{

/**
    Afficheur segmenté avec légende sérigraphiée et unité : latence, charge du traitement.
*/
class SegmentReadout final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    SegmentReadout (const juce::String& caption, const juce::String& unit, int digits);

    void setValue (const juce::String& text, juce::Colour colour);
    void setStatus (const juce::String& text, juce::Colour colour);
    void setScale (float newScale) { scale = newScale; repaint(); }

    void paint (juce::Graphics& g) override;

private:
    juce::String caption, unit, value, status;
    juce::Colour valueColour, statusColour;
    int digits;
    float scale = 1.0f;
};

/**
    Oscilloscope de la sortie : ce qu'on entend vraiment (prise ScopeTap du processeur).
    Déclenchement sur front montant pour une trace stable.
*/
class Oscilloscope final : public juce::Component
{
public:
    explicit Oscilloscope (const ScopeTap& tapToRead);

    void refresh();
    void setScale (float newScale) { scale = newScale; repaint(); }

    void paint (juce::Graphics& g) override;

private:
    const ScopeTap& tap;
    std::vector<float> latest, trace;
    float scale = 1.0f;
};

/**
    Écran d'un module, dans son panneau. N'affiche que du réel :
      - mesures publiées par le thread audio (niveau vu par le GATE, transposition en cours
        du WHAMMY, note suivie par l'HARMONIZER, nappe figée, spectre de la sortie) ;
      - ou courbes calculées à partir des réglages avec les mêmes formules que le DSP
        (égaliseur de l'ampli, cab simulé, décroissance de la réverbe, voix du POLY).
*/
class ModuleDisplay final : public juce::Component
{
public:
    ModuleDisplay (ModuleId moduleToShow, SubdAmpProcessor& processor);

    /** Relit les données ; redessine si quelque chose a changé (minuteur de l'interface). */
    void refresh();
    void setScale (float newScale) { scale = newScale; repaint(); }

    void paint (juce::Graphics& g) override;

private:
    float parameter (const char* id) const;
    std::vector<float> signature() const;
    void updateSpectrum();
    void updateImpulseResponseCurve();

    void paintGate (juce::Graphics&, juce::Rectangle<float>);
    void paintSub (juce::Graphics&, juce::Rectangle<float>);
    void paintWhammy (juce::Graphics&, juce::Rectangle<float>);
    void paintHarmonizer (juce::Graphics&, juce::Rectangle<float>);
    void paintAmp (juce::Graphics&, juce::Rectangle<float>);
    void paintCab (juce::Graphics&, juce::Rectangle<float>);
    void paintFreqShift (juce::Graphics&, juce::Rectangle<float>);
    void paintShimmer (juce::Graphics&, juce::Rectangle<float>);
    void paintPoly (juce::Graphics&, juce::Rectangle<float>);
    void paintFreeze (juce::Graphics&, juce::Rectangle<float>);
    void paintGranular (juce::Graphics&, juce::Rectangle<float>);

    /** Courbe de réponse en fréquence (axe logarithmique 40 Hz – 12 kHz, ±range dB). */
    void paintResponse (juce::Graphics&, juce::Rectangle<float>, const std::vector<float>& decibels, float range);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>, float markerHz);

    ModuleId module;
    SubdAmpProcessor& processor;
    float scale = 1.0f;

    std::vector<float> shownSignature;

    // Spectre de la sortie (FREQ SHIFT, SUB, POLY)
    dsp::Fft fft;
    std::vector<float> scopeSamples, spectrumDb;
    std::vector<std::complex<float>> fftData;

    // Réponse de l'IR chargée (CAB), recalculée quand le fichier change
    juce::String impulseResponseShown;
    std::vector<float> impulseResponseDb;
    double impulseResponseSeconds = 0.0, impulseResponseRate = 0.0;
};

} // namespace subdamp::gui
