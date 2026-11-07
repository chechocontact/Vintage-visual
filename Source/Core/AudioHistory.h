#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

namespace vintagevisual
{
// Stereo history ring: the audio thread appends, the UI thread reads copies of recent audio.
// The writer never blocks, never allocates and is never held back by a slow reader.
// Samples are relaxed atomics, so a reader that falls a whole lap behind sees newer data, not UB.
class AudioHistory final
{
public:
    // Message thread only (allocates). Capacity is rounded up to a power of two.
    void prepare (int minimumFrames)
    {
        std::uint64_t cap = 1024;
        while (cap < (std::uint64_t) minimumFrames) cap <<= 1;

        capacity = cap;
        mask = cap - 1;
        left = std::make_unique<std::atomic<float>[]> ((size_t) cap);
        right = std::make_unique<std::atomic<float>[]> ((size_t) cap);
        for (std::uint64_t i = 0; i < cap; ++i)
        {
            left[i].store (0.0f, std::memory_order_relaxed);
            right[i].store (0.0f, std::memory_order_relaxed);
        }
        written.store (0, std::memory_order_release);
    }

    // Audio thread only.
    void push (const float* l, const float* r, int numFrames) noexcept
    {
        if (capacity == 0) return;
        auto w = written.load (std::memory_order_relaxed);

        for (int i = 0; i < numFrames; ++i, ++w)
        {
            left[w & mask].store (l[i], std::memory_order_relaxed);
            right[w & mask].store (r[i], std::memory_order_relaxed);
        }

        written.store (w, std::memory_order_release);
    }

    std::uint64_t getCapacity() const noexcept { return capacity; }
    std::uint64_t getTotalFramesWritten() const noexcept { return written.load (std::memory_order_acquire); }

    // UI thread. Copies the most recent `numFrames` frames (oldest first). Returns frames actually copied.
    // Keep numFrames <= capacity / 2 so the writer cannot lap the copy.
    int copyLatest (float* l, float* r, int numFrames) const noexcept
    {
        const auto end = written.load (std::memory_order_acquire);
        const auto n = std::min<std::uint64_t> ((std::uint64_t) std::max (0, numFrames), std::min (end, capacity / 2));
        copyRange (end - n, (int) n, l, r);
        return (int) n;
    }

    // UI thread. Reads frames written since `cursor` (advanced on return), at most `maxFrames`.
    // If the reader fell too far behind, it skips ahead and reports the loss through `dropped`.
    int readNew (std::uint64_t& cursor, float* l, float* r, int maxFrames, std::uint64_t* dropped = nullptr) const noexcept
    {
        const auto end = written.load (std::memory_order_acquire);
        const auto limit = capacity / 2;

        if (end > cursor && end - cursor > limit)
        {
            if (dropped != nullptr) *dropped += (end - cursor) - limit;
            cursor = end - limit;
        }

        const auto n = std::min<std::uint64_t> ((std::uint64_t) std::max (0, maxFrames), end > cursor ? end - cursor : 0);
        copyRange (cursor, (int) n, l, r);
        cursor += n;
        return (int) n;
    }

private:
    void copyRange (std::uint64_t start, int n, float* l, float* r) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (start + (std::uint64_t) i) & mask;
            l[i] = left[idx].load (std::memory_order_relaxed);
            r[i] = right[idx].load (std::memory_order_relaxed);
        }
    }

    std::uint64_t capacity = 0, mask = 0;
    std::unique_ptr<std::atomic<float>[]> left, right;
    std::atomic<std::uint64_t> written { 0 };
};
} // namespace vintagevisual
