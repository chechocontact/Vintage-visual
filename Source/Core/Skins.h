#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace vintagevisual
{
// Colours are 0xRRGGBB. Kept free of JUCE so contrast can be tested in CoreTests.
struct SkinColors
{
    std::uint32_t canvas, surface, elevated;
    std::uint32_t control, controlHover, controlPressed;
    std::uint32_t textPrimary, textSecondary, textOnAccent;
    std::uint32_t separator, controlBorder;
    std::uint32_t accent, focus, warning;
    std::uint32_t displayBackground, displayText, displayDim;  // lit "screen" areas (visualizers)
    std::uint32_t traceLeft, traceRight, grid;                 // visualizer drawing colours
};

struct Skin
{
    const char* name;
    SkinColors colors;
};

constexpr int numSkins = 9;

// Index 0 is the default. Every skin must pass the WCAG contrast tests in CoreTests.
constexpr std::array<Skin, numSkins> skins { {
    { "Vintage",
      { 0x4A3424, 0xE6D9B8, 0xF1E4B8,
        0xD8C9A0, 0xCDBD90, 0xBFAE80,
        0x2B1D12, 0x5A4631, 0xF6EBCB,
        0xB9A878, 0x7A6A4A,
        0x7A4F12, 0x1F5FA8, 0xA8321F,
        0x1C1410, 0xFF9A1F, 0xB3702A,
        0xFF9A1F, 0xFFD36B, 0x5C4630 } },

    { "Night",
      { 0x17191E, 0x23262D, 0x2E323B,
        0x2B2F37, 0x353A44, 0x3F4550,
        0xE8EAED, 0xA9AFBA, 0x0D1520,
        0x3A3F4A, 0x6A7180,
        0x5AA9FF, 0x9CC8FF, 0xFFCC80,
        0x101216, 0x5AA9FF, 0x8F98A8,
        0x5AA9FF, 0xFF9CC8, 0x3A3F4A } },

    { "Day",
      { 0xE9ECF0, 0xFFFFFF, 0xF5F6F8,
        0xEEF0F3, 0xE2E5EA, 0xD5D9E0,
        0x1B1E24, 0x4A5160, 0xFFFFFF,
        0xD0D4DB, 0x6B7280,
        0x1F5FBF, 0x0B3D91, 0x8A4B00,
        0x1B1E24, 0x7CC4FF, 0x9AA3B2,
        0x7CC4FF, 0xFFB4D0, 0x4A5160 } },

    { "Nebula",
      { 0x0E0A1F, 0x1B1536, 0x261E48,
        0x2A2250, 0x352B63, 0x40357A,
        0xEDE9FF, 0xB9B0E6, 0x120A2B,
        0x3B3070, 0x8A7FC7,
        0xA78BFA, 0xD0C2FF, 0xFFB86B,
        0x0A0716, 0xC4B5FD, 0xA69BE0,
        0xA78BFA, 0xF0ABFC, 0x2E2560 } },

    { "Phosphor",
      { 0x050A05, 0x0C160C, 0x142214,
        0x132313, 0x1B311B, 0x244224,
        0xC8FFC8, 0x8FD68F, 0x021002,
        0x1F3A1F, 0x4FA64F,
        0x4DFF4D, 0xA8FFA8, 0xFFD966,
        0x020602, 0x4DFF4D, 0x3FBF3F,
        0x4DFF4D, 0xB6FF6B, 0x143014 } },

    { "Blueprint",
      { 0x0A2250, 0x103B80, 0x174A9C,
        0x1B52A8, 0x2260BD, 0x2A6FD1,
        0xFFFFFF, 0xD6E4FF, 0x0A2250,
        0x3F6FC4, 0x9CBCF2,
        0xFFFFFF, 0xFFE66D, 0xFFC857,
        0x0A2250, 0xFFFFFF, 0xB8CEF5,
        0xFFFFFF, 0xFFE66D, 0x2F5FB3 } },

    { "Synthwave",
      { 0x120024, 0x24103F, 0x341A5C,
        0x2E1752, 0x3D2169, 0x4C2A80,
        0xFFF0FF, 0xD9B8F5, 0x1A0033,
        0x4D2A80, 0xB07CE8,
        0xFF4FD8, 0x4DF3FF, 0xFFC24D,
        0x0B0016, 0xFF4FD8, 0xC77DDB,
        0xFF4FD8, 0x4DF3FF, 0x3A1B66 } },

    { "Pink",
      { 0xF6D6E4, 0xFFF0F6, 0xFFE3EE,
        0xFBD6E6, 0xF6C3DA, 0xEFAECB,
        0x3A0F26, 0x7A3A58, 0xFFFFFF,
        0xE8B5CC, 0xA0476E,
        0xC2185B, 0x6A1B9A, 0x9C2B00,
        0x3A0F26, 0xFFB3D1, 0xE59AB8,
        0xFF7FB0, 0xFFD1E3, 0x6A2A4C } },

    { "Celeste",
      { 0xCFE8F7, 0xF2FAFF, 0xE4F3FD,
        0xD6ECFA, 0xC4E2F5, 0xB0D6EE,
        0x0B2A3D, 0x2F5870, 0xFFFFFF,
        0xA9CFE6, 0x4F7F9C,
        0x0B6FA8, 0x6A1B9A, 0x9A3F00,
        0x0B2A3D, 0x9FDCFF, 0x7EBFE6,
        0x5CC8FF, 0xFFE08A, 0x1F4A66 } },
} };

inline double relativeLuminance (std::uint32_t rgb) noexcept
{
    auto channel = [] (std::uint32_t v)
    {
        const double c = (double) v / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
    };

    return 0.2126 * channel ((rgb >> 16) & 0xFF) + 0.7152 * channel ((rgb >> 8) & 0xFF) + 0.0722 * channel (rgb & 0xFF);
}

// WCAG 2.x contrast ratio, 1..21.
inline double contrastRatio (std::uint32_t a, std::uint32_t b) noexcept
{
    const double la = relativeLuminance (a), lb = relativeLuminance (b);
    return (std::fmax (la, lb) + 0.05) / (std::fmin (la, lb) + 0.05);
}
} // namespace vintagevisual
