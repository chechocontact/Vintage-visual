#pragma once

#include "AudioHistory.h"
#include "Layouts.h"
#include "LoudnessMeter.h"
#include "Skins.h"
#include "TripleBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

namespace vintagevisual
{
constexpr int numPanels = 15;  // see PanelKind in Panels.h

namespace ui
{
constexpr int minWidth = 560, minHeight = 300, maxWidth = 3000, maxHeight = 2000;
constexpr int defaultWidth = 760, defaultHeight = 420;
// DOCK strip: resizable in height, never smaller than the minimum.
constexpr int dockMinHeight = 118, dockMaxHeight = 480, dockDefaultHeight = dockMinHeight;
} // namespace ui

class FloatingWindow;

// Master-bus analyser: audio passes through UNCHANGED. The audio thread only measures and publishes.
class VintageVisualProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int stateVersion = 2;  // 2: adds the DOCK settings (older states load unchanged)

    VintageVisualProcessor();
    ~VintageVisualProcessor() override;

    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;  // Audio thread

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vintage Visual"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- User configuration (message thread) ----
    // One saved configuration per user, kept in a small XML file. A NEW plug-in instance starts from it;
    // a project restored by the DAW keeps its own saved state (setStateInformation runs after construction).
    bool saveUserConfig() const;
    bool loadUserConfig();
    static juce::File getUserConfigFile();
    static void setConfigFileOverride (const juce::File& file);  // tests: keeps them away from the real file

    // ---- Message thread API ----
    // Raw single-consumer pull. UI code should use refreshReadings() so several views can share one stream.
    bool pullReadings (LoudnessReadings& out) noexcept { return readingsMailbox.pull (out); }

    void refreshReadings() noexcept
    {
        LoudnessReadings r;
        if (readingsMailbox.pull (r))
        {
            latestReadings = r;
            ++readingsSerial;
        }
    }

    const LoudnessReadings& getLatestReadings() const noexcept { return latestReadings; }
    std::uint32_t getReadingsSerial() const noexcept { return readingsSerial; }

    void resetMeters() noexcept
    {
        requestMeterReset();
        latestReadings = {};
        ++readingsSerial;
    }

    // Floating meter window (PIN / DOCK / FULL live there).
    void showFloatingMeter();
    void hideFloatingMeter();
    bool isMeterFloating() const noexcept { return floatingWindow != nullptr; }
    FloatingWindow* getFloatingWindow() noexcept { return floatingWindow.get(); }
    const AudioHistory& getHistory() const noexcept { return history; }
    double getCurrentSampleRate() const noexcept { return sampleRateForUi.load(); }
    void requestMeterReset() noexcept { resetRequested.store (true); }

    // Editing/view settings (not audio parameters): shared with the UI, saved in the project.
    std::atomic<int> skinIndex { 0 };
    std::atomic<float> targetLufs { -14.0f };
    // Which panel each slot shows, for the normal layouts and for the FULL grid. Slot 0 is the "primary" panel.
    std::array<std::atomic<int>, maxSlots> slotPanels, fullSlotPanels;
    std::atomic<int>& panelIndex = slotPanels[0];
    std::atomic<int> layoutPreset { 0 };  // LayoutPreset, one of the first numUserLayouts
    std::atomic<bool> pinned { true };
    // DOCK strip: which panel each of the (up to 8) slots shows, how many are shown, and the strip's height.
    std::array<std::atomic<int>, maxDockSlots> dockSlotPanels;
    std::atomic<int> dockCount { 3 };
    std::atomic<int> dockHeight { ui::dockDefaultHeight };
    std::atomic<int> uiWidth { ui::defaultWidth }, uiHeight { ui::defaultHeight };

private:
    std::unique_ptr<juce::XmlElement> createStateXml() const;
    void applyStateXml (const juce::XmlElement&);

    LoudnessMeter meter;
    AudioHistory history;
    TripleBuffer<LoudnessReadings> readingsMailbox;
    std::atomic<bool> resetRequested { false };
    LoudnessReadings latestReadings;          // message thread
    std::uint32_t readingsSerial = 0;         // message thread
    std::unique_ptr<FloatingWindow> floatingWindow;
    std::atomic<double> sampleRateForUi { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageVisualProcessor)
};
} // namespace vintagevisual
