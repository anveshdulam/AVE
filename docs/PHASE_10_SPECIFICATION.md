# AVE — Phase 10 Technical Specification & Acceptance Plan (Native Application Integration)

**Document Status:** Final Consistency-Corrected Specification (Implementation Not Started)  
**Target Milestone:** Phase 10 — Native Application Integration (Ad-hoc post-Phase-8 milestone extending [ENGINEERING_ROADMAP.md](ENGINEERING_ROADMAP.md))  
**Repository Baseline Audited:** Branch `master`, HEAD `85bc61865e1b483897a3c6730fe7cbc0f34324ed`  
**Specification Date:** 2026-11-10  

---

## 1. Purpose, Scope & Layered Architecture

Phase 10 connects the existing native C++20 engine libraries (`core`, `media`, `engines`, `ai`) to the 8 QML views under `apps/desktop/qml/` inside one native Windows desktop executable (`ave_desktop.exe`), retiring the temporary PySide6 mockup runner ([`apps/desktop/launch_gui.py`](../apps/desktop/launch_gui.py) and [`run_editor.bat`](../run_editor.bat)) from primary production use.

### 1.1 Five-Layer Architecture Placement (ADR-0004 Compliant)

All Phase 10 components remain strictly inside the five architectural layers defined in [ADR-0004](adr/0004-layered-architecture.md). Dependencies flow strictly **downward**. No sixth layer exists; all five C++/Qt `QObject` service adapters live inside **Layer 1 (Presentation)**:

```text
+---------------------------------------------------------------------------------------------------+
| LAYER 1: PRESENTATION (Qt GUI Thread)                                                             |
|  - QML Views (8): main, ToolbarView, MediaPoolView, ViewerView, TimelineCanvas,                   |
|                   TrackHeaderView, InspectorView, AudioMeterView                                  |
|  - QQuickAsyncImageProvider: LiveFrameImageProvider -> LiveFrameImageResponse                     |
|  - C++/Qt Adapter Services (46 Q_PROPERTYs total):                                                |
|      PlaybackService (12) | MediaPoolModel (5) | TimelineModel (13)                               |
|      InspectorService (10) | AiStudioService (6)                                                  |
|  - Presentation ViewModel Helpers: TimelineViewport, TimelineSnapping,                             |
|                                    TimelineViewModel, PlaybackViewModel, InspectorViewModel       |
+-------------------------------------------------|-------------------------------------------------+
                                                  | (Downward Calls & Immutable Snapshots)
+-------------------------------------------------v-------------------------------------------------+
| LAYER 2: APPLICATION CORE                                                                         |
|  - Project, Timeline, Track, TimelineItem, MediaPool, MediaAsset, Rational, Timecode              |
|  - Command Bus: Command, Insert/Delete/Move/Split/SetTransform/SetTrackMute, CompositeCommand,    |
|                 CommandHistory, OtioInterchange, AssetRelinker                                    |
|  - AI Orchestration: LanguageSidecar (in-process mock in Phase 10), PromptParser,                 |
|                      DeterministicPlanner, FingerprintEngine, ProposalBridge, ProposalInspector   |
+-------------------------------------------------|-------------------------------------------------+
                                                  |
+-------------------------------------------------v-------------------------------------------------+
| LAYER 3: REAL-TIME ENGINES                                                                        |
|  - PlaybackSnapshot (Immutable Timeline + MediaPool Copy Published on Commit / Seek)              |
|  - PlaybackSession, PlaybackClock, PlaybackController, BoundedFrameQueue<VideoFrame>              |
|  - TimelinePrefetcher (Worker Thread reading PlaybackSnapshot only)                               |
|  - RenderGraph, VulkanCompositor, VulkanReadbackRing (2-slot staging), FramePresentationHub (4)   |
|  - AudioMixer, AudioRingBuffer, NullAudioDevice (Simulated Output in Phase 10)                    |
+-------------------------------------------------|-------------------------------------------------+
                                                  |
+-------------------------------------------------v-------------------------------------------------+
| LAYER 4: MEDIA & DATA                                                                             |
|  - MediaImportQueue (max 2 active, 16 pending, 18 total cap)                                      |
|  - FFmpegProbe, StreamDecoder, StreamAudioDecoder, ThumbnailExtractor, WaveformExtractor          |
|  - MediaCache, .aveproj JSON Persistence                                                          |
+-------------------------------------------------|-------------------------------------------------+
                                                  |
+-------------------------------------------------v-------------------------------------------------+
| LAYER 5: PLATFORM                                                                                 |
|  - Windows 11 x64 / MSVC v143 / Windows 11 SDK                                                    |
|  - Vulkan 1.4 (`VulkanDevice`, `VulkanTexture`) on Intel Arc Xe-LPG Graphics                      |
|  - FFmpeg n8.1.3 LGPLv3 Shared DLLs (`D:\DevTools\ffmpeg`)                                        |
+---------------------------------------------------------------------------------------------------+
```

### 1.2 Deferred Post-Phase-10 Scope
The following capabilities are **out of scope** for Phase 10 and are not claimed or partially faked:
- `QQuickRhiItem` / Vulkan `QRhi` external command-buffer zero-copy viewer interop (deferred to post-Phase-10 proof of concept; Section 10).
- Physical WASAPI / PortAudio audio output device backend (`PlaybackSession` uses `NullAudioDevice` in Phase 10).
- Timeline MP4 / H.264 / ProRes video export & muxing pipeline.
- Color grading UI, 3-way wheels, LUT browser, software/hardware scopes, and OCIO/ACES transforms.
- OpenFX plugin host execution.
- Custom PyTorch neural weights, C++ ONNX model inference, and out-of-process named-pipe LLM execution.
- Windows installer (MSIX / NSIS / WiX).

---

## 2. Pinned Toolchain, Build Prerequisites vs CI Prerequisites

### 2.1 Audited Environment & Pinned Phase 10 Baseline (Windows x64)
| Component | Audited Workspace / Host State (`2026-11-10`) | Phase 10 Contract |
|---|---|---|
| Compiler | MSVC `19.44.35229` x64 (`VS 2022 BuildTools` `14.44.35207`) | MSVC `v143` x64 (`/std:c++20 /permissive- /Zc:__cplusplus /EHsc /utf-8 /W4`) |
| Build System | CMake `4.4.4`, Ninja `1.13.2` | CMake `3.25+`, Ninja `1.13+` (`CMakePresets.json` `x64-debug` / `x64-release`) |
| **Qt C++ SDK** | **NOT INSTALLED (`Qt6_DIR-NOTFOUND`)** | **Qt 6.8.3 (`msvc2022_64`)** — selected **open-source LGPLv3 compatibility baseline** (see Section 2.2 below). Modules: `Core`, `Gui`, `Quick`, `Qml`, `QuickControls2`, `Test`; runtime QML module `QtQuick.Dialogs` (`IFileDialog` under `QGuiApplication`). |
| Vulkan SDK | LunarG Vulkan SDK `1.4.363.0` (`C:\VulkanSDK\1.4.363.0`) | Vulkan SDK `1.4.x` (`VK_LAYER_KHRONOS_validation` for debug verification) |
| GPU Hardware | Intel(R) Arc(TM) Graphics (`0x8086:0x7d55`, iGPU, Vulkan `1.4.325`) | Integrated GPU shared-memory budget applies (`<= 1280x720` live preview) |
| FFmpeg (Local) | `n8.1.3-34-ga35157abb9-20261106` (`D:\DevTools\ffmpeg`, `--enable-version3 --enable-shared`, no `--enable-gpl`) | **LGPLv3** shared runtime DLLs (`avformat-62.dll`, `avcodec-62.dll`, `avutil-60.dll`, `swscale-9.dll`, `swresample-6.dll`). MSVC `.lib` import libraries (`D:\DevTools\ffmpeg\lib\av*.lib`, `sw*.lib`) verified present; `dumpbin /dependents` on the DLLs confirms dependencies solely on Windows Universal CRT (`api-ms-win-crt-*.dll`), Windows OS DLLs, and peer FFmpeg DLLs — **zero external MinGW runtime DLLs required**. |
| `vcpkg` Manifest | Declares `gtest`, `nlohmann-json`; `build/smoke` did not run `vcpkg` (`GTest_DIR-NOTFOUND`, `nlohmann_json_DIR-NOTFOUND`) | Run `vcpkg` manifest toolchain via `CMakePresets.json` so `GTest::gtest` and `nlohmann-json` resolve cleanly. |

### 2.2 Prerequisites Separated: Local Build vs Pre-Merge CI / Release

#### Local Build Prerequisites (Must Be Present Before Phase 10 Code Compiles Locally)
1. **Install Qt 6.8.3 (`msvc2022_64`):** Qt 6.8.3 (released March 26, 2025) is an open-source Qt 6.8 patch release accessible under **LGPLv3** via the Qt Online Installer or `aqtinstall` (`D:\Qt\6.8.3\msvc2022_64`). We select Qt 6.8.3 as the **open-source LGPLv3 compatibility baseline** (giving us Qt 6.7+ `QQuickRhiItem` readiness and Qt 6.8 `Image.retainWhileLoading`); later commercial-only Qt 6.8.x LTS maintenance patch releases are **not** assumed available to open-source builds.
2. **Resolve `vcpkg` Dependencies:** Configure via `cmake --preset x64-debug` (with `VCPKG_ROOT=D:\vcpkg`) so `GTest::gtest` and `nlohmann-json` install cleanly from [`vcpkg.json`](../vcpkg.json).
3. **Remove PySide6 Build Fallbacks:** Phase 10 CMake target wiring requires `Qt6::Core`, `Qt6::Gui`, `Qt6::Quick`, `Qt6::Qml`, `Qt6::QuickControls2` (plus `Qt6::Test` for test targets) from a real `msvc2022_64` Qt 6 installation. Zero build rules may pull headers or DLLs from Python `PySide6`.

#### Pre-Merge CI / Release Prerequisite (Not Required to Build Locally)
- [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) lines 36–38 currently download `ffmpeg-master-latest-win64-gpl-shared.zip` (**GPL-licensed**), violating [ADR-0005](adr/0005-ffmpeg-media-foundation.md). Before merging Phase 10 into `master` or shipping binaries, CI must switch to an **LGPLv3 / LGPLv2.1+ shared** FFmpeg build (without `--enable-gpl`), install Qt 6.8.3 + `vcpkg`, and stage LGPL third-party notices and user-relinking documentation.

### 2.3 Native Runtime Independence vs External Python Environments
1. **`ave_desktop.exe` Production Runtime:** Pure native C++20 + Qt 6.8.3 binary. **Zero runtime dependency on Python, `PySide6`, `torch`, or `onnxruntime`.** Does not launch a Python subprocess at startup or during AI prompt generation.
2. **Temporary Prototype Retired:** [`apps/desktop/launch_gui.py`](../apps/desktop/launch_gui.py) (`PySide6 6.12.0`) is retired from [`run_editor.bat`](../run_editor.bat) in favor of `ave_desktop.exe`.
3. **External Python ML Environment (Offline Training Only):** Host `Python 3.11.9` (`torch 2.3.1`, `onnxruntime 1.26.0`, `numpy 1.26.4`) is completely external to the C++ runtime and has zero runtime coupling with `ave_desktop.exe`.

---

## 3. Canonical Five-Service C++ Interface Contract (46 `Q_PROPERTY`s)

**Audited Codebase Baseline (`HEAD 85bc618`):** **`0` `Q_OBJECT` classes and `0` `Q_PROPERTY` declarations** exist in C++ today.  
**Phase 10 Canonical Target:** **`46` `Q_PROPERTY` declarations across 5 `QObject` services** (`12 + 5 + 13 + 10 + 6 = 46`).

All non-`CONSTANT` properties use **0-argument `NOTIFY` signals** (universally valid in Qt 6 MOC and `QSignalSpy`, guarded by equality checks to suppress redundant emissions). Legacy property/signal divergences (`hardwareDeviceName`, `videoTrackCount`, `audioTrackCount`, `similarityChanged`, `WRITE` on `playheadPosition`, `rowCount` as READ accessor) are retired here and replaced by this single canonical contract.

### 3.1 Master Property Inventory (`46` Properties Total)

| Service | Count | Canonical `Q_PROPERTY` Names (`Type` / Mutability) |
|---|---:|---|
| `PlaybackService` | **12** | `isPlaying` (`bool`, R), `playheadPosition` (`double`, R — seconds `[0, duration]`), `durationSeconds` (`double`, R), `timecode` (`QString`, R), `fps` (`double`, R), `currentFrameSeq` (`quint64`, R), `compositingEnabled` (`bool`, R/W), `hasGpuAcceleration` (`bool`, CONST), `gpuDeviceName` (`QString`, CONST), `audioOutputStatus` (`QString`, CONST), `leftLevelDb` (`double`, R), `rightLevelDb` (`double`, R) |
| `MediaPoolModel` | **5** | `assetCount` (`int`, R via `assetCount()`), `isImporting` (`bool`, R), `activeImportCount` (`int`, R), `pendingImportCount` (`int`, R), `lastImportError` (`QString`, R) |
| `TimelineModel` | **13** | `trackCount` (`int`, R), `durationSeconds` (`double`, R), `activeTool` (`int`, R/W `0..3`), `snappingEnabled` (`bool`, R/W), `zoomFactor` (`double`, R/W), `selectedClipId` (`QString`, R/W), `canUndo` (`bool`, R), `canRedo` (`bool`, R), `isDirty` (`bool`, R), `projectName` (`QString`, R), `projectFilePath` (`QString`, R), `tracksModel` (`QAbstractListModel*`, CONST), `clipsModel` (`QAbstractListModel*`, CONST) |
| `InspectorService` | **10** | `hasSelection` (`bool`, R), `clipName` (`QString`, R), `trackName` (`QString`, R), `durationTimecode` (`QString`, R), `posX` (`double`, R/W), `posY` (`double`, R/W), `scaleX` (`double`, R/W), `scaleY` (`double`, R/W), `opacity` (`double`, R/W), `blendMode` (`QString`, R/W) |
| `AiStudioService` | **6** | `isBusy` (`bool`, R), `hasReference` (`bool`, R), `structuralReferenceSimilarity` (`QVariant`, R), `statusMessage` (`QString`, R), `proposedOperations` (`QStringList`, R), `hasValidProposal` (`bool`, R) |
| **Total** | **46** | **Reconciled (`12 + 5 + 13 + 10 + 6 = 46`)** |

### 3.2 Canonical C++ Class Declarations

```cpp
namespace ave::desktop {

class PlaybackService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    isPlaying           READ isPlaying           NOTIFY isPlayingChanged)
    Q_PROPERTY(double  playheadPosition    READ playheadPosition    NOTIFY playheadPositionChanged) // READ-ONLY; mutate via seekSeconds()
    Q_PROPERTY(double  durationSeconds     READ durationSeconds     NOTIFY durationSecondsChanged)
    Q_PROPERTY(QString timecode            READ timecode            NOTIFY timecodeChanged)
    Q_PROPERTY(double  fps                 READ fps                 NOTIFY fpsChanged)
    Q_PROPERTY(quint64 currentFrameSeq     READ currentFrameSeq     NOTIFY currentFrameSeqChanged)
    Q_PROPERTY(bool    compositingEnabled  READ compositingEnabled  WRITE setCompositingEnabled NOTIFY compositingEnabledChanged)
    Q_PROPERTY(bool    hasGpuAcceleration  READ hasGpuAcceleration  CONSTANT)
    Q_PROPERTY(QString gpuDeviceName       READ gpuDeviceName       CONSTANT)
    Q_PROPERTY(QString audioOutputStatus   READ audioOutputStatus   CONSTANT)
    Q_PROPERTY(double  leftLevelDb         READ leftLevelDb         NOTIFY audioLevelsChanged)
    Q_PROPERTY(double  rightLevelDb        READ rightLevelDb        NOTIFY audioLevelsChanged)

public:
    explicit PlaybackService(ave::PlaybackSession& session,
                             std::shared_ptr<FramePresentationHub> frame_hub,
                             std::shared_ptr<PlaybackSnapshotStore> snapshot_store,
                             QObject* parent = nullptr);
    ~PlaybackService() override;

    [[nodiscard]] bool    isPlaying() const noexcept;
    [[nodiscard]] double  playheadPosition() const noexcept;
    [[nodiscard]] double  durationSeconds() const noexcept;
    [[nodiscard]] QString timecode() const;
    [[nodiscard]] double  fps() const noexcept;
    [[nodiscard]] quint64 currentFrameSeq() const noexcept;
    [[nodiscard]] bool    compositingEnabled() const noexcept;
    void setCompositingEnabled(bool enabled);
    [[nodiscard]] bool    hasGpuAcceleration() const noexcept;
    [[nodiscard]] QString gpuDeviceName() const;
    [[nodiscard]] QString audioOutputStatus() const; // "NullAudioDevice (Simulated; Physical WASAPI Deferred)"
    [[nodiscard]] double  leftLevelDb() const noexcept;  // Clamped [-60.0, 0.0] dBFS; -60.0 when stopped
    [[nodiscard]] double  rightLevelDb() const noexcept; // Clamped [-60.0, 0.0] dBFS; -60.0 when stopped

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekSeconds(double seconds);
    Q_INVOKABLE void seekTimecode(const QString& tc);
    Q_INVOKABLE void stepForward(int frames = 1);   // Ignores frames <= 0
    Q_INVOKABLE void stepBackward(int frames = 1);  // Ignores frames <= 0
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void syncFromTimeline();
    Q_INVOKABLE void shutdown();

signals:
    void isPlayingChanged();
    void playheadPositionChanged();
    void durationSecondsChanged();
    void timecodeChanged();
    void fpsChanged();
    void currentFrameSeqChanged();
    void compositingEnabledChanged();
    void audioLevelsChanged();
};

class MediaPoolModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int     assetCount         READ assetCount         NOTIFY assetCountChanged)
    Q_PROPERTY(bool    isImporting        READ isImporting        NOTIFY isImportingChanged)
    Q_PROPERTY(int     activeImportCount  READ activeImportCount  NOTIFY importCountsChanged)
    Q_PROPERTY(int     pendingImportCount READ pendingImportCount NOTIFY importCountsChanged)
    Q_PROPERTY(QString lastImportError    READ lastImportError    NOTIFY lastImportErrorChanged)

public:
    enum Roles {
        AssetIdRole = Qt::UserRole + 1,
        NameRole,
        FilePathRole,
        DurationTextRole,
        DurationSecondsRole,
        ResolutionTextRole,
        FpsTextRole,
        CodecTextRole,
        MediaTypeRole,
        IsOnlineRole,
        ThumbnailDataUrlRole
    };

    explicit MediaPoolModel(ave::Project& project,
                            std::shared_ptr<MediaImportQueue> import_queue,
                            QObject* parent = nullptr);
    ~MediaPoolModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int     assetCount() const noexcept; // Dedicated 0-arg accessor (NEVER rowCount)
    [[nodiscard]] bool    isImporting() const noexcept;
    [[nodiscard]] int     activeImportCount() const noexcept;
    [[nodiscard]] int     pendingImportCount() const noexcept;
    [[nodiscard]] QString lastImportError() const;

    Q_INVOKABLE QString importMediaFile(const QString& localPath);   // Returns jobId or "" on reject
    Q_INVOKABLE void    importMediaFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void    cancelImport(const QString& jobId);
    Q_INVOKABLE void    cancelAllImports();
    Q_INVOKABLE void    reloadFromProject();
    Q_INVOKABLE void    shutdown();

signals:
    void assetCountChanged();
    void isImportingChanged();
    void importCountsChanged();
    void lastImportErrorChanged();
    void projectMediaMutated(); // Notifies coordinator to republish PlaybackSnapshot
};

class TimelineModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int                 trackCount       READ trackCount       NOTIFY trackCountChanged)
    Q_PROPERTY(double              durationSeconds  READ durationSeconds  NOTIFY durationSecondsChanged)
    Q_PROPERTY(int                 activeTool       READ activeTool       WRITE setActiveTool       NOTIFY activeToolChanged)
    Q_PROPERTY(bool                snappingEnabled  READ snappingEnabled  WRITE setSnappingEnabled  NOTIFY snappingEnabledChanged)
    Q_PROPERTY(double              zoomFactor       READ zoomFactor       WRITE setZoomFactor       NOTIFY zoomFactorChanged)
    Q_PROPERTY(QString             selectedClipId   READ selectedClipId   WRITE setSelectedClipId   NOTIFY selectedClipIdChanged)
    Q_PROPERTY(bool                canUndo          READ canUndo          NOTIFY historyChanged)
    Q_PROPERTY(bool                canRedo          READ canRedo          NOTIFY historyChanged)
    Q_PROPERTY(bool                isDirty          READ isDirty          NOTIFY projectStateChanged)
    Q_PROPERTY(QString             projectName      READ projectName      NOTIFY projectStateChanged)
    Q_PROPERTY(QString             projectFilePath  READ projectFilePath  NOTIFY projectStateChanged)
    Q_PROPERTY(QAbstractListModel* tracksModel      READ tracksModel      CONSTANT)
    Q_PROPERTY(QAbstractListModel* clipsModel       READ clipsModel       CONSTANT)

public:
    enum class Tool : int { Select = 0, Razor = 1, Trim = 2, Slip = 3 }; // Matches ave::TimelineTool
    Q_ENUM(Tool)

    explicit TimelineModel(ave::Project& project, QObject* parent = nullptr);
    ~TimelineModel() override;

    [[nodiscard]] int     trackCount() const noexcept;
    [[nodiscard]] double  durationSeconds() const noexcept;
    [[nodiscard]] int     activeTool() const noexcept;
    void setActiveTool(int tool); // Rejects values outside [0, 3]
    [[nodiscard]] bool    snappingEnabled() const noexcept;
    void setSnappingEnabled(bool enabled);
    [[nodiscard]] double  zoomFactor() const noexcept;
    void setZoomFactor(double zoom);
    [[nodiscard]] QString selectedClipId() const;
    void setSelectedClipId(const QString& clipId);
    [[nodiscard]] bool    canUndo() const noexcept;
    [[nodiscard]] bool    canRedo() const noexcept;
    [[nodiscard]] bool    isDirty() const noexcept;
    [[nodiscard]] QString projectName() const;
    [[nodiscard]] QString projectFilePath() const;
    [[nodiscard]] QAbstractListModel* tracksModel() const noexcept;
    [[nodiscard]] QAbstractListModel* clipsModel() const noexcept;

    Q_INVOKABLE bool insertClipFromAsset(const QString& assetId, const QString& trackId = {}, double timelineInSeconds = -1.0);
    Q_INVOKABLE bool splitAtPlayhead(double playheadSeconds);
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool moveSelectedClip(const QString& dstTrackId, double newInSeconds);
    Q_INVOKABLE bool toggleTrackMute(const QString& trackId);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool newProject(const QString& name = QStringLiteral("Untitled Project"));
    Q_INVOKABLE bool openProject(const QString& filePath);
    Q_INVOKABLE bool saveProject(const QString& filePath = {});
    Q_INVOKABLE bool exportOtio(const QString& filePath);
    Q_INVOKABLE int  relinkOfflineMedia(const QString& searchDirectory);

signals:
    void trackCountChanged();
    void durationSecondsChanged();
    void activeToolChanged();
    void snappingEnabledChanged();
    void zoomFactorChanged();
    void selectedClipIdChanged();
    void historyChanged();
    void projectStateChanged();
    void projectReloaded();  // Triggers MediaPoolModel/PlaybackService/InspectorService full sync
};

class InspectorService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    hasSelection     READ hasSelection     NOTIFY selectionChanged)
    Q_PROPERTY(QString clipName         READ clipName         NOTIFY selectionChanged)
    Q_PROPERTY(QString trackName        READ trackName        NOTIFY selectionChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY selectionChanged)
    Q_PROPERTY(double  posX             READ posX             WRITE setPosX      NOTIFY transformChanged)
    Q_PROPERTY(double  posY             READ posY             WRITE setPosY      NOTIFY transformChanged)
    Q_PROPERTY(double  scaleX           READ scaleX           WRITE setScaleX    NOTIFY transformChanged)
    Q_PROPERTY(double  scaleY           READ scaleY           WRITE setScaleY    NOTIFY transformChanged)
    Q_PROPERTY(double  opacity          READ opacity          WRITE setOpacity   NOTIFY transformChanged)
    Q_PROPERTY(QString blendMode        READ blendMode        WRITE setBlendMode NOTIFY transformChanged)

public:
    explicit InspectorService(ave::Project& project, TimelineModel& timeline_model, QObject* parent = nullptr);
    ~InspectorService() override;

    [[nodiscard]] bool    hasSelection() const noexcept;
    [[nodiscard]] QString clipName() const;
    [[nodiscard]] QString trackName() const;
    [[nodiscard]] QString durationTimecode() const;
    [[nodiscard]] double  posX() const noexcept;
    [[nodiscard]] double  posY() const noexcept;
    [[nodiscard]] double  scaleX() const noexcept;
    [[nodiscard]] double  scaleY() const noexcept;
    [[nodiscard]] double  opacity() const noexcept;
    [[nodiscard]] QString blendMode() const;

    void setPosX(double x);
    void setPosY(double y);
    void setScaleX(double sx);
    void setScaleY(double sy);
    void setOpacity(double opacity);
    void setBlendMode(const QString& mode);

    Q_INVOKABLE void resetTransforms();
    Q_INVOKABLE void refreshFromProject();

signals:
    void selectionChanged();
    void transformChanged();
};

class AiStudioService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool        isBusy                        READ isBusy                        NOTIFY isBusyChanged)
    Q_PROPERTY(bool        hasReference                  READ hasReference                  NOTIFY hasReferenceChanged)
    Q_PROPERTY(QVariant    structuralReferenceSimilarity READ structuralReferenceSimilarity NOTIFY structuralReferenceSimilarityChanged)
    Q_PROPERTY(QString     statusMessage                 READ statusMessage                 NOTIFY statusMessageChanged)
    Q_PROPERTY(QStringList proposedOperations            READ proposedOperations            NOTIFY proposalChanged)
    Q_PROPERTY(bool        hasValidProposal              READ hasValidProposal              NOTIFY proposalChanged)

public:
    explicit AiStudioService(ave::Project& project, QObject* parent = nullptr);
    ~AiStudioService() override;

    [[nodiscard]] bool        isBusy() const noexcept;
    [[nodiscard]] bool        hasReference() const noexcept;
    [[nodiscard]] QVariant    structuralReferenceSimilarity() const; // Null QVariant{} unless std::optional<double> valid
    [[nodiscard]] QString     statusMessage() const;
    [[nodiscard]] QStringList proposedOperations() const;            // Formatted EditProposalEnvelope::operations lines
    [[nodiscard]] bool        hasValidProposal() const noexcept;

    Q_INVOKABLE void analyzeReferenceMedia(const QString& filePath);
    Q_INVOKABLE void clearReference();
    Q_INVOKABLE void generateProposal(const QString& prompt);
    Q_INVOKABLE bool applyProposal();   // Executes ProposalBridge::commit() -> 1-step CompositeCommand
    Q_INVOKABLE void discardProposal();
    Q_INVOKABLE void shutdown();

signals:
    void isBusyChanged();
    void hasReferenceChanged();
    void structuralReferenceSimilarityChanged();
    void statusMessageChanged();
    void proposalChanged();
    void proposalApplied(); // Triggers PlaybackSnapshot republish & Timeline/Inspector refresh

private:
#ifdef AVE_BUILD_TESTS
    friend class AiStudioServiceTestAccessor;
#endif
};

} // namespace ave::desktop
```

### 3.3 Interface Details & Sub-Model Contracts
1. **`InspectorService` Listener & Project Lifetime Contract:**
   - `ave::Project` outlives `InspectorService`; both live on the GUI thread.
   - `InspectorService` connects to `TimelineModel::selectedClipIdChanged` (mirroring `selectedClipId`) and `TimelineModel::projectReloaded` in its constructor, and registers a guarded callback (`QPointer<InspectorService>`) on `project_.command_history_mut().add_change_listener(...)`. Every `SetTransformCommand` skips no-op updates (`std::abs(new_val - cur_val) < 1e-4`), preventing QML `Slider` feedback loops on undo/redo. Note: `InspectorViewModel` C++ methods (`rotation`, `anchor`) remain available in C++, while Phase 10 exposes the 10 properties bound by `InspectorView.qml`.
2. **`TimelineModel::tracksModel` and `clipsModel` Roles:**
   - `tracksModel` roles: `trackId`, `trackName`, `trackType` (`"video"` or `"audio"`), `trackIndex`, `muted`, `solo`. Video tracks and audio tracks remain cleanly filterable by `trackType` without needing separate `videoTrackCount` / `audioTrackCount` top-level properties.
   - `clipsModel` roles: `clipId`, `trackId`, `assetId`, `clipName`, `timelineInSec`, `durationSec`, `sourceInSec`, `trackRow`, `isSelected`, `hasWaveform`, `waveformPeaks` (`QVariantList` normalized `[0.0, 1.0]` extracted from `MediaCache` or empty when silent/unextracted).
3. **`AiStudioService::proposedOperations` Formatting Contract:**
   - Derived directly from `EditProposalEnvelope::operations` ([`ai/include/ave/edit_proposal.hpp`](../ai/include/ave/edit_proposal.hpp)): `[+|-|~] <target_track>: <parameters.clip_name> [<timeline_in_tc> -> <timeline_out_tc>] (<evidence.justified_by>)`. Example: `"+ V1: clip_01.mp4 [00:00:00:00 -> 00:00:02:12] (beat)"`.

---

## 4. Rational Time, Checked 128-Bit Arithmetic & Timecode Contract

Canonical headers: [`core/include/ave/time.hpp`](../core/include/ave/time.hpp) and [`core/include/ave/timecode.hpp`](../core/include/ave/timecode.hpp).

### 4.1 MSVC v143 Checked 128-Bit Integer Abstraction (No `__int128` Assumption)
MSVC `19.44` (`/std:c++20`) does **not** natively support GCC/Clang's `__int128` / `__uint128_t`. All Phase 10 checked rational math and `ui_seconds_to_rational_pts_safe()` must use a small header-internal helper in `core`:
- **Windows x64 (`_MSC_VER && _M_X64`):** `#include <intrin.h>` -> `_umul128(a, b, &hi)` for `uint64_t x uint64_t -> u128{lo, hi}` and `_udiv128(hi, lo, divisor, &rem)` for exact 128-by-64 unsigned division (pre-checking `hi < divisor` when quotient must fit in `uint64_t`, or performing two-step 64-bit long division `(hi / d) << 64 + _udiv128(hi % d, lo, d, &rem)` when `hi >= d`).
- **Clang/GCC (`__SIZEOF_INT128__`) / Portable Fallback:** Use native `__uint128_t` where defined or portable 32-bit limb arithmetic. Zero platform-non-portable bare `__int128` code is permitted.

### 4.2 `ave::Rational` Safety Fixes (`core/include/ave/time.hpp`)
1. **`simplify()` `INT64_MIN` Guard:** Before `-m_num` / `-m_den` and before `std::gcd(m_num, m_den)` (UB if either value is `INT64_MIN`), check `if (m_num == std::numeric_limits<int64_t>::min() || m_den == std::numeric_limits<int64_t>::min())` -> factor out common powers of 2 in `uint64_t` or throw `std::overflow_error` when unrepresentable as signed 64-bit canonical positive denominator.
2. **Checked Operators Across the Whole Class:**
   - `operator<`: compare exact signed 128-bit cross products `(i128(m_num) * other.m_den) < (i128(other.m_num) * m_den)` (zero `int64_t` overflow!).
   - `operator+`, `operator-`: compute common reduced denominator via `g = std::gcd(m_den, other.m_den)`, 128-bit numerator sum/difference, reduce by GCD if needed, and verify signed `int64_t` bounds before returning (throwing `std::overflow_error` if unrepresentable).
   - `operator*`, `operator/`, `operator*(scalar)`, `operator/(scalar)`: check intermediate cross-reduced `int64_t` products via `_mul128` against `[INT64_MIN + 1, INT64_MAX]`.
   - `to_frames(const Rational& frame_rate)`: throw `std::invalid_argument` if `frame_rate.numerator() <= 0 || frame_rate.denominator() <= 0`, compute exact 128-bit `(m_num * frame_rate.num) / (m_den * frame_rate.den)`, check `int64_t` range.

### 4.3 `ui_seconds_to_rational_pts_safe()` Exact Specification
**Max Timeline Duration Ceiling:** `240` hours = `864,000` seconds.

**Mathematical Frame-Aligned Maximum Formulation:**
For validated `fps = Rational(fps_num, fps_den)` with `fps_num > 0`, `fps_den > 0`:
$$\text{MAX\_FRAME\_INDEX} = \left\lfloor \frac{864000 \times \text{fps\_num}}{\text{fps\_den}} \right\rfloor$$
$$\text{MAX\_SAFE\_PTS} = \text{Rational}\!\left(\text{MAX\_FRAME\_INDEX} \times \text{fps\_den},\ \text{fps\_num}\right)$$

**Supported Frame-Rate Range & Boundary:** `fps_num > 0`, `fps_den > 0`, `1 <= MAX_FRAME_INDEX <= 9007199254740992` (`2^53`, guaranteeing exact IEEE-754 `double` integer representation), **and** exact 128-bit product `MAX_FRAME_INDEX * fps_den <= INT64_MAX`.

**Canonical Status Enum & Contract (Used Identically in All Code, Docs & Tests):**
```cpp
enum class TimeConversionStatus {
    Ok,                      // Finite seconds > 0 rounded to frame_index >= 1 and <= MAX_FRAME_INDEX
    Zero,                    // +0.0, -0.0, or positive < half-frame rounding cleanly to Rational{0, 1}
    InvalidFrameRate,        // fps_num <= 0, fps_den <= 0, or sub-1-frame-per-240h (MAX_FRAME_INDEX == 0)
    InvalidNaN,              // std::isnan(seconds)
    ClampedNegative,         // Finite seconds < 0.0 -> Rational{0, 1}
    ClampedNegativeInfinity, // -Inf -> Rational{0, 1}
    ClampedPositiveInfinity, // +Inf -> MAX_SAFE_PTS
    ClampedAboveMax,         // Finite seconds > 864000.0 or rounded > MAX_FRAME_INDEX -> MAX_SAFE_PTS
    Overflow                 // Unsupported extreme positive rate where MAX_FRAME_INDEX > 2^53 or product > INT64_MAX, or raw_frames overflow
};

struct TimeConversionResult {
    TimeConversionStatus status{TimeConversionStatus::Ok};
    Rational pts{0, 1};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return status == TimeConversionStatus::Ok || status == TimeConversionStatus::Zero;
    }
};
```

**Mandatory Execution Order (Zero Unchecked Float-to-Int Casts, Zero Post-Cast `std::clamp`):**
1. **Validate Frame Rate & Precompute `max_frame_index` / `MAX_SAFE_PTS`:**
   - If `fps_num <= 0 || fps_den <= 0` -> `{InvalidFrameRate, Rational{0, 1}}`.
   - Compute `u128 max_frame_u128 = mul_div_u128(864000ULL, static_cast<uint64_t>(fps_num), static_cast<uint64_t>(fps_den))` via `u128` helper.
   - If `max_frame_u128 == 0` -> `{InvalidFrameRate, Rational{0, 1}}`.
   - Check `max_frame_u128 <= u128(9007199254740992ULL)` (`2^53`) **and** `mul_u128(max_frame_u128, u128(fps_den)) <= u128(INT64_MAX)`. If either fails -> `{Overflow, Rational{0, 1}}`.
   - Set `int64_t max_frame_index` and `Rational max_safe_pts{max_frame_index * fps_den, fps_num}` (overflow-checked).
2. **Validate Floating Input `seconds`:**
   - If `std::isnan(seconds)` -> `{InvalidNaN, Rational{0, 1}}`.
   - If `std::isinf(seconds)`:
     - Negative (`seconds < 0.0`) -> `{ClampedNegativeInfinity, Rational{0, 1}}`.
     - Positive -> `{ClampedPositiveInfinity, max_safe_pts}`.
   - If `seconds < 0.0` (finite; IEEE `-0.0 < 0.0` is `false`) -> `{ClampedNegative, Rational{0, 1}}`.
   - If `seconds > 864000.0` -> `{ClampedAboveMax, max_safe_pts}`.
3. **Compute Exact Floating Frame Count & Safe Bounds Before Any Integer Cast:**
   - `double raw_frames = (seconds * static_cast<double>(fps_num)) / static_cast<double>(fps_den);`
   - If `!std::isfinite(raw_frames) || raw_frames < 0.0` -> `{Overflow, Rational{0, 1}}`.
   - Deterministic half-away-from-zero non-negative rounding without `fl + 0.5` ULP promotion bugs:
     `double fl = std::floor(raw_frames);`
     `double rounded = fl + ((raw_frames - fl) >= 0.5 ? 1.0 : 0.0);`
   - Strictly prove `rounded < 9223372036854775808.0` (`0x1p63`) before numeric comparisons or cast.
   - If `rounded > static_cast<double>(max_frame_index)` (for example `864000.0` at `30000/1001`, where `raw_frames ≈ 25894105.89` rounds to `25894106 > 25894105`) -> `{ClampedAboveMax, max_safe_pts}`.
   - Now `0.0 <= rounded <= static_cast<double>(max_frame_index) <= 2^53 < 0x1p63` is proven:
     `int64_t frame_index = static_cast<int64_t>(rounded);`
   - If `frame_index == 0` -> `{Zero, Rational{0, 1}}`.
   - Checked `int64_t` multiplication `pts_num = checked_mul_i64(frame_index, fps_den)` -> `{Ok, Rational(pts_num, fps_num)}`.

### 4.4 Verified Boundary Cases & SMPTE `Timecode` Fixes
- **24 FPS Half-Frame Boundaries (`fps = Rational{24, 1}`, half-frame = `1/48` s = `0.020833333333333332` s):**
  - `0.0` and `-0.0` -> `{Zero, Rational{0, 1}}`
  - `(1.0 / 48.0) - 1e-9` -> `{Zero, Rational{0, 1}}` (frame `0`)
  - `1.0 / 48.0` -> `(1.0 / 48.0) * 24.0 == 0.5` -> `{Ok, Rational{1, 24}}` (frame `1`)
  - `(1.0 / 48.0) + 1e-9` -> `{Ok, Rational{1, 24}}` (frame `1`)
- **`30000/1001` 240-Hour Boundary:** `MAX_FRAME_INDEX = 25,894,105`, `MAX_SAFE_PTS = Rational(5183999821, 6000)` (`≈ 863999.9701666667` s). Input `864000.0` yields `{ClampedAboveMax, MAX_SAFE_PTS}`; `+Inf` yields `{ClampedPositiveInfinity, MAX_SAFE_PTS}`.
- **`Timecode` ([`core/src/timecode.cpp`](../core/src/timecode.cpp)):**
  - `from_string(std::string_view)`: validate length (`== 11`) and copy into bounded stack/std::string buffer before parsing (never pass non-null-terminated `string_view::data()` directly to `std::sscanf`).
  - Phase 10 formats SMPTE **Non-Drop-Frame (`HH:MM:SS:FF`)** only (`from_rational()` always produces `drop_frame = false` using exact checked integer division; `to_rational()` / `total_frames()` validate `hours >= 0`, `0 <= minutes < 60`, `0 <= seconds < 60`, `0 <= frames < ceil(fps)` and avoid raw 64-bit multiplication overflow; Drop-Frame `;` notation is rejected/flagged consistently rather than silently applying uncompensated math).

---

## 5. Playback, Vulkan GPU Readback, Frame Presentation & Bounded Memory

### 5.1 End-to-End Connected Preview Pipeline

```text
PlaybackSnapshotPtr (Immutable Timeline + MediaAsset Copy)
   |
   +--> TimelinePrefetcher (Decode Worker Thread)
   |       +--> StreamDecoder (FFmpeg + AVIOInterruptCB) --> BoundedFrameQueue<VideoFrame>
   |
   v
PlaybackSession::update() (Playback/Render Worker Thread)
   |
   +--> RenderGraph::build_for_timeline(snapshot->timeline, cur_time, track_frames, ctx <= 1280x720)
   |
   +--> VulkanCompositor (VulkanDevice Intel Arc) -> composited output_target (VkImage, RGBA8_UNORM)
   |
   +--> VulkanReadbackRing (2-slot staging VkBuffer + VkCommandBuffer + VkFence, no vkDeviceWaitIdle)
   |
   v
VideoFramePacketPtr (std::shared_ptr<const VideoFramePacket>)
   |
   +--> FramePresentationHub (4-slot sequence-indexed ring, zero silent mismatch)
   |
   v
LiveFrameImageProvider ("aveframe", QQuickAsyncImageProvider)
   |
   +--> LiveFrameImageResponse (max 4 in-flight, mutex state + cancel -> finished() last)
   |
   v
VideoFrameTextureFactory (QQuickTextureFactory) -> QML ViewerView Image (cache: false, retainWhileLoading: true)
```

### 5.2 Immutable `VideoFramePacket` & SDR Pixel/Color Contract
```cpp
enum class AlphaRepresentation { Straight, Premultiplied };

struct VideoFramePacket {
    uint64_t sequence_id{0};
    uint64_t seek_generation{0};
    Rational pts{0, 1};
    int width{0};
    int height{0};
    int stride_bytes{0};
    RenderFormat pixel_format{RenderFormat::RGBA8_UNORM};
    AlphaRepresentation alpha_mode{AlphaRepresentation::Straight};
    ColorSpace color_space{ColorSpace::sRGB}; // Preview SDR target encoded as sRGB IEC 61966-2-1
    std::vector<uint8_t> pixels;
};
using VideoFramePacketPtr = std::shared_ptr<const VideoFramePacket>;
```
- **Pre-Construct Texture Validation:** Verify `width > 0`, `height > 0`, `width <= 1280`, `height <= 720`, `stride_bytes >= width * 4`, overflow-safe `checked_mul_size_t(static_cast<size_t>(stride_bytes), static_cast<size_t>(height)) <= pixels.size()`, and explicit mapping: `Straight -> QImage::Format_RGBA8888`, `Premultiplied -> QImage::Format_RGBA8888_Premultiplied`.
- **SDR Color / Transfer Policy (No Rec.709 vs sRGB Conflation):** Qt's `QImage` + Qt Quick scene graph display preview buffers in **sRGB (IEC 61966-2-1)**. Phase 10's compositing/readback preview target uses explicit sRGB display-referred 8-bit RGBA (`ColorSpace::sRGB`); source linear buffers (`LinearRec709`) apply linear-to-sRGB OETF conversion, and scene-referred BT.709 inputs apply explicit SDR transfer/normalization mapping during compositor output rather than blindly labeling Rec.709 OETF bytes as sRGB. Full OCIO/ACES color pipelines remain deferred post-Phase 10.
- **CPU Fallback Continuity:** When no Vulkan device is present (e.g., headless test or `--no-vulkan`), `PlaybackSession::update()` executes the CPU `RenderGraph` path at clamped preview bounds (`<= 1280x720`, default `960x540`) and wraps the result in `VideoFramePacketPtr` identically.

### 5.3 Double-Buffered `VulkanReadbackRing` Synchronization & Fault Handling
Phase 10 eliminates the per-frame staging allocation and blocking `end_single_time_commands()` (`vkQueueWaitIdle`) inside [`vulkan_texture.cpp`](../engines/render/src/vulkan_texture.cpp) on the live playback path via `VulkanReadbackRing`:
1. **Ring Structure (`kSlotCount = 2`):** Each slot `s ∈ {0, 1}` owns a persistently mapped `VkBuffer` (`VK_BUFFER_USAGE_TRANSFER_DST_BIT`), dedicated `VkDeviceMemory` (preferring `HOST_VISIBLE | HOST_COHERENT | HOST_CACHED`), primary `VkCommandBuffer` from a render-thread-owned `VkCommandPool`, `VkFence` (initially unsignaled, `in_flight = false`), `capacity_bytes`, and `bool host_coherent`.
2. **Thread Ownership:** Recorded, submitted, and waited **exclusively on the playback/render worker thread**. **Zero `VkFence` waits, `vkQueueWaitIdle`, or `vkDeviceWaitIdle` calls ever execute on the Qt GUI thread.**
3. **Command Recording, Barriers & Queue Submission for slot `idx`:**
   - If `slot.in_flight` -> `vkWaitForFences(dev, 1, &slot.fence, VK_TRUE, kFenceTimeoutNs)` on **only that slot's fence**. Never call `vkDeviceWaitIdle` during playback.
   - Call `vkResetFences(dev, 1, &slot.fence)` and `vkResetCommandBuffer(slot.cmd, 0)`.
   - Record barriers + copy into `slot.cmd`:
     - Pre-copy `VkImageMemoryBarrier` on `output_target.image()`: `srcStage = COMPUTE_SHADER_BIT` (`SHADER_WRITE_BIT`), `dstStage = TRANSFER_BIT` (`TRANSFER_READ_BIT`), `GENERAL -> TRANSFER_SRC_OPTIMAL`.
     - `vkCmdCopyImageToBuffer(slot.cmd, output_target.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.buffer, 1, &region)`.
     - Post-copy `VkImageMemoryBarrier` (`TRANSFER_SRC_OPTIMAL -> GENERAL`, `TRANSFER_READ_BIT -> SHADER_READ_BIT | SHADER_WRITE_BIT`) **plus** `VkMemoryBarrier` (`srcStage = TRANSFER_BIT`, `srcAccess = TRANSFER_WRITE_BIT`, `dstStage = HOST_BIT`, `dstAccess = HOST_READ_BIT`).
   - Submit via `vkQueueSubmit(device.queue(), 1, &submit_info, slot.fence)`, set `slot.in_flight = true`.
4. **Fence Wait & Non-Coherent Invalidation Before Host Copy:**
   - Before reading `slot.mapped_ptr` into a `VideoFramePacket`, wait on `slot.fence` on the playback worker (`vkWaitForFences`), mark `slot.in_flight = false`. (During steady playback, slot `(write_idx + 1) % 2` is harvested/drained while the next compute/copy overlaps on the opposite slot; on single-frame scrub/step, harvest proceeds on the submitted slot's own fence so the frame presents immediately.)
   - **If `!slot.host_coherent`**, call `vkInvalidateMappedMemoryRanges(dev, 1, &mapped_range)` (offset/size aligned to `physicalDeviceLimits.nonCoherentAtomSize`) **strictly before** `std::memcpy` into `VideoFramePacket::pixels`.
5. **`VK_ERROR_DEVICE_LOST` / Submission Error Handling & Teardown:**
   - If `vkQueueSubmit` or `vkWaitForFences` returns `VK_ERROR_DEVICE_LOST`, `VK_TIMEOUT`, or another fatal error: transition `faulted_ = true`, abandon/skip packet publication for that frame, emit diagnostic status for UI badge fallback without crashing, and fall back to safe hold / CPU path cleanly.
   - Destructor waits (with bounded error-safe handling ignoring `VK_ERROR_DEVICE_LOST`) for all `in_flight` fences, unmaps memory, destroys staging buffers, frees command buffers/pool, and destroys fences **before** `VulkanDevice` is destroyed.

### 5.4 `FramePresentationHub` & Async QML Image Provider (`LiveFrameImageProvider`)
- **Ring Capacity:** `4` slots indexed by monotonically increasing `sequence_id >= 1`.
- **Zero Silent Mismatch:** `lookup(requested_seq)` returns the `VideoFramePacketPtr` matching `requested_seq` **only if still in the 4-slot ring**. If `requested_seq` was evicted or invalid, `lookup()` returns `nullptr`, and `LiveFrameImageResponse` resolves in `Error`/`Cancelled` state (returning `nullptr` from `textureFactory()` and descriptive `errorString()`). Never substitute a different frame under a stale sequence ID.
- **Qt 6.8 Ownership & Signal Invariant:**
  - `QQmlApplicationEngine::addImageProvider(QStringLiteral("aveframe"), provider)` transfers ownership of `LiveFrameImageProvider` to the engine. Provider holds `std::shared_ptr<FramePresentationHub>` so the hub outlives QML engine teardown cleanly.
  - `requestImageResponse()` limits concurrent queued/active responses to `max_in_flight = 4`; excess older unstarted queued requests are cancelled/coalesced before dispatch.
  - `LiveFrameImageResponse` (`QQuickImageResponse`): mutex guards `{state, packet_, error_string_}`. Calling `cancel()` marks `Cancelled` and ensures **`emit finished()` still fires** (per Qt 6 docs so Qt destroys the response). **`emit finished()` runs strictly as the final action**; workers never touch `LiveFrameImageResponse*` afterward.
  - `VideoFrameTextureFactory` (`QQuickTextureFactory`) owns `VideoFramePacketPtr` by value; `textureSize()` is lock-free/GPU-context-free; `createTexture(QQuickWindow*)` creates `QImage` using validated dimensions/stride/format and uploads via `window->createTextureFromImage(...)` on Qt's scene-graph render thread.
- **QML Viewer Binding ([`apps/desktop/qml/ViewerView.qml`](../apps/desktop/qml/ViewerView.qml)):**
  ```qml
  Image {
      asynchronous: true
      cache: false
      retainWhileLoading: true // Qt 6.8: keeps previous SG texture until async replacement finishes
      fillMode: Image.PreserveAspectFit
      source: playbackService.currentFrameSeq > 0
              ? ("image://aveframe/" + playbackService.currentFrameSeq)
              : ""
  }
  ```

### 5.5 Total Bounded Resident Preview Memory Budget (`1280x720` Cap vs `960x540` Default)
The `14.75 MB` figure refers **only** to the 4-slot `FramePresentationHub` ring at `1280x720x4` (`3,686,400 B/frame`). Complete worst-case end-to-end resident memory across the whole preview chain is explicitly bounded:

| Pipeline Stage | Allocation Count | `960x540` Default | `1280x720` Max Cap |
|---|---:|---:|---:|
| GPU Output Target (`VulkanTexture`) | 1 | `2.07 MB` | `3.69 MB` |
| GPU Staging Readback (`VulkanReadbackRing`) | 2 | `4.15 MB` | `7.37 MB` |
| `FramePresentationHub` Ring Cache (Hub portion) | 4 | `8.29 MB` | `14.75 MB` |
| In-Flight `LiveFrameImageResponse` / `TextureFactory` | <= 4 (worst-case unshared) | `<= 8.29 MB` | `<= 14.75 MB` |
| QML `Image` (`retainWhileLoading: true`) SG Textures | 1 active + 1 retiring | `<= 4.15 MB` | `<= 7.37 MB` |
| **Bounded Total Resident Preview Footprint** | **13 max live frame instances** | **`~27.0 MB` worst-case** | **`~47.9 MB` worst-case** |

No frame growth beyond this bound occurs regardless of playback duration or scrub rate (`cache: false` prevents `QPixmapCache` accumulation; `BoundedFrameQueue` caps prefetch decode buffers separately at `12` frames).

---

## 6. Thread Ownership, Service Lifetimes & `PlaybackSnapshot` (G-08 Fix)

### 6.1 Explicit Thread-Ownership Matrix
| Thread / Pool | Exclusive Owners / Objects | Allowed Memory Access | Forbidden Operations |
|---|---|---|---|
| **Qt GUI Thread** | `QQmlApplicationEngine`, `ave::Project` (`Timeline`, `MediaPool`, `CommandHistory`), all 5 `QObject` services | Mutates `Project` via `CommandHistory`; constructs/publishes `PlaybackSnapshotPtr` | Blocking `vkWaitForFences`, `vkDeviceWaitIdle`, FFmpeg probing/decoding, blocking worker joins during frame ticks |
| **Playback / Render Worker** (`PlaybackSession` + `TimelinePrefetcher` + `VulkanCompositor` + `VulkanReadbackRing`) | `StreamDecoder` pool, `VulkanCompositor`, `VulkanReadbackRing` | Reads **only** `PlaybackSnapshotPtr` (`version`, copied `Timeline` + `MediaAsset` map) and locks SPSC/queue buffers | **Zero** direct reads of mutable `Timeline&`, `MediaPool&`, or GUI `QObject`s; never calls `vkDeviceWaitIdle` |
| **Audio Device Callback** (`NullAudioDevice` in Ph10) | `AudioRingBuffer` SPSC consumer | Lock-free read of float PCM frames from `AudioRingBuffer` | Allocations, mutexes, FFmpeg I/O, QML calls |
| **Media Import Worker Pool** (`MediaImportQueue`, `max_active = 2`) | Local `FFmpegProbe`, `ThumbnailExtractor`, `WaveformExtractor`, `AVIOInterruptCB` | Reads immutable path + per-job `std::shared_ptr<std::atomic<bool>> cancel_state` | Touching `MediaPool`, `Timeline`, or `MediaPoolModel` directly; uses `QueuedConnection` + `QPointer` + `alive_flag` |
| **AI Worker Thread** (`AiStudioService`, `max_active = 1`) | Cloned `Timeline`, `MediaPool`, `ReferenceFingerprint` snapshot, `PromptParser`, `DeterministicPlanner`, `ProposalBridge::validate/dry_run` | Reads cloned snapshot inputs only; checks request ID & cancel token | Mutating live `Project`; all commits happen synchronously via `ProposalBridge::commit` on GUI thread |

### 6.2 `PlaybackSnapshot` Design Solving Concurrent Timeline & MediaPool Access (G-08)
Current [`engines/playback/src/timeline_prefetcher.cpp`](../engines/playback/src/timeline_prefetcher.cpp) and [`engines/playback/src/playback_session.cpp`](../engines/playback/src/playback_session.cpp) hold raw references `const Timeline&` and `const MediaPool&` directly read on background decode threads while edits mutate `Timeline` / `MediaPool`.

**Phase 10 Fix:**
```cpp
struct PlaybackSnapshot {
    uint64_t version{0};
    ave::Timeline timeline;
    std::unordered_map<std::string, ave::MediaAsset> assets_by_id;
};
using PlaybackSnapshotPtr = std::shared_ptr<const PlaybackSnapshot>;

class PlaybackSnapshotStore {
public:
    void publish(const ave::Timeline& tl, const ave::MediaPool& pool); // Called on GUI thread after CommandHistory/import/project load
    [[nodiscard]] PlaybackSnapshotPtr load() const;
private:
    mutable std::mutex mutex_;
    uint64_t next_version_{1};
    PlaybackSnapshotPtr current_;
};
```
1. **Atomic Snapshot Swap:** Whenever `CommandHistory`, `MediaPoolModel`, or `TimelineModel::openProject()` commits a change on the GUI thread, `PlaybackSnapshotStore::publish(...)` clones `Timeline` and the `MediaAsset` map into a new `PlaybackSnapshotPtr` with incremented `version` and signals `PlaybackSession` (`on_snapshot_updated()`), bumping seek/refresh generation and waking prefetch if needed.
2. **Worker Isolation:** `TimelinePrefetcher::worker_loop()`, `PlaybackSession::update()`, and `PlaybackSession::pump_audio()` call `snapshot_store_->load()` and hold that `shared_ptr<const PlaybackSnapshot>` for the duration of one evaluation/decode unit — completely eliminating iterator invalidation or torn reads even during continuous live dragging, splitting, deleting, or relinking.
3. **Structural Reset (`newProject` / `openProject`):** Stops/pauses `PlaybackSession` (`prefetcher_.stop()`, clears `video_queue_` & audio decoders) before replacing `Project`, publishes the new `PlaybackSnapshot`, then resumes cleanly.

### 6.3 Mandatory Construction & Reverse Shutdown Order
- **Construction Order (Main / Composition Root):**
  1. `ave::Project`
  2. `std::shared_ptr<PlaybackSnapshotStore>` (seeded with initial empty `Project` snapshot)
  3. `std::shared_ptr<ave::VulkanDevice>` (nullable; CPU fallback if unavailable)
  4. `ave::PlaybackSession` (holds `snapshot_store`, `VulkanDevice`, owns `VulkanReadbackRing` on render worker)
  5. `std::shared_ptr<FramePresentationHub>`
  6. `std::shared_ptr<MediaImportQueue>`
  7. Five `QObject` services (`MediaPoolModel`, `TimelineModel`, `InspectorService`, `PlaybackService`, `AiStudioService`)
  8. `QQmlApplicationEngine` (`addImageProvider("aveframe", new LiveFrameImageProvider(hub))` -> set context properties -> `load(...)`)
- **Mandatory Reverse Shutdown Order (`app.aboutToQuit` / Scope Teardown before stack unwinding):**
  1. Destroy `QQmlApplicationEngine` (drops QML bindings + deletes `LiveFrameImageProvider`).
  2. Call `PlaybackService::shutdown()` -> `PlaybackSession::stop()` (joins `TimelinePrefetcher`, drains/waits `VulkanReadbackRing` fences cleanly, stops `NullAudioDevice`).
  3. Call `MediaPoolModel::shutdown()` -> `MediaImportQueue::shutdown()` (sets all `cancel_state` flags, disables enqueueing, joins `<= 2` active import workers cleanly).
  4. Call `AiStudioService::shutdown()` (signals cancel flag, joins AI worker thread cleanly).
  5. Destroy the five `QObject` services (`InspectorService` unregisters `CommandHistory` listener prior to `Project` teardown).
  6. Destroy `PlaybackSession` (`VulkanReadbackRing` / `VulkanCompositor` before `VulkanDevice`), then `PlaybackSnapshotStore`, then `ave::Project`.

---

## 7. Asynchronous Media Import Queue & Cooperative FFmpeg Cancellation

- **Bounded Admission Capacity:** `max_active = 2`, `max_pending = 16`, **total accepted capacity = `18`**. Submissions at `active + pending == 18` fail immediately on the caller thread without blocking and populate `lastImportError`.
- **`isImporting` Definition:** `isImporting == (activeImportCount + pendingImportCount > 0)`.
- **Single-Terminal-Transition Job State Machine:** `Queued -> Running -> { Succeeded | Failed | Cancelled }`. Mutex CAS prevents race conditions between worker commit and `cancelImport(jobId)` / `cancelAllImports()`. If cancellation wins first, extracted metadata/thumbnails/waveforms are discarded and never enter `MediaPoolModel`.
- **Cooperative `AVIOInterruptCB` Across FFmpeg Sites:** [`ffmpeg_probe.cpp`](../media/src/ffmpeg_probe.cpp), [`media_extractor.cpp`](../media/src/media_extractor.cpp), [`stream_decoder.cpp`](../engines/playback/src/stream_decoder.cpp), and [`stream_audio_decoder.cpp`](../engines/playback/src/stream_audio_decoder.cpp) pre-allocate `AVFormatContext* fmt_ctx = avformat_alloc_context()`, wire `fmt_ctx->interrupt_callback` to a `std::shared_ptr<std::atomic<bool>> cancel_state` (`avformat_open_input` + `avio_open2` where custom AVIO exists), free on failed open (`avformat_free_context`), and keep `cancel_state` alive past `avformat_close_input(&fmt_ctx)`. No unmeasured `< 500 ms` termination bound is guaranteed against slow I/O.
- **Model Destruction Guard:** Workers post results via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` guarded by **both** `QPointer<MediaPoolModel>` and a queue-owned `std::shared_ptr<std::atomic<bool>> model_alive`; `MediaPoolModel::~MediaPoolModel()` flips `model_alive = false` and calls `shutdown()` synchronously before member destruction.

---

## 8. AI Studio, Honest Capability Scope & Transaction Safety

1. **Honest Phase 10 AI Capability Boundary:**
   - Phase 10 wires `AiStudioService` to the **existing in-process C++ Phase 7/8 components**: `ave::LanguageSidecar` (currently `m_mock_mode = true`, zero external LLM daemon/weights), deterministic `ave::PromptParser` regex/keyword constraints, `ave::DeterministicPlanner` beat-grid DP solver, `ave::FingerprintEngine`, `ave::ProposalBridge`, and `ave::ProposalInspector`.
   - Zero trained neural weights (`.pt`/`.onnx`) or LLM token generation run inside `ave_desktop.exe` in Phase 10. Status strings cleanly state `"Proposal generated via Deterministic Planner (ADR-0018)"`; `structuralReferenceSimilarity` is never labeled `"calibrated"`.
2. **`FingerprintEngine` Non-Finite Guards & `std::optional<double>` Contract:**
   - [`FingerprintEngine::compute_distance_detailed()`](../ai/src/reference_fingerprint.cpp#L215-L266) validates all numeric fields across both `ReferenceFingerprint` inputs (and weights) via `std::isfinite(...)` **before** `(std::min)`/`(std::max)` (avoiding silent NaN absorption where `std::min(1.0, NaN) == 1.0`), returning invalid status when non-finite.
   - Internally `AiStudioService` holds `std::optional<double> similarity`:
     - Missing reference (`hasReference == false`) OR non-finite feature error -> `std::nullopt` internally -> invalid/null `QVariant{}` in QML (badge renders `"No Reference"` / `"N/A"`, never `"-1.0"` or hardcoded `"95.2% Match"`).
     - Valid finite inputs -> `std::clamp(1.0 - distance, 0.0, 1.0)` finite double in `[0.0, 1.0]`.
3. **Strict Test Seam Isolation:** Production public API of `AiStudioService` contains **zero** public fake-score or fake-proposal setters. Unit tests inspect/seed states solely via `friend class AiStudioServiceTestAccessor;` guarded under `#ifdef AVE_BUILD_TESTS`.
4. **Atomic Rollback & State-Aware Async Waiting:** `applyProposal()` commits through `ProposalBridge::commit(...)` (`ave::CompositeCommand`) yielding 1-step Undo/Redo and complete reverse rollback on child failure. Tests use state-aware wait helpers checking terminal state prior to `QSignalSpy::wait(...)` and test against populated fixture `Timeline` + `MediaPool` assets.

---

## 9. QML Integration & Mock Removal Policy

All 8 files in `apps/desktop/qml/` remove critical-path hardcoded demo state and `Math.random()` generators:
- `MediaPoolView.qml`: Bind `ListView.model` to `mediaPoolModel`, replace `"4 Assets"` with `mediaPoolModel.assetCount`, wire `QtQuick.Dialogs` `FileDialog` -> `mediaPoolModel.importMediaFiles(selectedFiles)`, display `isImporting` / progress counts / `lastImportError` + cancel control.
- `ViewerView.qml`: Replace static gradient + fake `"01:00:14:18"` + disconnected transport buttons with `LiveFrameImageProvider` `Image` (`asynchronous: true`, `cache: false`, `retainWhileLoading: true`), `playbackService` transport actions, `gpuDeviceName`, `timecode`, and `playheadPosition` scrub slider.
- `TimelineCanvas.qml` & `TrackHeaderView.qml`: Bind track rows and clip items to `timelineModel.tracksModel` and `timelineModel.clipsModel`, replace `Math.random()` waveforms with real extracted waveform envelopes (or honest empty/no-audio state when unavailable), route click-to-select, blade split (`Tool::Razor = 1`), clip move, playhead scrub, and mute toggles through `timelineModel` / `playbackService`.
- `AudioMeterView.qml`: Remove `Timer` and `Math.random()`; bind directly to `playbackService.leftLevelDb` and `playbackService.rightLevelDb` (`[-60.0, 0.0]` dBFS).
- `ToolbarView.qml`: Replace static hardware string and timecode with `playbackService`, `timelineModel` (`activeTool` `0..3`, marking unsupported ripple-edit mode disabled/mapped cleanly to `Tool::Trim = 2`, `snappingEnabled`, `canUndo`, `canRedo`, `isDirty`), and `File -> New / Open / Save / Export OTIO / Relink Offline Media` native dialogs.
- `InspectorView.qml`: Bind Tab 0 (`Properties`) to `inspectorService` (`SetTransformCommand`) and Tab 1 (`AI Assistant`) to `aiStudioService`, removing `"95.2% Match"` and static `SceneA..D` strings.

---

## 10. Phase 11 QRhi / Vulkan Interop Boundaries (Deferred)

Zero-copy `QQuickRhiItem` GPU viewer presentation is explicitly deferred beyond Phase 10, governed by verified **Qt 6.8 documentation**:
1. `QRhi::nativeHandles()` (`<rhi/qrhi.h>`, `Qt6::GuiPrivate`) returns **borrowed** Qt-owned Vulkan handles (`inst`, `physDev`, `dev`, `gfxQueue`, `gfxQueueFamilyIdx`) that the application **must never destroy**.
2. `QRhiCommandBuffer::nativeHandles()` returns borrowed `VkCommandBuffer` valid **only** inside `beginExternal()` ... `endExternal()` of a single frame; **never cache across frames**.
3. `QRhiCommandBuffer::beginPass(..., QRhiCommandBuffer::ExternalContent)` wraps graphics passes; `vkCmdDispatch` must never be recorded inside a graphics render pass.
4. **Qt 6.8 Compute Pass Caveat:** Qt 6.8 documentation explicitly notes `QRhiCommandBuffer::beginComputePass(resourceUpdates, flags)` currently ignores `flags` (*"flags is currently not used"*). Consequently, Phase 11 zero-copy compute interop requires a dedicated Proof-of-Concept gate verifying Vulkan validation layers, `VkImageLayout` restoration, queue family compatibility on Intel Arc iGPUs, and empirical benchmarks before adoption.

---

## 11. Reconciled 30-Entry CTest Plan & Entry-Point Rules

**Reconciled CTest Total:** **`24` existing repository tests + `6` Phase 10 CTest targets = `30` CTest entries.**  
*(Supersedes any outdated note referring to a single Suite #25.)*

### 11.1 Single Entry-Point Policy (Zero Linker Collisions)
CMake test targets **never** mix `GTest::gtest_main` with `QTEST_MAIN` in the same executable (both define `main()` and fail MSVC link). Every Phase 10 target follows one of three clean patterns:
1. **Pure C++ GoogleTest (`GTest::gtest_main`):** No Qt event loop required.
2. **Qt-Aware GoogleTest (`GTest::gtest` + explicit single `int main(int argc, char** argv)`):** Instantiates `QCoreApplication` (or `QGuiApplication`), initializes `::testing::InitGoogleTest(&argc, argv)`, runs tests.
3. **Pure `QTest` (`QTEST_MAIN`):** Only `Qt6::Test` macros (`QCOMPARE`/`QVERIFY`/`QTest`), zero `GTest` main library.

### 11.2 Six Planned Phase 10 CTest Targets (`#25` – `#30`)

| CTest # | CTest Entry Name | Target Binary | Entry Point / Framework | Verified Scope |
|---:|---|---|---|---|
| **25** | `Phase10TimeTest` | `ave_phase10_time_test` | `GTest::gtest_main` (pure C++20) | All `TimeConversionStatus` paths (`Ok`, `Zero`, `InvalidFrameRate`, `InvalidNaN`, `ClampedNegative`, `ClampedNegativeInfinity`, `ClampedPositiveInfinity`, `ClampedAboveMax`, `Overflow`), MSVC `_umul128`/`_udiv128`, 24 FPS `1/48` s `+-1e-9` half-frame boundaries, `30000/1001` 240h clamp, `Rational` `INT64_MIN` & operator overflow guards, `Timecode::from_string` non-null `string_view` & NDF limits |
| **26** | `Phase10ImportTest` | `ave_phase10_import_test` | `GTest::gtest` + `main()` (`QCoreApplication`) | Bounded `2/16/18` queue admission & rejection, `isImporting` semantics, cancel-vs-commit single-terminal CAS race, `AVIOInterruptCB` cooperative abort & context teardown against valid fixture via `AVE_TEST_FIXTURES_DIR`, `MediaPoolModel` safe destruction mid-import |
| **27** | `Phase10FrameProviderTest` | `ave_phase10_frame_provider_test` | `GTest::gtest` + `main()` (`QGuiApplication` offscreen) | `VideoFramePacket` overflow-safe stride/alpha validation, `FramePresentationHub` 4-slot eviction & stale-seq `nullptr` zero-mismatch, `LiveFrameImageResponse` concurrent requests + `cancel()` always emitting `finished()` last, `VideoFrameTextureFactory::textureSize()`, `VulkanReadbackRing` 2-slot fence/non-coherent invalidate & `VK_ERROR_DEVICE_LOST` fallback |
| **28** | `Phase10ServicesPropertyTest` | `ave_phase10_services_property_test` | `GTest::gtest` + `main()` (`QCoreApplication`) | Qt `QMetaObject` reflection check of all **46 `Q_PROPERTY` declarations** (`12+5+13+10+6`), 0-arg `NOTIFY` signatures, `assetCount()` non-`rowCount`, read-only `playheadPosition` + `seekSeconds` clamp, `Tool` enum `0..3`, `gpuDeviceName` vs `audioOutputStatus`, `InspectorService` transform sync + `CommandHistory` undo loop safety, `.aveproj` save/open/relink, and `PlaybackSnapshot` concurrent edit + prefetch race safety (G-08) |
| **29** | `Phase10AiStudioTest` | `ave_phase10_ai_studio_test` | `GTest::gtest` + `main()` (`QCoreApplication`) | `FingerprintEngine` `NaN`/`±Inf` pre-`min`/`max` guards, `std::optional<double>` / null `QVariant` missing/invalid similarity contract, `AiStudioServiceTestAccessor` friend seam isolation (`AVE_BUILD_TESTS`), state-aware bounded async signal waits on populated fixtures, `CompositeCommand` partial-failure reverse rollback |
| **30** | `Phase10QmlOffscreenSmoke` | `ave_phase10_qml_offscreen_smoke` | `QTEST_MAIN` (`Qt6::Test` + `Qt6::Quick`) | Instantiates all 5 services + `LiveFrameImageProvider("aveframe")`, loads real [`apps/desktop/qml/main.qml`](../apps/desktop/qml/main.qml) under `QT_QPA_PLATFORM=offscreen` + software Quick fallback, captures `qInstallMessageHandler` failing on any QML error/warning/ReferenceError/image failure, exercises startup & reverse shutdown cleanly |

**Offscreen Validation Honesty Boundary:** CTest `#30` (`QT_QPA_PLATFORM=offscreen`) verifies QML compilation, context/property resolution, signal binding, image-provider registration, and clean teardown without requiring a desktop display. It does **not** test live D3D11/Vulkan swapchain frame presentation, OS native `FileDialog` popups, or physical mouse drag UX, which remain covered exclusively by the 16-Step Manual Native GUI Gate.

---

## 12. Phase 10 Implementation Sequence (Explicitly Covering G-01..G-09)

*(Implementation begins **only** after Qt 6.8.3 (`msvc2022_64`) + `vcpkg` manifest install are complete and the user explicitly authorizes implementation.)*

| Task | Covers Audit Gaps | Concrete Engineering Deliverables & Verification Gate |
|---|---|---|
| **10.1** | **G-01** | Require `Qt6 6.8.3` (`Core Gui Quick Qml QuickControls2 Test`) in [`CMakeLists.txt`](../CMakeLists.txt) and [`apps/desktop/CMakeLists.txt`](../apps/desktop/CMakeLists.txt); remove headless fallback from `ave_desktop`; enable `CMAKE_AUTOMOC` + QML resources; rewrite [`apps/desktop/main.cpp`](../apps/desktop/main.cpp) with ordered setup/reverse teardown & `engine.load()`; update [`run_editor.bat`](../run_editor.bat) to launch `ave_desktop.exe` (retire PySide6 `launch_gui.py`). |
| **10.2** | **G-02** | Upgrade [`core/include/ave/time.hpp`](../core/include/ave/time.hpp) (`_umul128`/`_udiv128` MSVC helper, `INT64_MIN` `simplify()` fix, checked arithmetic, canonical `TimeConversionStatus` + `ui_seconds_to_rational_pts_safe()`), harden [`core/src/timecode.cpp`](../core/src/timecode.cpp), and pass **`Phase10TimeTest` (`#25`)**. |
| **10.3** | **G-03** | Add shared `AVIOInterruptCB` cooperative cancellation across [`ffmpeg_probe.cpp`](../media/src/ffmpeg_probe.cpp), [`media_extractor.cpp`](../media/src/media_extractor.cpp), [`stream_decoder.cpp`](../engines/playback/src/stream_decoder.cpp), [`stream_audio_decoder.cpp`](../engines/playback/src/stream_audio_decoder.cpp); implement `MediaImportQueue` (`2/16/18`, CAS state machine) and `MediaPoolModel` (`5` properties, `assetCount()`, `QPointer` + `alive_flag` + `shutdown()`); pass **`Phase10ImportTest` (`#26`)**. |
| **10.4** | **G-08** | Introduce `PlaybackSnapshot` & `PlaybackSnapshotStore` in `engines/playback`, refactor `TimelinePrefetcher` and `PlaybackSession` (`update()` + `pump_audio()`) to consume `PlaybackSnapshotPtr` only, wire GUI-thread edit commits to atomic snapshot republish without locking background threads onto live mutable `Timeline`/`MediaPool`. |
| **10.5** | **G-04** | Implement `VulkanReadbackRing` (2-slot staging buffers, per-slot `VkFence`, layout/host barriers, non-coherent `vkInvalidateMappedMemoryRanges`, `VK_ERROR_DEVICE_LOST` fallback), wire `PlaybackSession::update()` to invoke `VulkanCompositor` + `VulkanReadbackRing` (<= `1280x720`, `960x540` default, SDR sRGB preview rule, CPU fallback preserved), implement `FramePresentationHub` (4 slots), `LiveFrameImageProvider`/`LiveFrameImageResponse`/`VideoFrameTextureFactory`, and `PlaybackService` (`12` properties); pass **`Phase10FrameProviderTest` (`#27`)**. |
| **10.6** | **G-05, G-06** | Implement `TimelineModel` (`13` properties, `tracksModel`, `clipsModel`, `Tool` enum `0..3`, `.aveproj` new/open/save, OTIO export, `AssetRelinker` workflow) and `InspectorService` (`10` properties, `Project&` + `CommandHistory` listener lifetime, `SetTransformCommand` suppression of slider loops). |
| **10.7** | **G-07** | Add `std::isfinite` pre-`min`/`max` guards in [`FingerprintEngine::compute_distance_detailed()`](../ai/src/reference_fingerprint.cpp); implement `AiStudioService` (`6` properties, `std::optional<double>` / null `QVariant`, cloned snapshot worker, `AiStudioServiceTestAccessor` friend seam under `AVE_BUILD_TESTS`, `ProposalBridge::commit` 1-step `CompositeCommand` undo/rollback); pass **`Phase10ServicesPropertyTest` (`#28`)** & **`Phase10AiStudioTest` (`#29`)**. |
| **10.8** | **G-01..G-07** | Connect all 8 QML views in `apps/desktop/qml/` (`main.qml`, `ToolbarView.qml`, `MediaPoolView.qml`, `ViewerView.qml`, `TimelineCanvas.qml`, `TrackHeaderView.qml`, `AudioMeterView.qml`, `InspectorView.qml`) removing all critical-path literals and `Math.random()` timers; integrate native `QtQuick.Dialogs` `FileDialog` and real `WaveformExtractor` envelopes. |
| **10.9** | **G-09** | Implement **`Phase10QmlOffscreenSmoke` (`#30`)** with `AVE_TEST_FIXTURES_DIR` & `QtMsgHandler` capture (`0` QML errors); update [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) to use LGPL shared FFmpeg + Qt 6.8.3 + `vcpkg`; verify **`30/30` CTest pass** on `x64-debug` / `x64-release`. |
| **10.10** | **Manual Gate** | Run the **16-Step Manual Native GUI Acceptance Gate** on live Windows 11 `ave_desktop.exe` (Vulkan validation enabled for Debug check, confirming 0 crash / 0 Vulkan validation errors across full edit-save-reopen cycle). |

### 12.1 16-Step Manual Native GUI Acceptance Gate (Live Windows Desktop)
1. Launch native `ave_desktop.exe` (verify zero Python process spawned and no fallback stub banner).
2. Click Import -> select real fixture/video via native Windows `FileDialog`.
3. Confirm asset metadata, duration, codec, and thumbnail appear in `MediaPoolView` and `assetCount` increments.
4. Drag/insert clip onto timeline track (`V1`) in `TimelineCanvas`.
5. Verify clip box, duration, audio waveform (or explicit no-audio state), and timeline duration update via `TimelineModel`.
6. Toggle Play / Pause in `ViewerView`.
7. Confirm live composited decoded video frames render continuously inside `ViewerView` (`LiveFrameImageProvider`) and `AudioMeterView` reflects real audio telemetry (never `Math.random()`).
8. Scrub timeline ruler & `ViewerView` slider / step `±1` frame; verify responsive frame updates (`currentFrameSeq`) and non-drop SMPTE `HH:MM:SS:FF` sync.
9. Switch Inspector to Properties tab (`0`), edit `posX`/`scaleX`/`opacity`, verify live preview change and undo (`Ctrl+Z`).
10. Switch Inspector to AI Assistant tab (`1`), confirm missing reference shows `"No Plan"` / `"No Reference"` (never `"95.2% Match"`), enter natural language prompt -> click `Generate AI Edit Proposal`.
11. Review dynamically generated `proposedOperations` lines (zero `SceneA..D` strings) derived from `EditProposalEnvelope`.
12. Click `Apply (1-Step Undo)` -> verify clips commit atomically onto timeline and preview updates.
13. Press Single-Step Undo (`Ctrl+Z` / toolbar Undo) -> confirm **all** AI operations revert atomically in one step.
14. Click Save -> write `.aveproj` to disk via native `FileDialog`.
15. Restart `ave_desktop.exe` -> Open saved `.aveproj`.
16. Verify full state restoration and test `Relink Offline Media` after temporarily moving a source file to confirm `AssetRelinker` recovery cleanly restores playback.
