/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */

#include "pitchpreviewdialog.h"

#include <algorithm>
#include <set>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtMath>

#include "draw/painter.h"
#include "draw/types/transform.h"
#include "engraving/engravingproject.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/engravingitem.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/note.h"
#include "engraving/dom/page.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/editing/previewpitchchanges.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/types/types.h"
#include "notation/inotation.h"
#include "notation/inotationselection.h"

using namespace mu;
using namespace mu::notation;
using namespace mu::engraving;
using namespace muse::draw;

namespace mu::notation {
namespace {
QString previewResultMessage(PreviewPitchResult result)
{
    switch (result) {
    case PreviewPitchResult::Applied:
        return QObject::tr("候補の音高を反映しました。");
    case PreviewPitchResult::Empty:
        return QObject::tr("反映する音高変更がありません。");
    case PreviewPitchResult::Busy:
        return QObject::tr("元の譜面で別の編集が進行中です。");
    case PreviewPitchResult::InvalidScope:
    case PreviewPitchResult::InvalidChange:
        return QObject::tr("候補の音高または編集範囲が無効です。");
    case PreviewPitchResult::UnsupportedNote:
        return QObject::tr("タイ、リンク音符、打楽器、タブ譜、複音和音はこの試作では変更できません。");
    case PreviewPitchResult::Stale:
        return QObject::tr("候補を作った後に元の音符が変更されています。");
    case PreviewPitchResult::Failed:
        return QObject::tr("音高変更を反映できませんでした。");
    }
    return QObject::tr("音高変更を反映できませんでした。");
}
}

// This widget only paints score-owned page data. It never registers itself as a
// MuseScoreView, changes the score layout, or owns/deletes the score.
class PitchPreviewScoreView final : public QWidget
{
public:
    explicit PitchPreviewScoreView(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
    }

    void setScore(MasterScore* score)
    {
        m_score = score;
        m_canvasBounds = RectF();

        if (m_score) {
            for (const Page* page : m_score->pages()) {
                // Fit the actual notation, rather than a mostly empty sheet.
                const RectF pageBounds = page->tbbox().translated(page->pos());
                m_canvasBounds = m_canvasBounds.isValid() ? m_canvasBounds.united(pageBounds) : pageBounds;
            }
        }

        updateGeometry();
        resize(sizeHint());
        update();
    }

    QSize sizeHint() const override
    {
        if (!m_score || !m_canvasBounds.isValid()) {
            return QSize(480, 320);
        }

        constexpr qreal scale = 120.0 / engraving::DPI;
        constexpr qreal margin = 18.0;
        return QSize(qCeil(m_canvasBounds.width() * scale + margin * 2.0),
                     qCeil(m_canvasBounds.height() * scale + margin * 2.0));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        Painter painter(this, "pitch-preview-score");
        painter.fillRect(RectF(0.0, 0.0, width(), height()), Brush(Color::WHITE));
        if (!m_score || !m_canvasBounds.isValid()) {
            return;
        }

        painter.setAntialiasing(true);

        constexpr qreal scale = 120.0 / engraving::DPI;
        constexpr qreal margin = 18.0;
        const Transform transform(scale, 0.0, 0.0, scale,
                                  margin - m_canvasBounds.left() * scale,
                                  margin - m_canvasBounds.top() * scale);
        painter.setWorldTransform(transform);

        rendering::IScoreRenderer::ScorePaintOptions options;
        options.isSetViewport = false;
        options.isMultiPage = true;
        options.printPageBackground = true;
        m_score->renderer()->paintScore(&painter, m_score, options);
    }

private:
    MasterScore* m_score = nullptr;
    RectF m_canvasBounds;
};

PitchPreviewDialog::PitchPreviewDialog(QWidget* parent)
    : QDialog(parent), muse::Contextable(muse::iocCtxForQWidget(this))
{
    setObjectName(QStringLiteral("pitchPreviewDialog"));
    setWindowTitle(tr("音高候補のプレビュー"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(1180, 760);

    auto* root = new QVBoxLayout(this);

    auto* explanation = new QLabel(
        tr("検証用の手作り候補です。音符を1オクターブ上下した案を表示します。AI提案サービスと音声再生は未接続です。"), this);
    explanation->setWordWrap(true);
    root->addWidget(explanation);

    auto* candidateRow = new QHBoxLayout;
    candidateRow->addWidget(new QLabel(tr("候補:"), this));
    m_candidateSelector = new QComboBox(this);
    m_candidateSelector->setObjectName(QStringLiteral("pitchCandidateSelector"));
    m_candidateSelector->addItem(tr("1オクターブ上げる（+12半音）"));
    m_candidateSelector->addItem(tr("1オクターブ下げる（-12半音）"));
    candidateRow->addWidget(m_candidateSelector, 1);
    root->addLayout(candidateRow);

    auto* viewsRow = new QHBoxLayout;
    auto makePreviewColumn = [this, viewsRow](const QString& title, PitchPreviewScoreView** view) {
        auto* column = new QVBoxLayout;
        auto* heading = new QLabel(title, this);
        heading->setAlignment(Qt::AlignCenter);
        column->addWidget(heading);

        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(false);
        scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        auto* scoreView = new PitchPreviewScoreView(scroll);
        scroll->setWidget(scoreView);
        column->addWidget(scroll, 1);
        viewsRow->addLayout(column, 1);
        *view = scoreView;
    };
    makePreviewColumn(tr("変更前"), &m_beforeView);
    makePreviewColumn(tr("候補"), &m_candidateView);
    root->addLayout(viewsRow, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_applyButton = buttons->addButton(tr("この音高変更を採用"), QDialogButtonBox::ActionRole);
    m_applyButton->setObjectName(QStringLiteral("adoptPitchCandidate"));
    root->addWidget(buttons);

    connect(m_candidateSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        refreshCandidate(index == 0 ? 12 : -12);
    });
    connect(m_applyButton, &QPushButton::clicked, this, &PitchPreviewDialog::applyCandidate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (captureSource() && buildPitchChanges()) {
        if (!m_upIsValid && m_downIsValid) {
            m_candidateSelector->setCurrentIndex(1);
        } else {
            refreshCandidate(12);
        }
    }
    updateControls();
}

PitchPreviewDialog::~PitchPreviewDialog() = default;

bool PitchPreviewDialog::captureSource()
{
    m_sourceProject = globalContext()->currentProject();
    m_sourceNotation = globalContext()->currentNotation();

    if (!m_sourceProject || !m_sourceNotation || !m_sourceNotation->isMaster()
        || m_sourceNotation->project() != m_sourceProject.get()) {
        setStatus(tr("音高候補を表示するには、マスタースコアを開いてください。"), true);
        return false;
    }

    const IMasterNotationPtr masterNotation = m_sourceNotation->masterNotation();
    m_sourceScore = dynamic_cast<MasterScore*>(m_sourceNotation->elements()->msScore());
    if (!masterNotation || masterNotation->notation() != m_sourceNotation
        || !m_sourceScore || masterNotation->masterScore() != m_sourceScore) {
        m_sourceScore = nullptr;
        setStatus(tr("この試作では、パート譜ではなくマスタースコア全体を対象にしてください。"), true);
        return false;
    }

    m_beforeProject = m_sourceScore->createPreviewProject();
    if (!m_beforeProject) {
        setStatus(tr("変更前の独立プレビューを作成できませんでした。"), true);
        return false;
    }

    MasterScore* beforeScore = m_beforeProject->masterScore();
    beforeScore->setLayoutMode(LayoutMode::LINE);
    beforeScore->doLayout();
    m_beforeView->setScore(beforeScore);

    m_sourceScore->changesChannel().onReceive(this, [this](const ScoreChanges&) {
        if (m_applying || m_adopted) {
            return;
        }
        m_stale = true;
        setStatus(tr("元の譜面が変更されました。このプレビューを閉じて候補を作り直してください。"), true);
        updateControls();
    });

    // A round trip back to the original score must not revive this proposal.
    const auto invalidateOnContextChange = [this]() {
        if (m_adopted) {
            return;
        }
        m_stale = true;
        setStatus(tr("アクティブな元の譜面が変わりました。このプレビューを閉じて候補を作り直してください。"), true);
        updateControls();
    };
    globalContext()->currentProjectChanged().onNotify(this, invalidateOnContextChange);
    globalContext()->currentNotationChanged().onNotify(this, invalidateOnContextChange);

    return true;
}

bool PitchPreviewDialog::buildPitchChanges()
{
    const auto selection = m_sourceNotation->interaction()->selection();
    std::vector<Note*> notes = selection->notes();
    if (!selection->isNone() && notes.empty()) {
        setStatus(tr("選択箇所に音符がありません。音符を含む箇所を選び直してください。"), true);
        return false;
    }
    if (notes.empty()) {
        Measure* firstMeasure = m_sourceScore->firstMeasure();
        Segment* segment = m_sourceScore->firstSegment(SegmentType::ChordRest);
        while (firstMeasure && segment && segment->tick() < firstMeasure->endTick()) {
            for (track_idx_t track = 0; track < m_sourceScore->ntracks(); ++track) {
                EngravingItem* item = segment->element(track);
                if (item && item->isChord() && toChord(item)->notes().size() == 1) {
                    notes.push_back(toChord(item)->notes().front());
                }
            }
            segment = segment->next1(SegmentType::ChordRest);
        }
    }

    if (notes.empty()) {
        setStatus(tr("選択範囲にも先頭小節にも、単音の音符が見つかりませんでした。"), true);
        return false;
    }

    if (!collectChanges(notes, &m_upChanges, &m_downChanges)) {
        setStatus(tr("音高候補を作成できませんでした。"), true);
        return false;
    }

    m_upIsValid = std::all_of(m_upChanges.begin(), m_upChanges.end(), [](const PreviewPitchChange& change) {
        return pitchIsValid(change.pitch);
    });
    m_downIsValid = std::all_of(m_downChanges.begin(), m_downChanges.end(), [](const PreviewPitchChange& change) {
        return pitchIsValid(change.pitch);
    });
    if (!m_upIsValid && !m_downIsValid) {
        setStatus(tr("上下どちらの候補も対応音域（0〜127）を超えています。"), true);
        return false;
    }
    m_changesReady = true;
    return true;
}

bool PitchPreviewDialog::collectChanges(const std::vector<Note*>& notes,
                                        std::vector<PreviewPitchChange>* up,
                                        std::vector<PreviewPitchChange>* down)
{
    std::set<std::pair<Fraction, track_idx_t> > seen;
    bool hasRange = false;
    std::vector<PreviewPitchChange> collectedUp, collectedDown;
    std::vector<track_idx_t> collectedTracks;
    Fraction collectedStart, collectedEnd;

    for (Note* note : notes) {
        if (!note || !note->chord()) {
            continue;
        }

        Chord* chord = note->chord();
        // Grace notes share the main chord's tick/track. Never substitute that
        // main chord when the user selected a grace note.
        if (chord->isGrace()) {
            return false;
        }
        const Fraction tick = chord->tick();
        const track_idx_t track = chord->track();
        if (!seen.emplace(tick, track).second) {
            continue;
        }

        collectedUp.push_back({ tick, track, note->pitch(), note->pitch() + 12, note->tpc1(), note->tpc2() });
        collectedDown.push_back({ tick, track, note->pitch(), note->pitch() - 12, note->tpc1(), note->tpc2() });

        const Fraction noteEnd = chord->endTick();
        if (!hasRange) {
            collectedStart = tick;
            collectedEnd = noteEnd;
            hasRange = true;
        } else {
            if (tick < collectedStart) {
                collectedStart = tick;
            }
            if (noteEnd > collectedEnd) {
                collectedEnd = noteEnd;
            }
        }
        collectedTracks.push_back(track);
    }
    if (!hasRange || collectedUp.empty() || collectedEnd <= collectedStart) {
        return false;
    }
    std::sort(collectedTracks.begin(), collectedTracks.end());
    collectedTracks.erase(std::unique(collectedTracks.begin(), collectedTracks.end()), collectedTracks.end());
    *up = std::move(collectedUp);
    *down = std::move(collectedDown);
    m_tracks = std::move(collectedTracks);
    m_start = collectedStart;
    m_end = collectedEnd;
    return true;
}

bool PitchPreviewDialog::refreshCandidate(int direction)
{
    m_candidateView->setScore(nullptr);
    m_candidateProject.reset();

    if (!m_sourceScore || !m_changesReady || m_stale || m_adopted) {
        updateControls();
        return false;
    }

    const bool isUp = direction > 0;
    if ((isUp && !m_upIsValid) || (!isUp && !m_downIsValid)) {
        setStatus(tr("この候補は対応音域（0〜127）を超えています。"), true);
        updateControls();
        return false;
    }

    m_candidateProject = m_sourceScore->createPreviewProject();
    if (!m_candidateProject) {
        setStatus(tr("候補の独立プレビューを作成できませんでした。"), true);
        updateControls();
        return false;
    }

    MasterScore* candidateScore = m_candidateProject->masterScore();
    candidateScore->setLayoutMode(LayoutMode::LINE);
    const auto& changes = isUp ? m_upChanges : m_downChanges;
    const PreviewPitchResult result = applyPreviewPitchChanges(*candidateScore, changes, m_start, m_end, m_tracks);
    if (result != PreviewPitchResult::Applied) {
        m_candidateProject.reset();
        setStatus(previewResultMessage(result), true);
        updateControls();
        return false;
    }

    candidateScore->doLayout();
    for (const auto& change : changes) {
        auto* segment = candidateScore->tick2segment(change.tick, false, SegmentType::ChordRest);
        auto* chord = toChord(segment->element(change.track));
        chord->notes().front()->setColor(Color(40, 100, 200));
    }
    m_candidateView->setScore(candidateScore);
    setStatus(tr("%1個の音符・%2声部を変更する手作りの検証案です。青い音符が変更箇所です。採用するまで元譜は変わりません。")
              .arg(changes.size()).arg(m_tracks.size()));
    updateControls();
    return true;
}

void PitchPreviewDialog::applyCandidate()
{
    if (!m_sourceScore || !m_changesReady || !m_candidateProject || m_stale || m_adopted) {
        return;
    }

    if (globalContext()->currentProject() != m_sourceProject
        || globalContext()->currentNotation() != m_sourceNotation) {
        m_stale = true;
        setStatus(tr("アクティブな元の譜面が変わりました。このプレビューを閉じて候補を作り直してください。"), true);
        updateControls();
        return;
    }

    const bool isUp = m_candidateSelector->currentIndex() == 0;
    if ((isUp && !m_upIsValid) || (!isUp && !m_downIsValid)) {
        setStatus(tr("この候補は対応音域（0〜127）を超えています。"), true);
        return;
    }

    m_applying = true;
    const auto& changes = isUp ? m_upChanges : m_downChanges;
    const PreviewPitchResult result = applyPreviewPitchChanges(*m_sourceScore, changes, m_start, m_end, m_tracks);
    m_applying = false;

    if (result != PreviewPitchResult::Applied) {
        if (result == PreviewPitchResult::Stale || result == PreviewPitchResult::Busy) {
            m_stale = true;
        }
        setStatus(previewResultMessage(result), true);
        updateControls();
        return;
    }

    m_sourceNotation->notationChanged().notify();
    m_sourceNotation->undoStack()->stackChanged().notify();
    m_adopted = true;
    setStatus(tr("音高変更を1回で取り消せる編集として採用しました。"));
    updateControls();
}

void PitchPreviewDialog::updateControls()
{
    const bool currentSource = m_sourceProject && globalContext()->currentProject() == m_sourceProject
                               && globalContext()->currentNotation() == m_sourceNotation;
    const bool indexValid = m_candidateSelector && (m_candidateSelector->currentIndex() == 0 ? m_upIsValid : m_downIsValid);
    const bool canAdopt = m_sourceScore && m_changesReady && m_candidateProject && !m_stale && !m_adopted && currentSource && indexValid;
    m_applyButton->setEnabled(canAdopt);
    m_candidateSelector->setEnabled(m_sourceScore && m_changesReady && !m_stale && !m_adopted && (m_upIsValid || m_downIsValid));
}

void PitchPreviewDialog::setStatus(const QString& message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #b3261e;") : QString());
}
} // namespace mu::notation
