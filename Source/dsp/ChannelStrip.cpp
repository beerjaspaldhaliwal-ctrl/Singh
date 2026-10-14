#include "ChannelStrip.h"

#include <algorithm>
#include <cmath>

namespace sam
{
namespace
{
    inline float coefFromMs (float ms, double sr)
    {
        return 1.0f - (float) std::exp (-1.0 / (std::max (0.01f, ms) * 0.001 * sr));
    }

    inline float fastDbToGain (float db) { return std::exp (db * 0.11512925464970229f); }

    /** Effective (amount-scaled) values shared by the audio path and the display. */
    struct Effective
    {
        float hpf, lpf;
        std::array<float, MixSettings::kMaxBands> eqGain;
        float toneLow, toneHigh;
    };

    Effective effective (const MixSettings& s, const Macros& m)
    {
        Effective e;
        const float a = std::clamp (m.amount, 0.0f, 1.0f);
        e.hpf = 10.0f * std::pow (std::max (10.0f, s.hpfHz) / 10.0f, a);
        e.lpf = s.lpfHz >= 20000.0f ? 30000.0f : 22000.0f * std::pow (s.lpfHz / 22000.0f, a);
        for (size_t i = 0; i < e.eqGain.size(); ++i)
            e.eqGain[i] = s.eq[i].type == EqBandType::Off ? 0.0f : s.eq[i].gainDb * a;
        e.toneLow = -1.5f * m.tone;
        e.toneHigh = 3.0f * m.tone;
        return e;
    }

    BiquadCoeffs eqCoeffs (EqBandType t, double sr, float f, float q, float g)
    {
        const auto type = t == EqBandType::LowShelf ? FilterType::LowShelf
                        : t == EqBandType::HighShelf ? FilterType::HighShelf
                                                     : FilterType::Peak;
        return BiquadCoeffs::make (type, sr, f, q, g);
    }

    // 4th-order Butterworth high-pass as two biquads
    constexpr double kButterQ[2] = { 0.5411961, 1.3065630 };
} // namespace

//==============================================================================
void Limiter::prepare (double sampleRate, int numChannels, float lookaheadMs)
{
    sr_ = sampleRate;
    numCh_ = std::max (1, numChannels);
    len_ = std::max (2, (int) std::lround (lookaheadMs * 0.001 * sr_));
    delay_.assign ((size_t) numCh_, std::vector<float> ((size_t) (len_ - 1), 0.0f));
    dqVal_.assign ((size_t) len_ + 1, 1.0f);
    dqIdx_.assign ((size_t) len_ + 1, 0);
    box_.assign ((size_t) len_, 1.0f);
    reset();
}

void Limiter::reset()
{
    for (auto& d : delay_)
        std::fill (d.begin(), d.end(), 0.0f);
    delayPos_ = 0;
    dqHead_ = dqSize_ = 0;
    sampleIndex_ = 0;
    std::fill (box_.begin(), box_.end(), 1.0f);
    boxPos_ = 0;
    boxSum_ = (double) len_;
    release_ = 1.0f;
    lastGrDb_ = 0.0f;
}

void Limiter::process (float* const* ch, int numChannels, int numSamples, bool active, float ceilingGain, float releaseMs)
{
    numChannels = std::min (numChannels, numCh_);
    const int dlen = len_ - 1;
    const int cap = (int) dqVal_.size();
    const float relAlpha = coefFromMs (releaseMs, sr_);
    float minGain = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float gain = 1.0f;

        if (active)
        {
            float peak = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                peak = std::max (peak, std::abs (ch[c][i]));
            const float req = peak > ceilingGain ? ceilingGain / peak : 1.0f;

            // sliding minimum over the last len_ samples
            while (dqSize_ > 0 && dqVal_[(size_t) ((dqHead_ + dqSize_ - 1) % cap)] >= req)
                --dqSize_;
            const int tail = (dqHead_ + dqSize_) % cap;
            dqVal_[(size_t) tail] = req;
            dqIdx_[(size_t) tail] = sampleIndex_;
            ++dqSize_;
            while (dqIdx_[(size_t) dqHead_] <= sampleIndex_ - len_)
            {
                dqHead_ = (dqHead_ + 1) % cap;
                --dqSize_;
            }
            const float held = dqVal_[(size_t) dqHead_];

            release_ = std::min (held, release_ + (1.0f - release_) * relAlpha);

            boxSum_ += (double) release_ - box_[(size_t) boxPos_];
            box_[(size_t) boxPos_] = release_;
            if (++boxPos_ >= len_)
            {
                boxPos_ = 0;
                boxSum_ = 0.0; // re-sum to remove accumulated rounding error
                for (float v : box_)
                    boxSum_ += v;
            }
            gain = (float) (boxSum_ / len_);
            ++sampleIndex_;
        }

        minGain = std::min (minGain, gain);

        for (int c = 0; c < numChannels; ++c)
        {
            auto& d = delay_[(size_t) c];
            const float delayed = d[(size_t) delayPos_];
            d[(size_t) delayPos_] = ch[c][i];
            float y = delayed * gain;
            if (active)
                y = std::clamp (y, -ceilingGain, ceilingGain);
            ch[c][i] = y;
        }
        if (++delayPos_ >= dlen)
            delayPos_ = 0;
    }

    if (! active && (release_ < 1.0f || dqSize_ > 0))
    {
        // re-arm cleanly for when the limiter is switched on again
        dqHead_ = dqSize_ = 0;
        std::fill (box_.begin(), box_.end(), 1.0f);
        boxSum_ = (double) len_;
        boxPos_ = 0;
        release_ = 1.0f;
    }

    lastGrDb_ = gainToDb (minGain);
}

//==============================================================================
void ChannelStrip::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr_ = sampleRate;
    numCh_ = std::clamp (numChannels, 1, kMaxCh);
    limiter_.prepare (sr_, numCh_, 5.0f);
    dry_.assign ((size_t) numCh_, std::vector<float> ((size_t) std::max (1, maxBlockSize), 0.0f));
    dryDelay_.assign ((size_t) numCh_, std::vector<float> ((size_t) std::max (1, limiter_.latency()), 0.0f));
    reset();
}

void ChannelStrip::reset()
{
    for (auto& stage : hpf_)
        for (auto& b : stage)
            b.reset();
    for (auto& b : lpf_)
        b.reset();
    for (auto& band : eq_)
        for (auto& b : band)
            b.reset();
    for (auto& b : deessBand_)
        b.reset();
    deessDetect_.reset();
    sideLow_.reset();
    deessBandEnv_ = deessFullEnv_ = 0.0f;
    deessGain_ = 1.0f;
    compEnvMs_ = compGrState_ = 0.0f;
    duckState_ = 0.0f;
    linkEnv_ = 0.0f;
    linkPeak_ = 1.0e-4f;
    limiter_.reset();
    for (auto& d : dryDelay_)
        std::fill (d.begin(), d.end(), 0.0f);
    dryDelayPos_ = 0;
    first_ = true;
    hpfApplied_ = lpfApplied_ = monoApplied_ = deessFreqApplied_ = -1.0f;
    eqActive_.fill (false);
    eqAppliedGain_.fill (1000.0f);
    eqAppliedFreq_.fill (-1.0f);
}

void ChannelStrip::designDisplayFilters (const MixSettings& s, const Macros& m, double sr, std::vector<BiquadCoeffs>& out)
{
    out.clear();
    const auto e = effective (s, m);
    for (double q : kButterQ)
        out.push_back (BiquadCoeffs::make (FilterType::HighPass, sr, e.hpf, q, 0.0));
    if (e.lpf < 20000.0f)
        out.push_back (BiquadCoeffs::make (FilterType::LowPass, sr, e.lpf, 0.707, 0.0));
    for (size_t i = 0; i < s.eq.size(); ++i)
        if (s.eq[i].type != EqBandType::Off && std::abs (e.eqGain[i]) > 0.01f)
            out.push_back (eqCoeffs (s.eq[i].type, sr, s.eq[i].freq, s.eq[i].q, e.eqGain[i]));
    if (std::abs (m.tone) > 0.001f)
    {
        out.push_back (BiquadCoeffs::make (FilterType::LowShelf, sr, 200.0, 0.7, e.toneLow));
        out.push_back (BiquadCoeffs::make (FilterType::HighShelf, sr, 6000.0, 0.7, e.toneHigh));
    }
}

void ChannelStrip::updateFilters (const Macros& m, const LinkInputs& link)
{
    const auto& s = settings_;
    const auto e = effective (s, m);
    const float a = std::clamp (m.amount, 0.0f, 1.0f);
    const float k = first_ ? 1.0f : 0.25f; // per-block smoothing

    auto smooth = [k] (float& v, float target) { v += (target - v) * k; };

    smooth (smTrim_, s.inputTrimDb * a);
    smooth (smOut_, m.outputDb);
    smHpf_ = first_ ? e.hpf : smHpf_ * std::pow (e.hpf / smHpf_, k);
    smLpf_ = first_ ? e.lpf : smLpf_ * std::pow (e.lpf / smLpf_, k);

    if (std::abs (smHpf_ - hpfApplied_) > 0.05f)
    {
        hpfApplied_ = smHpf_;
        for (int st = 0; st < 2; ++st)
        {
            const auto c = BiquadCoeffs::make (FilterType::HighPass, sr_, smHpf_, kButterQ[st], 0.0);
            for (auto& b : hpf_[(size_t) st])
                b.setCoeffs (c);
        }
    }
    if (smLpf_ < 20000.0f && std::abs (smLpf_ - lpfApplied_) > 1.0f)
    {
        lpfApplied_ = smLpf_;
        const auto c = BiquadCoeffs::make (FilterType::LowPass, sr_, smLpf_, 0.707, 0.0);
        for (auto& b : lpf_)
            b.setCoeffs (c);
    }

    // EQ bands: 0..7 AI bands, 8/9 tone tilt, 10 dynamic vocal pocket
    for (int i = 0; i < kNumEq; ++i)
    {
        EqBandType type = EqBandType::Peak;
        float freq = 1000.0f, q = 1.0f, target = 0.0f;

        if (i < MixSettings::kMaxBands)
        {
            const auto& b = s.eq[(size_t) i];
            type = b.type;
            freq = b.freq;
            q = b.q;
            target = e.eqGain[(size_t) i];
            if (type == EqBandType::Off)
            {
                type = EqBandType::Peak;
                target = 0.0f;
            }
        }
        else if (i == MixSettings::kMaxBands)
        {
            type = EqBandType::LowShelf;
            freq = 200.0f;
            q = 0.7f;
            target = e.toneLow;
        }
        else if (i == MixSettings::kMaxBands + 1)
        {
            type = EqBandType::HighShelf;
            freq = 6000.0f;
            q = 0.7f;
            target = e.toneHigh;
        }
        else
        {
            const bool pocket = s.unmaskDepthDb > 0.0f && link.vocalFreqHz > 0.0f && s.role != Role::LeadVocal;
            freq = pocket ? link.vocalFreqHz : (eqAppliedFreq_[(size_t) i] > 0 ? eqAppliedFreq_[(size_t) i] : 2500.0f);
            q = 1.2f;
            target = pocket ? -s.unmaskDepthDb * a * std::clamp (link.vocalActivity, 0.0f, 1.0f) : 0.0f;
        }

        float& g = smEqGain_[(size_t) i];
        smooth (g, target);
        if (std::abs (g) < 0.01f && std::abs (target) < 0.01f)
            g = 0.0f;

        const bool active = std::abs (g) >= 0.01f;
        if (active && (std::abs (g - eqAppliedGain_[(size_t) i]) > 0.02f || std::abs (freq - eqAppliedFreq_[(size_t) i]) > 0.5f))
        {
            eqCoeffs_[(size_t) i] = eqCoeffs (type, sr_, freq, q, g);
            for (auto& b : eq_[(size_t) i])
                b.setCoeffs (eqCoeffs_[(size_t) i]);
            eqAppliedGain_[(size_t) i] = g;
            eqAppliedFreq_[(size_t) i] = freq;
        }
        if (! active && eqActive_[(size_t) i])
            for (auto& b : eq_[(size_t) i])
                b.reset();
        eqActive_[(size_t) i] = active;
    }

    if (s.deessOn && std::abs (s.deessHz - deessFreqApplied_) > 1.0f)
    {
        deessFreqApplied_ = s.deessHz;
        const auto c = BiquadCoeffs::make (FilterType::BandPass, sr_, s.deessHz, 1.5, 0.0);
        for (auto& b : deessBand_)
            b.setCoeffs (c);
        deessDetect_.setCoeffs (c);
    }

    if (s.monoBelowHz > 0.0f && std::abs (s.monoBelowHz - monoApplied_) > 0.5f)
    {
        monoApplied_ = s.monoBelowHz;
        sideLow_.setCoeffs (BiquadCoeffs::make (FilterType::LowPass, sr_, s.monoBelowHz, 0.707, 0.0));
    }

    first_ = false;
}

void ChannelStrip::process (float* const* ch, int numChannels, int numSamples, const Macros& m, const LinkInputs& link)
{
    numChannels = std::min (numChannels, numCh_);
    if (numChannels <= 0 || numSamples <= 0)
        return;
    numSamples = std::min (numSamples, (int) dry_[0].size());

    const auto& s = settings_;
    const float a = std::clamp (m.amount, 0.0f, 1.0f);
    const float mix = std::clamp (m.mix, 0.0f, 1.0f);

    for (int c = 0; c < numChannels; ++c)
        std::copy (ch[c], ch[c] + numSamples, dry_[(size_t) c].begin());

    const float prevTrim = smTrim_;
    updateFilters (m, link);

    // ---- trim (ramped) + filters + EQ
    for (int c = 0; c < numChannels; ++c)
    {
        float* x = ch[c];
        const float g0 = fastDbToGain (prevTrim), g1 = fastDbToGain (smTrim_);
        const float step = (g1 - g0) / (float) numSamples;
        for (int i = 0; i < numSamples; ++i)
            x[i] *= g0 + step * (float) i;

        for (auto& stage : hpf_)
        {
            auto& f = stage[(size_t) c];
            for (int i = 0; i < numSamples; ++i)
                x[i] = f.process (x[i]);
        }
        if (smLpf_ < 20000.0f)
            for (int i = 0; i < numSamples; ++i)
                x[i] = lpf_[(size_t) c].process (x[i]);

        for (int b = 0; b < kNumEq; ++b)
        {
            if (! eqActive_[(size_t) b])
                continue;
            auto& f = eq_[(size_t) b][(size_t) c];
            for (int i = 0; i < numSamples; ++i)
                x[i] = f.process (x[i]);
        }
    }

    // ---- de-esser (relative, split-band)
    float maxDeess = 0.0f;
    if (s.deessOn && a > 0.0f)
    {
        const float atk = coefFromMs (1.0f, sr_), rel = coefFromMs (60.0f, sr_);
        const float range = s.deessRangeDb * a;
        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mono += ch[c][i];
            mono /= (float) numChannels;

            const float band = std::abs (deessDetect_.process (mono));
            const float full = std::abs (mono);
            deessBandEnv_ += (band - deessBandEnv_) * (band > deessBandEnv_ ? atk : rel);
            deessFullEnv_ += (full - deessFullEnv_) * (full > deessFullEnv_ ? atk : rel);

            float redDb = 0.0f;
            if (deessBandEnv_ > 1.0e-5f)
            {
                const float relDb = 20.0f * std::log10 (deessBandEnv_ / std::max (deessFullEnv_, 1.0e-6f));
                redDb = std::clamp (relDb - s.deessThreshDb, 0.0f, range);
            }
            const float target = fastDbToGain (-redDb);
            deessGain_ += (target - deessGain_) * (target < deessGain_ ? atk : rel);
            maxDeess = std::max (maxDeess, -gainToDb (deessGain_));

            for (int c = 0; c < numChannels; ++c)
            {
                const float bp = deessBand_[(size_t) c].process (ch[c][i]);
                ch[c][i] += (deessGain_ - 1.0f) * bp;
            }
        }
    }
    deessGrDb.store (maxDeess, std::memory_order_relaxed);

    // ---- compressor (stereo-linked RMS, soft knee, optional parallel)
    float maxComp = 0.0f;
    {
        const float dyn = std::clamp (m.dynamics, 0.0f, 2.0f) * a;
        const float ratio = 1.0f + (s.compRatio - 1.0f) * dyn;
        if (ratio > 1.001f)
        {
            const float slope = 1.0f / ratio - 1.0f;
            const float baseSlope = 1.0f / s.compRatio - 1.0f;
            const float makeup = s.compMakeupDb * (baseSlope != 0.0f ? slope / baseSlope : 0.0f);
            const float T = s.compThreshDb, W = std::max (0.01f, s.compKneeDb);
            const float rmsA = coefFromMs (5.0f, sr_);
            const float atk = coefFromMs (s.compAttackMs, sr_), rel = coefFromMs (s.compReleaseMs, sr_);
            const float wet = s.compMix;

            for (int i = 0; i < numSamples; ++i)
            {
                float ms = 0.0f;
                for (int c = 0; c < numChannels; ++c)
                    ms += ch[c][i] * ch[c][i];
                ms /= (float) numChannels;
                compEnvMs_ += (ms - compEnvMs_) * rmsA;

                const float lvl = 10.0f * std::log10 (compEnvMs_ + 1.0e-12f);
                const float over = lvl - T;
                float gr;
                if (2.0f * over < -W)
                    gr = 0.0f;
                else if (2.0f * std::abs (over) <= W)
                    gr = slope * (over + W * 0.5f) * (over + W * 0.5f) / (2.0f * W);
                else
                    gr = slope * over;

                compGrState_ += (gr - compGrState_) * (gr < compGrState_ ? atk : rel);
                maxComp = std::max (maxComp, -compGrState_);

                const float g = fastDbToGain (compGrState_ + makeup);
                const float gMix = 1.0f - wet + wet * g;
                for (int c = 0; c < numChannels; ++c)
                    ch[c][i] *= gMix;
            }
        }
        else
        {
            compGrState_ = 0.0f;
        }
    }
    compGrDb.store (maxComp, std::memory_order_relaxed);

    // ---- saturation (level-referenced soft clipper, blended)
    {
        const float d = std::clamp (s.saturation * a * std::clamp (m.warmth, 0.0f, 2.0f), 0.0f, 2.0f);
        if (d > 0.001f)
        {
            const float ref = dbToGain (s.satRefDb);
            const float invRef = 1.0f / ref;
            const float k = 1.0f + 2.0f * d;
            const float invK = 1.0f / k;
            const float wet = std::min (1.0f, d * 1.5f);
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = ch[c][i];
                    const float sat = ref * std::tanh (k * x * invRef) * invK;
                    ch[c][i] = x + wet * (sat - x);
                }
        }
    }

    // ---- kick ducking (bass / 808)
    float maxDuck = 0.0f;
    if (s.duckFromKick && a > 0.0f)
    {
        const float target = -s.duckDepthDb * a * std::clamp (link.kickEnvelope, 0.0f, 1.0f);
        const float atk = coefFromMs (2.0f, sr_), rel = coefFromMs (80.0f, sr_);
        for (int i = 0; i < numSamples; ++i)
        {
            duckState_ += (target - duckState_) * (target < duckState_ ? atk : rel);
            const float g = fastDbToGain (duckState_);
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] *= g;
            maxDuck = std::max (maxDuck, -duckState_);
        }
    }
    else
    {
        duckState_ = 0.0f;
    }
    duckGrDb.store (maxDuck, std::memory_order_relaxed);

    // ---- stereo width + mono bass
    if (numChannels == 2)
    {
        const float w = (1.0f + (s.width - 1.0f) * a) * std::clamp (m.width, 0.0f, 2.0f);
        const bool mono = s.monoBelowHz > 0.0f && a > 0.0f;
        if (std::abs (w - 1.0f) > 0.001f || mono)
        {
            float* L = ch[0];
            float* R = ch[1];
            for (int i = 0; i < numSamples; ++i)
            {
                const float mid = 0.5f * (L[i] + R[i]);
                float side = 0.5f * (L[i] - R[i]);
                if (mono)
                    side -= a * sideLow_.process (side);
                side *= w;
                L[i] = mid + side;
                R[i] = mid - side;
            }
        }
    }

    // ---- limiter (master) — always run for constant latency
    {
        const bool limOn = s.limiterOn && a > 0.0f;
        if (limOn)
        {
            // drive into a soft clipper (shaves fast transients cleanly), then the limiter
            const float drive = dbToGain (s.limiterDriveDb * a);
            const float clipLevel = dbToGain (s.limiterCeilingDb + 1.0f);
            const float invClip = 1.0f / clipLevel;
            constexpr float knee = 0.6f, span = 1.0f - knee;
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = ch[c][i] * drive;
                    const float u = std::abs (x) * invClip;
                    ch[c][i] = u <= knee ? x : std::copysign (clipLevel * (knee + span * std::tanh ((u - knee) / span)), x);
                }
        }
        limiter_.process (ch, numChannels, numSamples, limOn, dbToGain (s.limiterCeilingDb), 120.0f);
        limiterGrDb.store (limOn ? -limiter_.lastGainReductionDb() : 0.0f, std::memory_order_relaxed);
    }

    // ---- output gain, dry/wet (dry delayed to match latency), link envelope
    const int lat = limiter_.latency();
    const float outG = fastDbToGain (smOut_);
    const float envAtk = coefFromMs (1.0f, sr_), envRel = coefFromMs (80.0f, sr_);
    const float peakDecay = 1.0f - coefFromMs (3000.0f, sr_);
    int pos = dryDelayPos_;
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            float dryS = dry_[(size_t) c][(size_t) i];
            if (lat > 0)
            {
                auto& d = dryDelay_[(size_t) c];
                const float delayed = d[(size_t) pos];
                d[(size_t) pos] = dryS;
                dryS = delayed;
            }
            const float y = outG * (mix * ch[c][i] + (1.0f - mix) * dryS);
            ch[c][i] = y;
            mono += y;
        }
        if (lat > 0 && ++pos >= lat)
            pos = 0;

        const float v = std::abs (mono) / (float) numChannels;
        linkEnv_ += (v - linkEnv_) * (v > linkEnv_ ? envAtk : envRel);
        linkPeak_ = std::max (linkEnv_, linkPeak_ * peakDecay);
    }
    dryDelayPos_ = pos;

    const float norm = linkEnv_ > 1.0e-3f ? linkEnv_ / std::max (linkPeak_, 1.0e-4f) : 0.0f;
    outputEnvelope.store (std::clamp (norm, 0.0f, 1.0f), std::memory_order_relaxed);
}

} // namespace sam
