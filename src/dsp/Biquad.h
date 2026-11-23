#pragma once

#include <cmath>
#include <numbers>

namespace novapitch::dsp
{

/** Minimal RBJ biquad (transposed direct form II, double precision state so
    very low cutoffs stay stable at high sample rates). Real-time safe. */
class Biquad
{
public:
    void setLowPass (double sampleRate, double cutoffHz, double q) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * cutoffHz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cw = std::cos (w0);
        setNormalised ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    void setHighPass (double sampleRate, double cutoffHz, double q) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * cutoffHz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cw = std::cos (w0);
        setNormalised ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    /** RBJ peaking EQ ("campana"). */
    void setPeak (double sampleRate, double centreHz, double q, double gainDb) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * centreHz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cw = std::cos (w0);
        setNormalised (1.0 + alpha * a, -2.0 * cw, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cw, 1.0 - alpha / a);
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    float process (float x) noexcept
    {
        const double in = x;
        const double out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return static_cast<float> (out);
    }

private:
    void setNormalised (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

} // namespace novapitch::dsp
