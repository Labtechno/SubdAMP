#pragma once

#include "subdamp/dsp/MonoEffect.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Charte graphique de SubdAmp : « laboratoire audio clandestin ».

    Matériel de labo bricolé dans une arrière-boutique : panneaux de métal graphite, plaques
    vissées, sérigraphies, écrans techniques au phosphore vert, témoins lumineux. Chaque
    couleur a une fonction :
      - vert médical  : écrans, traces, catégorie LIVE ;
      - ambre         : ce qui est allumé / actif (témoins, valeurs, arcs des potards) ;
      - orange        : catégorie JOUABLE (latence perceptible) ;
      - bleu acier    : catégorie TEXTURE (module lent, à doser) ;
      - rouge         : alertes (saturation, gate qui coupe, bypass engagé) ;
      - blanc cassé   : textes et informations principales.

    Toutes les couleurs, polices et matières passent par ici : la charte se change d'un bloc.
*/
namespace subdamp::gui::theme
{
    //==============================================================================
    // Surfaces
    inline const juce::Colour graphite      { 0xff0b0c0d }; // fond, le plus profond
    inline const juce::Colour anthracite    { 0xff141618 }; // grands panneaux
    inline const juce::Colour plate         { 0xff1b1e21 }; // plaques de modules
    inline const juce::Colour plateRaised   { 0xff23272b }; // plaque allumée / en relief
    inline const juce::Colour metalDark     { 0xff2c3035 };
    inline const juce::Colour metalMid      { 0xff3b4046 };
    inline const juce::Colour metalLight    { 0xff5d646b };
    inline const juce::Colour seam          { 0xff050607 }; // joints, gorges, ombres portées
    inline const juce::Colour hairline      { 0xff2b2f34 }; // filets de séparation

    // Textes (sérigraphie)
    inline const juce::Colour text          { 0xffe4dfd3 }; // blanc cassé
    inline const juce::Colour textDim       { 0xff8e9089 }; // gris chaud, étiquettes secondaires
    inline const juce::Colour textFaint     { 0xff5a5d59 }; // marquages discrets

    // Écrans
    inline const juce::Colour screenGlass   { 0xff08100c }; // verre, noir verdâtre
    inline const juce::Colour screenGrid    { 0xff15291f }; // graticule
    inline const juce::Colour phosphor      { 0xff92d4ad }; // trace lumineuse

    // Fonctions
    inline const juce::Colour medical       { 0xff6fa88b }; // vert médical désaturé
    inline const juce::Colour amber         { 0xffe3a13f }; // actif
    inline const juce::Colour incandescent  { 0xffee7d2f }; // chauffe / JOUABLE
    inline const juce::Colour steel         { 0xff7f9fb5 }; // TEXTURE
    inline const juce::Colour alert         { 0xffcc4630 }; // rouge industriel

    // Noms historiques, utilisés par les composants existants
    inline const juce::Colour background      = graphite;
    inline const juce::Colour panel           = anthracite;
    inline const juce::Colour panelRaised     = plateRaised;
    inline const juce::Colour panelOutline    = hairline;
    inline const juce::Colour accent          = amber;
    inline const juce::Colour bypassed        = metalMid;
    inline const juce::Colour error           = alert;
    inline const juce::Colour meterBackground = screenGlass;
    inline const juce::Colour meterLow        = medical;
    inline const juce::Colour meterMid        = amber;
    inline const juce::Colour meterHigh       = alert;
    inline const juce::Colour latencyLive     = medical;
    inline const juce::Colour latencyPlayable = incandescent;
    inline const juce::Colour latencyTexture  = steel;

    juce::Colour colourFor (dsp::LatencyTier tier);
    juce::String labelFor (dsp::LatencyTier tier);

    //==============================================================================
    /**
        Ressources partagées : polices intégrées (licence OFL, voir assets/fonts) et textures.
        Créées une fois tant qu'une interface les tient (SharedResourcePointer), libérées avec
        elle : rien ne survit à l'arrêt de JUCE.
    */
    struct Resources
    {
        Resources();

        juce::Typeface::Ptr title, label, mono, segment7, segment14;
        juce::Image brushedMetal; // stries de métal brossé, en surimpression très légère
    };

    using SharedResources = juce::SharedResourcePointer<Resources>;

    /** Titres, noms de modules : Chakra Petch SemiBold. */
    juce::FontOptions titleFont (float height);
    /** Étiquettes, sérigraphies : Chakra Petch Medium. */
    juce::FontOptions labelFont (float height);
    /** Valeurs, données techniques, textes courants : IBM Plex Mono Medium. */
    juce::FontOptions monoFont (float height);
    /** Afficheurs segmentés : DSEG7 (chiffres) ou DSEG14 (lettres et chiffres). */
    juce::FontOptions segmentFont (float height, bool alphanumeric);

    //==============================================================================
    // Dessins réutilisables. Les coins sont chanfreinés (pièces usinées), jamais arrondis.

    juce::Path chamfered (juce::Rectangle<float> area, float chamfer);

    /** Plaque de métal vissée : dégradé, stries brossées, biseau. */
    void drawPlate (juce::Graphics& g, juce::Rectangle<float> area, float scale, juce::Colour base, float chamfer);

    /** Logement creusé (gorge sous un fader, fond de vumètre...). */
    void drawRecess (juce::Graphics& g, juce::Rectangle<float> area, float scale, float chamfer = 0.0f);

    /** Écran : cadre en retrait et verre sombre. Renvoie la zone utile, dans le verre. */
    juce::Rectangle<float> drawScreen (juce::Graphics& g, juce::Rectangle<float> area, float scale);

    /** Graticule d'oscilloscope : divisions et sous-divisions. */
    void drawGraticule (juce::Graphics& g, juce::Rectangle<float> area, int columns, int rows, float scale);

    void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle);

    /** Texte sérigraphié : léger relief, lisible sur métal. */
    void drawSilkscreen (juce::Graphics& g, const juce::String& label, juce::Rectangle<float> area,
                         const juce::FontOptions& font, juce::Justification justification,
                         juce::Colour colour = textDim);

    /** Témoin lumineux rond : verre éteint, lumière proportionnelle à level (0 à 1), halo. */
    void drawLed (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float level, float scale);

    /** Afficheur segmenté : segments éteints en fantôme, texte allumé par-dessus. */
    void drawSegments (juce::Graphics& g, const juce::String& content, juce::Rectangle<float> area, float height,
                       juce::Colour colour, juce::Justification justification, bool alphanumeric);

    /** Étiquette de latence : plaquette chanfreinée, bande de couleur de la catégorie, valeur. */
    void drawLatencyTag (juce::Graphics& g, juce::Rectangle<float> area, dsp::LatencyTier tier,
                         const juce::String& value, float scale);

    /** Largeur nécessaire à une étiquette de latence. */
    float latencyTagWidth (dsp::LatencyTier tier, const juce::String& value, float height);

    /** Bandes de signalisation (jaune et noir industriels), en fond d'une zone. */
    void drawHazardStripes (juce::Graphics& g, juce::Rectangle<float> area, float scale, juce::Colour colour);

    /** Ancien nom (cartes, application standalone). */
    inline void drawLatencyBadge (juce::Graphics& g, juce::Rectangle<float> area, dsp::LatencyTier tier,
                                  const juce::String& suffix, float scale)
    {
        drawLatencyTag (g, area, tier, suffix.trimCharactersAtStart (juce::String::fromUTF8 (" ·")), scale);
    }
}
