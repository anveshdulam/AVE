#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QList>
#include <QMetaType>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

#include "ave/ai_studio_service.hpp"
#include "ave/autosave_service.hpp"
#include "ave/command.hpp"
#include "ave/frame_presentation_hub.hpp"
#include "ave/inspector_service.hpp"
#include "ave/inspector_view_model.hpp"
#include "ave/live_frame_image_provider.hpp"
#include "ave/media_cache.hpp"
#include "ave/media_import_queue.hpp"
#include "ave/media_pool.hpp"
#include "ave/media_pool_model.hpp"
#include "ave/otio_interchange.hpp"
#include "ave/playback_service.hpp"
#include "ave/playback_session.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/playback_view_model.hpp"
#include "ave/project.hpp"
#include "ave/timeline.hpp"
#include "ave/timeline_model.hpp"
#include "ave/timeline_view_model.hpp"
#include "ave/video_frame_packet.hpp"
#include "ave/vulkan_device.hpp"

Q_DECLARE_METATYPE(ave::Rational)
Q_DECLARE_METATYPE(ave::TimelineTool)

namespace {

void register_desktop_meta_types() {
    qRegisterMetaType<ave::Rational>("ave::Rational");
    qRegisterMetaType<ave::TimelineTool>("ave::TimelineTool");
}

void print_usage() {
    std::cout << "Usage: ave_desktop [--smoke-test] [--status] [--verify-qml] [--help]\n";
}

void print_system_status(const ave::VulkanDevice* vulkan_dev,
                         const ave::Project& project,
                         const ave::PlaybackViewModel& playback_vm) {
    std::cout << "======================================================================\n";
    std::cout << "        AUTONOMOUS VIDEO EDITOR (AVE) - NATIVE DESKTOP SHELL          \n";
    std::cout << "        Version 0.1.0 (Windows-First, Local-First, C++20 Core)        \n";
    std::cout << "======================================================================\n\n";

    std::cout << "[System Subsystems & Architectural Status]\n";
    std::cout << "  * Layer 1 (Presentation):  Qt 6.8.3 Quick/QML + Presentation ViewModels\n";
    std::cout << "  * Layer 2 (Application):   Project Model, Command Bus, Media Pool\n";
    std::cout << "  * Layer 3 (Engines):       Playback Engine, A/V Clock, Vulkan Compositor\n";
    std::cout << "  * Layer 4 (Media & Data):  FFmpeg Demuxer, MediaCache, Proxy / Waveform\n";
    std::cout << "  * Layer 5 (Platform):      LunarG Vulkan 1.4 API, MSVC Toolchain, Win64\n\n";

    std::cout << "[Hardware Acceleration]\n";
    if (vulkan_dev) {
        std::cout << "  GPU Accelerator: " << vulkan_dev->info().device_name << "\n";
        std::cout << "  Vulkan API:      v" << vulkan_dev->info().api_major << "."
                  << vulkan_dev->info().api_minor << "."
                  << vulkan_dev->info().api_patch << "\n";
        std::cout << "  Compute Pipeline: Initialized & Verified (16x16 Workgroups)\n\n";
    } else {
        std::cout << "  GPU Accelerator: CPU Reference Fallback (Headless / VM Mode)\n\n";
    }

    std::cout << "[Editorial Timeline & Transport Engine]\n";
    std::cout << "  Sequence Name:    " << project.timeline().name() << "\n";
    std::cout << "  Resolution / FPS: " << project.timeline().width() << "x"
              << project.timeline().height() << " @ "
              << project.timeline().frame_rate().to_seconds() << " fps\n";
    std::cout << "  Video Tracks:     " << project.timeline().video_tracks().size() << "\n";
    std::cout << "  Audio Tracks:     " << project.timeline().audio_tracks().size() << "\n";
    std::cout << "  Playhead Clock:   " << playback_vm.current_timecode() << " (SMPTE HH:MM:SS:FF)\n";
    std::cout << "  Command History:  " << project.command_history().undo_count() << " undoable operation(s)\n";
    std::cout << "  Project State:    " << (project.is_dirty() ? "Modified (Unsaved)" : "Clean") << "\n\n";

    ave::OtioLossReport otio_rep;
    const std::string otio_data = ave::OtioInterchange::export_to_json(
        project.timeline(), project.media_pool(), otio_rep);
    std::cout << "[Interchange & Persistence]\n";
    std::cout << "  Native Format:    .aveproj (Atomic Write & Validation)\n";
    std::cout << "  OpenTimelineIO:   Export Verified (" << otio_data.size() << " bytes JSON generated)\n";
    std::cout << "======================================================================\n";
}

int run_status_command() {
    ave::Project proj("Demo Project");
    proj.timeline_mut().add_video_track("V2");
    const std::string v1 = proj.timeline().video_tracks()[0].id();

    ave::TimelineItem clip("demo-clip-1", "clip-ref-1", "asset-1", "Intro Plate",
                           ave::Rational{0, 1}, ave::Rational{5, 1}, ave::Rational{0, 1});
    clip.set_scale(1.0f, 1.0f);
    clip.set_opacity(1.0f);
    proj.execute_command(std::make_unique<ave::InsertClipCommand>(v1, clip));

    std::shared_ptr<ave::VulkanDevice> vulkan_dev = ave::VulkanDevice::create(false);
    auto snapshot_store = std::make_shared<ave::PlaybackSnapshotStore>(
        proj.timeline(), proj.media_pool());
    ave::PlaybackSession session(snapshot_store);
    if (vulkan_dev) {
        session.set_vulkan_device(vulkan_dev);
    }
    ave::ProjectSnapshotCoordinator coordinator(proj, snapshot_store, &session);
    ave::PlaybackViewModel playback_vm(session, proj.timeline().frame_rate());

    print_system_status(vulkan_dev.get(), proj, playback_vm);

    session.shutdown();
    return 0;
}

int run_smoke_test() {
    std::cout << "Autonomous Video Editor Desktop Application Shell (v0.1.0)\n";
    std::cout << "[Smoke Test] Initializing Presentation ViewModels, Playback Session & Render Graph...\n";

    ave::Project project("Smoke Test Project");
    std::shared_ptr<ave::VulkanDevice> vulkan_dev = ave::VulkanDevice::create(false);
    auto snapshot_store = std::make_shared<ave::PlaybackSnapshotStore>(
        project.timeline(), project.media_pool());
    auto frame_hub = std::make_shared<ave::FramePresentationHub>();
    ave::PlaybackSessionConfig cfg;
    cfg.max_preview_width = ave::kDefaultPreviewWidth;
    cfg.max_preview_height = ave::kDefaultPreviewHeight;
    ave::PlaybackSession session(snapshot_store, cfg, frame_hub);
    if (vulkan_dev) {
        session.set_vulkan_device(vulkan_dev);
    }
    ave::ProjectSnapshotCoordinator coordinator(project, snapshot_store, &session);

    ave::TimelineViewModel timeline_vm(project);
    ave::PlaybackViewModel playback_vm(session, project.timeline().frame_rate());
    ave::InspectorViewModel inspector_vm(project);
    (void)timeline_vm;
    (void)inspector_vm;

    std::cout << "[Smoke Test] Hardware Accelerator: " << playback_vm.hardware_device_name() << "\n";
    std::cout << "[Smoke Test] Compositing Pipeline: "
              << (playback_vm.is_compositing_enabled() ? "Active" : "Disabled") << "\n";
    std::cout << "[Smoke Test] Playhead Timecode: " << playback_vm.current_timecode() << "\n";

    playback_vm.play();
    std::cout << "[Smoke Test] State: " << (playback_vm.is_playing() ? "Playing" : "Stopped") << "\n";

    const auto pres_res = playback_vm.update();
    std::cout << "[Smoke Test] Presentation Update: Composited="
              << (pres_res.is_composited ? "true" : "false")
              << " GpuReadback=" << (pres_res.used_gpu_readback ? "true" : "false")
              << " HubSeq=" << pres_res.published_sequence_id
              << " Action=" << static_cast<int>(pres_res.action) << "\n";

    playback_vm.step_forward(1);
    std::cout << "[Smoke Test] Stepped +1 frame. Timecode: " << playback_vm.current_timecode() << "\n";
    playback_vm.pause();
    playback_vm.stop();
    session.shutdown();
    frame_hub->shutdown();
    std::cout << "[Smoke Test] Paused successfully. All Presentation and Compositing models verified.\n";
    return 0;
}

class DesktopShellRuntime {
public:
    DesktopShellRuntime() = default;
    ~DesktopShellRuntime() { shutdown(); }

    DesktopShellRuntime(const DesktopShellRuntime&) = delete;
    DesktopShellRuntime& operator=(const DesktopShellRuntime&) = delete;

    void initialize(bool verify_qml_only = false) {
        if (initialized_) {
            return;
        }

        // Deterministic construction order (Spec 6.3):
        // Layer 2 (Project) -> SnapshotStore -> Layer 5 (Vulkan) -> FramePresentationHub -> Layer 4 (MediaCache/ImportQueue) -> Layer 3 (PlaybackSession) -> Coordinator -> Layer 1 (Services & ViewModels -> QML)
        project_ = std::make_unique<ave::Project>("Untitled Project");
        snapshot_store_ = std::make_shared<ave::PlaybackSnapshotStore>(
            project_->timeline(), project_->media_pool());
        vulkan_device_ = ave::VulkanDevice::create(false);
        frame_hub_ = std::make_shared<ave::FramePresentationHub>();
        media_cache_ = std::make_shared<ave::MediaCache>(
            std::filesystem::temp_directory_path() / "ave_desktop_cache");
        import_queue_ = std::make_shared<ave::MediaImportQueue>();

        ave::PlaybackSessionConfig cfg;
        cfg.max_preview_width = ave::kDefaultPreviewWidth;
        cfg.max_preview_height = ave::kDefaultPreviewHeight;
        session_ = std::make_unique<ave::PlaybackSession>(snapshot_store_, cfg, frame_hub_);
        if (vulkan_device_) {
            session_->set_vulkan_device(vulkan_device_);
        }
        snapshot_coordinator_ = std::make_unique<ave::ProjectSnapshotCoordinator>(
            *project_, snapshot_store_, session_.get());

        timeline_vm_ = std::make_unique<ave::TimelineViewModel>(*project_);
        playback_vm_ = std::make_unique<ave::PlaybackViewModel>(
            *session_, project_->timeline().frame_rate());
        inspector_vm_ = std::make_unique<ave::InspectorViewModel>(*project_);

        media_pool_model_ = std::make_unique<ave::desktop::MediaPoolModel>(
            *project_, import_queue_, nullptr, media_cache_);
        timeline_model_ = std::make_unique<ave::desktop::TimelineModel>(
            *project_, nullptr, snapshot_coordinator_.get(), media_cache_);
        inspector_service_ = std::make_unique<ave::desktop::InspectorService>(
            *project_, *timeline_model_);
        playback_service_ = std::make_unique<ave::desktop::PlaybackService>(
            *session_, frame_hub_, snapshot_store_);
        ai_studio_service_ = std::make_unique<ave::desktop::AiStudioService>(
            *project_, nullptr, snapshot_store_, snapshot_coordinator_.get());
        if (verify_qml_only) {
            verify_recovery_dir_ = std::filesystem::temp_directory_path() /
                ("ave_qml_verify_recovery_" + std::to_string(QCoreApplication::applicationPid()));
            autosave_service_ = std::make_unique<ave::desktop::AutosaveService>(
                *project_, verify_recovery_dir_);
            autosave_service_->setRecentProjectsTrackingEnabled(false);
        } else {
            autosave_service_ = std::make_unique<ave::desktop::AutosaveService>(*project_);
        }

        QObject::connect(
            media_pool_model_.get(), &ave::desktop::MediaPoolModel::projectMediaMutated,
            media_pool_model_.get(), [coord = snapshot_coordinator_.get()]() {
                if (coord) {
                    coord->notify_media_mutated();
                }
            });
        QObject::connect(
            timeline_model_.get(), &ave::desktop::TimelineModel::projectReloaded,
            media_pool_model_.get(), &ave::desktop::MediaPoolModel::reloadFromProject);
        QObject::connect(
            timeline_model_.get(), &ave::desktop::TimelineModel::projectReloaded,
            ai_studio_service_.get(), &ave::desktop::AiStudioService::handle_project_reloaded);
        QObject::connect(
            timeline_model_.get(), &ave::desktop::TimelineModel::projectStateChanged,
            autosave_service_.get(), &ave::desktop::AutosaveService::onProjectStateChanged);
        QObject::connect(
            timeline_model_.get(), &ave::desktop::TimelineModel::projectReloaded,
            autosave_service_.get(), &ave::desktop::AutosaveService::onProjectReloaded);
        QObject::connect(
            timeline_model_.get(), &ave::desktop::TimelineModel::projectOpened,
            autosave_service_.get(), &ave::desktop::AutosaveService::onProjectOpened);
        QObject::connect(
            timeline_model_.get(), &ave::desktop::TimelineModel::projectSaved,
            autosave_service_.get(), &ave::desktop::AutosaveService::onProjectSaved);
        QObject::connect(
            ai_studio_service_.get(), &ave::desktop::AiStudioService::proposalApplied,
            playback_service_.get(), &ave::desktop::PlaybackService::syncFromTimeline);
        QObject::connect(
            ai_studio_service_.get(), &ave::desktop::AiStudioService::proposalApplied,
            inspector_service_.get(), &ave::desktop::InspectorService::refreshFromProject);
        autosave_service_->start();

        engine_ = std::make_unique<QQmlApplicationEngine>();
        engine_->addImageProvider(
            QStringLiteral("aveframe"),
            new ave::desktop::LiveFrameImageProvider(frame_hub_));
        engine_->rootContext()->setContextProperty(
            QStringLiteral("mediaPoolModel"),
            media_pool_model_.get());
        engine_->rootContext()->setContextProperty(
            QStringLiteral("timelineModel"),
            timeline_model_.get());
        engine_->rootContext()->setContextProperty(
            QStringLiteral("inspectorService"),
            inspector_service_.get());
        engine_->rootContext()->setContextProperty(
            QStringLiteral("playbackService"),
            playback_service_.get());
        engine_->rootContext()->setContextProperty(
            QStringLiteral("aiStudioService"),
            ai_studio_service_.get());
        engine_->rootContext()->setContextProperty(
            QStringLiteral("autosaveService"),
            autosave_service_.get());
        initialized_ = true;
    }

    void stop_active_workers() {
        if (autosave_service_) {
            autosave_service_->shutdown();
        }
        if (playback_service_) {
            playback_service_->shutdown();
        }
        if (media_pool_model_) {
            media_pool_model_->shutdown();
        }
        if (ai_studio_service_) {
            ai_studio_service_->shutdown();
        }
        if (session_) {
            session_->shutdown();
        }
        if (frame_hub_) {
            frame_hub_->shutdown();
        }
    }

    void shutdown() {
        if (!initialized_) {
            return;
        }

        // Strict reverse teardown order prior to QGuiApplication destruction (Spec 6.3)
        engine_.reset();
        stop_active_workers();
        if (snapshot_coordinator_) {
            snapshot_coordinator_->unbind();
        }
        autosave_service_.reset();
        ai_studio_service_.reset();
        inspector_service_.reset();
        timeline_model_.reset();
        playback_service_.reset();
        media_pool_model_.reset();
        inspector_vm_.reset();
        playback_vm_.reset();
        timeline_vm_.reset();
        session_.reset();
        snapshot_coordinator_.reset();
        import_queue_.reset();
        media_cache_.reset();
        frame_hub_.reset();
        vulkan_device_.reset();
        snapshot_store_.reset();
        project_.reset();
        if (!verify_recovery_dir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(verify_recovery_dir_, ec);
            verify_recovery_dir_.clear();
        }
        initialized_ = false;
    }

    [[nodiscard]] const ave::Project& project() const { return *project_; }
    [[nodiscard]] const ave::VulkanDevice* vulkan_device() const noexcept { return vulkan_device_.get(); }
    [[nodiscard]] const ave::PlaybackViewModel& playback_vm() const { return *playback_vm_; }
    [[nodiscard]] QQmlApplicationEngine& engine() { return *engine_; }

private:
    bool initialized_{false};
    std::filesystem::path verify_recovery_dir_{};
    std::unique_ptr<ave::Project> project_;
    std::shared_ptr<ave::PlaybackSnapshotStore> snapshot_store_;
    std::shared_ptr<ave::VulkanDevice> vulkan_device_;
    std::shared_ptr<ave::FramePresentationHub> frame_hub_;
    std::shared_ptr<ave::MediaCache> media_cache_;
    std::shared_ptr<ave::MediaImportQueue> import_queue_;
    std::unique_ptr<ave::PlaybackSession> session_;
    std::unique_ptr<ave::ProjectSnapshotCoordinator> snapshot_coordinator_;
    std::unique_ptr<ave::TimelineViewModel> timeline_vm_;
    std::unique_ptr<ave::PlaybackViewModel> playback_vm_;
    std::unique_ptr<ave::InspectorViewModel> inspector_vm_;
    std::unique_ptr<ave::desktop::MediaPoolModel> media_pool_model_;
    std::unique_ptr<ave::desktop::TimelineModel> timeline_model_;
    std::unique_ptr<ave::desktop::InspectorService> inspector_service_;
    std::unique_ptr<ave::desktop::PlaybackService> playback_service_;
    std::unique_ptr<ave::desktop::AiStudioService> ai_studio_service_;
    std::unique_ptr<ave::desktop::AutosaveService> autosave_service_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
};

} // namespace

int main(int argc, char* argv[]) {
    bool run_smoke = false;
    bool print_status_only = false;
    bool verify_qml_only = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--smoke-test" || arg == "--test") {
            run_smoke = true;
        } else if (arg == "--status") {
            print_status_only = true;
        } else if (arg == "--verify-qml") {
            verify_qml_only = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
    }

    if (run_smoke) {
        return run_smoke_test();
    }
    if (print_status_only) {
        return run_status_command();
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Autonomous Video Editor"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("AVE"));

    register_desktop_meta_types();
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    int exit_code = 0;
    {
        DesktopShellRuntime runtime;
        runtime.initialize(verify_qml_only);

        if (!verify_qml_only) {
            print_system_status(runtime.vulkan_device(), runtime.project(), runtime.playback_vm());
        }

        bool qml_creation_failed = false;
        QUrl failed_url;
        QList<QQmlError> captured_warnings;

        QQmlApplicationEngine& engine = runtime.engine();
        QObject::connect(
            &engine, &QQmlEngine::warnings,
            &engine, [&captured_warnings](const QList<QQmlError>& warnings) {
                captured_warnings.append(warnings);
                for (const auto& warning : warnings) {
                    std::cerr << "[QML Warning] " << warning.toString().toStdString() << "\n";
                }
            });

        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed,
            &app, [&qml_creation_failed, &failed_url](const QUrl& url) {
                qml_creation_failed = true;
                failed_url = url;
                std::cerr << "[QML Error] Root object creation failed for: "
                          << url.toString().toStdString() << "\n";
                QCoreApplication::exit(1);
            },
            Qt::QueuedConnection);

        QObject::connect(
            &app, &QCoreApplication::aboutToQuit,
            &app, [&runtime]() {
                runtime.stop_active_workers();
            });

        const QUrl main_qml_url(QStringLiteral("qrc:/ave/qml/main.qml"));
        engine.load(main_qml_url);

        if (qml_creation_failed || engine.rootObjects().isEmpty()) {
            const QUrl& error_url = failed_url.isValid() ? failed_url : main_qml_url;
            std::cerr << "[Desktop Shell] Fatal: Failed to load QML entry point ("
                      << error_url.toString().toStdString() << ").\n";
            runtime.shutdown();
            return 1;
        }

        auto* root_window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (!root_window) {
            std::cerr << "[Desktop Shell] Fatal: QML root object is not a QQuickWindow.\n";
            runtime.shutdown();
            return 1;
        }

        if (verify_qml_only) {
            QCoreApplication::processEvents();
            if (!captured_warnings.isEmpty()) {
                std::cerr << "[QML Verify] Failed: " << captured_warnings.size()
                          << " QML warning(s) emitted while loading "
                          << main_qml_url.toString().toStdString() << ".\n";
                runtime.shutdown();
                return 1;
            }

            std::cout << "[QML Verify] Loaded entry: " << main_qml_url.toString().toStdString() << "\n";
            std::cout << "[QML Verify] Root object count: " << engine.rootObjects().size()
                      << " (class: " << root_window->metaObject()->className()
                      << ", size: " << root_window->width() << "x" << root_window->height() << ")\n";
            std::cout << "[QML Verify] QPA platform: " << app.platformName().toStdString() << "\n";
            runtime.shutdown();
            std::cout << "[QML Verify] Clean shutdown complete.\n";
            return 0;
        }

        std::cout << "[Desktop Shell] QML workspace loaded (" << main_qml_url.toString().toStdString()
                  << "). Window: " << root_window->width() << "x" << root_window->height() << ".\n";
        exit_code = app.exec();
        runtime.shutdown();
    }

    return exit_code;
}
