#include "gui/PluginEditor.h"

#include "gui/Theme.h"
#include "plugin/Parameters.h"

namespace subdamp::gui
{

namespace
{
    juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    std::atomic<float>& rawParameter (SubdAmpProcessor& processor, const char* parameterId)
    {
        return *processor.getParameters().getRawParameterValue (parameterId);
    }

    bool isAfterAmp (const std::atomic<float>& position)
    {
        return position.load (std::memory_order_relaxed) > 0.5f;
    }

    // Largeurs des cartes et gouttière, à l'échelle 1.
    constexpr int cardGap = 12;
    constexpr int ioColumnWidth = 100;
    constexpr int headerHeight = 64;
}

//==============================================================================
/** Apparence provisoire : potards et boutons aux couleurs de Theme.h. */
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
        setColour (juce::ComboBox::outlineColourId, theme::panelOutline);
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

        // Paramètres centrés sur zéro (gains en dB, décalage) : l'arc part du milieu.
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
        g.setColour (slider.isEnabled() ? theme::accent : theme::textDim);
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
      gateCard (processorToEdit.getParameters(), "GATE", dsp::LatencyTier::live, params::id::gateEnabled),
      subCard (processorToEdit.getParameters(), "SUB", dsp::LatencyTier::live, params::id::subEnabled, params::id::subPosition),
      ampCard (processorToEdit.getParameters(), "AMPLI", dsp::LatencyTier::live, params::id::ampEnabled),
      cabCard (processorToEdit.getParameters(), "CAB", dsp::LatencyTier::live, params::id::cabEnabled),
      shifterCard (processorToEdit.getParameters(), "FREQ SHIFT", dsp::LatencyTier::live, params::id::fsEnabled, params::id::fsPosition),
      inputAttachment (processorToEdit.getParameters(), params::id::inputGain, inputKnob),
      outputAttachment (processorToEdit.getParameters(), params::id::outputGain, outputKnob),
      bypassAttachment (processorToEdit.getParameters(), params::id::bypass, bypassButton),
      subPosition (rawParameter (processorToEdit, params::id::subPosition)),
      shifterPosition (rawParameter (processorToEdit, params::id::fsPosition))
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

    // Modules
    gateCard.addKnob (params::id::gateThreshold, "SEUIL");
    gateCard.addKnob (params::id::gateRelease, utf8 ("RELÂCHE"));
    gateCard.setKnobColumns (2);
    gateCard.setBaseWidth (140);

    subCard.addKnob (params::id::subDry, "DIRECT");
    subCard.addKnob (params::id::subDown1, "-1 OCT");
    subCard.addKnob (params::id::subDown2, "-2 OCT");
    subCard.addKnob (params::id::subUp, "+1 OCT");
    subCard.addKnob (params::id::subTone, utf8 ("TONALITÉ"));
    subCard.setKnobColumns (3);
    subCard.setBaseWidth (240);

    ampCard.addKnob (params::id::ampGain, "GAIN");
    ampCard.addKnob (params::id::ampBass, "GRAVES");
    ampCard.addKnob (params::id::ampMid, utf8 ("MÉDIUMS"));
    ampCard.addKnob (params::id::ampTreble, "AIGUS");
    ampCard.addKnob (params::id::ampLevel, "NIVEAU");
    ampCard.setKnobColumns (3);
    ampCard.setFooter (&ampFile, 56);
    ampCard.setBaseWidth (300);

    cabCard.setFooter (&cabFile, 96);
    cabCard.setBaseWidth (190);

    shifterCard.addKnob (params::id::fsShift, utf8 ("DÉCALAGE"));
    shifterCard.addKnob (params::id::fsMix, "MIX");
    shifterCard.addKnob (params::id::fsFeedback, "RETOUR");
    shifterCard.setKnobColumns (3);
    shifterCard.setBaseWidth (210);

    for (auto* card : { &gateCard, &subCard, &ampCard, &cabCard, &shifterCard })
        addAndMakeVisible (*card);

    layoutPositions = { isAfterAmp (subPosition), isAfterAmp (shifterPosition) };

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

float SubdAmpEditor::scale() const noexcept
{
    return static_cast<float> (getWidth()) / static_cast<float> (baseWidth);
}

std::vector<ModuleCard*> SubdAmpEditor::chainOrder()
{
    // Même ordre que le traitement : gate → [avant l'ampli : SUB, FREQ SHIFT] → ampli → cab → [après].
    std::vector<ModuleCard*> order { &gateCard };

    const auto addPitchModules = [&] (bool afterAmp)
    {
        if (layoutPositions.first == afterAmp)
            order.push_back (&subCard);

        if (layoutPositions.second == afterAmp)
            order.push_back (&shifterCard);
    };

    addPitchModules (false);
    order.push_back (&ampCard);
    order.push_back (&cabCard);
    addPitchModules (true);

    return order;
}

//==============================================================================
void SubdAmpEditor::paint (juce::Graphics& g)
{
    const auto s = scale();
    auto area = getLocalBounds().toFloat();

    g.fillAll (theme::background);

    // En-tête
    auto header = area.removeFromTop (headerHeight * s).reduced (24.0f * s, 0.0f);

    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (28.0f * s, juce::Font::bold));
    g.drawText ("SUBDAMP", header.removeFromLeft (170.0f * s), juce::Justification::centredLeft);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f * s));
    g.drawText (utf8 ("pitch · ampli · textures"), header.removeFromLeft (220.0f * s), juce::Justification::centredLeft);

    // Latence de la chaîne principale (le son sec) : reste LIVE tant qu'elle est sous 3 ms.
    const auto sampleRate = subdAmp.getSampleRate();
    const auto latencyMs = sampleRate > 0.0 ? 1000.0 * subdAmp.getLatencySamples() / sampleRate : 0.0;
    const auto tier = latencyMs < 3.0 ? dsp::LatencyTier::live : dsp::LatencyTier::playable;
    const auto badge = juce::Rectangle<float> (128.0f * s, 26.0f * s)
                           .withCentre ({ static_cast<float> (bypassButton.getX()) - 80.0f * s,
                                          static_cast<float> (bypassButton.getBounds().getCentreY()) });

    theme::drawLatencyBadge (g, badge, tier, utf8 (" · ") + juce::String (latencyMs, 1) + " ms", s);

    // Fil du signal, de l'entrée à la sortie, derrière les cartes.
    const auto order = chainOrder();
    const auto lineY = static_cast<float> (order.front()->getBounds().getCentreY());
    g.setColour (theme::panelOutline.brighter (0.2f));
    g.drawLine (static_cast<float> (inputKnob.getRight()), lineY, static_cast<float> (outputKnob.getX()), lineY, 2.0f * s);

    for (std::size_t i = 1; i < order.size(); ++i)
    {
        const auto x = (static_cast<float> (order[i - 1]->getRight()) + static_cast<float> (order[i]->getX())) * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (x - 3.0f * s, lineY - 5.0f * s);
        chevron.lineTo (x + 3.0f * s, lineY);
        chevron.lineTo (x - 3.0f * s, lineY + 5.0f * s);
        g.setColour (theme::accent.withAlpha (0.8f));
        g.strokePath (chevron, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
}

void SubdAmpEditor::resized()
{
    const auto s = scale();
    const auto px = [s] (float value) { return juce::roundToInt (value * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px (headerHeight)).reduced (px (24.0f), 0);
    bypassButton.setBounds (header.removeFromRight (px (100.0f)).withSizeKeepingCentre (px (100.0f), px (30.0f)));

    auto body = area.reduced (px (20.0f), px (12.0f));

    const auto layoutIoColumn = [&] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& knob,
                                     LevelMeter& meter, bool meterOnLeft)
    {
        label.setFont (juce::FontOptions (13.0f * s, juce::Font::bold));
        label.setBounds (column.removeFromTop (px (24.0f)));

        auto meterArea = meterOnLeft ? column.removeFromLeft (px (12.0f)) : column.removeFromRight (px (12.0f));
        meter.setBounds (meterArea.reduced (0, px (6.0f)));

        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (80.0f), px (20.0f));
        knob.setBounds (column.withSizeKeepingCentre (column.getWidth() - px (8.0f), px (110.0f)));
    };

    layoutIoColumn (body.removeFromLeft (px (ioColumnWidth)), inputLabel, inputKnob, inputMeter, true);
    layoutIoColumn (body.removeFromRight (px (ioColumnWidth)), outputLabel, outputKnob, outputMeter, false);

    // Chaîne de modules, centrée dans l'espace restant.
    auto chain = body.reduced (px (cardGap), 0);
    const auto order = chainOrder();

    int totalWidth = 0;
    for (auto* card : order)
        totalWidth += px (static_cast<float> (card->getBaseWidth()));
    totalWidth += px (cardGap) * static_cast<int> (order.size() - 1);

    auto x = chain.getX() + juce::jmax (0, (chain.getWidth() - totalWidth) / 2);

    for (auto* card : order)
    {
        const auto width = px (static_cast<float> (card->getBaseWidth()));
        card->setBounds (x, chain.getY(), width, chain.getHeight());
        card->setScale (s);
        x += width + px (cardGap);
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

    // Un module de pitch a changé de place (interface ou automation) : on réordonne les cartes.
    const std::pair positions { isAfterAmp (subPosition), isAfterAmp (shifterPosition) };

    if (positions != layoutPositions)
    {
        layoutPositions = positions;
        resized();
        repaint();
    }

    // La latence change quand une capture est rééchantillonnée : mise à jour du badge.
    if (const auto latency = subdAmp.getLatencySamples(); latency != displayedLatency)
    {
        displayedLatency = latency;
        repaint();
    }
}

} // namespace subdamp::gui
