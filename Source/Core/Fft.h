#pragma once

#include <cmath>
#include <utility>
#include <vector>

namespace vintagevisual
{
// In-place radix-2 FFT (forward). UI-thread analysis tool: constructed once, then allocation-free.
class Fft final
{
public:
    explicit Fft (int order)
        : n (1 << order), cosTable ((size_t) n / 2), sinTable ((size_t) n / 2), reversed ((size_t) n)
    {
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * 3.14159265358979323846 * (double) i / (double) n;
            cosTable[(size_t) i] = (float) std::cos (a);
            sinTable[(size_t) i] = (float) std::sin (a);
        }

        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b)) r |= 1 << (order - 1 - b);
            reversed[(size_t) i] = r;
        }
    }

    int size() const noexcept { return n; }

    // re and im each hold size() values.
    void forward (float* re, float* im) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = reversed[(size_t) i];
            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;

            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosTable[(size_t) (k * step)], wi = sinTable[(size_t) (k * step)];
                    const int a = i + k, b = i + k + half;
                    const float vr = re[b] * wr - im[b] * wi;
                    const float vi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - vr;
                    im[b] = im[a] - vi;
                    re[a] += vr;
                    im[a] += vi;
                }
            }
        }
    }

private:
    int n;
    std::vector<float> cosTable, sinTable;
    std::vector<int> reversed;
};
} // namespace vintagevisual
