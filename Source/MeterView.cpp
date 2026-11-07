#include "MeterView.h"

#include <array>
#include <cmath>

namespace vintagevisual
{
namespace
{
struct Target
{
    const char* label;
    float lufs;
};

constexpr std::array<Target, 4> targets { { { "-14 LUFS  Streaming", -14.0f },
                                            { "-16 LUFS  Apple", -16.0f },
                                            { "-23 LUFS  EBU R128", -23.0f },
                                            { "-9 LUFS  Club", -9.0f } } };

constexpr int slotButtonWidth = 34, slotButtonHeight = 20;  // the small "..." selector
constexpr int readoutHeight = 44;
constexpr int dockMenuWidth = theme::space::md;  // the DOCK strip's menu bar

// Ids of the DOCK strip menu. 1..maxDockSlots are the panel counts in the "Panels" submenu.
constexpr int dockMenuSave = 101, dockMenuPin = 102, dockMenuUndock = 103, dockMenuClose = 104;

juce::String formatValue (double v, int decimals = 1)
{
    return v <= silenceLufs + 0.5 ? juce::String ("-inf") : juce::String (v, decimals);
}

int nearestTarget (float lufs)
{
    int best = 0;
    for (size_t i = 1; i < targets.size(); ++i)
        if (std::abs (targets[i].lufs - lufs) < std::abs (targets[(size_t) best].lufs - lufs))
            best = (int) i;
    return best;
}

// Menu entry that shows a skin in its own colours: surface, name in its text colour, and four swatches.
class SkinMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    SkinMenuItem (int skin, bool isCurrent) : juce::PopupMenu::CustomComponent (true), index (skin), current (isCurrent) {}

    void getIdealSize (int& width, int& height) override
    {
        width = 240;
        height = theme::controlHeight + 4;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& skin = skins[(size_t) index];
        const auto& c = skin.colors;
        auto r = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (theme::colour (c.surface));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::colour (c.controlBorder));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        auto swatches = r.reduced (6.0f, 7.0f).removeFromRight (4.0f * 16.0f + 3.0f * 3.0f);
        for (std::uint32_t colour : { c.canvas, c.accent, c.traceLeft, c.traceRight })
        {
            auto chip = swatches.removeFromLeft (16.0f);
            swatches.removeFromLeft (3.0f);
            g.setColour (theme::colour (colour));
            g.fillRoundedRectangle (chip, 3.0f);
            g.setColour (theme::colour (c.controlBorder));
            g.drawRoundedRectangle (chip, 3.0f, 1.0f);
        }

        g.setColour (theme::colour (c.textPrimary));
        g.setFont (theme::bodyFont (theme::fontSize::label));
        g.drawText (juce::String (skin.name) + (current ? "  (current)" : ""), r.reduced (10.0f, 0.0f).withTrimmedRight (4.0f * 19.0f + 6.0f),
                    juce::Justification::centredLeft, true);

        if (isItemHighlighted())
        {
            g.setColour (theme::colour (c.focus));
            g.drawRoundedRectangle (r.reduced (1.0f), 4.0f, theme::focusRingWidth);
        }
    }

private:
    int index;
    bool current;
};
} // namespace

juce::Image renderSkinMenuItem (int skin, bool isCurrent)
{
    SkinMenuItem item (juce::jlimit (0, numSkins - 1, skin), isCurrent);
    int w = 0, h = 0;
    item.getIdealSize (w, h);
    item.setSize (w, h);
    return item.createComponentSnapshot (item.getLocalBounds());
}

void DockMenuBar::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (palette == nullptr) return;

    // Same shape as the strip's panels so the row stays even; the fill reacts to hover and press.
    const auto r = getLocalBounds().toFloat();
    g.setColour (down ? palette->controlPressed : highlighted ? palette->controlHover : palette->displayBackground);
    g.fillRoundedRectangle (r, theme::cornerRadius);
    g.setColour (palette->separator);
    g.drawRoundedRectangle (r.reduced (0.5f), theme::cornerRadius, 1.0f);

    if (hasKeyboardFocus (false))
    {
        g.setColour (palette->focus);
        g.drawRoundedRectangle (r.reduced (theme::focusRingWidth * 0.5f), theme::cornerRadius, theme::focusRingWidth);
    }

    // Three dots stacked in the centre.
    g.setColour (feedback == Feedback::saved ? palette->accent
                 : feedback == Feedback::error ? palette->warning
                 : highlighted ? palette->textPrimary : palette->textSecondary);
    constexpr float dot = 3.0f, step = (float) theme::space::xs + dot;
    const auto centre = r.getCentre();
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre ({ centre.x, centre.y + (float) i * step }));
}

MeterView::MeterView (VintageVisualProcessor& p, Mode m)
    : owner (p), mode (m), palette (skins[0].colors)
{
    appliedKinds.fill (-1);
    slotButtonAlpha.fill (0.0f);
    setLookAndFeel (&laf);
    setTitle ("Vintage Visual meter");

    for (int i = 0; i < numUserLayouts; ++i)
        layoutBox.addItem (layoutName (static_cast<LayoutPreset> (i)), i + 1);
    for (size_t i = 0; i < targets.size(); ++i)
        targetBox.addItem (targets[i].label, (int) i + 1);

    layoutBox.setTitle ("Layout");
    layoutBox.setTooltip ("How many panels to show");
    layoutBox.onChange = [this]
    {
        owner.layoutPreset.store (layoutBox.getSelectedId() - 1);
        rebuildSlots();
    };

    dockCountBox.setTitle ("Number of panels");
    dockCountBox.setTooltip ("How many visualizers the strip shows (1 to 8)");
    for (int n = 1; n <= maxDockSlots; ++n)
        dockCountBox.addItem (juce::String (n) + (n == 1 ? " panel" : " panels"), n);
    dockCountBox.onChange = [this]
    {
        owner.dockCount.store (juce::jlimit (1, maxDockSlots, dockCountBox.getSelectedId()));
        rebuildSlots();
    };
    addChildComponent (dockCountBox);

    for (int s = 0; s < maxViewSlots; ++s)
    {
        auto& button = slotButtons[(size_t) s];
        button.setButtonText ("...");
        button.setTitle ("Panel " + juce::String (s + 1) + " visualizer");
        button.setTooltip ("Choose what this panel shows. Drag a panel onto another to swap them");
        button.onClick = [this, s] { showSlotMenu (s); };
        button.setAlpha (0.0f);  // fades in when the pointer is over the slot
        addChildComponent (button);
    }

    skinButton.setTitle ("Skin");
    skinButton.setTooltip ("Choose the colour scheme");
    skinButton.onClick = [this] { showSkinMenu(); };

    targetBox.setTitle ("Loudness target");
    targetBox.setTooltip ("Reference level for the integrated loudness comparison");
    targetBox.onChange = [this]
    {
        const int idx = targetBox.getSelectedId() - 1;
        if (idx >= 0 && idx < (int) targets.size())
        {
            owner.targetLufs.store (targets[(size_t) idx].lufs);
            repaint();
        }
    };

    saveButton.setTitle ("Save configuration");
    saveButton.setTooltip ("Save this configuration. New instances of the plug-in will open with it");
    saveButton.onClick = [this] { saveConfiguration(); };

    resetButton.setTitle ("Reset meters");
    resetButton.setTooltip ("Restart integrated loudness, range and true peak");
    resetButton.onClick = [this]
    {
        owner.resetMeters();
        juce::AccessibilityHandler::postAnnouncement ("Meters reset", juce::AccessibilityHandler::AnnouncementPriority::medium);
    };

    detachButton.setTitle ("Float meter");
    detachButton.setTooltip ("Open the meter in its own window, outside the DAW editor");
    detachButton.onClick = [this]
    {
        if (owner.isMeterFloating()) owner.hideFloatingMeter();
        else owner.showFloatingMeter();
    };

    pinButton.setTitle ("Keep on top");
    pinButton.setTooltip ("Keep this window above all others");
    pinButton.onClick = [this] { notify (WindowAction::togglePin); };

    dockButton.setTitle ("Dock to top of screen");
    dockButton.setTooltip ("Shrink to a strip at the top of the screen");
    dockButton.onClick = [this] { notify (WindowAction::toggleDock); };

    fullButton.setTitle ("Full screen");
    fullButton.setTooltip ("Fill the screen with a grid of modules. Press Escape to leave");
    fullButton.onClick = [this] { notify (WindowAction::toggleFull); };

    closeButton.setTitle ("Close meter window");
    closeButton.setTooltip ("Close this window. The meter keeps running in the DAW");
    closeButton.onClick = [this] { notify (WindowAction::close); };

    dockMenuBar.setTitle ("Strip menu");
    dockMenuBar.setTooltip ("Panels, save, keep on top, undock and close");
    dockMenuBar.setPalette (&palette);
    dockMenuBar.onClick = [this] { showDockMenu(); };
    addChildComponent (dockMenuBar);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &layoutBox, &skinButton, &targetBox, &resetButton, &detachButton,
                                                                         &pinButton, &dockButton, &fullButton, &closeButton, &saveButton })
        addAndMakeVisible (c);

    if (mode == Mode::floating)
        setWantsKeyboardFocus (true);

    layoutBox.setSelectedId (owner.layoutPreset.load() + 1, juce::dontSendNotification);
    targetBox.setSelectedId (nearestTarget (owner.targetLufs.load()) + 1, juce::dontSendNotification);
    applySkin (owner.skinIndex.load());
    rebuildSlots();
    startTimerHz (30);
}

MeterView::~MeterView()
{
    setLookAndFeel (nullptr);
}

void MeterView::notify (WindowAction a) const
{
    if (onWindowAction) onWindowAction (a);
}

bool MeterView::canDragWindow() const noexcept
{
    return mode == Mode::floating && ! windowState.full && ! windowState.docked;
}

// ------------------------------------------------------------------ state

void MeterView::setWindowState (const WindowState& s)
{
    const bool fullChanged = s.full != windowState.full;
    windowState = s;
    pinButton.setToggleState (s.pinned, juce::dontSendNotification);
    dockButton.setToggleState (s.docked, juce::dontSendNotification);
    fullButton.setToggleState (s.full, juce::dontSendNotification);
    if (fullChanged) rebuildSlots();
    repaint();
}

void MeterView::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact) return;
    compact = shouldBeCompact;
    rebuildSlots();  // the strip has its own set of panels; this also updates visibility, layout and painting
}

LayoutPreset MeterView::activePreset() const
{
    if (windowState.full) return LayoutPreset::full5;
    return static_cast<LayoutPreset> (juce::jlimit (0, numUserLayouts - 1, owner.layoutPreset.load()));
}

int MeterView::getActiveSlotCount() const noexcept
{
    if (compact) return juce::jlimit (1, maxDockSlots, owner.dockCount.load());
    return slotCount (activePreset());
}

// A key that changes whenever the arrangement of slots changes (layout preset, or the DOCK strip and its count).
int MeterView::layoutKey() const
{
    return compact ? 100 + getActiveSlotCount() : (int) activePreset();
}

int MeterView::kindAt (int slot) const
{
    int raw = 0;

    if (compact)
    {
        raw = owner.dockSlotPanels[(size_t) std::clamp (slot, 0, maxDockSlots - 1)].load();
    }
    else
    {
        const auto& source = windowState.full ? owner.fullSlotPanels : owner.slotPanels;
        raw = source[(size_t) std::clamp (slot, 0, maxSlots - 1)].load();
    }

    return juce::jlimit (0, numPanels - 1, raw);
}

void MeterView::storeKind (int slot, int kind)
{
    const int value = juce::jlimit (0, numPanels - 1, kind);

    if (compact)
    {
        owner.dockSlotPanels[(size_t) std::clamp (slot, 0, maxDockSlots - 1)].store (value);
    }
    else
    {
        auto& target = windowState.full ? owner.fullSlotPanels : owner.slotPanels;
        target[(size_t) std::clamp (slot, 0, maxSlots - 1)].store (value);
    }
}

bool MeterView::needsRebuild() const
{
    if (layoutKey() != appliedPreset) return true;
    for (int i = 0; i < getActiveSlotCount(); ++i)
        if (kindAt (i) != appliedKinds[(size_t) i]) return true;
    return false;
}

void MeterView::rebuildSlots()
{
    const int count = getActiveSlotCount();
    appliedPreset = layoutKey();

    for (int i = 0; i < maxViewSlots; ++i)
    {
        auto& panel = panels[(size_t) i];

        if (i < count)
        {
            const int kind = kindAt (i);
            if (appliedKinds[(size_t) i] != kind || panel == nullptr)
            {
                panel = makePanel (static_cast<PanelKind> (kind), owner);
                appliedKinds[(size_t) i] = kind;
                if (panel != nullptr)
                {
                    panel->setPalette (palette);
                    addAndMakeVisible (*panel);
                }
            }
        }
        else
        {
            panel.reset();
            appliedKinds[(size_t) i] = -1;
        }
    }

    updateVisibility();
    resized();
    repaint();
}

void MeterView::swapSlots (int a, int b)
{
    const int count = getActiveSlotCount();
    if (a < 0 || b < 0 || a >= count || b >= count || a == b) return;

    const int ka = kindAt (a), kb = kindAt (b);
    storeKind (a, kb);
    storeKind (b, ka);
    rebuildSlots();
    juce::AccessibilityHandler::postAnnouncement ("Panels swapped", juce::AccessibilityHandler::AnnouncementPriority::medium);
}

void MeterView::setSlotKind (int slot, int kind)
{
    if (slot < 0 || slot >= getActiveSlotCount()) return;
    storeKind (slot, kind);
    rebuildSlots();
}

void MeterView::showSlotMenu (int slot)
{
    if (slot < 0 || slot >= getActiveSlotCount()) return;

    juce::PopupMenu menu;
    const int current = kindAt (slot);
    for (int k = 0; k < numPanels; ++k)
        menu.addItem (k + 1, getPanelName (static_cast<PanelKind> (k)), true, k == current);

    menuOpenSlot = slot;
    juce::Component::SafePointer<MeterView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slotButtons[(size_t) slot]), [safe, slot] (int result)
    {
        if (safe == nullptr) return;
        safe->menuOpenSlot = -1;
        if (result > 0) safe->setSlotKind (slot, result - 1);
    });
}

// The DOCK strip menu. Each entry does exactly what its control does outside the strip.
void MeterView::showDockMenu()
{
    juce::PopupMenu panelsMenu;
    const int count = getActiveSlotCount();
    for (int n = 1; n <= maxDockSlots; ++n)
        panelsMenu.addItem (n, juce::String (n) + (n == 1 ? " panel" : " panels"), true, n == count);

    juce::PopupMenu menu;
    menu.addSubMenu ("Panels", panelsMenu);
    menu.addSeparator();
    menu.addItem (dockMenuSave, "Save configuration");
    menu.addItem (dockMenuPin, "Keep on top", true, windowState.pinned);
    menu.addItem (dockMenuUndock, "Undock");
    menu.addSeparator();
    menu.addItem (dockMenuClose, "Close");

    juce::Component::SafePointer<MeterView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dockMenuBar), [safe] (int result)
    {
        if (safe == nullptr || result <= 0) return;

        if (result <= maxDockSlots)
        {
            safe->owner.dockCount.store (result);
            safe->rebuildSlots();
        }
        else if (result == dockMenuSave) safe->saveConfiguration();
        else if (result == dockMenuPin) safe->notify (WindowAction::togglePin);
        else if (result == dockMenuUndock) safe->notify (WindowAction::toggleDock);
        else if (result == dockMenuClose) safe->notify (WindowAction::close);  // may delete this view: nothing after it
    });
}

// The "..." selectors are only drawn while the pointer is over their slot (or they have keyboard focus or an open menu).
// They stay in the focus order the whole time, so keyboard and screen-reader users can always reach them.
void MeterView::updateSlotButtons()
{
    const bool inside = isMouseOver (true);
    const auto mouse = getMouseXYRelative();
    const int count = getActiveSlotCount();

    for (int i = 0; i < count; ++i)
    {
        auto& button = slotButtons[(size_t) i];
        const bool wanted = menuOpenSlot == i || button.hasKeyboardFocus (true) || (inside && slotBounds[(size_t) i].contains (mouse));

        float& alpha = slotButtonAlpha[(size_t) i];
        const float target = wanted ? 1.0f : 0.0f;
        alpha += (target - alpha) * 0.5f;
        if (std::abs (target - alpha) < 0.04f) alpha = target;
        if (button.getAlpha() != alpha) button.setAlpha (alpha);
    }
}

void MeterView::saveConfiguration()
{
    const bool ok = owner.saveUserConfig();
    saveButton.setButtonText (ok ? "Saved" : "Error");
    dockMenuBar.setFeedback (ok ? DockMenuBar::Feedback::saved : DockMenuBar::Feedback::error);
    saveFeedbackTicks = 75;  // about 2.5 s at 30 Hz, then the label returns to "Save"
    juce::AccessibilityHandler::postAnnouncement (ok ? "Configuration saved. New instances will open with it" : "Could not save the configuration",
                                                  juce::AccessibilityHandler::AnnouncementPriority::medium);
}

bool MeterView::loudnessOnScreen() const
{
    for (int i = 0; i < getActiveSlotCount(); ++i)
        if (appliedKinds[(size_t) i] == (int) PanelKind::loudness) return true;
    return false;
}

void MeterView::updateVisibility()
{
    const bool floating = mode == Mode::floating;
    const int count = getActiveSlotCount();

    layoutBox.setVisible (! compact && ! windowState.full);
    skinButton.setVisible (! compact);
    targetBox.setVisible (! compact);
    resetButton.setVisible (! compact);
    detachButton.setVisible (! floating);
    // In the DOCK strip these controls live in the strip menu (dockMenuBar) instead.
    pinButton.setVisible (floating && ! compact);
    dockButton.setVisible (floating && ! compact);
    closeButton.setVisible (floating && ! compact);
    fullButton.setVisible (floating && ! compact);

    dockCountBox.setVisible (false);
    saveButton.setVisible (! compact);
    dockMenuBar.setVisible (floating && compact);

    for (int i = 0; i < maxViewSlots; ++i)
    {
        const bool shown = i < count;
        slotButtons[(size_t) i].setVisible (shown);
        if (panels[(size_t) i] != nullptr) panels[(size_t) i]->setVisible (shown);
    }
}

void MeterView::applySkin (int index)
{
    index = juce::jlimit (0, numSkins - 1, index);
    appliedSkin = index;
    palette = theme::Palette (skins[(size_t) index].colors);
    laf.applyPalette (palette);
    skinButton.setButtonText (skins[(size_t) index].name);
    for (auto& panel : panels)
        if (panel != nullptr) panel->setPalette (palette);
    dockMenuBar.repaint();
    sendLookAndFeelChange();
    syncWindowBackground();
    repaint();
}

void MeterView::showSkinMenu()
{
    juce::PopupMenu menu;
    for (int i = 0; i < numSkins; ++i)
        menu.addCustomItem (i + 1, std::make_unique<SkinMenuItem> (i, i == appliedSkin), nullptr, skins[(size_t) i].name);

    juce::Component::SafePointer<MeterView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&skinButton), [safe] (int result)
    {
        if (safe == nullptr || result <= 0) return;
        safe->owner.skinIndex.store (result - 1);
        safe->applySkin (result - 1);
    });
}

// The floating window's own background must follow the skin, or its edge shows another skin's colour.
void MeterView::syncWindowBackground()
{
    if (auto* window = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        window->setBackgroundColour (palette.canvas);
}

void MeterView::parentHierarchyChanged()
{
    syncWindowBackground();
}

void MeterView::refreshNow()
{
    owner.refreshReadings();

    if (lastSerial != owner.getReadingsSerial())
    {
        lastSerial = owner.getReadingsSerial();
        readings = owner.getLatestReadings();
        if (! compact) repaint (readoutArea.getUnion (deltaArea));  // the strip shows readings through its LUFS panel
    }

    if (owner.skinIndex.load() != appliedSkin) applySkin (owner.skinIndex.load());

    const int wantedLayout = owner.layoutPreset.load() + 1;
    if (layoutBox.getSelectedId() != wantedLayout) layoutBox.setSelectedId (wantedLayout, juce::dontSendNotification);

    const int wantedTarget = nearestTarget (owner.targetLufs.load()) + 1;
    if (targetBox.getSelectedId() != wantedTarget)
    {
        targetBox.setSelectedId (wantedTarget, juce::dontSendNotification);
        repaint();
    }

    const int wantedDock = owner.dockCount.load();
    if (dockCountBox.getSelectedId() != wantedDock) dockCountBox.setSelectedId (wantedDock, juce::dontSendNotification);

    if (needsRebuild()) rebuildSlots();

    for (auto& panel : panels)
        if (panel != nullptr) panel->update();

    updateSlotButtons();

    if (saveFeedbackTicks > 0 && --saveFeedbackTicks == 0)
    {
        saveButton.setButtonText ("Save");
        dockMenuBar.setFeedback (DockMenuBar::Feedback::none);
    }

    if (mode == Mode::embedded)
        detachButton.setToggleState (owner.isMeterFloating(), juce::dontSendNotification);
}

juce::String MeterView::deltaText (bool& outOfRange) const
{
    outOfRange = false;
    if (readings.integrated <= silenceLufs + 0.5)
        return "Integrated: waiting for audio";

    const double delta = readings.integrated - (double) owner.targetLufs.load();
    outOfRange = std::abs (delta) >= 1.0;

    if (std::abs (delta) < 0.05) return "Integrated: on target";
    return "Integrated: " + juce::String (std::abs (delta), 1) + (delta > 0 ? " LU above target" : " LU below target");
}

// ------------------------------------------------------------------ layout

void MeterView::resized()
{
    if (compact)
        layoutCompact (getLocalBounds().reduced (theme::space::sm));
    else
        layoutFull (getLocalBounds().reduced (theme::space::sm + theme::space::md));
}

void MeterView::layoutFull (juce::Rectangle<int> area)
{
    using namespace theme;

    auto row1 = area.removeFromTop (controlHeight);
    if (mode == Mode::floating)
    {
        closeButton.setBounds (row1.removeFromRight (64));
        row1.removeFromRight (space::sm);
        fullButton.setBounds (row1.removeFromRight (60));
        row1.removeFromRight (space::sm);
        dockButton.setBounds (row1.removeFromRight (64));
        row1.removeFromRight (space::sm);
        pinButton.setBounds (row1.removeFromRight (56));
        row1.removeFromRight (space::sm);
    }
    else
    {
        detachButton.setBounds (row1.removeFromRight (72));
        row1.removeFromRight (space::sm);
    }
    saveButton.setBounds (row1.removeFromRight (64));
    row1.removeFromRight (space::sm);
    titleArea = row1;

    area.removeFromTop (space::sm);
    auto row2 = area.removeFromTop (controlHeight);
    if (! windowState.full)  // the layout selector is hidden in FULL, so do not leave a gap
    {
        layoutBox.setBounds (row2.removeFromLeft (100));
        row2.removeFromLeft (space::sm);
    }
    skinButton.setBounds (row2.removeFromLeft (120));
    row2.removeFromLeft (space::sm);
    targetBox.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (space::sm);
    resetButton.setBounds (row2.removeFromLeft (72));

    area.removeFromTop (space::md);
    footerArea = area.removeFromBottom (18);
    area.removeFromBottom (space::sm);
    deltaArea = area.removeFromBottom (22);
    area.removeFromBottom (space::sm);

    if (loudnessOnScreen())
    {
        readoutArea = {};
    }
    else
    {
        readoutArea = area.removeFromBottom (readoutHeight);
        area.removeFromBottom (space::sm);
    }

    gridArea = area;
    const int count = getActiveSlotCount();
    const auto slots = computeSlots (activePreset(), gridArea.getWidth(), gridArea.getHeight(), space::sm);

    for (int i = 0; i < maxViewSlots; ++i)
    {
        if (i >= count)
        {
            slotBounds[(size_t) i] = {};
            continue;
        }

        const auto& s = slots[(size_t) i];
        placeSlot (i, { gridArea.getX() + s.x, gridArea.getY() + s.y, s.w, s.h }, space::sm);
    }
}

// Puts a slot's rectangle, its "..." button (top-left corner, on top of the panel) and its panel in place.
void MeterView::placeSlot (int i, juce::Rectangle<int> rect, int panelInset)
{
    using namespace theme;

    slotBounds[(size_t) i] = rect;
    slotButtons[(size_t) i].setBounds (rect.getX() + space::sm, rect.getY() + space::xs, slotButtonWidth, slotButtonHeight);
    if (panels[(size_t) i] != nullptr)
        panels[(size_t) i]->setBounds (rect.reduced (panelInset));
}

void MeterView::layoutCompact (juce::Rectangle<int> area)
{
    using namespace theme;

    // One row: the panels use the full height; the strip menu is a thin bar of the same height at the right end,
    // separated by the same gap as between panels.
    dockMenuBar.setBounds (area.removeFromRight (dockMenuWidth));
    area.removeFromRight (space::sm);

    titleArea = {};
    deltaArea = footerArea = readoutArea = {};

    gridArea = area;

    const int count = getActiveSlotCount();
    const auto slots = computeDockSlots (count, gridArea.getWidth(), gridArea.getHeight(), space::sm);

    for (int i = 0; i < maxViewSlots; ++i)
    {
        if (i >= count)
        {
            slotBounds[(size_t) i] = {};
            continue;
        }

        const auto& s = slots[(size_t) i];
        placeSlot (i, { gridArea.getX() + s.x, gridArea.getY() + s.y, s.w, s.h }, space::xs);
    }
}

// ------------------------------------------------------------------ painting

void MeterView::drawCells (juce::Graphics& g, juce::Rectangle<int> cells, bool includeMomentary)
{
    using namespace theme;

    juce::StringArray names, values;
    if (includeMomentary)
    {
        names.add ("Momentary");
        values.add (formatValue (readings.momentary) + " LUFS");
    }
    names.addArray ({ "Short-term", "Integrated", "Range", "True peak" });
    values.add (formatValue (readings.shortTerm) + " LUFS");
    values.add (formatValue (readings.integrated) + " LUFS");
    values.add (juce::String (readings.loudnessRange, 1) + " LU");
    values.add (formatValue (readings.truePeakDb) + " dBTP");

    const int cellWidth = cells.getWidth() / names.size();

    for (int i = 0; i < names.size(); ++i)
    {
        auto cell = cells.removeFromLeft (cellWidth);
        g.setColour (palette.displayDim);
        g.setFont (bodyFont (fontSize::note));
        g.drawText (names[i], cell.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (palette.displayText);
        g.setFont (monoFont (fontSize::body));
        g.drawText (values[i], cell, juce::Justification::centredLeft);
    }
}

void MeterView::paint (juce::Graphics& g)
{
    g.fillAll (palette.canvas);
    g.setColour (palette.surface);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced ((float) theme::space::xs), theme::cornerRadius * 2.0f);

    if (compact) paintCompact (g);
    else paintFull (g);
}

void MeterView::paintCompact (juce::Graphics& g)
{
    using namespace theme;

    for (int i = 0; i < getActiveSlotCount(); ++i)
    {
        const auto r = slotBounds[(size_t) i].toFloat();
        g.setColour (palette.displayBackground);
        g.fillRoundedRectangle (r, cornerRadius);
        g.setColour (palette.separator);
        g.drawRoundedRectangle (r.reduced (0.5f), cornerRadius, 1.0f);
    }
}

void MeterView::paintFull (juce::Graphics& g)
{
    using namespace theme;

    g.setColour (palette.textPrimary);
    g.setFont (titleFont (fontSize::title));
    g.drawText ("Vintage Visual", titleArea, juce::Justification::centredLeft);

    for (int i = 0; i < getActiveSlotCount(); ++i)
    {
        const auto r = slotBounds[(size_t) i].toFloat();
        g.setColour (palette.displayBackground);
        g.fillRoundedRectangle (r, cornerRadius);
        g.setColour (palette.separator);
        g.drawRoundedRectangle (r.reduced (0.5f), cornerRadius, 1.0f);
    }

    if (! readoutArea.isEmpty())
    {
        g.setColour (palette.displayBackground);
        g.fillRoundedRectangle (readoutArea.toFloat(), cornerRadius);
        g.setColour (palette.separator);
        g.drawRoundedRectangle (readoutArea.toFloat().reduced (0.5f), cornerRadius, 1.0f);
        drawCells (g, readoutArea.reduced (space::md, space::xs), true);
    }

    bool outOfRange = false;
    const auto delta = deltaText (outOfRange);
    g.setColour (outOfRange ? palette.warning : palette.textPrimary);
    g.setFont (bodyFont (fontSize::body));
    g.drawText (delta, deltaArea, juce::Justification::centredLeft);

    g.setColour (palette.textSecondary);
    g.setFont (bodyFont (fontSize::note));
    g.drawText ("Audio passes through unchanged. Insert on the master track. Drag a panel onto another to swap them.", footerArea,
                juce::Justification::centredLeft);
}

void MeterView::paintOverChildren (juce::Graphics& g)
{
    if (dragFrom < 0) return;

    g.setColour (palette.surface.withAlpha (0.35f));
    g.fillRoundedRectangle (slotBounds[(size_t) dragFrom].toFloat(), theme::cornerRadius);

    if (dragTo >= 0 && dragTo != dragFrom)
    {
        g.setColour (palette.focus);
        g.drawRoundedRectangle (slotBounds[(size_t) dragTo].toFloat().reduced (1.5f), theme::cornerRadius, 3.0f);
    }
}

// ------------------------------------------------------------------ mouse and keyboard

int MeterView::slotAt (juce::Point<int> p) const
{
    for (int i = 0; i < getActiveSlotCount(); ++i)
        if (slotBounds[(size_t) i].contains (p)) return i;
    return -1;
}

void MeterView::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = slotAt (e.getPosition());
    dragTo = -1;

    if (dragFrom < 0 && canDragWindow())
        dragger.startDraggingComponent (getTopLevelComponent(), e);
}

void MeterView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom >= 0)
    {
        const int target = slotAt (e.getPosition());
        if (target != dragTo)
        {
            dragTo = target;
            repaint (gridArea);
        }
    }
    else if (canDragWindow())
    {
        dragger.dragComponent (getTopLevelComponent(), e, nullptr);
    }
}

void MeterView::mouseUp (const juce::MouseEvent&)
{
    const int from = dragFrom, to = dragTo;
    dragFrom = dragTo = -1;
    if (from >= 0 && to >= 0 && from != to) swapSlots (from, to);
    else repaint (gridArea);
}

bool MeterView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && windowState.full)
    {
        notify (WindowAction::toggleFull);
        return true;
    }

    return false;
}
} // namespace vintagevisual
