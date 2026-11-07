#pragma once

#include <algorithm>
#include <array>

namespace vintagevisual
{
constexpr int maxSlots = 5;

// The DOCK strip is a single row of up to this many panels (independent from the normal layouts above).
constexpr int maxDockSlots = 8;

struct SlotRect
{
    int x = 0, y = 0, w = 0, h = 0;
};

// Panel arrangements. The first four are chosen by the user; `full5` is the FULL-mode grid.
enum class LayoutPreset
{
    single = 0,
    twoColumns,
    threeSplit,
    quad,
    full5
};

constexpr int numUserLayouts = 4;

constexpr int slotCount (LayoutPreset p) noexcept
{
    switch (p)
    {
        case LayoutPreset::single: return 1;
        case LayoutPreset::twoColumns: return 2;
        case LayoutPreset::threeSplit: return 3;
        case LayoutPreset::quad: return 4;
        case LayoutPreset::full5: return 5;
    }
    return 1;
}

inline const char* layoutName (LayoutPreset p) noexcept
{
    switch (p)
    {
        case LayoutPreset::single: return "Single";
        case LayoutPreset::twoColumns: return "Two";
        case LayoutPreset::threeSplit: return "Three";
        case LayoutPreset::quad: return "Four";
        case LayoutPreset::full5: return "Full";
    }
    return "";
}

// Slot rectangles inside a width x height area, separated by `gap` pixels. Pure geometry (no JUCE).
inline std::array<SlotRect, maxSlots> computeSlots (LayoutPreset preset, int width, int height, int gap) noexcept
{
    std::array<SlotRect, maxSlots> s {};
    const int W = std::max (1, width), H = std::max (1, height);

    auto halves = [&] (int total, int& first, int& second)
    {
        first = std::max (1, (total - gap) / 2);
        second = std::max (1, total - gap - first);
    };

    switch (preset)
    {
        case LayoutPreset::single:
            s[0] = { 0, 0, W, H };
            break;

        case LayoutPreset::twoColumns:
        {
            int a, b;
            halves (W, a, b);
            s[0] = { 0, 0, a, H };
            s[1] = { a + gap, 0, b, H };
            break;
        }

        case LayoutPreset::threeSplit:
        {
            const int left = std::max (1, (W - gap) * 3 / 5), right = std::max (1, W - gap - left);
            int top, bottom;
            halves (H, top, bottom);
            s[0] = { 0, 0, left, H };
            s[1] = { left + gap, 0, right, top };
            s[2] = { left + gap, top + gap, right, bottom };
            break;
        }

        case LayoutPreset::quad:
        {
            int cw, cw2, rh, rh2;
            halves (W, cw, cw2);
            halves (H, rh, rh2);
            s[0] = { 0, 0, cw, rh };
            s[1] = { cw + gap, 0, cw2, rh };
            s[2] = { 0, rh + gap, cw, rh2 };
            s[3] = { cw + gap, rh + gap, cw2, rh2 };
            break;
        }

        case LayoutPreset::full5:
        {
            const int left = std::max (1, (W - gap) / 2), right = std::max (1, W - gap - left), rx = left + gap;
            const int usable = std::max (3, H - 2 * gap);
            const int h1 = usable * 3 / 10, h2 = usable * 3 / 10, h3 = std::max (1, usable - h1 - h2);
            int a, b;
            halves (right, a, b);
            s[0] = { 0, 0, left, H };
            s[1] = { rx, 0, right, h1 };
            s[2] = { rx, h1 + gap, right, h2 };
            s[3] = { rx, h1 + gap + h2 + gap, a, h3 };
            s[4] = { rx + a + gap, h1 + gap + h2 + gap, b, h3 };
            break;
        }
    }

    return s;
}

// DOCK strip: `count` (1..maxDockSlots) panels side by side, each as tall as the area, separated by `gap` pixels.
// Widths differ by at most one pixel and the row always ends exactly at `width`. Pure geometry (no JUCE).
inline std::array<SlotRect, maxDockSlots> computeDockSlots (int count, int width, int height, int gap) noexcept
{
    std::array<SlotRect, maxDockSlots> s {};
    const int n = std::clamp (count, 1, maxDockSlots);
    const int W = std::max (n, width), H = std::max (1, height);
    const int usable = std::max (n, W - gap * (n - 1));
    const int base = usable / n, extra = usable % n;

    int x = 0;
    for (int i = 0; i < n; ++i)
    {
        const int w = base + (i < extra ? 1 : 0);
        s[(size_t) i] = { x, 0, w, H };
        x += w + gap;
    }

    return s;
}
} // namespace vintagevisual
