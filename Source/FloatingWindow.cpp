#include "FloatingWindow.h"

#include <algorithm>

namespace vintagevisual
{
FloatingWindow::FloatingWindow (VintageVisualProcessor& p, std::function<void()> requestClose)
    : juce::ResizableWindow ("Vintage Visual", theme::colour (skins[0].colors.canvas), true),
      owner (p), closeRequested (std::move (requestClose)), pinned (p.pinned.load())
{
    setUsingNativeTitleBar (false);  // borderless
    setDropShadowEnabled (true);
    setResizable (true, false);      // edge resizing, since there is no title bar
    applyNormalLimits();

    view = new MeterView (owner, MeterView::Mode::floating);
    view->onWindowAction = [this] (WindowAction a) { handleAction (a); };
    setContentOwned (view, false);

    centreWithSize (ui::defaultWidth, ui::defaultHeight);
    setAlwaysOnTop (pinned);
    syncState();
    setVisible (true);
    view->grabKeyboardFocus();
}

FloatingWindow::~FloatingWindow() = default;

void FloatingWindow::applyNormalLimits()
{
    setResizeLimits (ui::minWidth, ui::minHeight, ui::maxWidth, ui::maxHeight);
}

void FloatingWindow::resized()
{
    juce::ResizableWindow::resized();

    // While docked, a resize can only be the user changing the strip's height: remember it.
    if (docked && getHeight() > 0)
        owner.dockHeight.store (std::clamp (getHeight(), ui::dockMinHeight, ui::dockMaxHeight));
}

void FloatingWindow::userTriedToCloseWindow()
{
    if (closeRequested) closeRequested();
}

void FloatingWindow::handleAction (WindowAction a)
{
    switch (a)
    {
        case WindowAction::togglePin: setPinned (! pinned); break;
        case WindowAction::toggleDock: setDocked (! docked); break;
        case WindowAction::toggleFull: setFull (! full); break;
        case WindowAction::close: userTriedToCloseWindow(); break;
    }
}

void FloatingWindow::syncState()
{
    view->setWindowState ({ pinned, docked, full });
}

void FloatingWindow::setPinned (bool shouldPin)
{
    if (pinned == shouldPin) return;
    pinned = shouldPin;
    owner.pinned.store (pinned);
    setAlwaysOnTop (pinned);
    syncState();
    juce::AccessibilityHandler::postAnnouncement (pinned ? "Window kept on top" : "Window no longer kept on top",
                                                  juce::AccessibilityHandler::AnnouncementPriority::medium);
}

void FloatingWindow::setDocked (bool shouldDock)
{
    if (docked == shouldDock) return;

    if (shouldDock)
    {
        if (full) setFull (false);
        restoreBounds = getBounds();
        docked = true;

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = displays.getDisplayForRect (getScreenBounds());
        if (display == nullptr) display = displays.getPrimaryDisplay();
        const auto area = display != nullptr ? display->userBounds.toNearestInt() : getScreenBounds();  // excludes the taskbar

        // The strip keeps the height the user last gave it (remembered by the processor).
        const int height = std::clamp (owner.dockHeight.load(), ui::dockMinHeight, ui::dockMaxHeight);
        setResizeLimits (ui::minWidth, ui::dockMinHeight, std::max (ui::maxWidth, area.getWidth()), ui::dockMaxHeight);
        setBounds (area.getX(), area.getY(), area.getWidth(), height);
        view->setCompact (true);
    }
    else
    {
        docked = false;
        applyNormalLimits();
        view->setCompact (false);
        setBounds (restoreBounds);
    }

    syncState();
    juce::AccessibilityHandler::postAnnouncement (docked ? "Docked to the top of the screen" : "Undocked",
                                                  juce::AccessibilityHandler::AnnouncementPriority::medium);
}

void FloatingWindow::setFull (bool shouldBeFull)
{
    if (full == shouldBeFull) return;

    if (shouldBeFull)
    {
        if (docked) setDocked (false);
        full = true;
        setFullScreen (true);
    }
    else
    {
        full = false;
        setFullScreen (false);
    }

    syncState();
    view->grabKeyboardFocus();
    juce::AccessibilityHandler::postAnnouncement (full ? "Full screen. Press Escape to leave" : "Left full screen",
                                                  juce::AccessibilityHandler::AnnouncementPriority::medium);
}
} // namespace vintagevisual
