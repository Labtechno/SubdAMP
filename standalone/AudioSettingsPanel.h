#pragma once

#include "gui/LevelMeter.h"
#include "standalone/StandaloneHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace subdamp::standalone
{

/**
    Réglages audio de l'application standalone : pilote, interface, canal guitare,
    sorties, fréquence, buffer, latence estimée et vumètre du signal guitare brut
    (pour vérifier d'un coup d'œil que le bon canal est sélectionné).
*/
class AudioSettingsPanel final : public juce::Component,
                                 private juce::ChangeListener,
                                 private juce::Timer
{
public:
    explicit AudioSettingsPanel (StandaloneHost& hostToControl);
    ~AudioSettingsPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label label;
        juce::ComboBox box;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refresh();
    void report (const juce::String& error);
    void addRow (Row& row, const char* utf8Label);

    void onDriverChosen();
    void onDeviceChosen();

    StandaloneHost& host;

    Row driverRow, inputDeviceRow, outputDeviceRow, guitarRow, outputRow, sampleRateRow, bufferRow;
    gui::LevelMeter guitarMeter;
    juce::TextButton controlPanelButton;
    juce::Label latencyLabel, statusLabel;

    juce::Array<double> sampleRates;
    juce::Array<int> bufferSizes;
    juce::Array<int> outputStarts;

    double lastTimerSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};

} // namespace subdamp::standalone
