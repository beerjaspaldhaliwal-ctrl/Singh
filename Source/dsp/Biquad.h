#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace sam
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -120.0f)
{
    return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb;
}

enum class FilterType
{
    Peak,
    LowShelf,
    HighShelf,
    HighPass,
    LowPass,
    BandPass
};

/** RBJ-cookbook biquad coefficients (normalised so a0 == 1). */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs make (FilterType type, double sampleRate, double freq, double q, double gainDb)
    {
        freq = std::clamp (freq, 5.0, sampleRate * 0.49);
        q = std::max (q, 0.05);

        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cw = std::cos (w0);
        const double sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
            case FilterType::Peak:
                b0 = 1.0 + alpha * A;
                b1 = -2.0 * cw;
                b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A;
                a1 = -2.0 * cw;
                a2 = 1.0 - alpha / A;
                break;

            case FilterType::LowShelf:
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - s;
                break;
            }

            case FilterType::HighShelf:
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - s;
                break;
            }

            case FilterType::HighPass:
                b0 = (1 + cw) * 0.5;
                b1 = -(1 + cw);
                b2 = (1 + cw) * 0.5;
                a0 = 1 + alpha;
                a1 = -2 * cw;
                a2 = 1 - alpha;
                break;

            case FilterType::LowPass:
                b0 = (1 - cw) * 0.5;
                b1 = 1 - cw;
                b2 = (1 - cw) * 0.5;
                a0 = 1 + alpha;
                a1 = -2 * cw;
                a2 = 1 - alpha;
                break;

            case FilterType::BandPass: // constant 0 dB peak gain
                b0 = alpha;
                b1 = 0;
                b2 = -alpha;
                a0 = 1 + alpha;
                a1 = -2 * cw;
                a2 = 1 - alpha;
                break;
        }

        BiquadCoeffs c;
        c.b0 = b0 / a0;
        c.b1 = b1 / a0;
        c.b2 = b2 / a0;
        c.a1 = a1 / a0;
        c.a2 = a2 / a0;
        return c;
    }

    double magnitude (double freq, double sampleRate) const
    {
        const double w = 2.0 * kPi * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs (num / den);
    }

    double magnitudeDb (double freq, double sampleRate) const
    {
        return 20.0 * std::log10 (std::max (1.0e-9, magnitude (freq, sampleRate)));
    }
};

/** Transposed direct form II biquad, double precision state. */
class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) noexcept { c_ = c; }
    const BiquadCoeffs& coeffs() const noexcept { return c_; }
    void reset() noexcept { z1_ = z2_ = 0.0; }

    inline float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return (float) y;
    }

private:
    BiquadCoeffs c_;
    double z1_ = 0.0, z2_ = 0.0;
};

/** ITU-R BS.1770 K-weighting pre-filter (two biquads) for any sample rate. */
inline void makeKWeighting (double fs, BiquadCoeffs& stage1, BiquadCoeffs& stage2)
{
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        stage1.b0 = (Vh + Vb * K / Q + K * K) / a0;
        stage1.b1 = 2.0 * (K * K - Vh) / a0;
        stage1.b2 = (Vh - Vb * K / Q + K * K) / a0;
        stage1.a1 = 2.0 * (K * K - 1.0) / a0;
        stage1.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        stage2.b0 = 1.0;
        stage2.b1 = -2.0;
        stage2.b2 = 1.0;
        stage2.a1 = 2.0 * (K * K - 1.0) / a0;
        stage2.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

} // namespace sam
