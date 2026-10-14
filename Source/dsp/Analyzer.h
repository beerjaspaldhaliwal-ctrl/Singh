#pragma once

#include "Biquad.h"
#include "FFT.h"
#include "MixTypes.h"

#include <array>
#include <complex>
#include <vector>

namespace sam
{
/** Short-term (3 s) BS.1770 loudness meter, safe to run on the audio thread. */
class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* left, const float* right, int numSamples);
    float shortTermLufs() const noexcept { return shortTerm_; }
    float momentaryLufs() const noexcept { return momentary_; }

private:
    double sr_ = 48000.0;
    Biquad kL1_, kL2_, kR1_, kR2_;
    int blockLen_ = 4800, blockPos_ = 0;
    double blockSum_ = 0.0;
    std::array<double, 30> blocks_ {}; // 30 x 100 ms
    int blockIndex_ = 0, blocksFilled_ = 0;
    float shortTerm_ = -70.0f, momentary_ = -70.0f;
};

/** The "ears" of the AI. While listening it accumulates long-term spectral,
    loudness, dynamics and stereo statistics of the incoming signal. process()
    is allocation-free; finalize() turns the statistics into an AnalysisResult. */
class Analyzer
{
public:
    Analyzer();

    void prepare (double sampleRate);
    void reset();

    /** right may equal left for mono input. */
    void process (const float* left, const float* right, int numSamples);

    double activeSeconds() const noexcept { return activeWindows_ * 0.05; }
    AnalysisResult finalize() const;

    /** Audio heard while listening (non-silent parts, up to kCaptureSeconds), used to
        render the chain offline and calibrate loudness. Valid after listening stops. */
    static constexpr double kCaptureSeconds = 15.0;
    const std::vector<float>& capturedLeft() const noexcept { return capL_; }
    const std::vector<float>& capturedRight() const noexcept { return capR_; }
    int capturedSamples() const noexcept { return capPos_; }

    /** Heuristic instrument classifier used when the role is set to Auto. */
    static Role classify (const AnalysisResult& r);

private:
    void runFft();
    void endRmsWindow();
    void endLoudnessBlock();

    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kHistBins = 240; // 0.5 dB bins, -120 .. 0 dBFS
    static constexpr int kMaxLoudnessBlocks = 20000;

    double sr_ = 48000.0;
    FFT fft_ { kFftOrder };
    std::vector<float> window_;
    std::vector<float> ring_;
    std::vector<std::complex<double>> fftBuf_;
    std::vector<double> powerAccum_;
    int ringPos_ = 0, hopCounter_ = 0;
    long fftFrames_ = 0;

    // 50 ms RMS windows (activity gate + level histogram)
    int rmsLen_ = 2400, rmsPos_ = 0;
    double rmsSum_ = 0.0;
    std::array<long, kHistBins> hist_ {};
    long activeWindows_ = 0;

    // 100 ms blocks for loudness gating
    Biquad kL1_, kL2_, kR1_, kR2_;
    int blockLen_ = 4800, blockPos_ = 0;
    double blockK_ = 0.0;
    std::array<double, 4> lastSubBlocks_ {};
    int subBlockCount_ = 0;
    std::vector<float> loudnessBlocks_; // 400 ms momentary loudness, 75% overlap
    // 1 s windows for crest factor (captures transients vs. gaps between hits)
    int crestLen_ = 48000, crestPos_ = 0;
    double crestMs_ = 0.0, crestPeak_ = 0.0;
    double crestSum_ = 0.0;
    long crestCount_ = 0;

    // stereo
    double sLL_ = 0, sRR_ = 0, sLR_ = 0, sMM_ = 0, sSS_ = 0, sLowMM_ = 0, sLowSS_ = 0;
    Biquad lowM_, lowS_;

    double peak_ = 0.0;

    std::vector<float> capL_, capR_;
    std::vector<float> winL_, winR_; // current 50 ms window, committed if not silent
    int capPos_ = 0;
};

/** Gated BS.1770 integrated loudness of a buffer (right may be null for mono). */
float measureIntegratedLufs (const float* left, const float* right, int numSamples, double sampleRate);

} // namespace sam
