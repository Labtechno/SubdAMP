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

    // Mise en page, à l'échelle 1.
    constexpr int headerHeight = 64;
    constexpr int boardHeight = 180;
    constexpr int ioColumnWidth = 110;
    constexpr int margin = 20;
}

//==============================================================================
/** Apparence provisoire : potards, listes et boutons aux couleurs de Theme.h. */
class SubdAmpEditor::LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme::background);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::Slider::textBoxTextColourId, theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, theme::bypassed);
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::TextButton::textColourOnId, theme::text);
        setColour (juce::ComboBox::backgroundColourId, theme::panelRaised);
        setColour (juce::ComboBox::outlineColourId, theme::panelOutline);
        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::ComboBox::arrowColourId, theme::accent);
        setColour (juce::PopupMenu::backgroundColourId, theme::panelRaised);
        setColour (juce::PopupMenu::textColourId, theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.3f));
        setColour (juce::PopupMenu::highlightedTextColourId, theme::text);
        setColour (juce::TooltipWindow::backgroundColourId, theme::panelRaised);
        setColour (juce::TooltipWindow::textColourId, theme::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto trackWidth = juce::jmax (2.0f, radius * 0.12f);
        const auto arcRadius = radius - trackWidth * 0.5f;
        const auto valueAngle = startAngle + sliderPos * (endAngle - startAngle);

        // Paramètres centrés sur zéro (gains en dB, intervalles, décalage) : l'arc part du milieu.
        const auto range = slider.getRange();
        const auto isBipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const auto arcStart = isBipolar ? startAngle + static_cast<float> (slider.valueToProportionOfLength (0.0)) * (endAngle - startAngle)
                                        : startAngle;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::panelOutline);
        g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (arcStart, valueAngle), juce::jmax (arcStart, valueAngle), true);
        g.setColour (theme::accent);
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto knobRadius = arcRadius - trackWidth * 1.6f;
        g.setColour (theme::panelRaised.brighter (0.08f));
        g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

        const auto pointerStart = centre.getPointOnCircumference (knobRadius * 0.35f, valueAngle);
        const auto pointerEnd = centre.getPointOnCircumference (knobRadius * 0.9f, valueAngle);
        g.setColour (theme::text);
        g.drawLine ({ pointerStart, pointerEnd }, trackWidth * 0.8f);
    }
};

//==============================================================================
SubdAmpEditor::SubdAmpEditor (SubdAmpProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      subdAmp (processorToEdit),
      lookAndFeel (std::make_unique<LookAndFeel>()),
      ampFile (processorToEdit, FileSlot::Kind::ampModel),
      cabFile (processorToEdit, FileSlot::Kind::impulseResponse),
      pedalBoard (processorToEdit.getParameters(),
                  [this] (ModuleId module) { selectModule (module); },
                  [this] (ModuleId module, int newIndex) { moveModule (module, newIndex); }),
      inputAttachment (processorToEdit.getParameters(), params::id::inputGain, inputKnob),
      outputAttachment (processorToEdit.getParameters(), params::id::outputGain, outputKnob),
      bypassAttachment (processorToEdit.getParameters(), params::id::bypass, bypassButton)
{
    setLookAndFeel (lookAndFeel.get());

    // Entrée / sortie
    for (auto* knob : { &inputKnob, &outputKnob })
    {
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob->setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (*knob);
    }

    for (auto [label, text] : { std::pair { &inputLabel, "ENTRÉE" }, std::pair { &outputLabel, "SORTIE" } })
    {
        label->setText (utf8 (text), juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (*label);
    }

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (utf8 ("Contourne toute la chaîne (fondu sans clic)"));
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (pedalBoard);

    buildCards();

    shownOrder = subdAmp.getChainOrder();
    pedalBoard.setOrder (shownOrder);
    shownLatency = subdAmp.getLatencyStatus();
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
    gate.addLamp (utf8 ("ÉTAT"), [this] { return subdAmp.getGateCutting(); }, theme::error, "COUPE", "PASSE")
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
    shimmer.setDescription (utf8 ("Réverbe dont la queue monte d'un intervalle à chaque passage : la nappe s'élève. Stéréo."));
    shimmer.addChoice (params::id::shInterval, "INTERVALLE", 180);
    shimmer.addKnob (params::id::shDecay, utf8 ("DURÉE"));
    shimmer.addKnob (params::id::shAmount, "SHIMMER");
    shimmer.addKnob (params::id::shTone, utf8 ("TONALITÉ"));

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

    // Chaque module : son MIX, isolé à droite du panneau (sauf le GATE, qui n'en a pas).
    for (const auto& info : moduleInfos)
        if (info.mixParameter != nullptr)
            cardFor (info.id).setMixKnob (info.mixParameter);

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
}

void SubdAmpEditor::moveModule (ModuleId module, int newIndex)
{
    subdAmp.moveModule (module, newIndex);
    shownOrder = subdAmp.getChainOrder();
    pedalBoard.setOrder (shownOrder);
    selectModule (module); // la pédale déplacée devient celle dont on voit les réglages
}

float SubdAmpEditor::scale() const noexcept
{
    return static_cast<float> (getWidth()) / static_cast<float> (baseWidth);
}

//==============================================================================
void SubdAmpEditor::paint (juce::Graphics& g)
{
    const auto s = scale();
    auto area = getLocalBounds().toFloat();

    g.fillAll (theme::background);

    auto header = area.removeFromTop (headerHeight * s).reduced (margin * s, 0.0f);

    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (28.0f * s, juce::Font::bold));
    g.drawText ("SUBDAMP", header.removeFromLeft (170.0f * s), juce::Justification::centredLeft);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f * s));
    g.drawText (utf8 ("pitch · ampli · textures"), header.removeFromLeft (220.0f * s), juce::Justification::centredLeft);

    // Latence de ce que tu entends : la catégorie la plus lente parmi les modules allumés.
    const auto badge = juce::Rectangle<float> (150.0f * s, 26.0f * s)
                           .withCentre ({ static_cast<float> (bypassButton.getX()) - 95.0f * s,
                                          static_cast<float> (bypassButton.getBounds().getCentreY()) });

    theme::drawLatencyBadge (g, badge, shownLatency.tier, utf8 (" · ") + milliseconds (shownLatency.milliseconds), s);

    // Textures mélangées : second badge, leur retard est à part (le son direct ne les attend pas).
    if (shownLatency.texturesActive)
    {
        const auto suffix = shownLatency.textureMilliseconds > 0.0 ? utf8 (" · ") + milliseconds (shownLatency.textureMilliseconds)
                                                                   : juce::String();
        theme::drawLatencyBadge (g, badge.translated (-162.0f * s, 0.0f), dsp::LatencyTier::texture, suffix, s);
    }

    // Fond de la zone du pédalier
    const auto board = area.removeFromTop (boardHeight * s).reduced (margin * s, 6.0f * s);
    g.setColour (theme::panel.withAlpha (0.5f));
    g.fillRoundedRectangle (board, 12.0f * s);
}

void SubdAmpEditor::resized()
{
    const auto s = scale();
    const auto px = [s] (float value) { return juce::roundToInt (value * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px (headerHeight)).reduced (px (margin), 0);
    bypassButton.setBounds (header.removeFromRight (px (100.0f)).withSizeKeepingCentre (px (100.0f), px (30.0f)));

    // Rangée du haut : entrée, pédalier, sortie
    auto row = area.removeFromTop (px (boardHeight)).reduced (px (margin), px (6.0f));

    const auto layoutIoColumn = [&] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& knob,
                                     LevelMeter& meter, bool meterOnLeft)
    {
        column = column.reduced (px (8.0f), px (8.0f));
        label.setFont (juce::FontOptions (12.0f * s, juce::Font::bold));
        label.setBounds (column.removeFromTop (px (20.0f)));

        auto meterArea = meterOnLeft ? column.removeFromLeft (px (10.0f)) : column.removeFromRight (px (10.0f));
        meter.setBounds (meterArea.reduced (0, px (4.0f)));

        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (80.0f), px (20.0f));
        knob.setBounds (column.reduced (px (4.0f), 0));
    };

    layoutIoColumn (row.removeFromLeft (px (ioColumnWidth)), inputLabel, inputKnob, inputMeter, true);
    layoutIoColumn (row.removeFromRight (px (ioColumnWidth)), outputLabel, outputKnob, outputMeter, false);

    pedalBoard.setBounds (row);
    pedalBoard.setScale (s);

    // Panneau du module ouvert
    const auto panel = area.reduced (px (margin), px (12.0f)).withTrimmedBottom (px (8.0f));

    for (auto& card : cards)
    {
        card->setBounds (panel);
        card->setScale (s);
    }

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

    // L'ordre a changé ailleurs (preset, session rechargée) : le pédalier suit l'ordre réel.
    if (const auto order = subdAmp.getChainOrder(); order != shownOrder && ! pedalBoard.isDragging())
    {
        shownOrder = order;
        pedalBoard.setOrder (shownOrder);
    }

    pedalBoard.refreshMixes();

    // Latence affichée : mise à jour quand elle change de façon visible.
    const auto latency = subdAmp.getLatencyStatus();

    if (latency.tier != shownLatency.tier || latency.texturesActive != shownLatency.texturesActive
        || std::abs (latency.milliseconds - shownLatency.milliseconds) >= 0.5
        || std::abs (latency.textureMilliseconds - shownLatency.textureMilliseconds) >= 0.5)
    {
        shownLatency = latency;
        repaint (getLocalBounds().withHeight (juce::roundToInt (headerHeight * scale())));
    }

    cardFor (subdAmp.openModule).refreshLatency();
    cardFor (subdAmp.openModule).refreshLamps();
}

} // namespace subdamp::gui
