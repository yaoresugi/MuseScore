// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <vector>
#include "../types/fraction.h"
#include "../types/types.h"

namespace mu::engraving {
class MasterScore;

// The first preview spike supports existing single-note, untied pitched chords.
// The representation contains no AI-service-specific data.
struct PreviewPitchChange {
    Fraction tick;
    track_idx_t track;
    int expectedPitch;
    int pitch;
    int tpc1;
    int tpc2;
};

enum class PreviewPitchResult {
    Applied, Empty, Busy, InvalidScope, InvalidChange, UnsupportedNote, Stale, Failed
};

// Use the same validated changes on a preview copy and, on adoption, on the source.
// All targets are checked before mutation. A successful batch is one Undo step.
PreviewPitchResult applyPreviewPitchChanges(MasterScore& score, const std::vector<PreviewPitchChange>& changes, const Fraction& start,
                                            const Fraction& end, const std::vector<track_idx_t>& allowedTracks);
}
