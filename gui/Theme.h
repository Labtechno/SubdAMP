#pragma once

#include <juce_graphics/juce_graphics.h>

/**
    Palette provisoire de l'interface. L'identité visuelle définitive sera posée avec la
    maquette ; toutes les couleurs passent par ici pour pouvoir la changer d'un bloc.
*/
namespace subdamp::gui::theme
{
    inline const juce::Colour background      { 0xff121316 };
    inline const juce::Colour panel           { 0xff1b1d22 };
    inline const juce::Colour panelOutline    { 0xff2a2d35 };
    inline const juce::Colour text            { 0xffe8e6e1 };
    inline const juce::Colour textDim         { 0xff8a8d96 };
    inline const juce::Colour accent          { 0xffd4a24c }; // ambre : identité SubdAmp
    inline const juce::Colour bypassed        { 0xff4a4d55 };

    inline const juce::Colour meterBackground { 0xff0b0c0e };
    inline const juce::Colour meterLow        { 0xff5fae6e };
    inline const juce::Colour meterMid        { 0xffd4a24c };
    inline const juce::Colour meterHigh       { 0xffd6533f };

    // Badge de latence de la chaîne. JOUABLE (orange) et TEXTURE (violet) arriveront avec les modules.
    inline const juce::Colour latencyLive     { 0xff5fae6e };
}
