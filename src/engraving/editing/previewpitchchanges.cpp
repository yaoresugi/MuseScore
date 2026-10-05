// SPDX-License-Identifier: GPL-3.0-only
#include "previewpitchchanges.h"

#include <algorithm>
#include <set>
#include "dom/chord.h"
#include "dom/masterscore.h"
#include "dom/note.h"
#include "dom/pitchspelling.h"
#include "dom/segment.h"
#include "dom/staff.h"
#include "undo.h"

namespace mu::engraving {
PreviewPitchResult applyPreviewPitchChanges(MasterScore& score,
                                            const std::vector<PreviewPitchChange>& changes,
                                            const Fraction& start, const Fraction& end,
                                            const std::vector<track_idx_t>& allowedTracks)
{
    if (changes.empty()) {
        return PreviewPitchResult::Empty;
    }
    if (score.undoStack()->hasActiveCommand() || score.undoStack()->isLocked() || score.readOnly()) {
        return PreviewPitchResult::Busy;
    }
    if (start < Fraction(0, 1) || end <= start || allowedTracks.empty()) {
        return PreviewPitchResult::InvalidScope;
    }
    std::set<std::pair<Fraction, track_idx_t> > addresses;
    std::vector<Note*> notes;
    notes.reserve(changes.size());
    for (const auto& change : changes) {
        if (change.tick < start || change.tick >= end || change.track >= score.ntracks()
            || std::find(allowedTracks.begin(), allowedTracks.end(), change.track) == allowedTracks.end()
            || change.pitch < 0 || change.pitch > 127 || !tpcIsValid(change.tpc1) || !tpcIsValid(change.tpc2)
            || !addresses.emplace(change.tick, change.track).second) {
            return PreviewPitchResult::InvalidChange;
        }
        Segment* segment = score.tick2segment(change.tick, false, SegmentType::ChordRest);
        EngravingItem* item = segment ? segment->element(change.track) : nullptr;
        if (!item || !item->isChord() || toChord(item)->notes().size() != 1) {
            return PreviewPitchResult::UnsupportedNote;
        }
        Note* note = toChord(item)->notes().front();
        if (note->tieFor() || note->tieBack() || note->linkList().size() != 1
            || note->staff()->isDrumStaff(change.tick) || note->staff()->isTabStaff(change.tick)) {
            return PreviewPitchResult::UnsupportedNote;
        }
        if (note->pitch() != change.expectedPitch) {
            return PreviewPitchResult::Stale;
        }
        const auto pitchClass = [](int pitch) { return (pitch % 12 + 12) % 12; };
        if (pitchClass(tpc2pitch(change.tpc1)) != pitchClass(change.pitch)
            || pitchClass(tpc2pitch(change.tpc2)) != pitchClass(change.pitch - note->transposition())) {
            return PreviewPitchResult::InvalidChange;
        }
        notes.push_back(note);
    }

    score.startCmd(muse::TranslatableString::untranslatable("Apply preview pitch changes"));
    try {
        for (size_t i = 0; i < changes.size(); ++i) {
            const auto& change = changes[i];
            score.undoChangePitch(notes[i], change.pitch, change.tpc1, change.tpc2);
        }
        // Pitch edits cannot change durations. sanityCheck() also repairs unrelated
        // rests without Undo, so it must not run on the source in this transaction.
    } catch (...) {
        score.endCmd(true);
        return PreviewPitchResult::Failed;
    }
    score.endCmd();
    return PreviewPitchResult::Applied;
}
}
