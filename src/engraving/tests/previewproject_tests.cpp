// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include <tuple>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "global/io/buffer.h"
#include "engraving/engravingproject.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/systemlock.h"
#include "engraving/editing/undo.h"
#include "engraving/editing/editdata.h"
#include "engraving/editing/previewpitchchanges.h"
#include "engraving/rw/rwregister.h"
#include "engraving/rw/inoutdata.h"
#include "utils/scorerw.h"

using namespace mu::engraving;

namespace {
bool writeSnapshot(MasterScore* score, muse::io::Buffer& buffer)
{
    if (!buffer.open(muse::io::IODevice::WriteOnly)) {
        return false;
    }
    rw::WriteInOutData snapshot(score);
    snapshot.ctx.setSnapshotMode(true);
    const bool result = rw::RWRegister::writer(score->iocContext())->writeScore(score, &buffer, &snapshot);
    buffer.close();
    return result;
}

QString snapshotContent(const muse::io::Buffer& buffer)
{
    // Legacy files may lack EIDs. Snapshot IDs are local and random; check the
    // complete notation payload separately from the source's EID register.
    auto xml = QString::fromUtf8(buffer.data().toQByteArray());
    return xml.remove(QRegularExpression("<eid>[^<]*</eid>"));
}

Note* firstNote(MasterScore* score)
{
    for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = 0; track < score->ntracks(); ++track) {
            EngravingItem* item = segment->element(track);
            if (item && item->isChord() && !toChord(item)->notes().empty()) {
                return toChord(item)->notes().front();
            }
        }
    }
    return nullptr;
}
}

TEST(Engraving_PreviewProjectTests, independentOwnershipAndEdits)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"note_data/grace.mscx"));
    ASSERT_NE(original, nullptr);
    Note* originalNote = firstNote(original.get());
    ASSERT_NE(originalNote, nullptr);
    const int pitch = originalNote->pitch();
    auto* undoStack = original->undoStack();
    const auto undoSize = undoStack->size();
    const bool wasClean = undoStack->isClean();
    const auto version = original->mscoreVersion();
    const auto revision = original->mscoreRevision();
    const auto fileVersion = original->mscVersion();
    auto* sourceSegment = original->firstSegment(SegmentType::ChordRest);
    sourceSegment->setWritten(true);
    std::vector<std::tuple<int, int, const InstrChannel*> > midiMappings;
    for (const auto& mapping : original->midiMapping()) {
        midiMappings.emplace_back(mapping.port(), mapping.channel(), mapping.articulation());
    }
    const auto exportMidi = original->exportMidiMapping();
    const auto midiPorts = original->midiPortCount();
    auto* playbackScore = original->playbackScore();
    std::vector<std::pair<EngravingObject*, EID> > sourceIds;
    original->scanElements([&sourceIds](EngravingItem* item) {
        sourceIds.emplace_back(item, item->eid());
    });
    for (auto* measure = original->first(); measure; measure = measure->next()) {
        sourceIds.emplace_back(measure, measure->eid());
    }
    original->inputState().setNoteEntryMode(true);
    auto* inputSegment = original->inputState().segment();
    const auto inputTrack = original->inputState().track();

    muse::io::Buffer before;
    ASSERT_TRUE(writeSnapshot(original.get(), before));

    auto preview = original->createPreviewProject();
    ASSERT_NE(preview, nullptr);
    auto* candidate = preview->masterScore();
    ASSERT_NE(candidate, original.get());
    EXPECT_EQ(candidate->project().lock(), preview);
    EXPECT_NE(candidate->undoStack(), undoStack);
    EXPECT_NE(candidate->firstMeasure(), original->firstMeasure());
    Note* candidateNote = firstNote(candidate);
    ASSERT_NE(candidateNote, nullptr);
    ASSERT_NE(candidateNote, originalNote);
    EXPECT_EQ(candidateNote->pitch(), pitch);

    candidateNote->setPitch(pitch + 1);
    candidateNote->setTpcFromPitch();
    candidate->setMetaTag(u"workTitle", u"Candidate only");
    EXPECT_EQ(originalNote->pitch(), pitch);
    EXPECT_EQ(original->undoStack(), undoStack);
    EXPECT_EQ(undoStack->size(), undoSize);
    EXPECT_EQ(undoStack->isClean(), wasClean);
    EXPECT_TRUE(original->noteEntryMode());
    EXPECT_EQ(original->inputState().segment(), inputSegment);
    EXPECT_EQ(original->inputState().track(), inputTrack);
    EXPECT_EQ(original->mscoreVersion(), version);
    EXPECT_EQ(original->mscoreRevision(), revision);
    EXPECT_EQ(original->mscVersion(), fileVersion);
    EXPECT_TRUE(sourceSegment->written());
    EXPECT_EQ(original->midiMapping().size(), midiMappings.size());
    for (size_t i = 0; i < midiMappings.size(); ++i) {
        EXPECT_EQ(original->midiMapping()[i].port(), std::get<0>(midiMappings[i]));
        EXPECT_EQ(original->midiMapping()[i].channel(), std::get<1>(midiMappings[i]));
        EXPECT_EQ(original->midiMapping()[i].articulation(), std::get<2>(midiMappings[i]));
    }
    EXPECT_EQ(original->exportMidiMapping(), exportMidi);
    EXPECT_EQ(original->midiPortCount(), midiPorts);
    EXPECT_EQ(original->playbackScore(), playbackScore);
    for (const auto& [item, eid] : sourceIds) {
        EXPECT_EQ(item->eid(), eid);
    }

    muse::io::Buffer after;
    ASSERT_TRUE(writeSnapshot(original.get(), after));
    EXPECT_EQ(snapshotContent(before), snapshotContent(after));

    // A preview must remain valid after the source score has been closed.
    original.reset();
    EXPECT_EQ(candidateNote->pitch(), pitch + 1);
    EXPECT_EQ(candidate->project().lock(), preview);
    candidate->doLayout();
}

TEST(Engraving_PreviewProjectTests, discardingPreviewKeepsSourceAlive)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"note_data/grace.mscx"));
    ASSERT_NE(original, nullptr);
    Note* note = firstNote(original.get());
    ASSERT_NE(note, nullptr);
    const int pitch = note->pitch();
    auto preview = original->createPreviewProject();
    ASSERT_NE(preview, nullptr);
    preview.reset();
    EXPECT_EQ(note->pitch(), pitch);
    original->doLayout();
}

namespace {
std::vector<PreviewPitchChange> firstChordChanges(MasterScore* score)
{
    std::vector<PreviewPitchChange> result;
    Segment* segment = score->firstSegment(SegmentType::ChordRest);
    if (!segment) {
        return result;
    }
    for (track_idx_t track : { 0u, 4u, 8u, 12u }) {
        auto* item = segment->element(track);
        if (!item || !item->isChord() || toChord(item)->notes().size() != 1) {
            return {};
        }
        Note* note = toChord(item)->notes().front();
        result.push_back({ segment->tick(), track, note->pitch(), note->pitch() + 12, note->tpc1(), note->tpc2() });
    }
    return result;
}
}

TEST(Engraving_PreviewProjectTests, fourVoiceAdoptionIsOneUndoStep)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
    ASSERT_NE(original, nullptr);
    ASSERT_EQ(original->nstaves(), 4);
    ASSERT_EQ(original->parts().size(), 4);
    ASSERT_TRUE(original->sanityCheck());
    auto changes = firstChordChanges(original.get());
    ASSERT_EQ(changes.size(), 4);
    auto preview = original->createPreviewProject();
    ASSERT_NE(preview, nullptr);
    const auto undoSize = original->undoStack()->size();
    const std::vector<track_idx_t> tracks { 0, 4, 8, 12 };
    const Fraction start(0, 1), end(1, 1);
    ASSERT_EQ(applyPreviewPitchChanges(*preview->masterScore(), changes, start, end, tracks), PreviewPitchResult::Applied);
    EXPECT_EQ(firstChordChanges(original.get()).front().expectedPitch, changes.front().expectedPitch);
    EXPECT_EQ(original->undoStack()->size(), undoSize);

    ASSERT_EQ(applyPreviewPitchChanges(*original, changes, start, end, tracks), PreviewPitchResult::Applied);
    EXPECT_EQ(original->undoStack()->size(), undoSize + 1);
    const auto adopted = firstChordChanges(original.get());
    for (size_t i = 0; i < changes.size(); ++i) {
        EXPECT_EQ(adopted[i].expectedPitch, changes[i].pitch);
    }
    EditData editData;
    original->undoStack()->undo(&editData);
    const auto undone = firstChordChanges(original.get());
    for (size_t i = 0; i < changes.size(); ++i) {
        EXPECT_EQ(undone[i].expectedPitch, changes[i].expectedPitch);
    }
    original->undoStack()->redo(&editData);
    const auto redone = firstChordChanges(original.get());
    for (size_t i = 0; i < changes.size(); ++i) {
        EXPECT_EQ(redone[i].expectedPitch, changes[i].pitch);
    }
}

TEST(Engraving_PreviewProjectTests, pitchAdoptionDoesNotRepairUnselectedRest)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
    ASSERT_NE(original, nullptr);
    Rest* rest = nullptr;
    for (auto* segment = original->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        auto* item = segment->element(12);
        if (item && item->isRest()) {
            rest = toRest(item);
            break;
        }
    }
    ASSERT_NE(rest, nullptr);
    rest->setDurationType(DurationType::V_MEASURE);
    rest->setTicks(Fraction(1, 4));
    auto* measure = rest->measure();
    measure->setCorrupted(3, true);
    original->setHasCorruptedMeasures(false);
    auto changes = firstChordChanges(original.get());
    ASSERT_EQ(changes.size(), 4);
    changes.resize(1);
    EXPECT_EQ(applyPreviewPitchChanges(*original, changes, Fraction(0, 1), Fraction(1, 1), { 0 }),
              PreviewPitchResult::Applied);
    EXPECT_EQ(rest->ticks(), Fraction(1, 4));
    EXPECT_TRUE(measure->corrupted(3));
    EXPECT_FALSE(original->hasCorruptedMeasures());
}

TEST(Engraving_PreviewProjectTests, busyScoreIsNotEdited)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
    ASSERT_NE(original, nullptr);
    auto changes = firstChordChanges(original.get());
    ASSERT_EQ(changes.size(), 4);
    original->startCmd(muse::TranslatableString::untranslatable("User edit"));
    auto* note = firstNote(original.get());
    original->undoChangePitch(note, note->pitch() + 12, note->tpc1(), note->tpc2());
    EXPECT_EQ(applyPreviewPitchChanges(*original, changes, Fraction(0, 1), Fraction(1, 1), { 0, 4, 8, 12 }),
              PreviewPitchResult::Busy);
    EXPECT_TRUE(original->undoStack()->hasActiveCommand());
    EXPECT_EQ(note->pitch(), changes.front().pitch);
    EXPECT_EQ(original->createPreviewProject(), nullptr);
    EXPECT_TRUE(original->undoStack()->hasActiveCommand());
    EXPECT_EQ(note->pitch(), changes.front().pitch);
    original->endCmd(true);
    EXPECT_EQ(note->pitch(), changes.front().expectedPitch);
}

TEST(Engraving_PreviewProjectTests, hiddenPartSnapshotKeepsVisibilityAndHistory)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
    ASSERT_NE(original, nullptr);
    MStyle style = original->style();
    style.set(Sid::createMultiMeasureRests, true);
    original->setStyle(style);
    auto* hiddenPart = original->parts().back();
    hiddenPart->setShow(false);
    original->doLayout();
    const auto undoSize = original->undoStack()->size();
    const bool clean = original->undoStack()->isClean();
    auto preview = original->createPreviewProject();
    ASSERT_NE(preview, nullptr);
    EXPECT_FALSE(hiddenPart->show());
    EXPECT_FALSE(preview->masterScore()->parts().back()->show());
    EXPECT_EQ(original->undoStack()->size(), undoSize);
    EXPECT_EQ(original->undoStack()->isClean(), clean);
    EXPECT_FALSE(original->undoStack()->hasActiveCommand());
}

TEST(Engraving_PreviewProjectTests, adoptedScoreCanBeSavedAndReopened)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
    ASSERT_NE(original, nullptr);
    auto changes = firstChordChanges(original.get());
    ASSERT_EQ(changes.size(), 4);
    ASSERT_EQ(applyPreviewPitchChanges(*original, changes, Fraction(0, 1), Fraction(1, 1), { 0, 4, 8, 12 }),
              PreviewPitchResult::Applied);
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const String path(temporary.filePath("adopted.mscx"));
    ASSERT_TRUE(ScoreRW::saveScore(original.get(), path));
    std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(path, true));
    ASSERT_NE(reopened, nullptr);
    ASSERT_EQ(reopened->nstaves(), 4);
    const auto actual = firstChordChanges(reopened.get());
    ASSERT_EQ(actual.size(), 4);
    for (size_t i = 0; i < changes.size(); ++i) {
        EXPECT_EQ(actual[i].expectedPitch, changes[i].pitch);
    }
    EXPECT_TRUE(reopened->sanityCheck());
}

TEST(Engraving_PreviewProjectTests, previewPreservesSystemLocks)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"system_locks_data/system_locks-1.mscx"));
    ASSERT_NE(original, nullptr);
    const auto locks = original->systemLocks()->allLocks();
    ASSERT_FALSE(locks.empty());
    auto preview = original->createPreviewProject();
    ASSERT_NE(preview, nullptr);
    const auto copiedLocks = preview->masterScore()->systemLocks()->allLocks();
    ASSERT_EQ(copiedLocks.size(), locks.size());
    for (size_t i = 0; i < locks.size(); ++i) {
        EXPECT_EQ(copiedLocks[i]->startMB()->tick(), locks[i]->startMB()->tick());
        EXPECT_EQ(copiedLocks[i]->endMB()->tick(), locks[i]->endMB()->tick());
        EXPECT_NE(copiedLocks[i]->startMB(), locks[i]->startMB());
    }
}

TEST(Engraving_PreviewProjectTests, invalidSecondChangeDoesNotApplyFirst)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
    ASSERT_NE(original, nullptr);
    auto changes = firstChordChanges(original.get());
    ASSERT_EQ(changes.size(), 4);
    const auto undoSize = original->undoStack()->size();
    changes[1].pitch = 128;
    EXPECT_EQ(applyPreviewPitchChanges(*original, changes, Fraction(0, 1), Fraction(1, 1), { 0, 4, 8, 12 }),
              PreviewPitchResult::InvalidChange);
    EXPECT_EQ(firstChordChanges(original.get()).front().expectedPitch, changes.front().expectedPitch);
    EXPECT_EQ(original->undoStack()->size(), undoSize);
}

TEST(Engraving_PreviewProjectTests, rejectsOutsideScopeStaleTargetsAndDuplicateAddresses)
{
    std::unique_ptr<MasterScore> original(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
    ASSERT_NE(original, nullptr);
    const auto changes = firstChordChanges(original.get());
    ASSERT_EQ(changes.size(), 4);
    EXPECT_EQ(applyPreviewPitchChanges(*original, changes, Fraction(0, 1), Fraction(1, 1), { 0 }),
              PreviewPitchResult::InvalidChange);
    EXPECT_EQ(applyPreviewPitchChanges(*original, changes, Fraction(1, 4), Fraction(1, 1), { 0, 4, 8, 12 }),
              PreviewPitchResult::InvalidChange);
    auto stale = changes;
    ++stale[1].expectedPitch;
    EXPECT_EQ(applyPreviewPitchChanges(*original, stale, Fraction(0, 1), Fraction(1, 1), { 0, 4, 8, 12 }),
              PreviewPitchResult::Stale);
    auto duplicates = changes;
    duplicates.push_back(changes.front());
    EXPECT_EQ(applyPreviewPitchChanges(*original, duplicates, Fraction(0, 1), Fraction(1, 1), { 0, 4, 8, 12 }),
              PreviewPitchResult::InvalidChange);
    EXPECT_EQ(firstChordChanges(original.get()).front().expectedPitch, changes.front().expectedPitch);
}
