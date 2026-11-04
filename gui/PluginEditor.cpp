#include "gui/PluginEditor.h"

#include "gui/Theme.h"
#include "plugin/Parameters.h"

namespace subdamp::gui
{

//==============================================================================
/** Apparence provisoire : potards et boutons aux couleurs de theme.h. */
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
        setColour (juce::TextButton::buttonColourId, theme::panel);
        setColour (juce::TextButton::buttonOnColourId, theme::bypassed);
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::TextButton::textColourOnId, theme::text);
        setColour (juce::ComboBox::outlineColourId, theme::panelOutline);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto trackWidth = juce::jmax (2.0f, radius * 0.1f);
        const auto arcRadius = radius - trackWidth * 0.5f;
        const auto valueAngle = startAngle + sliderPos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::panelOutline);
        g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, valueAngle, true);
        g.setColour (theme::accent);
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto knobRadius = arcRadius - trackWidth * 1.8f;
        g.setColour (theme::panel.brighter (0.08f));
        g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

        const auto pointerStart = centre.getPointOnCircumference (knobRadius * 0.35f, valueAngle);
        const auto pointerEnd = centre.getPointOnCircumference (knobRadius * 0.9f, valueAngle);
        g.setColour (theme::text);
        g.drawLine ({ pointerStart, pointerEnd }, trackWidth * 0.8f);
    }
};

//==============================================================================
namespace
{
    void setupKnob (juce::Slider& knob)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f,
                                  true);
        knob.setDoubleClickReturnValue (true, 0.0);
    }

    void setupLabel (juce::Label& label, const char* utf8Text)
    {
        label.setText (juce::String::fromUTF8 (utf8Text), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, theme::textDim);
    }
}

SubdAmpEditor::SubdAmpEditor (SubdAmpProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      subdAmp (processorToEdit),
      lookAndFeel (std::make_unique<LookAndFeel>()),
      inputAttachment (processorToEdit.getParameters(), params::id::inputGain, inputKnob),
      outputAttachment (processorToEdit.getParameters(), params::id::outputGain, outputKnob),
      bypassAttachment (processorToEdit.getParameters(), params::id::bypass, bypassButton)
{
    setLookAndFeel (lookAndFeel.get());

    for (auto* knob : { &inputKnob, &outputKnob })
    {
        setupKnob (*knob);
        addAndMakeVisible (*knob);
    }

    setupLabel (inputLabel, "ENTRÉE");
    setupLabel (outputLabel, "SORTIE");
    addAndMakeVisible (inputLabel);
    addAndMakeVisible (outputLabel);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
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

//==============================================================================
void SubdAmpEditor::paint (juce::Graphics& g)
{
    const auto s = scale();
    auto area = getLocalBounds().toFloat();

    g.fillAll (theme::background);

    // En-tête
    auto header = area.removeFromTop (64.0f * s).reduced (24.0f * s, 0.0f);

    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (28.0f * s, juce::Font::bold));
    g.drawText ("SUBDAMP", header.removeFromLeft (170.0f * s), juce::Justification::centredLeft);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (13.0f * s));
    g.drawText (juce::String::fromUTF8 ("pitch · ampli · textures"),
                header.removeFromLeft (220.0f * s), juce::Justification::centredLeft);

    // Badge de latence de la chaîne (le son sec, toujours LIVE à cette étape).
    const auto sampleRate = subdAmp.getSampleRate();
    const auto latencyMs = sampleRate > 0.0 ? 1000.0 * subdAmp.getLatencySamples() / sampleRate : 0.0;
    const auto badge = juce::Rectangle<float> (118.0f * s, 26.0f * s)
                           .withCentre ({ bypassButton.getBounds().toFloat().getX() - 75.0f * s,
                                          bypassButton.getBounds().toFloat().getCentreY() });

    g.setColour (theme::latencyLive.withAlpha (0.15f));
    g.fillRoundedRectangle (badge, badge.getHeight() * 0.5f);
    g.setColour (theme::latencyLive);
    g.drawRoundedRectangle (badge, badge.getHeight() * 0.5f, 1.0f * s);
    g.setFont (juce::FontOptions (12.0f * s, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("LIVE · ") + juce::String (latencyMs, 1) + " ms",
                badge, juce::Justification::centred);

    // Zone centrale : emplacement de la future chaîne de modules.
    auto body = area.reduced (24.0f * s, 16.0f * s);
    body.removeFromLeft (150.0f * s);
    body.removeFromRight (150.0f * s);
    const auto chain = body.reduced (16.0f * s, 0.0f);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (chain, 10.0f * s);
    g.setColour (theme::panelOutline);
    g.drawRoundedRectangle (chain, 10.0f * s, 1.0f * s);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (14.0f * s));
    g.drawText (juce::String::fromUTF8 ("Chaîne de modules — à venir"), chain, juce::Justification::centred);
}

void SubdAmpEditor::resized()
{
    const auto s = scale();
    const auto px = [s] (float value) { return juce::roundToInt (value * s); };

    auto area = getLocalBounds();
    auto header = area.removeFromTop (px (64.0f)).reduced (px (24.0f), 0);
    bypassButton.setBounds (header.removeFromRight (px (100.0f)).withSizeKeepingCentre (px (100.0f), px (30.0f)));

    auto body = area.reduced (px (24.0f), px (16.0f));

    const auto layoutColumn = [&] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& knob,
                                   LevelMeter& meter, bool meterOnLeft)
    {
        label.setFont (juce::FontOptions (13.0f * s, juce::Font::bold));
        label.setBounds (column.removeFromTop (px (24.0f)));

        auto meterArea = meterOnLeft ? column.removeFromLeft (px (14.0f)) : column.removeFromRight (px (14.0f));
        meter.setBounds (meterArea.reduced (0, px (6.0f)));

        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (90.0f), px (22.0f));
        knob.setBounds (column.reduced (px (8.0f), 0).withSizeKeepingCentre (px (120.0f), px (140.0f)));
    };

    layoutColumn (body.removeFromLeft (px (150.0f)), inputLabel, inputKnob, inputMeter, true);
    layoutColumn (body.removeFromRight (px (150.0f)), outputLabel, outputKnob, outputMeter, false);
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
}

} // namespace subdamp::gui
