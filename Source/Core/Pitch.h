#pragma once

#include <cmath>
#include <vector>

namespace vintagevisual
{
struct NoteInfo
{
    int midi = 69;
    int pitchClass = 9;  // 0 = C .. 11 = B
    int octave = 4;
    double cents = 0.0;  // -50 .. +50 relative to the nearest note
};

inline const char* noteName (int pitchClass) noexcept
{
    static constexpr const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pitchClass % 12) + 12) % 12];
}

inline NoteInfo describeNote (double frequencyHz, double referenceA4 = 440.0) noexcept
{
    NoteInfo n;
    const double exact = 69.0 + 12.0 * std::log2 (frequencyHz / referenceA4);
    n.midi = (int) std::lround (exact);
    n.cents = (exact - (double) n.midi) * 100.0;
    n.pitchClass = ((n.midi % 12) + 12) % 12;
    n.octave = (int) std::floor ((double) n.midi / 12.0) - 1;
    return n;
}

struct PitchResult
{
    bool valid = false;
    double frequencyHz = 0.0;
    double confidence = 0.0;  // 0..1
};

// Monophonic pitch detector (YIN: difference function, cumulative mean normalisation, parabolic interpolation).
// Works on a decimated copy of the input (about 24 kHz) so the cost does not grow with the sample rate.
// UI thread only (allocates in prepare()).
class PitchDetector final
{
public:
    void prepare (double sampleRate, double minHz = 30.0, double maxHz = 2000.0);

    // Number of input samples detect() reads (newest audio, oldest first).
    int getRequiredSamples() const noexcept { return requiredSamples; }

    PitchResult detect (const float* mono) noexcept;

private:
    static constexpr int windowSize = 1024;

    int decimation = 1, tauMin = 2, tauMax = 100, requiredSamples = 0;
    double effectiveRate = 24000.0;
    std::vector<float> signal;
    std::vector<float> difference, normalised;
};
} // namespace vintagevisual
