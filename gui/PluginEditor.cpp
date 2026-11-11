#include "gui/PluginEditor.h"

#include "gui/Theme.h"
#include "plugin/Parameters.h"

namespace subdamp::gui
{

namespace
{
    juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    juce::String milliseconds (double ms)
    {
        return (ms < 10.0 ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms))) + " ms";
    }

    /** Valeur pour un afficheur segmenté : une décimale sous 10, entier au-delà. */
    juce::String segmentValue (double value)
    {
        return value < 10.0 ? juce::String (value, 1) : juce::String (juce::roundToInt (value));
    }

    // Mise en page, à l'échelle 1.
    constexpr int headerHeight = 92;
    constexpr int stripWidth = 92;
    constexpr int boardHeight = 184;
    constexpr int margin = 14;
}

//==============================================================================
SubdAmpEditor::SubdAmpEditor (SubdAmpProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      subdAmp (processorToEdit),
      lookAndFeel (std::make_unique<LabLookAndFeel>()),
      scope (processorToEdit.getScope()),
      latencyReadout (utf8 ("LATENCE"), "ms", 4),
      textureReadout (utf8 ("TEXTURES"), "ms", 4),
      cpuReadout (utf8 ("CHARGE DSP"), "%", 3),
      ampFile (processorToEdit, FileSlot::Kind::ampModel),
      cabFile (processorToEdit, FileSlot::Kind::impulseResponse),
      pedalBoard (processorToEdit.getParameters(),
                  [this] (ModuleId module) { selectModule (module); },
                  [this] (ModuleId module, int newIndex) { moveModule (module, newIndex); }),
      inputAttachment (processorToEdit.getParameters(), params::id::inputGain, inputFader),
      outputAttachment (processorToEdit.getParameters(), params::id::outputGain, outputFader),
      bypassAttachment (processorToEdit.getParameters(), params::id::bypass, bypassButton)
{
    setLookAndFeel (lookAndFeel.get());

    // Faders d'entrée et de sortie
    for (auto* fader : { &inputFader, &outputFader })
    {
        fader->setSliderStyle (juce::Slider::LinearVertical);
        fader->setDoubleClickReturnValue (true, 0.0);
        fader->setTooltip (utf8 ("Double-clic : 0 dB"));
        addAndMakeVisible (*fader);
    }

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setClickingTogglesState (true);
    bypassButton.getProperties().set (LabLookAndFeel::styleProperty, "bypass");
    bypassButton.setTooltip (utf8 ("Contourne toute la chaîne (fondu sans clic)"));
    addAndMakeVisible (bypassButton);

    latencyReadout.setTooltip (utf8 ("Retard du son entendu : catégorie la plus lente parmi les modules mélangés, retards cumulés"));
    textureReadout.setTooltip (utf8 ("Retard des textures mélangées au son direct (le son direct ne les attend pas)"));
    cpuReadout.setTooltip (utf8 ("Part du temps de chaque bloc audio passée à calculer SubdAmp (mesure réelle)"));

    for (juce::Component* component : { static_cast<juce::Component*> (&inputMeter), static_cast<juce::Component*> (&outputMeter),
                                        static_cast<juce::Component*> (&scope), static_cast<juce::Component*> (&latencyReadout),
                                        static_cast<juce::Component*> (&textureReadout), static_cast<juce::Component*> (&cpuReadout),
                                        static_cast<juce::Component*> (&pedalBoard) })
        addAndMakeVisible (component);

    buildCards();

    shownOrder = subdAmp.getChainOrder();
    pedalBoard.setOrder (shownOrder);
    updateIdentities();
    shownLatency = subdAmp.getLatencyStatus();
    updateReadouts();
    selectModule (subdAmp.openModule);

    setResizable (true, true);
    setResizeLimits (baseWidth * 6 / 10, baseHeight * 6 / 10, baseWidth * 3 / 2, baseHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (baseWidth) / baseHeight);
    setSize (baseWidth, baseHeight);

    startTimerHz (30);
}

SubdAmpEditor::~SubdAmpEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SubdAmpEditor::buildCards()
{
    auto& parameters = subdAmp.getParameters();

    for (const auto& info : moduleInfos)
        cards[indexOf (info.id)] = std::make_unique<ModuleCard> (parameters, utf8 (info.name), info.tier, info.enabledParameter);

    auto& gate = cardFor (ModuleId::gate);
    gate.setDescription (utf8 ("Coupe le souffle entre les notes. Hystérésis et maintien : les fins de notes ne hachent pas. Le voyant s'allume quand il coupe."));
    gate.addKnob (params::id::gateThreshold, "SEUIL");
    gate.addKnob (params::id::gateRelease, utf8 ("RELÂCHE"));
    gate.addLamp (utf8 ("ÉTAT"), [this] { return subdAmp.getGateCutting(); }, theme::alert, "COUPE", "PASSE")
        .setTooltip (utf8 ("S'allume quand le gate coupe : pendant la relâche, puis entre les notes. Éteint quand le son passe."));

    auto& sub = cardFor (ModuleId::sub);
    sub.setDescription (utf8 ("Octaver analogique : une et deux octaves en dessous, une au-dessus. Sans latence, sur notes isolées. MIX 100 % : octaves seules."));
    sub.addKnob (params::id::subDown1, "-1 OCT");
    sub.addKnob (params::id::subDown2, "-2 OCT");
    sub.addKnob (params::id::subUp, "+1 OCT");
    sub.addKnob (params::id::subTone, utf8 ("TONALITÉ"));

    auto& whammy = cardFor (ModuleId::whammy);
    whammy.setDescription (utf8 ("Transposition jusqu'à ±2 octaves. En mode attaque, chaque coup de médiator relance le glissement."));
    whammy.addKnob (params::id::whInterval, "INTERVALLE");
    whammy.addChoice (params::id::whMode, "MODE", 240);
    whammy.addKnob (params::id::whGlide, "GLISSEMENT");
    whammy.addKnob (params::id::whSensitivity, utf8 ("SENSIBILITÉ"));
    whammy.setLatencyText ([this] { return utf8 (" · ") + milliseconds (subdAmp.getModuleLatencyMs (ModuleId::whammy)); });

    auto& harmonizer = cardFor (ModuleId::harmonizer);
    harmonizer.setDescription (utf8 ("Deux voix d'harmonie dans la tonalité, sur notes isolées. La part sèche du MIX reste sans latence."));
    harmonizer.addChoice (params::id::hmKey, utf8 ("TONALITÉ"), 116);
    harmonizer.addChoice (params::id::hmScale, "GAMME", 196);
    harmonizer.addChoice (params::id::hmVoice1, "VOIX 1", 118);
    harmonizer.addKnob (params::id::hmLevel1, "NIVEAU 1");
    harmonizer.addChoice (params::id::hmVoice2, "VOIX 2", 118);
    harmonizer.addKnob (params::id::hmLevel2, "NIVEAU 2");
    harmonizer.addKnob (params::id::hmFormant, "FORMANTS");
    harmonizer.setLatencyText ([this]
    {
        // La latence suit la note jouée : on affiche la plage tant qu'aucune note n'est suivie.
        const auto ms = subdAmp.getModuleLatencyMs (ModuleId::harmonizer);
        return ms > 0.0 ? utf8 (" · ") + milliseconds (ms) : utf8 (" · 5–25 ms");
    });

    auto& amp = cardFor (ModuleId::amp);
    amp.setDescription (utf8 ("Capture Neural Amp Modeler (.nam) et égaliseur. Volume normalisé d'une capture à l'autre."));
    amp.addKnob (params::id::ampGain, "GAIN");
    amp.addKnob (params::id::ampBass, "GRAVES");
    amp.addKnob (params::id::ampMid, utf8 ("MÉDIUMS"));
    amp.addKnob (params::id::ampTreble, "AIGUS");
    amp.addKnob (params::id::ampLevel, "NIVEAU");
    amp.setFooter (&ampFile, 380, 60);
    amp.setLatencyText ([this]
    {
        // Seulement quand la capture est rééchantillonnée (projet hors 48 kHz).
        const auto ms = subdAmp.getModuleLatencyMs (ModuleId::amp);
        return ms > 0.0 ? utf8 (" · ") + milliseconds (ms) : juce::String();
    });

    auto& cab = cardFor (ModuleId::cab);
    cab.setDescription (utf8 ("Réponse impulsionnelle de baffle (.wav, .aif). Sans fichier : 4×12 simulé."));
    cab.setFooter (&cabFile, 460, 64);

    auto& shimmer = cardFor (ModuleId::shimmer);
    shimmer.setDescription (utf8 ("Réverbe dont la queue monte d'un intervalle à chaque passage (ou monte et descend, en mode double) : la nappe s'élève. Stéréo."));
    shimmer.addChoice (params::id::shInterval, "INTERVALLE", 170);
    shimmer.addChoice (params::id::shMode, "MODE", 150);
    shimmer.addKnob (params::id::shDecay, utf8 ("DURÉE"));
    shimmer.addKnob (params::id::shAmount, "SHIMMER");
    shimmer.addKnob (params::id::shSize, "TAILLE");
    shimmer.addKnob (params::id::shPreDelay, utf8 ("PRÉ-DÉLAI"));
    shimmer.addKnob (params::id::shDiffusion, "DIFFUSION");
    shimmer.addKnob (params::id::shModRate, "VITESSE MOD");
    shimmer.addKnob (params::id::shModDepth, "PROF. MOD");
    shimmer.addKnob (params::id::shLowCut, "COUPE-BAS");
    shimmer.addKnob (params::id::shTone, "COUPE-HAUT");
    shimmer.addKnob (params::id::shWidth, "LARGEUR");

    auto& poly = cardFor (ModuleId::poly);
    poly.setDescription (utf8 ("Transposition polyphonique haute qualité, deux voix en stéréo. Accords bienvenus. À MIX 100 %, tout ce qui suit attend sa latence."));
    poly.addKnob (params::id::poInterval1, "INTERVALLE 1");
    poly.addKnob (params::id::poLevel1, "NIVEAU 1");
    poly.addKnob (params::id::poInterval2, "INTERVALLE 2");
    poly.addKnob (params::id::poLevel2, "NIVEAU 2");
    poly.addKnob (params::id::poFormant, "FORMANTS");
    poly.addKnob (params::id::poWidth, "LARGEUR");
    poly.setLatencyText ([this] { return utf8 (" · ") + milliseconds (subdAmp.getModuleLatencyMs (ModuleId::poly)); });

    auto& freeze = cardFor (ModuleId::freeze);
    freeze.setDescription (utf8 ("FIGER capture le son du moment et le tient en nappe infinie : rejoue par-dessus. Le MIX dose la nappe, le jeu passe toujours."));
    freeze.addToggle (params::id::frHold, "FIGER", 160);
    freeze.addKnob (params::id::frAttack, "ATTAQUE");
    freeze.addKnob (params::id::frRelease, utf8 ("RELÂCHE"));
    freeze.addKnob (params::id::frTone, utf8 ("TONALITÉ"));

    auto& granular = cardFor (ModuleId::granular);
    granular.setDescription (utf8 ("Grains des 8 dernières secondes : nuage dispersé, ou stutter calé sur le tempo (celui du DAW, sinon TEMPO)."));
    granular.addChoice (params::id::grMode, "MODE", 120);
    granular.addKnob (params::id::grSize, "TAILLE");
    granular.addKnob (params::id::grDensity, utf8 ("DENSITÉ"));
    granular.addKnob (params::id::grPitch, "HAUTEUR");
    granular.addKnob (params::id::grSpread, "DISPERSION");
    granular.addKnob (params::id::grReverse, utf8 ("INVERSÉS"));
    granular.addChoice (params::id::grDivision, "DIVISION", 120);
    granular.addKnob (params::id::grRepeats, utf8 ("RÉPÉT."));
    granular.addKnob (params::id::tempo, "TEMPO");

    auto& freqShift = cardFor (ModuleId::freqShift);
    freqShift.setDescription (utf8 ("Décale toutes les fréquences du même nombre de Hz : sons métalliques, cloches, spirales."));
    freqShift.addKnob (params::id::fsShift, utf8 ("DÉCALAGE"));
    freqShift.addKnob (params::id::fsFeedback, "RETOUR");

    // Chaque module : son MIX, isolé à droite (sauf le GATE, qui n'en a pas), et son écran à gauche.
    for (const auto& info : moduleInfos)
    {
        auto& card = cardFor (info.id);

        if (info.mixParameter != nullptr)
            card.setMixKnob (info.mixParameter);

        displays[indexOf (info.id)] = std::make_unique<ModuleDisplay> (info.id, subdAmp);
        card.setDisplay (displays[indexOf (info.id)].get());
    }

    for (auto& card : cards)
        addChildComponent (*card);
}

ModuleCard& SubdAmpEditor::cardFor (ModuleId module)
{
    return *cards[indexOf (module)];
}

void SubdAmpEditor::selectModule (ModuleId module)
{
    subdAmp.openModule = module;
    pedalBoard.setSelected (module);

    for (const auto& info : moduleInfos)
        cardFor (info.id).setVisible (info.id == module);

    cardFor (module).refreshLatency();
    cardFor (module).refreshLamps();
    displays[indexOf (module)]->refresh();
}

void SubdAmpEditor::moveModule (ModuleId module, int newIndex)
{
    subdAmp.moveModule (module, newIndex);
    shownOrder = subdAmp.getChainOrder();
    pedalBoard.setOrder (shownOrder);
    updateIdentities();
    selectModule (module); // la pédale déplacée devient celle dont on voit les réglages
}

void SubdAmpEditor::updateIdentities()
{
    // Plaque de chaque façade : référence du module et sa place réelle dans la chaîne.
    for (std::size_t i = 0; i < shownOrder.size(); ++i)
    {
        const auto& info = infoFor (shownOrder[i]);
        cardFor (info.id).setIdentity (info.code, static_cast<int> (i) + 1, static_cast<int> (numModules));
    }
}

void SubdAmpEditor::updateReadouts()
{
    using namespace theme;

    if (shownLatency.milliseconds > 0.0 || shownLatency.tier != dsp::LatencyTier::live)
        latencyReadout.setValue (segmentValue (shownLatency.milliseconds), colourFor (shownLatency.tier));
    else
        latencyReadout.setValue ("0.0", colourFor (shownLatency.tier));

    latencyReadout.setStatus (labelFor (shownLatency.tier), colourFor (shownLatency.tier));

    if (shownLatency.texturesActive)
    {
        textureReadout.setValue (segmentValue (shownLatency.textureMilliseconds), steel);
        textureReadout.setStatus ("TEXTURE", steel);
    }
    else
    {
        textureReadout.setValue ("--", textFaint);
        textureReadout.setStatus ("AUCUNE", textFaint);
    }

    const auto load = juce::roundToInt (smoothedCpu * 100.0);
    cpuReadout.setValue (juce::String (load), load >= 80 ? alert : (load >= 50 ? amber : medical));
}

float SubdAmpEditor::scale() const noexcept
{
    return static_cast<float> (getWidth()) / static_cast<float> (baseWidth);
}

//==============================================================================
void SubdAmpEditor::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto s = scale();
    g.fillAll (graphite);

    // Bandeau : plaque pleine largeur
    drawPlate (g, headerArea, s, anthracite, 6.0f * s);

    // Plaque de marque : nom gravé, désignation, numéro de série (version et format réels)
    {
        auto brand = brandArea;
        g.setColour (amber);
        g.fillRect (brand.removeFromLeft (4.0f * s).reduced (0.0f, 8.0f * s));
        brand.removeFromLeft (12.0f * s);

        drawSilkscreen (g, "SUBDAMP", brand.removeFromTop (brand.getHeight() * 0.52f).withTrimmedTop (4.0f * s),
                        titleFont (38.0f * s).withKerningFactor (0.08f), juce::Justification::bottomLeft, text);

        g.setColour (textDim);
        g.setFont (monoFont (10.5f * s));
        g.drawText (utf8 ("PROCESSEUR DE HAUTEUR · AMPLI · TEXTURES"), brand.removeFromTop (16.0f * s), juce::Justification::centredLeft, false);

        g.setColour (textFaint);
        g.setFont (monoFont (9.5f * s));
        // Numéro de série : codes fabricant et produit du plugin, version, format réellement chargé.
        auto serial = utf8 ("S/N SBDM-SDP1  ·  V") + juce::String (JucePlugin_VersionString);

        if (subdAmp.wrapperType != juce::AudioProcessor::wrapperType_Undefined)
            serial += utf8 ("  ·  ") + juce::String (juce::AudioProcessor::getWrapperTypeDescription (subdAmp.wrapperType)).toUpperCase();

        g.drawText (serial, brand.removeFromTop (14.0f * s), juce::Justification::centredLeft, false);
    }

    for (const auto& corner : { headerArea.getTopLeft(), headerArea.getTopRight(), headerArea.getBottomLeft(), headerArea.getBottomRight() })
        drawScrew (g, corner + juce::Point<float> (corner.x < headerArea.getCentreX() ? 10.0f : -10.0f,
                                                   corner.y < headerArea.getCentreY() ? 10.0f : -10.0f) * s, 3.4f * s, corner.x * 0.01f);

    // Flancs : entrée et sortie
    for (const auto& [strip, caption] : { std::pair { inputStrip, utf8 ("ENTRÉE") }, std::pair { outputStrip, utf8 ("SORTIE") } })
    {
        drawPlate (g, strip, s, anthracite, 6.0f * s);
        drawSilkscreen (g, caption, strip.withHeight (30.0f * s), labelFont (13.0f * s), juce::Justification::centred, textDim);

        for (const auto y : { strip.getY() + 10.0f * s, strip.getBottom() - 10.0f * s })
            for (const auto x : { strip.getX() + 10.0f * s, strip.getRight() - 10.0f * s })
                if (y > strip.getCentreY())
                    drawScrew (g, { x, y }, 3.0f * s, x * 0.02f);
    }

    // Baie du pédalier, en retrait
    drawRecess (g, bayArea, s, 6.0f * s);
}

void SubdAmpEditor::resized()
{
    const auto s = scale();
    const auto px = [s] (float value) { return juce::roundToInt (value * s); };

    auto area = getLocalBounds().reduced (px (margin));

    // Bandeau
    auto header = area.removeFromTop (px (headerHeight));
    headerArea = header.toFloat();
    area.removeFromTop (px (10.0f));

    auto headerContent = header.reduced (px (24.0f), px (12.0f));
    brandArea = headerContent.removeFromLeft (px (330.0f)).toFloat();

    bypassButton.setBounds (headerContent.removeFromRight (px (120.0f)).withSizeKeepingCentre (px (120.0f), px (44.0f)));
    headerContent.removeFromRight (px (14.0f));
    cpuReadout.setBounds (headerContent.removeFromRight (px (112.0f)));
    headerContent.removeFromRight (px (10.0f));
    textureReadout.setBounds (headerContent.removeFromRight (px (150.0f)));
    headerContent.removeFromRight (px (10.0f));
    latencyReadout.setBounds (headerContent.removeFromRight (px (170.0f)));
    headerContent.removeFromRight (px (12.0f));
    scope.setBounds (headerContent);

    for (auto* readout : { &latencyReadout, &textureReadout, &cpuReadout })
        readout->setScale (s);

    scope.setScale (s);

    // Flancs : fader et échelle de témoins
    const auto layoutStrip = [&] (juce::Rectangle<int> strip, juce::Slider& fader, LevelMeter& meter, bool meterOnLeft)
    {
        auto content = strip.reduced (px (8.0f), 0).withTrimmedTop (px (32.0f)).withTrimmedBottom (px (24.0f));
        auto meterArea = meterOnLeft ? content.removeFromLeft (px (14.0f)) : content.removeFromRight (px (14.0f));
        meter.setBounds (meterArea.withTrimmedBottom (px (30.0f)).withTrimmedTop (px (6.0f)));

        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, content.getWidth(), px (22.0f));
        fader.setBounds (content);
    };

    const auto inputArea = area.removeFromLeft (px (stripWidth));
    const auto outputArea = area.removeFromRight (px (stripWidth));
    inputStrip = inputArea.toFloat();
    outputStrip = outputArea.toFloat();
    layoutStrip (inputArea, inputFader, inputMeter, false);
    layoutStrip (outputArea, outputFader, outputMeter, true);

    area.reduce (px (12.0f), 0);

    // Baie du pédalier, puis façade du module ouvert
    auto bay = area.removeFromTop (px (boardHeight));
    bayArea = bay.toFloat();
    pedalBoard.setBounds (bay.reduced (px (4.0f), px (6.0f)));
    pedalBoard.setScale (s);

    area.removeFromTop (px (10.0f));

    for (auto& card : cards)
    {
        card->setBounds (area);
        card->setScale (s);
    }

    for (auto& display : displays)
        display->setScale (s);

    ampFile.setScale (s);
    cabFile.setScale (s);
}

//==============================================================================
void SubdAmpEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsed = lastTimerSeconds > 0.0 ? now - lastTimerSeconds : 0.0;
    lastTimerSeconds = now;

    auto& meters = subdAmp.getMeters();
    inputMeter.pushPeak (meters.inputPeak.exchange (0.0f, std::memory_order_relaxed), elapsed);
    outputMeter.pushPeak (meters.outputPeak.exchange (0.0f, std::memory_order_relaxed), elapsed);
    scope.refresh();

    // L'ordre a changé ailleurs (preset, session rechargée) : le pédalier suit l'ordre réel.
    if (const auto order = subdAmp.getChainOrder(); order != shownOrder && ! pedalBoard.isDragging())
    {
        shownOrder = order;
        pedalBoard.setOrder (shownOrder);
        updateIdentities();
    }

    pedalBoard.refreshMixes();

    // Latence et charge : lissage de la charge pour une lecture stable, comme un vrai afficheur.
    smoothedCpu += 0.1 * (subdAmp.getCpuLoad() - smoothedCpu);
    const auto latency = subdAmp.getLatencyStatus();

    if (latency.tier != shownLatency.tier || latency.texturesActive != shownLatency.texturesActive
        || std::abs (latency.milliseconds - shownLatency.milliseconds) >= 0.05
        || std::abs (latency.textureMilliseconds - shownLatency.textureMilliseconds) >= 0.5)
        shownLatency = latency;

    updateReadouts();

    auto& card = cardFor (subdAmp.openModule);
    card.refreshLatency();
    card.refreshLamps();
    displays[indexOf (subdAmp.openModule)]->refresh();
}

} // namespace subdamp::gui
