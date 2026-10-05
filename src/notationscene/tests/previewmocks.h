// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#pragma once

#include <gmock/gmock.h>
#include "context/iglobalcontext.h"
#include "project/inotationproject.h"
#include "notation/inotation.h"
#include "notation/imasternotation.h"
#include "notation/inotationelements.h"
#include "notation/internal/inotationundostack.h"

namespace mu::context {
class PreviewGlobalContextMock : public IGlobalContext
{
public:
    MOCK_METHOD(void, setCurrentProject, (const project::INotationProjectPtr& project), (override));
    MOCK_METHOD(project::INotationProjectPtr, currentProject, (), (const, override));
    MOCK_METHOD(muse::async::Notification, currentProjectChanged, (), (const, override));
    MOCK_METHOD(notation::IMasterNotationPtr, currentMasterNotation, (), (const, override));
    MOCK_METHOD(muse::async::Notification, currentMasterNotationChanged, (), (const, override));
    MOCK_METHOD(void, setCurrentNotation, (const notation::INotationPtr& notation), (override));
    MOCK_METHOD(notation::INotationPtr, currentNotation, (), (const, override));
    MOCK_METHOD(muse::async::Notification, currentNotationChanged, (), (const, override));
    MOCK_METHOD(void, setCurrentPlayer, (const muse::audio::IPlayerPtr& player), (override));
    MOCK_METHOD(IPlaybackStatePtr, playbackState, (), (const, override));
};
}

namespace mu::project {
class PreviewProjectMock : public INotationProject
{
public:
    MOCK_METHOD(muse::io::path_t, path, (), (const, override));
    MOCK_METHOD(void, setPath, (const muse::io::path_t& path), (override));
    MOCK_METHOD(muse::async::Notification, pathChanged, (), (const, override));
    MOCK_METHOD(QString, displayName, (), (const, override));
    MOCK_METHOD(muse::async::Notification, displayNameChanged, (), (const, override));
    MOCK_METHOD(muse::Ret, load, (const muse::io::path_t& path, const OpenParams& params, const std::string& format), (override));
    MOCK_METHOD(muse::Ret, createNew, (const ProjectCreateOptions& projectInfo), (override));
    MOCK_METHOD(bool, isCloudProject, (), (const, override));
    MOCK_METHOD(const CloudProjectInfo&, cloudInfo, (), (const, override));
    MOCK_METHOD(void, setCloudInfo, (const CloudProjectInfo& info), (override));
    MOCK_METHOD(const CloudAudioInfo&, cloudAudioInfo, (), (const, override));
    MOCK_METHOD(void, setCloudAudioInfo, (const CloudAudioInfo& audioInfo), (override));
    MOCK_METHOD(bool, isNewlyCreated, (), (const, override));
    MOCK_METHOD(void, markAsNewlyCreated, (), (override));
    MOCK_METHOD(bool, isImported, (), (const, override));
    MOCK_METHOD(void, markAsUnsaved, (), (override));
    MOCK_METHOD(muse::ValNt<bool>, needSave, (), (const, override));
    MOCK_METHOD(muse::Ret, canSave, (), (const, override));
    MOCK_METHOD(bool, needAutoSave, (), (const, override));
    MOCK_METHOD(void, setNeedAutoSave, (bool val), (override));
    MOCK_METHOD(muse::Ret, save, (const muse::io::path_t& path, SaveMode saveMode, bool createBackup), (override));
    MOCK_METHOD(muse::Ret, savePage, (const muse::io::path_t& path, const size_t pageNum), (override));
    MOCK_METHOD((muse::async::Channel<muse::io::path_t, SaveMode>), saveComplited, (), (const, override));
    MOCK_METHOD(muse::Ret, writeToDevice, (QIODevice * device), (override));
    MOCK_METHOD(ProjectMeta, metaInfo, (), (const, override));
    MOCK_METHOD(void, setMetaInfo, (const ProjectMeta& meta, bool undoable), (override));
    MOCK_METHOD(notation::IMasterNotationPtr, masterNotation, (), (const, override));
    MOCK_METHOD(IProjectAudioSettingsPtr, audioSettings, (), (const, override));
};
}

namespace mu::notation {
class PreviewNotationMock : public INotation
{
public:
    MOCK_METHOD(project::INotationProject*, project, (), (const, override));
    MOCK_METHOD(IMasterNotationPtr, masterNotation, (), (const, override));
    MOCK_METHOD(QString, name, (), (const, override));
    MOCK_METHOD(QString, projectName, (), (const, override));
    MOCK_METHOD(QString, projectNameAndPartName, (), (const, override));
    MOCK_METHOD(QString, workTitle, (), (const, override));
    MOCK_METHOD(QString, projectWorkTitle, (), (const, override));
    MOCK_METHOD(QString, projectWorkTitleAndPartName, (), (const, override));
    MOCK_METHOD(bool, isOpen, (), (const, override));
    MOCK_METHOD(void, setIsOpen, (bool opened), (override));
    MOCK_METHOD(muse::async::Notification, openChanged, (), (const, override));
    MOCK_METHOD(bool, hasVisibleParts, (), (const, override));
    MOCK_METHOD(bool, isMaster, (), (const, override));
    MOCK_METHOD(ViewMode, viewMode, (), (const, override));
    MOCK_METHOD(void, setViewMode, (const ViewMode& viewMode), (override));
    MOCK_METHOD(muse::async::Notification, viewModeChanged, (), (const, override));
    MOCK_METHOD(INotationPaintingPtr, painting, (), (const, override));
    MOCK_METHOD(INotationViewStatePtr, viewState, (), (const, override));
    MOCK_METHOD(INotationSoloMuteStatePtr, soloMuteState, (), (const, override));
    MOCK_METHOD(INotationInteractionPtr, interaction, (), (const, override));
    MOCK_METHOD(INotationMidiInputPtr, midiInput, (), (const, override));
    MOCK_METHOD(INotationUndoStackPtr, undoStack, (), (const, override));
    MOCK_METHOD(INotationStylePtr, style, (), (const, override));
    MOCK_METHOD(INotationElementsPtr, elements, (), (const, override));
    MOCK_METHOD(INotationAccessibilityPtr, accessibility, (), (const, override));
    MOCK_METHOD(INotationPartsPtr, parts, (), (const, override));
    MOCK_METHOD(muse::async::Notification, notationChanged, (), (const, override));
};
}

namespace mu::notation {
class PreviewMasterNotationMock : public IMasterNotation
{
public:
    MOCK_METHOD(project::INotationProject*, project, (), (const, override));
    MOCK_METHOD(muse::Ret, setupNewScore, (engraving::MasterScore * score, const ScoreCreateOptions& options), (override));
    MOCK_METHOD(void, applyOptions, (engraving::MasterScore * score, const ScoreCreateOptions& options, bool createdFromTemplate),
                (override));
    MOCK_METHOD(engraving::MasterScore*, masterScore, (), (const, override));
    MOCK_METHOD(void, setMasterScore, (engraving::MasterScore * masterScore), (override));
    MOCK_METHOD(INotationPtr, notation, (), (override));
    MOCK_METHOD(int, mscVersion, (), (const, override));
    MOCK_METHOD(IExcerptNotationPtr, createEmptyExcerpt, (const QString& name), (const, override));
    MOCK_METHOD(const ExcerptNotationList&, excerpts, (), (const, override));
    MOCK_METHOD(muse::async::Notification, excerptsChanged, (), (const, override));
    MOCK_METHOD(const ExcerptNotationList&, potentialExcerpts, (), (const, override));
    MOCK_METHOD(void, initExcerpts, (const ExcerptNotationList& excerpts), (override));
    MOCK_METHOD(void, setExcerpts, (const ExcerptNotationList& excerpts), (override));
    MOCK_METHOD(void, resetExcerpt, (IExcerptNotationPtr excerpt), (override));
    MOCK_METHOD(void, sortExcerpts, (ExcerptNotationList & excerpts), (override));
    MOCK_METHOD(void, setExcerptIsOpen, (const INotationPtr excerptNotation, bool opened), (override));
    MOCK_METHOD(INotationPartsPtr, parts, (), (const, override));
    MOCK_METHOD(bool, hasParts, (), (const, override));
    MOCK_METHOD(muse::async::Notification, hasPartsChanged, (), (const, override));
    MOCK_METHOD(INotationPlaybackPtr, playback, (), (const, override));
    MOCK_METHOD(void, initNotationSoloMuteState, (const INotationPtr notation), (override));
};
}

namespace mu::notation {
class PreviewElementsMock : public INotationElements
{
public:
    MOCK_METHOD(mu::engraving::Score*, msScore, (), (const, override));
    MOCK_METHOD(std::vector<EngravingItem*>, search, (const QString& searchText), (const, override));
    MOCK_METHOD(std::vector<EngravingItem*>, elements, (const FilterElementsOptions& elementOptions), (const, override));
    MOCK_METHOD(Measure*, measure, (const int measureIndex), (const, override));
    MOCK_METHOD(const PageList&, pages, (), (const, override));
    MOCK_METHOD(const Page*, pageByPoint, (const muse::PointF& point), (const, override));
};
}

namespace mu::notation {
class PreviewUndoStackMock : public INotationUndoStack
{
public:
    MOCK_METHOD(bool, canUndo, (), (const, override));
    MOCK_METHOD(void, undo, (mu::engraving::EditData*), (override));
    MOCK_METHOD(bool, canRedo, (), (const, override));
    MOCK_METHOD(void, redo, (mu::engraving::EditData*), (override));
    MOCK_METHOD(void, undoRedoToIndex, (size_t, mu::engraving::EditData*), (override));
    MOCK_METHOD(void, prepareChanges, (const muse::TranslatableString&), (override));
    MOCK_METHOD(void, rollbackChanges, (), (override));
    MOCK_METHOD(void, commitChanges, (), (override));
    MOCK_METHOD(void, mergeCommands, (const size_t startIdx), (override));
    MOCK_METHOD(bool, isStackClean, (), (const, override));
    MOCK_METHOD(void, lock, (), (override));
    MOCK_METHOD(void, unlock, (), (override));
    MOCK_METHOD(bool, isLocked, (), (const, override));
    MOCK_METHOD(const muse::TranslatableString, topMostUndoActionName, (), (const, override));
    MOCK_METHOD(const muse::TranslatableString, topMostRedoActionName, (), (const, override));
    MOCK_METHOD(size_t, undoRedoActionCount, (), (const, override));
    MOCK_METHOD(size_t, currentStateIndex, (), (const, override));
    MOCK_METHOD(const muse::TranslatableString, lastActionNameAtIdx, (size_t), (const, override));
    MOCK_METHOD(muse::async::Notification, stackChanged, (), (const, override));
    MOCK_METHOD(muse::async::Channel<ScoreChanges>, changesChannel, (), (const, override));
    MOCK_METHOD(muse::async::Notification, undoRedoNotification, (), (const, override));
};
}
