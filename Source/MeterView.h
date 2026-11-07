#pragma once

#include "Layouts.h"
#include "LookAndFeel.h"
#include "Panels.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <functional>

namespace vintagevisual
{
// Slots a MeterView can hold at once: the normal layouts use up to 5, the DOCK strip up to 8.
constexpr int maxViewSlots = std::max (maxSlots, maxDockSlots);

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

// Thin vertical bar at the right end of the DOCK strip, as tall as the panels. Clicking it (or Enter/Space)
// opens the strip menu: panel count, save, pin, undock and close. It draws three dots, which briefly turn
// accent (saved) or warning (error) after a save. Message thread only.
class DockMenuBar final : public juce::Button
{
public:
    enum class Feedback
    {
        none,
        saved,
        error
    };

    DockMenuBar() : juce::Button ("Strip menu") {}

    void setPalette (const theme::Palette* p) { palette = p; repaint(); }
    void setFeedback (Feedback f) { if (feedback != f) { feedback = f; repaint(); } }
    Feedback getFeedback() const noexcept { return feedback; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const theme::Palette* palette = nullptr;
    Feedback feedback = Feedback::none;
};

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
    void setSlotKind (int slot, int kind);  // what the "..." menu does

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
    void showSlotMenu (int slot);
    void showDockMenu();
    void updateSlotButtons();
    void saveConfiguration();
    void placeSlot (int slot, juce::Rectangle<int> rect, int panelInset);
    int layoutKey() const;
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

    juce::ComboBox layoutBox, targetBox, dockCountBox;
    // Small "..." buttons, one per slot. They fade in only while the pointer is over their slot (or they have focus).
    std::array<juce::TextButton, maxViewSlots> slotButtons;
    std::array<float, maxViewSlots> slotButtonAlpha;
    std::array<std::unique_ptr<VisualizerPanel>, maxViewSlots> panels;
    std::array<int, maxViewSlots> appliedKinds;
    std::array<juce::Rectangle<int>, maxViewSlots> slotBounds;
    int appliedPreset = -1;
    int menuOpenSlot = -1;
    int saveFeedbackTicks = 0;

    juce::TextButton skinButton { "Skin" }, resetButton { "Reset" }, detachButton { "Float" }, saveButton { "Save" };
    juce::TextButton pinButton { "PIN" }, dockButton { "DOCK" }, fullButton { "FULL" }, closeButton { "Close" };
    DockMenuBar dockMenuBar;  // DOCK only: replaces the panel count, Save, PIN, DOCK and Close controls

    juce::Rectangle<int> titleArea, gridArea, readoutArea, deltaArea, footerArea;
    LoudnessReadings readings;
    std::uint32_t lastSerial = 0;
    int appliedSkin = -1;
    bool compact = false;
    WindowState windowState;
    int dragFrom = -1, dragTo = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterView)
};
} // namespace vintagevisual
