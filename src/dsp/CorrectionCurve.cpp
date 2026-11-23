#include "dsp/CorrectionCurve.h"

#include <algorithm>
#include <cmath>

namespace novapitch::dsp
{

void CorrectionCurve::prepare (double frameRateHz) noexcept
{
    frameDt = frameRateHz > 0.0 ? 1.0 / frameRateHz : 1.0 / 375.0;
    reset();
}

void CorrectionCurve::reset() noexcept
{
    correction = 0.0f;
    noteTime = 0.0f;
    lastNote = -1;
    lastTauMs = settings.retuneMs;
}

float CorrectionCurve::toleranceWeight (float absErrorCents, float toleranceCents) noexcept
{
    if (toleranceCents <= 0.0f || absErrorCents <= toleranceCents)
        return 1.0f;
    const float x = (absErrorCents - toleranceCents) / kToleranceEdgeCents;
    if (x >= 1.0f)
        return 0.0f;
    // smooth (cosine) edge: 1 -> 0
    return 0.5f + 0.5f * std::cos (x * 3.14159265f);
}

float CorrectionCurve::process (bool voiced, float detectedMidi, const NoteTarget& target) noexcept
{
    if (! voiced || ! std::isfinite (detectedMidi))
    {
        reset();
        return 0.0f;
    }

    float error = 0.0f;
    if (target.hasTarget && target.correct)
    {
        error = (target.targetMidi - detectedMidi) * 100.0f;
        error *= toleranceWeight (std::abs (error), settings.toleranceCents);
    }

    if (target.note != lastNote)
    {
        lastNote = target.note;
        noteTime = 0.0f;
    }
    noteTime += static_cast<float> (frameDt);

    const float holdRamp = std::clamp ((noteTime - kHoldStartSeconds) / kHoldRampSeconds, 0.0f, 1.0f);
    const float tauMs = std::max (0.0f, settings.retuneMs) + std::clamp (settings.naturalHold, 0.0f, 1.0f) * holdRamp * kHoldExtraMs;
    lastTauMs = tauMs;

    if (tauMs < 0.5f)
        correction = error;
    else
    {
        const float alpha = 1.0f - std::exp (-static_cast<float> (frameDt) * 1000.0f / tauMs);
        correction += alpha * (error - correction);
    }
    return correction;
}

} // namespace novapitch::dsp
