#pragma once

#include "MeterView.h"

#include <functional>
#include <memory>

namespace vintagevisual
{
// Borderless, resizable top-level window that hosts a MeterView outside the DAW's editor frame.
// PIN: always on top. DOCK: strip across the top of the display (full width, resizable height, never below
// ui::dockMinHeight) holding up to 8 visualisers. FULL: full screen.
// Message thread only.
class FloatingWindow final : public juce::ResizableWindow
{
public:
    static constexpr int stripHeight = ui::dockDefaultHeight;  // default and minimum height of the DOCK strip

    FloatingWindow (VintageVisualProcessor&, std::function<void()> requestClose);
    ~FloatingWindow() override;

    void setPinned (bool);
    void setDocked (bool);
    void setFull (bool);

    bool isPinned() const noexcept { return pinned; }
    bool isDocked() const noexcept { return docked; }
    bool isFull() const noexcept { return full; }
    MeterView& getView() noexcept { return *view; }

    void userTriedToCloseWindow() override;

private:
    void resized() override;
    void handleAction (WindowAction);
    void syncState();
    void applyNormalLimits();

    VintageVisualProcessor& owner;
    std::function<void()> closeRequested;
    MeterView* view = nullptr;  // owned by the window as its content component
    bool pinned = true, docked = false, full = false;
    juce::Rectangle<int> restoreBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingWindow)
};
} // namespace vintagevisual
