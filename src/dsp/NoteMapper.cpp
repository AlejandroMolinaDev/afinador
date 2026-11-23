#include "dsp/NoteMapper.h"

#include <cmath>

namespace novapitch::dsp
{

NoteMapper::NoteMapper()
{
    overrides.fill (NoteState::FollowScale);
    recompute();
}

void NoteMapper::setKeyAndScale (int keyPitchClass, ScaleType newScale) noexcept
{
    const int k = wrap (keyPitchClass);
    if (k == key && newScale == scale)
        return;
    key = k;
    scale = newScale;
    recompute();
}

void NoteMapper::setNoteState (int pitchClass, NoteState state) noexcept
{
    auto& slot = overrides[static_cast<size_t> (wrap (pitchClass))];
    if (slot == state)
        return;
    slot = state;
    recompute();
}

void NoteMapper::recompute() noexcept
{
    const auto mask = scaleMask (scale);
    for (int pc = 0; pc < 12; ++pc)
    {
        const auto o = overrides[static_cast<size_t> (pc)];
        if (o != NoteState::FollowScale)
        {
            effective[static_cast<size_t> (pc)] = o;
            continue;
        }
        const int degree = wrap (pc - key);
        effective[static_cast<size_t> (pc)] = ((mask >> degree) & 1u) != 0 ? NoteState::Active : NoteState::Remove;
    }

    if (currentNote >= 0 && ! isValid (currentNote))
        currentNote = -1;
}

bool NoteMapper::isValid (int midiNote) const noexcept
{
    const auto s = effective[static_cast<size_t> (wrap (midiNote))];
    return s == NoteState::Active || s == NoteState::Ignore;
}

int NoteMapper::nearestValid (float midi) const noexcept
{
    const int centre = static_cast<int> (std::lround (midi));
    int best = -1;
    float bestDist = 1.0e9f;
    for (int offset = -12; offset <= 12; ++offset)
    {
        const int n = centre + offset;
        if (! isValid (n))
            continue;
        const float d = std::abs (midi - static_cast<float> (n));
        if (d < bestDist)
        {
            bestDist = d;
            best = n;
        }
    }
    return best;
}

NoteTarget NoteMapper::map (float midi) noexcept
{
    NoteTarget t;
    if (! std::isfinite (midi))
        return t;

    const int nearest = nearestValid (midi);
    if (nearest < 0)
    {
        currentNote = -1;
        return t; // every note removed: nothing to correct to
    }

    int chosen = nearest;
    if (currentNote >= 0 && currentNote != nearest && isValid (currentNote))
    {
        const float dCurrent = std::abs (midi - static_cast<float> (currentNote));
        const float dNearest = std::abs (midi - static_cast<float> (nearest));
        // Stay on the current note until the pitch is clearly closer to another one.
        if (dCurrent <= dNearest + 2.0f * hysteresis && dCurrent < 1.5f)
            chosen = currentNote;
    }

    currentNote = chosen;
    t.hasTarget = true;
    t.note = chosen;
    t.targetMidi = static_cast<float> (chosen);
    t.correct = effective[static_cast<size_t> (wrap (chosen))] == NoteState::Active;
    return t;
}

} // namespace novapitch::dsp
