#include "gui/FileSlot.h"

#include "gui/Theme.h"

namespace subdamp::gui
{

namespace
{
    juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
}

FileSlot::FileSlot (SubdAmpProcessor& processor, Kind slotKind)
    : subdAmp (processor),
      kind (slotKind)
{
    loadButton.setButtonText (utf8 ("Charger…"));
    loadButton.setTooltip (kind == Kind::ampModel ? utf8 ("Choisir une capture d'ampli (.nam)")
                                                  : utf8 ("Choisir une réponse impulsionnelle de cab (.wav, .aif)"));
    loadButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (loadButton);

    clearButton.setButtonText (utf8 ("✕"));
    clearButton.setTooltip (kind == Kind::ampModel ? utf8 ("Retirer la capture") : utf8 ("Revenir au cab interne"));
    clearButton.onClick = [this]
    {
        if (kind == Kind::ampModel)
            subdAmp.clearAmpModel();
        else
            subdAmp.clearCabImpulseResponse();
    };
    addAndMakeVisible (clearButton);

    nameLabel.setMinimumHorizontalScale (0.7f);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    detailLabel.setMinimumHorizontalScale (0.7f);
    detailLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (detailLabel);

    subdAmp.getFileStatusBroadcaster().addChangeListener (this);
    refresh();
}

FileSlot::~FileSlot()
{
    subdAmp.getFileStatusBroadcaster().removeChangeListener (this);
}

FileSlotStatus FileSlot::currentStatus() const
{
    return kind == Kind::ampModel ? subdAmp.getAmpStatus() : subdAmp.getCabStatus();
}

void FileSlot::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void FileSlot::refresh()
{
    const auto status = currentStatus();
    const auto hasFile = status.fileName.isNotEmpty();

    if (hasFile)
        nameLabel.setText (juce::File::createFileWithoutCheckingPath (status.fileName).getFileNameWithoutExtension(),
                           juce::dontSendNotification);
    else
        nameLabel.setText (kind == Kind::ampModel ? utf8 ("Aucune capture") : utf8 ("Cab interne"),
                           juce::dontSendNotification);

    nameLabel.setColour (juce::Label::textColourId, hasFile ? theme::text : theme::textDim);

    juce::String detail;

    if (status.message.isNotEmpty())
        detail = status.message;
    else if (! hasFile)
        detail = kind == Kind::ampModel ? utf8 ("Le signal traverse l'ampli sans modification")
                                        : utf8 ("Simulation 4×12 par filtres");

    detailLabel.setText (detail, juce::dontSendNotification);
    detailLabel.setColour (juce::Label::textColourId, status.hasError ? theme::error : theme::textDim);
    detailLabel.setTooltip (status.hasError ? status.message : juce::String());

    clearButton.setEnabled (hasFile);
}

void FileSlot::chooseFile()
{
    const auto current = kind == Kind::ampModel ? subdAmp.getAmpModelFile() : subdAmp.getCabImpulseResponseFile();
    const auto startFolder = current.getParentDirectory().isDirectory()
                                 ? current.getParentDirectory()
                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> (kind == Kind::ampModel ? utf8 ("Choisir une capture d'ampli")
                                                                          : utf8 ("Choisir une IR de cab"),
                                                   startFolder,
                                                   kind == Kind::ampModel ? "*.nam" : "*.wav;*.aif;*.aiff");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<FileSlot> (this)] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr)
                                  return;

                              const auto file = fc.getResult();

                              if (file == juce::File())
                                  return; // annulé

                              if (safeThis->kind == Kind::ampModel)
                                  safeThis->subdAmp.loadAmpModel (file);
                              else
                                  safeThis->subdAmp.loadCabImpulseResponse (file);
                          });
}

void FileSlot::setScale (float newScale)
{
    scale = newScale;
    resized();
}

void FileSlot::paint (juce::Graphics& g)
{
    g.setColour (theme::background.withAlpha (0.6f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f * scale);
}

void FileSlot::resized()
{
    const auto px = [this] (float value) { return juce::roundToInt (value * scale); };

    auto area = getLocalBounds().reduced (px (8.0f), px (6.0f));
    nameLabel.setFont (juce::FontOptions (13.0f * scale, juce::Font::bold));
    detailLabel.setFont (juce::FontOptions (11.0f * scale));

    // Emplacement étroit (carte du cab) : nom en haut sur toute la largeur, boutons dessous.
    const auto stacked = area.getWidth() < px (240.0f);
    auto buttons = stacked ? area.removeFromBottom (px (30.0f)) : area.removeFromRight (px (104.0f));

    clearButton.setBounds (buttons.removeFromRight (px (26.0f)).reduced (0, px (stacked ? 2.0f : 6.0f)));
    buttons.removeFromRight (px (4.0f));
    loadButton.setBounds (buttons.reduced (0, px (stacked ? 2.0f : 6.0f)));

    if (! stacked)
        area.removeFromRight (px (6.0f));

    nameLabel.setBounds (area.removeFromTop (area.getHeight() / 2));
    detailLabel.setBounds (area);
}

} // namespace subdamp::gui
