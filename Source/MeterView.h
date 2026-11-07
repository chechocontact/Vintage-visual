#pragma once

#include "Layouts.h"
#include "LookAndFeel.h"
#include "Panels.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace vintagevisual
{
enum class WindowAction
{
    togglePin,
    toggleDock,
    toggleFull,
    close
};

struct WindowState
{
    bool pinned = true, docked = false, full = false;
};

// Draws one row of the skin menu (the row shows the skin in its own colours). Used by tests and previews.
juce::Image renderSkinMenuItem (int skin, bool isCurrent);

// The meter UI: a toolbar, a grid of 1..5 visualiser panels and a readout bar.
// It lives either inside the DAW's plug-in editor (embedded) or in the floating window.
// Message thread only.
class MeterView final : public juce::Component,
                        private juce::Timer
{
public:
    enum class Mode
    {
        embedded,
        floating
    };

    MeterView (VintageVisualProcessor&, Mode);
    ~MeterView() override;

    std::function<void (WindowAction)> onWindowAction;  // floating mode only

    void setWindowState (const WindowState&);
    void setCompact (bool shouldBeCompact);  // thin strip layout used by DOCK
    bool isCompact() const noexcept { return compact; }
    void refreshNow();  // what the 30 Hz timer does; also useful for tests

    // Panel slots. Dragging one panel onto another swaps them; the same operation is available here.
    int getActiveSlotCount() const noexcept;
    int getSlotKind (int slot) const noexcept { return appliedKinds[(size_t) slot]; }
    void swapSlots (int a, int b);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override { refreshNow(); }
    void applySkin (int index);
    void showSkinMenu();
    void syncWindowBackground();
    void updateVisibility();
    void rebuildSlots();
    bool needsRebuild() const;
    LayoutPreset activePreset() const;
    int kindAt (int slot) const;
    void storeKind (int slot, int kind);
    int slotAt (juce::Point<int>) const;
    bool loudnessOnScreen() const;
    void layoutFull (juce::Rectangle<int> area);
    void layoutCompact (juce::Rectangle<int> area);
    void paintFull (juce::Graphics&);
    void paintCompact (juce::Graphics&);
    void drawCells (juce::Graphics&, juce::Rectangle<int> area, bool includeMomentary);
    void notify (WindowAction a) const;
    bool canDragWindow() const noexcept;
    juce::String deltaText (bool& outOfRange) const;

    VintageVisualProcessor& owner;
    const Mode mode;
    theme::Palette palette;
    VintageLookAndFeel laf;
    juce::TooltipWindow tooltipWindow { this };
    juce::ComponentDragger dragger;

    juce::ComboBox layoutBox, targetBox;
    std::array<juce::ComboBox, maxSlots> slotBoxes;
    std::array<std::unique_ptr<VisualizerPanel>, maxSlots> panels;
    std::array<int, maxSlots> appliedKinds;
    std::array<juce::Rectangle<int>, maxSlots> slotBounds;
    int appliedPreset = -1;

    juce::TextButton skinButton { "Skin" }, resetButton { "Reset" }, detachButton { "Float" };
    juce::TextButton pinButton { "PIN" }, dockButton { "DOCK" }, fullButton { "FULL" }, closeButton { "Close" };

    juce::Rectangle<int> titleArea, gridArea, readoutArea, deltaArea, footerArea, compactArea;
    LoudnessReadings readings;
    std::uint32_t lastSerial = 0;
    int appliedSkin = -1;
    bool compact = false;
    WindowState windowState;
    int dragFrom = -1, dragTo = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterView)
};
} // namespace vintagevisual
