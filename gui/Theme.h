#pragma once

#include "subdamp/dsp/MonoEffect.h"

#include <juce_graphics/juce_graphics.h>

/**
    Palette provisoire de l'interface. L'identité visuelle définitive sera posée avec la
    maquette ; toutes les couleurs passent par ici pour pouvoir la changer d'un bloc.
*/
namespace subdamp::gui::theme
{
    inline const juce::Colour background      { 0xff121316 };
    inline const juce::Colour panel           { 0xff1b1d22 };
    inline const juce::Colour panelRaised     { 0xff22252b };
    inline const juce::Colour panelOutline    { 0xff2a2d35 };
    inline const juce::Colour text            { 0xffe8e6e1 };
    inline const juce::Colour textDim         { 0xff8a8d96 };
    inline const juce::Colour accent          { 0xffd4a24c }; // ambre : identité SubdAmp
    inline const juce::Colour bypassed        { 0xff4a4d55 };
    inline const juce::Colour error           { 0xffd6533f };

    inline const juce::Colour meterBackground { 0xff0b0c0e };
    inline const juce::Colour meterLow        { 0xff5fae6e };
    inline const juce::Colour meterMid        { 0xffd4a24c };
    inline const juce::Colour meterHigh       { 0xffd6533f };

    // Badges de latence (voir dsp::LatencyTier).
    inline const juce::Colour latencyLive     { 0xff5fae6e };
    inline const juce::Colour latencyPlayable { 0xffe08a3c };
    inline const juce::Colour latencyTexture  { 0xff9b6fd6 };

    inline juce::Colour colourFor (dsp::LatencyTier tier)
    {
        switch (tier)
        {
            case dsp::LatencyTier::live:     return latencyLive;
            case dsp::LatencyTier::playable: return latencyPlayable;
            case dsp::LatencyTier::texture:  return latencyTexture;
        }

        return latencyLive;
    }

    inline juce::String labelFor (dsp::LatencyTier tier)
    {
        switch (tier)
        {
            case dsp::LatencyTier::live:     return "LIVE";
            case dsp::LatencyTier::playable: return "JOUABLE";
            case dsp::LatencyTier::texture:  return "TEXTURE";
        }

        return "LIVE";
    }

    /** Badge arrondi « LIVE / JOUABLE / TEXTURE », avec un texte facultatif à droite. */
    inline void drawLatencyBadge (juce::Graphics& g, juce::Rectangle<float> area, dsp::LatencyTier tier,
                                  const juce::String& suffix, float scale)
    {
        const auto colour = colourFor (tier);
        const auto radius = area.getHeight() * 0.5f;

        g.setColour (colour.withAlpha (0.15f));
        g.fillRoundedRectangle (area, radius);
        g.setColour (colour);
        g.drawRoundedRectangle (area, radius, 1.0f * scale);
        g.setFont (juce::FontOptions (area.getHeight() * 0.5f, juce::Font::bold));
        g.drawText (labelFor (tier) + suffix, area, juce::Justification::centred);
    }
}
