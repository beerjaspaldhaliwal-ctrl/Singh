#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace sam
{
/** Small iterative radix-2 FFT. Twiddles and bit-reversal are precomputed, so
    perform() never allocates and can be used on the audio thread. */
class FFT
{
public:
    explicit FFT (int order = 12) { setOrder (order); }

    void setOrder (int order)
    {
        size_ = 1 << order;
        twiddles_.resize ((size_t) size_ / 2);
        for (int i = 0; i < size_ / 2; ++i)
            twiddles_[(size_t) i] = std::polar (1.0, -2.0 * 3.14159265358979323846 * i / size_);

        bitrev_.resize ((size_t) size_);
        for (int i = 0; i < size_; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b))
                    r |= 1 << (order - 1 - b);
            bitrev_[(size_t) i] = r;
        }
    }

    int size() const noexcept { return size_; }

    void perform (std::complex<double>* data) const noexcept
    {
        for (int i = 0; i < size_; ++i)
        {
            const int j = bitrev_[(size_t) i];
            if (j > i)
                std::swap (data[i], data[j]);
        }

        for (int len = 2; len <= size_; len <<= 1)
        {
            const int half = len / 2;
            const int step = size_ / len;
            for (int start = 0; start < size_; start += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const auto t = twiddles_[(size_t) (k * step)] * data[start + k + half];
                    const auto u = data[start + k];
                    data[start + k] = u + t;
                    data[start + k + half] = u - t;
                }
            }
        }
    }

private:
    int size_ = 0;
    std::vector<std::complex<double>> twiddles_;
    std::vector<int> bitrev_;
};

} // namespace sam
