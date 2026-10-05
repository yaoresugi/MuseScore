/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#pragma once

#include <memory>
#include <vector>

#include <QDialog>

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "engraving/editing/previewpitchchanges.h"
#include "engraving/types/fraction.h"
#include "modularity/ioc.h"
#include "notation/inotation.h"
#include "project/inotationproject.h"

class QLabel;
class QComboBox;
class QPushButton;

namespace mu::engraving {
class EngravingProject;
class MasterScore;
class Note;
}

namespace mu::notation {
class PitchPreviewScoreView;

// A small native UI spike for reviewing deterministic octave-shift candidates.
// It deliberately does not use the global current notation as its render source.
class PitchPreviewDialog : public QDialog, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit PitchPreviewDialog(QWidget* parent = 0);
    ~PitchPreviewDialog() override;

private:
    bool captureSource();
    bool buildPitchChanges();
    bool collectChanges(const std::vector<engraving::Note*>& notes, std::vector<engraving::PreviewPitchChange>* up,
                        std::vector<engraving::PreviewPitchChange>* down);
    bool refreshCandidate(int direction);
    void applyCandidate();
    void updateControls();
    void setStatus(const QString& message, bool isError = false);

    project::INotationProjectPtr m_sourceProject;
    INotationPtr m_sourceNotation;
    engraving::MasterScore* m_sourceScore = nullptr;
    std::shared_ptr<engraving::EngravingProject> m_beforeProject;
    std::shared_ptr<engraving::EngravingProject> m_candidateProject;
    std::vector<engraving::PreviewPitchChange> m_upChanges;
    std::vector<engraving::PreviewPitchChange> m_downChanges;
    engraving::Fraction m_start;
    engraving::Fraction m_end;
    std::vector<engraving::track_idx_t> m_tracks;

    PitchPreviewScoreView* m_beforeView = nullptr;
    PitchPreviewScoreView* m_candidateView = nullptr;
    QComboBox* m_candidateSelector = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_applyButton = nullptr;
    bool m_stale = false;
    bool m_changesReady = false;
    bool m_applying = false;
    bool m_adopted = false;
    bool m_upIsValid = true;
    bool m_downIsValid = true;
};
}
