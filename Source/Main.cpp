#include "MainComponent.h"

class MotiveStudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Motive Studio"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff16181c), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (createMainComponent(), true);
            setResizable (true, true);
            setResizeLimits (960, 540, 10000, 10000);
            centreWithSize (1280, 720);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MotiveStudioApplication)
