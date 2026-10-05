// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies

#include <gtest/gtest.h>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QImage>
#include <QHash>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>

#include "previewmocks.h"
#include "notation/tests/mocks/notationinteractionmock.h"
#include "notation/tests/mocks/notationselectionmock.h"
#include "notationscene/widgets/pitchpreviewdialog.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/editing/editdata.h"
#include "engraving/editing/undo.h"
#include "engraving/rw/inoutdata.h"
#include "engraving/rw/rwregister.h"
#include "engraving/tests/utils/scorerw.h"
#include "global/io/buffer.h"

using namespace mu;
using namespace mu::engraving;
using namespace mu::notation;
using namespace testing;

namespace {
void saveCapture(PitchPreviewDialog& dialog, const QString& name)
{
    const QString directory = qEnvironmentVariable("MUSE_PREVIEW_CAPTURE_DIR");
    if (directory.isEmpty()) {
        return;
    }
    ASSERT_TRUE(QDir().mkpath(directory));
    ASSERT_TRUE(dialog.grab().save(QDir(directory).filePath(name + QStringLiteral(".png"))));
}

// Snapshot serialization checks all score data, including properties that a
// pitch-only assertion would miss. Legacy fixtures get transient snapshot IDs;
// canonicalize those IDs and their exact references while retaining topology.
QString snapshot(MasterScore* score)
{
    muse::io::Buffer buffer;
    EXPECT_TRUE(buffer.open(muse::io::IODevice::WriteOnly));
    rw::WriteInOutData data(score);
    data.ctx.setSnapshotMode(true);
    EXPECT_TRUE(rw::RWRegister::writer(score->iocContext())->writeScore(score, &buffer, &data));
    buffer.close();
    QString xml = QString::fromUtf8(buffer.data().toQByteArray());
    QHash<QString, QString> ids;
    auto matches = QRegularExpression("<eid>([^<]+)</eid>").globalMatch(xml);
    while (matches.hasNext()) {
        const QString id = matches.next().captured(1);
        if (!ids.contains(id)) {
            ids.insert(id, QStringLiteral("snapshot-id-%1").arg(ids.size()));
        }
    }
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
        // Exact XML scalar/attribute values: do not discard linkedTo entries
        // or replace the numeric staff indices used by older score formats.
        xml.replace(QRegularExpression(QStringLiteral("(?<=[>\"])") + QRegularExpression::escape(it.key())
                                       + QStringLiteral("(?=[<\"])")), it.value());
    }
    return xml;
}

int darkPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            count += pixel.red() < 180 && pixel.green() < 180 && pixel.blue() < 180;
        }
    }
    return count;
}

int bluePixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            count += pixel.blue() > 150 && pixel.blue() > pixel.red() + 40 && pixel.blue() > pixel.green() + 40;
        }
    }
    return count;
}

class NotationScene_PitchPreviewDialogTests : public Test
{
protected:
    void SetUp() override
    {
        source.reset(ScoreRW::readScore(u"previewproject_data/four-parts.mscx"));
        ASSERT_NE(source, nullptr);
        ASSERT_EQ(source->nstaves(), 4);
        const auto* segment = source->firstSegment(SegmentType::ChordRest);
        ASSERT_NE(segment, nullptr);
        for (track_idx_t track : { 0u, 4u, 8u, 12u }) {
            auto* item = segment->element(track);
            ASSERT_TRUE(item && item->isChord());
            ASSERT_EQ(toChord(item)->notes().size(), 1);
            selectedNotes.push_back(toChord(item)->notes().front());
        }

        ON_CALL(*globalContext, currentProject()).WillByDefault(Invoke([this]() { return sourceProject; }));
        ON_CALL(*globalContext, currentNotation()).WillByDefault(Invoke([this]() { return notation; }));
        ON_CALL(*notation, isMaster()).WillByDefault(Return(true));
        ON_CALL(*notation, project()).WillByDefault(Return(sourceProject.get()));
        ON_CALL(*notation, masterNotation()).WillByDefault(Invoke([this]() { return master; }));
        ON_CALL(*master, notation()).WillByDefault(Invoke([this]() { return notation; }));
        ON_CALL(*master, masterScore()).WillByDefault(Invoke([this]() { return source.get(); }));
        ON_CALL(*notation, elements()).WillByDefault(Return(elements));
        ON_CALL(*elements, msScore()).WillByDefault(Invoke([this]() { return source.get(); }));
        ON_CALL(*notation, interaction()).WillByDefault(Return(interaction));
        ON_CALL(*interaction, selection()).WillByDefault(Return(selection));
        ON_CALL(*selection, notes(_)).WillByDefault(Invoke([this](NoteFilter) { return selectedNotes; }));
        ON_CALL(*selection, isNone()).WillByDefault(Return(false));
        ON_CALL(*notation, undoStack()).WillByDefault(Return(undoStack));
        ON_CALL(*notation, notationChanged()).WillByDefault(Return(notationChanged));
        ON_CALL(*undoStack, stackChanged()).WillByDefault(Return(stackChanged));

        muse::modularity::globalIoc()->registerExport<context::IGlobalContext>("widget-tests", globalContext);
    }

    void TearDown() override
    {
        muse::modularity::globalIoc()->unregister<context::IGlobalContext>("widget-tests");
    }

    static QComboBox* selector(PitchPreviewDialog& dialog)
    {
        return dialog.findChild<QComboBox*>(QStringLiteral("pitchCandidateSelector"));
    }

    static QPushButton* adopt(PitchPreviewDialog& dialog)
    {
        return dialog.findChild<QPushButton*>(QStringLiteral("adoptPitchCandidate"));
    }

    std::unique_ptr<MasterScore> source;
    std::vector<Note*> selectedNotes;
    // The project is an identity handle only. Any call into it is unexpected.
    std::shared_ptr<StrictMock<project::PreviewProjectMock>> sourceProject = std::make_shared<StrictMock<project::PreviewProjectMock>>();
    std::shared_ptr<NiceMock<context::PreviewGlobalContextMock>> globalContext
        = std::make_shared<NiceMock<context::PreviewGlobalContextMock>>();
    std::shared_ptr<NiceMock<PreviewNotationMock>> notation = std::make_shared<NiceMock<PreviewNotationMock>>();
    std::shared_ptr<NiceMock<PreviewMasterNotationMock>> master = std::make_shared<NiceMock<PreviewMasterNotationMock>>();
    std::shared_ptr<NiceMock<PreviewElementsMock>> elements = std::make_shared<NiceMock<PreviewElementsMock>>();
    std::shared_ptr<NiceMock<NotationInteractionMock>> interaction = std::make_shared<NiceMock<NotationInteractionMock>>();
    std::shared_ptr<NiceMock<NotationSelectionMock>> selection = std::make_shared<NiceMock<NotationSelectionMock>>();
    std::shared_ptr<NiceMock<PreviewUndoStackMock>> undoStack = std::make_shared<NiceMock<PreviewUndoStackMock>>();
    muse::async::Notification notationChanged;
    muse::async::Notification stackChanged;
};

TEST_F(NotationScene_PitchPreviewDialogTests, rendersIndependentScoresAndAdoptsSelectedCandidateAsOneUndoStep)
{
    const QString original = snapshot(source.get());
    const auto undoSize = source->undoStack()->size();
    std::vector<int> originalPitches;
    for (auto* note : selectedNotes) {
        originalPitches.push_back(note->pitch());
    }
    EXPECT_CALL(*notation, notationChanged()).Times(1);
    EXPECT_CALL(*undoStack, stackChanged()).Times(1);

    PitchPreviewDialog dialog;
    dialog.show();
    QApplication::processEvents();
    ASSERT_NE(selector(dialog), nullptr);
    ASSERT_NE(adopt(dialog), nullptr);
    ASSERT_TRUE(adopt(dialog)->isEnabled());
    const auto views = dialog.findChildren<QScrollArea*>();
    ASSERT_EQ(views.size(), 2);
    ASSERT_NE(views[0]->widget(), nullptr);
    ASSERT_NE(views[1]->widget(), nullptr);
    SCOPED_TRACE(Message() << "before viewport=" << views[0]->viewport()->width() << 'x' << views[0]->viewport()->height()
                          << ", canvas=" << views[0]->widget()->width() << 'x' << views[0]->widget()->height()
                          << "; candidate viewport=" << views[1]->viewport()->width() << 'x' << views[1]->viewport()->height()
                          << ", canvas=" << views[1]->widget()->width() << 'x' << views[1]->widget()->height());
    // Check what the user actually sees. A giant canvas can contain valid
    // engraving while its initially visible scroll viewport remains blank.
    const QImage before = views[0]->viewport()->grab().toImage();
    const QImage up = views[1]->viewport()->grab().toImage();
    EXPECT_GT(darkPixels(before), 300);
    EXPECT_GT(darkPixels(up), 300);
    EXPECT_EQ(bluePixels(before), 0);
    EXPECT_GT(bluePixels(up), 10);
    saveCapture(dialog, QStringLiteral("pitch-preview-up"));
    EXPECT_NE(before, up);
    EXPECT_EQ(snapshot(source.get()), original);
    EXPECT_EQ(source->undoStack()->size(), undoSize);

    selector(dialog)->setCurrentIndex(1);
    QApplication::processEvents();
    ASSERT_TRUE(adopt(dialog)->isEnabled());
    const QImage down = views[1]->viewport()->grab().toImage();
    EXPECT_GT(darkPixels(down), 300)
        << "candidate canvas after switch=" << views[1]->widget()->width() << 'x' << views[1]->widget()->height();
    EXPECT_GT(bluePixels(down), 10);
    EXPECT_NE(up, down);
    saveCapture(dialog, QStringLiteral("pitch-preview-down"));
    EXPECT_EQ(views[0]->viewport()->grab().toImage(), before);
    EXPECT_EQ(snapshot(source.get()), original);
    EXPECT_EQ(source->undoStack()->size(), undoSize);

    adopt(dialog)->click();
    EXPECT_FALSE(adopt(dialog)->isEnabled());
    EXPECT_FALSE(selector(dialog)->isEnabled());
    ASSERT_EQ(source->undoStack()->size(), undoSize + 1);
    for (size_t i = 0; i < selectedNotes.size(); ++i) {
        EXPECT_EQ(selectedNotes[i]->pitch(), originalPitches[i] - 12);
    }
    EditData editData;
    source->undoStack()->undo(&editData);
    for (size_t i = 0; i < selectedNotes.size(); ++i) {
        EXPECT_EQ(selectedNotes[i]->pitch(), originalPitches[i]);
    }
    EXPECT_EQ(snapshot(source.get()), original);
    source->undoStack()->redo(&editData);
    for (size_t i = 0; i < selectedNotes.size(); ++i) {
        EXPECT_EQ(selectedNotes[i]->pitch(), originalPitches[i] - 12);
    }
}

TEST_F(NotationScene_PitchPreviewDialogTests, closingAfterCandidateSwitchLeavesSourceAndHistoryUntouched)
{
    const QString original = snapshot(source.get());
    const auto undoSize = source->undoStack()->size();
    const bool clean = source->undoStack()->isClean();
    EXPECT_CALL(*notation, notationChanged()).Times(0);
    EXPECT_CALL(*undoStack, stackChanged()).Times(0);
    {
        PitchPreviewDialog dialog;
        ASSERT_NE(selector(dialog), nullptr);
        ASSERT_NE(adopt(dialog), nullptr);
        selector(dialog)->setCurrentIndex(1);
        selector(dialog)->setCurrentIndex(0);
        ASSERT_TRUE(adopt(dialog)->isEnabled());
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        ASSERT_NE(buttons, nullptr);
        ASSERT_NE(buttons->button(QDialogButtonBox::Close), nullptr);
        buttons->button(QDialogButtonBox::Close)->click();
        EXPECT_EQ(dialog.result(), QDialog::Rejected);
    }
    EXPECT_EQ(snapshot(source.get()), original);
    EXPECT_EQ(source->undoStack()->size(), undoSize);
    EXPECT_EQ(source->undoStack()->isClean(), clean);
    EXPECT_FALSE(source->undoStack()->hasActiveCommand());
}

TEST_F(NotationScene_PitchPreviewDialogTests, mixedRegularAndGraceSelectionCannotBeRevivedBySwitchingCandidate)
{
    // note_data/grace.mscx is the input for a test that creates grace notes;
    // this fixture actually stores an acciaccatura alongside its main chord.
    source.reset(ScoreRW::readScore(u"midi/midirenderer_data/grace_before_beat.mscx"));
    ASSERT_NE(source, nullptr);
    selectedNotes.clear();
    for (auto* segment = source->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = 0; track < source->ntracks(); ++track) {
            auto* item = segment->element(track);
            if (item && item->isChord() && !toChord(item)->graceNotes().empty()) {
                auto* chord = toChord(item);
                ASSERT_FALSE(chord->notes().empty());
                ASSERT_FALSE(chord->graceNotes().front()->notes().empty());
                // A regular note first catches partial collection before the
                // unsupported grace note; selector signals must not revive it.
                selectedNotes = { chord->notes().front(), chord->graceNotes().front()->notes().front() };
                break;
            }
        }
        if (!selectedNotes.empty()) {
            break;
        }
    }
    ASSERT_EQ(selectedNotes.size(), 2);
    std::vector<std::pair<EngravingItem*, EID>> sourceIds;
    source->scanElements([&sourceIds](EngravingItem* item) {
        sourceIds.emplace_back(item, item->eid());
    });
    const QString original = snapshot(source.get());
    const auto undoSize = source->undoStack()->size();
    EXPECT_CALL(*notation, notationChanged()).Times(0);
    EXPECT_CALL(*undoStack, stackChanged()).Times(0);
    PitchPreviewDialog dialog;
    ASSERT_NE(selector(dialog), nullptr);
    ASSERT_NE(adopt(dialog), nullptr);
    EXPECT_FALSE(adopt(dialog)->isEnabled());
    EXPECT_FALSE(selector(dialog)->isEnabled());
    // Programmatic changes emit currentIndexChanged even on a disabled combo.
    selector(dialog)->setCurrentIndex(1);
    EXPECT_FALSE(adopt(dialog)->isEnabled());
    selector(dialog)->setCurrentIndex(0);
    EXPECT_FALSE(adopt(dialog)->isEnabled());
    adopt(dialog)->click();
    const QString after = snapshot(source.get());
    if (after != original) {
        const auto beforeLines = original.split('\n');
        const auto afterLines = after.split('\n');
        std::string differences;
        for (qsizetype i = 0; i < std::min(beforeLines.size(), afterLines.size()); ++i) {
            if (beforeLines[i] != afterLines[i]) {
                differences += "line " + std::to_string(i + 1) + ":\n  before: " + beforeLines[i].toStdString()
                               + "\n  after: " + afterLines[i].toStdString() + "\n";
            }
        }
        EXPECT_EQ(afterLines.size(), beforeLines.size());
        ADD_FAILURE() << differences;
    }
    EXPECT_EQ(after, original);
    EXPECT_EQ(source->undoStack()->size(), undoSize);
    for (const auto& [item, id] : sourceIds) {
        EXPECT_EQ(item->eid(), id);
    }
}
}
