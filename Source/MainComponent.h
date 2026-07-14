#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The app lives in MainComponent.cpp as a single translation unit because the
// Tracktion example UI kit (Components.h) includes its .cpp directly.
juce::Component* createMainComponent();
