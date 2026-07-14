cmake_minimum_required(VERSION 3.22)
project(MotiveStudio VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# vendored in libs/ (cloned from github.com/Tracktion/tracktion_engine, master)
add_subdirectory(libs/tracktion_engine/modules/juce)
add_subdirectory(libs/tracktion_engine/modules)

juce_add_gui_app(MotiveStudio
    COMPANY_NAME "Motive"
    PRODUCT_NAME "Motive Studio"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Motive Studio needs your microphone/audio interface to record."
)

juce_add_binary_data(MotiveStudioAssets SOURCES
    assets/DrumImage.jpeg
)

target_sources(MotiveStudio PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/RoomSession.cpp
)

target_compile_definitions(MotiveStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(MotiveStudio PRIVATE
    MotiveStudioAssets
    tracktion::tracktion_engine
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
