// Application standalone de SubdAmp (activée par JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1).
// Compilée dans le code partagé du plugin, mais seule la cible Standalone l'utilise.

#include "gui/LabLookAndFeel.h"
#include "gui/Theme.h"
#include "standalone/AudioSettingsPanel.h"
#include "standalone/StandaloneHost.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace subdamp::standalone
{

namespace theme = gui::theme;

/** Contenu de la fenêtre principale : barre d'état audio + interface du plugin. */
class MainContent final : public juce::Component,
                          private juce::ComponentListener,
                          private juce::ChangeListener
{
public:
    explicit MainContent (StandaloneHost& hostToShow)
        : host (hostToShow),
          editor (host.getProcessor().createEditorAndMakeActive())
    {
        setLookAndFeel (&lookAndFeel);

        settingsButton.setButtonText (juce::String::fromUTF8 ("RÉGLAGES AUDIO"));
        settingsButton.onClick = [this] { openSettings(); };
        addAndMakeVisible (settingsButton);

        statusLabel.setColour (juce::Label::textColourId, theme::textDim);
        statusLabel.setFont (theme::monoFont (12.0f));
        statusLabel.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (statusLabel);

        addAndMakeVisible (*editor);
        editor->addComponentListener (this);

        host.getDeviceManager().addChangeListener (this);
        updateStatus();

        setSize (editor->getWidth(), editor->getHeight() + barHeight);

        // Au premier lancement, on ouvre directement les réglages pour choisir la carte son et l'entrée guitare.
        if (host.isFirstLaunch())
            juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainContent> (this)]
            {
                if (safeThis != nullptr)
                    safeThis->openSettings();
            });
    }

    ~MainContent() override
    {
        host.getDeviceManager().removeChangeListener (this);
        editor->removeComponentListener (this);

        // Protocole JUCE : le processeur doit être prévenu avant la destruction de son interface.
        host.getProcessor().editorBeingDeleted (editor.get());
        editor = nullptr;

        settingsWindow = nullptr;
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        // Barre de la carte son : bandeau métallique, filet gravé dessous.
        const auto bar = getLocalBounds().removeFromTop (barHeight).toFloat();
        g.fillAll (theme::graphite);
        theme::drawPlate (g, bar, 1.0f, theme::anthracite, 0.0f);
        g.setColour (theme::seam);
        g.drawHorizontalLine (barHeight - 1, 0.0f, static_cast<float> (getWidth()));
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (10, 6);
        settingsButton.setBounds (bar.removeFromLeft (130));
        bar.removeFromLeft (12);
        statusLabel.setBounds (bar);

        editor->setBounds (0, barHeight, getWidth(), getHeight() - barHeight);
    }

private:
    static constexpr int barHeight = 40;

    void componentMovedOrResized (juce::Component& component, bool, bool wasResized) override
    {
        // L'interface du plugin est redimensionnable : la fenêtre suit sa taille.
        if (wasResized && &component == editor.get())
            setSize (editor->getWidth(), editor->getHeight() + barHeight);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        updateStatus();
    }

    void updateStatus()
    {
        statusLabel.setText (host.describeSetup(), juce::dontSendNotification);
    }

    void openSettings()
    {
        // Fenêtre non modale, créée une fois puis simplement masquée à la fermeture :
        // on peut continuer à jouer et à régler le plugin pendant qu'elle est ouverte.
        if (settingsWindow == nullptr)
        {
            juce::DialogWindow::LaunchOptions options;
            auto* panel = new AudioSettingsPanel (host);
            panel->setLookAndFeel (&lookAndFeel);
            options.content.setOwned (panel);
            options.dialogTitle = juce::String::fromUTF8 ("Réglages audio");
            options.dialogBackgroundColour = theme::background;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;
            options.componentToCentreAround = this;
            settingsWindow.reset (options.create());
        }

        settingsWindow->setVisible (true);
        settingsWindow->toFront (true);
    }

    StandaloneHost& host;
    theme::SharedResources resources;
    gui::LabLookAndFeel lookAndFeel;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::TextButton settingsButton;
    juce::Label statusLabel;
    std::unique_ptr<juce::DialogWindow> settingsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContent)
};

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (StandaloneHost& host)
        : DocumentWindow (JucePlugin_Name, theme::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainContent (host), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class SubdAmpStandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }

    // Deux instances se disputeraient la carte son (un pilote ASIO n'accepte qu'un client).
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        host = std::make_unique<StandaloneHost>();
        window = std::make_unique<MainWindow> (*host);
    }

    void shutdown() override
    {
        // La fenêtre (et l'interface du plugin) doit disparaître avant le processeur.
        window = nullptr;
        host = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<StandaloneHost> host;
    std::unique_ptr<MainWindow> window;
};

} // namespace subdamp::standalone

START_JUCE_APPLICATION (subdamp::standalone::SubdAmpStandaloneApp)
