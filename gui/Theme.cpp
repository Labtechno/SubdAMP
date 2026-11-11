#include "gui/Theme.h"

#include <SubdAmpAssets.h>

namespace subdamp::gui::theme
{

//==============================================================================
juce::Colour colourFor (dsp::LatencyTier tier)
{
    switch (tier)
    {
        case dsp::LatencyTier::live:     return latencyLive;
        case dsp::LatencyTier::playable: return latencyPlayable;
        case dsp::LatencyTier::texture:  return latencyTexture;
    }

    return latencyLive;
}

juce::String labelFor (dsp::LatencyTier tier)
{
    switch (tier)
    {
        case dsp::LatencyTier::live:     return "LIVE";
        case dsp::LatencyTier::playable: return "JOUABLE";
        case dsp::LatencyTier::texture:  return "TEXTURE";
    }

    return "LIVE";
}

//==============================================================================
namespace
{
    juce::Typeface::Ptr loadTypeface (const void* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
    }

    /**
        Métal brossé : des stries horizontales de longueurs variables, très peu contrastées.
        Calculé une fois ; posé en surimpression (moins de 10 % d'opacité) sur les plaques.
    */
    juce::Image makeBrushedMetal()
    {
        constexpr int width = 512, height = 256;
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Random random (1987);

        {
            juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < height; ++y)
            {
                // Chaque ligne a sa propre « passe » de brossage : légèrement plus claire ou sombre.
                const auto rowTone = random.nextFloat() * 2.0f - 1.0f;
                auto streak = 0.0f;

                for (int x = 0; x < width; ++x)
                {
                    // Bruit très lissé le long de x : longues stries, pas de grain.
                    streak += 0.08f * ((random.nextFloat() * 2.0f - 1.0f) - streak);
                    const auto value = juce::jlimit (-1.0f, 1.0f, 0.55f * rowTone + 2.2f * streak);
                    const auto alpha = static_cast<juce::uint8> (juce::jlimit (0.0f, 255.0f, std::abs (value) * 255.0f));
                    pixels.setPixelColour (x, y, (value > 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha (alpha));
                }
            }
        }

        return image;
    }
}

Resources::Resources()
    : title (loadTypeface (SubdAmpAssets::ChakraPetchSemiBold_ttf, SubdAmpAssets::ChakraPetchSemiBold_ttfSize)),
      label (loadTypeface (SubdAmpAssets::ChakraPetchMedium_ttf, SubdAmpAssets::ChakraPetchMedium_ttfSize)),
      mono (loadTypeface (SubdAmpAssets::IBMPlexMonoMedium_ttf, SubdAmpAssets::IBMPlexMonoMedium_ttfSize)),
      segment7 (loadTypeface (SubdAmpAssets::DSEG7ClassicBold_ttf, SubdAmpAssets::DSEG7ClassicBold_ttfSize)),
      segment14 (loadTypeface (SubdAmpAssets::DSEG14ClassicBold_ttf, SubdAmpAssets::DSEG14ClassicBold_ttfSize)),
      brushedMetal (makeBrushedMetal())
{
}

namespace
{
    juce::FontOptions fontFrom (juce::Typeface::Ptr Resources::* member, float height)
    {
        const SharedResources resources;
        return juce::FontOptions ((*resources).*member).withHeight (height);
    }
}

juce::FontOptions titleFont (float height)   { return fontFrom (&Resources::title, height); }
juce::FontOptions labelFont (float height)   { return fontFrom (&Resources::label, height); }
juce::FontOptions monoFont (float height)    { return fontFrom (&Resources::mono, height); }

juce::FontOptions segmentFont (float height, bool alphanumeric)
{
    return fontFrom (alphanumeric ? &Resources::segment14 : &Resources::segment7, height);
}

//==============================================================================
juce::Path chamfered (juce::Rectangle<float> r, float c)
{
    c = juce::jmin (c, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
    juce::Path path;

    if (c <= 0.0f)
    {
        path.addRectangle (r);
        return path;
    }

    path.startNewSubPath (r.getX() + c, r.getY());
    path.lineTo (r.getRight() - c, r.getY());
    path.lineTo (r.getRight(), r.getY() + c);
    path.lineTo (r.getRight(), r.getBottom() - c);
    path.lineTo (r.getRight() - c, r.getBottom());
    path.lineTo (r.getX() + c, r.getBottom());
    path.lineTo (r.getX(), r.getBottom() - c);
    path.lineTo (r.getX(), r.getY() + c);
    path.closeSubPath();
    return path;
}

void drawPlate (juce::Graphics& g, juce::Rectangle<float> area, float scale, juce::Colour base, float chamfer)
{
    const auto shape = chamfered (area, chamfer);

    // Ombre portée sur le fond
    g.setColour (seam.withAlpha (0.8f));
    g.fillPath (chamfered (area.translated (0.0f, 2.0f * scale), chamfer));

    // Métal : légèrement plus clair en haut (lumière zénithale de l'atelier)
    g.setGradientFill (juce::ColourGradient::vertical (base.brighter (0.10f), area.getY(), base.darker (0.25f), area.getBottom()));
    g.fillPath (shape);

    // Stries brossées
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);
        const SharedResources resources;
        g.setTiledImageFill (resources->brushedMetal, juce::roundToInt (area.getX()), juce::roundToInt (area.getY()), 0.07f);
        g.fillRect (area);
    }

    // Biseau : arête claire en haut, sombre en bas
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (chamfered (area.reduced (0.5f * scale), chamfer), juce::PathStrokeType (1.0f * scale));
    g.setColour (seam);
    g.strokePath (shape, juce::PathStrokeType (1.0f * scale));
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> area, float scale, float chamfer)
{
    const auto shape = chamfered (area, chamfer);
    g.setColour (seam);
    g.fillPath (shape);

    // Lumière rasante sur l'arête basse du logement
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (area.getX() + chamfer, area.getBottom() - 0.5f * scale, area.getRight() - chamfer, area.getBottom() - 0.5f * scale, 1.0f * scale);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine (area.getX() + chamfer, area.getY() + 0.5f * scale, area.getRight() - chamfer, area.getY() + 0.5f * scale, 1.0f * scale);
}

juce::Rectangle<float> drawScreen (juce::Graphics& g, juce::Rectangle<float> area, float scale)
{
    const auto bezel = 3.0f * scale;
    drawRecess (g, area, scale, 3.0f * scale);

    const auto glass = area.reduced (bezel);
    g.setGradientFill (juce::ColourGradient (screenGlass.brighter (0.06f), glass.getCentreX(), glass.getCentreY(),
                                             screenGlass.darker (0.4f), glass.getX(), glass.getY(), true));
    g.fillRect (glass);

    // Lignes de balayage, à peine visibles
    g.setColour (juce::Colours::black.withAlpha (0.18f));

    for (auto y = glass.getY(); y < glass.getBottom(); y += 3.0f * scale)
        g.fillRect (glass.getX(), y, glass.getWidth(), 1.0f * scale);

    // Reflet du verre, en haut
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::white.withAlpha (0.045f), glass.getY(),
                                                       juce::Colours::transparentWhite, glass.getY() + glass.getHeight() * 0.45f));
    g.fillRect (glass);

    return glass.reduced (3.0f * scale);
}

void drawGraticule (juce::Graphics& g, juce::Rectangle<float> area, int columns, int rows, float scale)
{
    g.setColour (screenGrid);

    for (int i = 1; i < columns; ++i)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (columns);
        g.drawLine (x, area.getY(), x, area.getBottom(), 1.0f * scale);
    }

    for (int i = 1; i < rows; ++i)
    {
        const auto y = area.getY() + area.getHeight() * static_cast<float> (i) / static_cast<float> (rows);
        g.drawLine (area.getX(), y, area.getRight(), y, 1.0f * scale);
    }

    // Sous-divisions sur les axes centraux, comme sur un vrai oscilloscope
    g.setColour (screenGrid.brighter (0.35f));
    const auto centreY = area.getCentreY();
    const auto centreX = area.getCentreX();

    for (int i = 1; i < columns * 5; ++i)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (columns * 5);
        g.drawLine (x, centreY - 2.0f * scale, x, centreY + 2.0f * scale, 1.0f * scale);
    }

    for (int i = 1; i < rows * 5; ++i)
    {
        const auto y = area.getY() + area.getHeight() * static_cast<float> (i) / static_cast<float> (rows * 5);
        g.drawLine (centreX - 2.0f * scale, y, centreX + 2.0f * scale, y, 1.0f * scale);
    }
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    g.setColour (seam);
    g.fillEllipse (head.expanded (radius * 0.25f));
    g.setGradientFill (juce::ColourGradient (metalLight, head.getX(), head.getY(), metalDark, head.getRight(), head.getBottom(), false));
    g.fillEllipse (head);

    // Empreinte six pans
    juce::Path hex;
    hex.addPolygon (centre, 6, radius * 0.48f, angle);
    g.setColour (seam.withAlpha (0.9f));
    g.fillPath (hex);
}

void drawSilkscreen (juce::Graphics& g, const juce::String& label, juce::Rectangle<float> area,
                     const juce::FontOptions& font, juce::Justification justification, juce::Colour colour)
{
    g.setFont (font);
    g.setColour (seam.withAlpha (0.85f));
    g.drawText (label, area.translated (0.0f, 1.0f), justification, false);
    g.setColour (colour);
    g.drawText (label, area, justification, false);
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float level, float scale)
{
    level = juce::jlimit (0.0f, 1.0f, level);
    const auto diameter = juce::jmin (area.getWidth(), area.getHeight());
    const auto lamp = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());

    // Halo
    if (level > 0.0f)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f * level), lamp.getCentreX(), lamp.getCentreY(),
                                                 colour.withAlpha (0.0f), lamp.getCentreX() + diameter * 1.1f, lamp.getCentreY(), true));
        g.fillEllipse (lamp.expanded (diameter * 0.6f));
    }

    // Bague métallique
    g.setColour (seam);
    g.fillEllipse (lamp.expanded (1.5f * scale));
    g.setColour (metalMid);
    g.drawEllipse (lamp.expanded (1.0f * scale), 1.0f * scale);

    // Verre : teinté même éteint, lumineux allumé
    const auto off = colour.darker (2.6f).withMultipliedSaturation (0.6f);
    const auto on = colour.brighter (0.25f);
    g.setGradientFill (juce::ColourGradient (off.interpolatedWith (on, level).brighter (0.3f * level), lamp.getCentreX(), lamp.getY() + diameter * 0.3f,
                                             off.interpolatedWith (colour, level), lamp.getCentreX(), lamp.getBottom(), true));
    g.fillEllipse (lamp);

    // Reflet
    g.setColour (juce::Colours::white.withAlpha (0.18f + 0.3f * level));
    g.fillEllipse (lamp.withSizeKeepingCentre (diameter * 0.42f, diameter * 0.24f).translated (-diameter * 0.12f, -diameter * 0.2f));
}

void drawSegments (juce::Graphics& g, const juce::String& content, juce::Rectangle<float> area, float height,
                   juce::Colour colour, juce::Justification justification, bool alphanumeric)
{
    // Segments éteints : mêmes positions, tous les segments allumés (« 8 » ou « ~ »), en très pâle.
    juce::String ghost;

    for (const auto character : content)
    {
        if (character == '.' || character == ':')
            ghost += juce::String::charToString (character);
        else
            ghost += alphanumeric ? "~" : "8";
    }

    // Le texte doit tenir en largeur : on réduit la hauteur des segments si besoin.
    const auto width = juce::GlyphArrangement::getStringWidth (segmentFont (height, alphanumeric), ghost);

    if (width > area.getWidth() && width > 0.0f)
        height *= area.getWidth() / width;

    g.setFont (segmentFont (height, alphanumeric));
    g.setColour (colour.withAlpha (0.08f));
    g.drawText (ghost, area, justification, false);

    // Légère diffusion de la lumière autour des segments allumés
    g.setColour (colour.withAlpha (0.25f));
    g.drawText (content, area.translated (0.0f, 0.6f), justification, false);
    g.setColour (colour);
    g.drawText (content, area, justification, false);
}

float latencyTagWidth (dsp::LatencyTier tier, const juce::String& value, float height)
{
    const auto labelWidth = juce::GlyphArrangement::getStringWidth (labelFont (height * 0.52f), labelFor (tier));
    const auto valueWidth = value.isEmpty() ? 0.0f
                                            : juce::GlyphArrangement::getStringWidth (monoFont (height * 0.48f), value) + height * 0.45f;
    return height * 0.55f + labelWidth + valueWidth + height * 0.5f;
}

void drawLatencyTag (juce::Graphics& g, juce::Rectangle<float> area, dsp::LatencyTier tier,
                     const juce::String& value, float scale)
{
    const auto colour = colourFor (tier);
    const auto chamfer = area.getHeight() * 0.25f;

    g.setColour (seam);
    g.fillPath (chamfered (area, chamfer));
    g.setColour (colour.withAlpha (0.12f));
    g.fillPath (chamfered (area, chamfer));
    g.setColour (colour.withAlpha (0.7f));
    g.strokePath (chamfered (area.reduced (0.5f * scale), chamfer), juce::PathStrokeType (1.0f * scale));

    // Bande de couleur de la catégorie, à gauche, comme une étiquette de maintenance
    auto inner = area.reduced (area.getHeight() * 0.18f);
    g.setColour (colour);
    g.fillRect (inner.removeFromLeft (area.getHeight() * 0.14f));
    inner.removeFromLeft (area.getHeight() * 0.22f);

    g.setFont (labelFont (area.getHeight() * 0.52f));
    g.drawText (labelFor (tier), inner, juce::Justification::centredLeft, false);

    if (value.isNotEmpty())
    {
        g.setColour (text);
        g.setFont (monoFont (area.getHeight() * 0.48f));
        g.drawText (value, inner, juce::Justification::centredRight, false);
    }
}

void drawHazardStripes (juce::Graphics& g, juce::Rectangle<float> area, float scale, juce::Colour colour)
{
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.toNearestInt());

    g.setColour (seam);
    g.fillRect (area);
    g.setColour (colour.withAlpha (0.85f));

    const auto step = 10.0f * scale;

    for (auto x = area.getX() - area.getHeight(); x < area.getRight(); x += step * 2.0f)
    {
        juce::Path stripe;
        stripe.addQuadrilateral (x, area.getBottom(), x + step, area.getBottom(),
                                 x + step + area.getHeight(), area.getY(), x + area.getHeight(), area.getY());
        g.fillPath (stripe);
    }
}

} // namespace subdamp::gui::theme
