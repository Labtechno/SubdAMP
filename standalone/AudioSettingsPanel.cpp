#include "standalone/AudioSettingsPanel.h"

#include "gui/Theme.h"

namespace subdamp::standalone
{

namespace theme = gui::theme;

namespace
{
    juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    void fillBox (juce::ComboBox& box, const juce::StringArray& items, int selectedIndex)
    {
        box.clear (juce::dontSendNotification);

        for (int i = 0; i < items.size(); ++i)
            box.addItem (items[i], i + 1);

        if (juce::isPositiveAndBelow (selectedIndex, items.size()))
            box.setSelectedId (selectedIndex + 1, juce::dontSendNotification);
    }

    constexpr int rowHeight = 30;
    constexpr int rowGap = 8;
    constexpr int labelWidth = 170;
    constexpr int margin = 20;
}

AudioSettingsPanel::AudioSettingsPanel (StandaloneHost& hostToControl)
    : host (hostToControl)
{
    addRow (driverRow, "Pilote");
    addRow (inputDeviceRow, "Interface d'entrée");
    addRow (outputDeviceRow, "Interface");
    addRow (guitarRow, "Entrée guitare");
    addRow (outputRow, "Sortie");
    addRow (sampleRateRow, "Fréquence");
    addRow (bufferRow, "Buffer");

    driverRow.box.onChange = [this] { onDriverChosen(); };
    inputDeviceRow.box.onChange = [this] { onDeviceChosen(); };
    outputDeviceRow.box.onChange = [this] { onDeviceChosen(); };

    guitarRow.box.onChange = [this]
    {
        report (host.selectGuitarInput (guitarRow.box.getSelectedItemIndex()));
    };

    outputRow.box.onChange = [this]
    {
        const auto index = outputRow.box.getSelectedItemIndex();

        if (juce::isPositiveAndBelow (index, outputStarts.size()))
            report (host.selectOutputChannels (outputStarts[index]));
    };

    sampleRateRow.box.onChange = [this]
    {
        const auto index = sampleRateRow.box.getSelectedItemIndex();

        if (juce::isPositiveAndBelow (index, sampleRates.size()))
            report (host.selectSampleRate (sampleRates[index]));
    };

    bufferRow.box.onChange = [this]
    {
        const auto index = bufferRow.box.getSelectedItemIndex();

        if (juce::isPositiveAndBelow (index, bufferSizes.size()))
            report (host.selectBufferSize (bufferSizes[index]));
    };

    controlPanelButton.setButtonText (utf8 ("Panneau de contrôle du pilote"));
    controlPanelButton.onClick = [this]
    {
        auto& deviceManager = host.getDeviceManager();

        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            // Le panneau du pilote (ASIO) peut changer buffer ou fréquence : on rouvre le périphérique.
            if (device->showControlPanel())
            {
                deviceManager.closeAudioDevice();
                deviceManager.restartLastAudioDevice();
            }
        }
    };
    addAndMakeVisible (controlPanelButton);

    addAndMakeVisible (guitarMeter);

    latencyLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (latencyLabel);

    statusLabel.setColour (juce::Label::textColourId, theme::meterHigh);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statusLabel);

    host.getDeviceManager().addChangeListener (this);
    refresh();

    startTimerHz (30);
}

AudioSettingsPanel::~AudioSettingsPanel()
{
    stopTimer();
    host.getDeviceManager().removeChangeListener (this);
}

void AudioSettingsPanel::addRow (Row& row, const char* utf8Label)
{
    row.label.setText (utf8 (utf8Label), juce::dontSendNotification);
    row.label.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (row.label);
    addAndMakeVisible (row.box);
}

//==============================================================================
void AudioSettingsPanel::refresh()
{
    auto& deviceManager = host.getDeviceManager();
    auto* currentType = deviceManager.getCurrentDeviceTypeObject();
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto setup = deviceManager.getAudioDeviceSetup();

    // Pilotes
    juce::StringArray typeNames;
    int currentTypeIndex = -1;

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type == currentType)
            currentTypeIndex = typeNames.size();

        typeNames.add (type->getTypeName());
    }

    fillBox (driverRow.box, typeNames, currentTypeIndex);

    // Interfaces : ASIO n'a qu'un seul périphérique pour l'entrée et la sortie.
    const auto separate = currentType != nullptr && currentType->hasSeparateInputsAndOutputs();

    inputDeviceRow.label.setVisible (separate);
    inputDeviceRow.box.setVisible (separate);
    outputDeviceRow.label.setText (separate ? utf8 ("Interface de sortie") : utf8 ("Interface"), juce::dontSendNotification);

    if (currentType != nullptr)
    {
        const auto inputNames = currentType->getDeviceNames (true);
        const auto outputNames = currentType->getDeviceNames (false);
        fillBox (inputDeviceRow.box, inputNames, inputNames.indexOf (setup.inputDeviceName));
        fillBox (outputDeviceRow.box, outputNames, outputNames.indexOf (setup.outputDeviceName));
    }

    // Canaux, fréquence, buffer
    juce::StringArray guitarItems, outputItems, rateItems, bufferItems;
    outputStarts.clearQuick();
    sampleRates.clearQuick();
    bufferSizes.clearQuick();
    int selectedOutput = -1, selectedRate = -1, selectedBuffer = -1;

    if (device != nullptr)
    {
        guitarItems = device->getInputChannelNames();

        const auto outputNames = device->getOutputChannelNames();
        const auto firstOutput = host.getFirstOutputChannel();

        for (int i = 0; i < outputNames.size(); i += 2)
        {
            if (firstOutput == i)
                selectedOutput = outputStarts.size();

            outputStarts.add (i);
            outputItems.add (i + 1 < outputNames.size() ? outputNames[i] + " + " + outputNames[i + 1]
                                                        : outputNames[i]);
        }

        sampleRates = device->getAvailableSampleRates();

        for (const auto rate : sampleRates)
        {
            if (juce::approximatelyEqual (rate, device->getCurrentSampleRate()))
                selectedRate = rateItems.size();

            rateItems.add (juce::String (juce::roundToInt (rate)) + " Hz");
        }

        bufferSizes = device->getAvailableBufferSizes();

        for (const auto size : bufferSizes)
        {
            if (size == device->getCurrentBufferSizeSamples())
                selectedBuffer = bufferItems.size();

            const auto ms = device->getCurrentSampleRate() > 0.0 ? 1000.0 * size / device->getCurrentSampleRate() : 0.0;
            bufferItems.add (juce::String (size) + utf8 (" éch.  (") + juce::String (ms, 1) + " ms)");
        }
    }

    fillBox (guitarRow.box, guitarItems, host.getGuitarInputChannel());
    fillBox (outputRow.box, outputItems, selectedOutput);
    fillBox (sampleRateRow.box, rateItems, selectedRate);
    fillBox (bufferRow.box, bufferItems, selectedBuffer);

    controlPanelButton.setVisible (device != nullptr && device->hasControlPanel());

    latencyLabel.setText (device != nullptr
                              ? utf8 ("Latence aller-retour estimée : ≈ ") + juce::String (host.getRoundTripLatencyMs(), 1) + " ms"
                              : utf8 ("Aucune carte son ouverte"),
                          juce::dontSendNotification);

    // La hauteur suit les lignes affichées (l'entrée séparée et le panneau du pilote dépendent du pilote).
    const auto visibleRows = inputDeviceRow.box.isVisible() ? 7 : 6;
    const auto panelButtonHeight = controlPanelButton.isVisible() ? rowHeight + rowGap : 0;
    const auto statusHeight = 40;

    setSize (540, margin * 2 + visibleRows * (rowHeight + rowGap) + rowGap + panelButtonHeight + rowHeight + statusHeight);
    resized();
}

void AudioSettingsPanel::report (const juce::String& error)
{
    statusLabel.setText (error, juce::dontSendNotification);
}

void AudioSettingsPanel::onDriverChosen()
{
    const auto typeName = driverRow.box.getText();

    if (typeName.isNotEmpty() && typeName != host.getDeviceManager().getCurrentAudioDeviceType())
        report (host.selectDriver (typeName));
}

void AudioSettingsPanel::onDeviceChosen()
{
    const auto outputName = outputDeviceRow.box.getText();
    const auto inputName = inputDeviceRow.box.isVisible() ? inputDeviceRow.box.getText() : outputName;

    report (host.selectDevices (inputName, outputName));
}

void AudioSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void AudioSettingsPanel::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsed = lastTimerSeconds > 0.0 ? now - lastTimerSeconds : 0.0;
    lastTimerSeconds = now;

    guitarMeter.pushPeak (host.popInputPeak(), elapsed);
}

//==============================================================================
void AudioSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void AudioSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (margin);

    const auto placeRow = [&] (Row& row, bool withMeter)
    {
        if (! row.box.isVisible())
            return;

        auto line = area.removeFromTop (rowHeight);
        area.removeFromTop (rowGap);

        row.label.setBounds (line.removeFromLeft (labelWidth));

        if (withMeter)
        {
            guitarMeter.setBounds (line.removeFromRight (12));
            line.removeFromRight (8);
        }

        row.box.setBounds (line);
    };

    placeRow (driverRow, false);
    placeRow (inputDeviceRow, false);
    placeRow (outputDeviceRow, false);
    placeRow (guitarRow, true);
    placeRow (outputRow, false);
    placeRow (sampleRateRow, false);
    placeRow (bufferRow, false);

    area.removeFromTop (rowGap);

    if (controlPanelButton.isVisible())
    {
        controlPanelButton.setBounds (area.removeFromTop (rowHeight).withTrimmedLeft (labelWidth));
        area.removeFromTop (rowGap);
    }

    latencyLabel.setBounds (area.removeFromTop (rowHeight));
    statusLabel.setBounds (area);
}

} // namespace subdamp::standalone
