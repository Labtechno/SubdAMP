#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace subdamp::gui
{

/**
    Sélecteur de fichier d'un module : nom du fichier chargé (ou état « chargement »,
    « introuvable »...), bouton pour en choisir un, bouton pour le retirer.
*/
class FileSlot final : public juce::Component,
                       private juce::ChangeListener
{
public:
    enum class Kind
    {
        ampModel,       // capture .nam
        impulseResponse // IR de cab (.wav, .aif)
    };

    FileSlot (SubdAmpProcessor& processor, Kind kind);
    ~FileSlot() override;

    void setScale (float newScale);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void chooseFile();
    FileSlotStatus currentStatus() const;

    SubdAmpProcessor& subdAmp;
    Kind kind;

    juce::TextButton loadButton, clearButton;
    juce::Label nameLabel, detailLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> screenArea;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileSlot)
};

} // namespace subdamp::gui
