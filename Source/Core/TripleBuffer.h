#pragma once

#include <array>
#include <atomic>

namespace vintagevisual
{
// Wait-free "latest value" mailbox: ONE writer thread, ONE reader thread.
// The writer never blocks and never allocates; the reader always gets a complete value (no tearing).
// Intended direction: audio thread -> message thread (readings, meters).
template <typename T>
class TripleBuffer final
{
public:
    // Writer thread only (audio thread). T must be cheap to copy.
    void push (const T& value) noexcept
    {
        slots[(size_t) back] = value;
        back = middle.exchange (back | dirtyBit, std::memory_order_acq_rel) & indexMask;
    }

    // Reader thread only. Returns true and fills `out` only when a newer value was published.
    bool pull (T& out) noexcept
    {
        if ((middle.load (std::memory_order_relaxed) & dirtyBit) == 0)
            return false;

        front = middle.exchange (front, std::memory_order_acq_rel) & indexMask;
        out = slots[(size_t) front];
        return true;
    }

private:
    static constexpr int dirtyBit = 4;
    static constexpr int indexMask = 3;

    std::array<T, 3> slots {};
    std::atomic<int> middle { 1 };
    int back = 0;   // owned by the writer
    int front = 2;  // owned by the reader
};
} // namespace vintagevisual
