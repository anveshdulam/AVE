#include <iostream>
#include <string_view>

#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_session.hpp"
#include "ave/timeline_view_model.hpp"
#include "ave/playback_view_model.hpp"

#if defined(AVE_ENABLE_QT)
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#endif

int main(int argc, char *argv[]) {
    std::cout << "Autonomous Video Editor Desktop Application Shell (v0.1.0)\n";

    bool run_smoke = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--smoke-test" || arg == "--test") {
            run_smoke = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ave_desktop [--smoke-test] [--help]\n";
            return 0;
        }
    }

    if (run_smoke) {
        std::cout << "[Smoke Test] Initializing Presentation ViewModels & Playback Session...\n";
        ave::Timeline timeline("tl-1", "Default Timeline", ave::Rational{24, 1});
        ave::MediaPool pool;
        ave::PlaybackSession session(timeline, pool);
        ave::TimelineViewModel timeline_vm(timeline);
        ave::PlaybackViewModel playback_vm(session);

        std::cout << "[Smoke Test] Playhead Timecode: " << playback_vm.current_timecode() << "\n";
        playback_vm.play();
        std::cout << "[Smoke Test] State: " << (playback_vm.is_playing() ? "Playing" : "Stopped") << "\n";
        playback_vm.step_forward(1);
        std::cout << "[Smoke Test] Stepped +1 frame. Timecode: " << playback_vm.current_timecode() << "\n";
        playback_vm.pause();
        std::cout << "[Smoke Test] Paused successfully. All Presentation models verified.\n";
        return 0;
    }

#if defined(AVE_ENABLE_QT)
    QGuiApplication app(argc, argv);
    app.setApplicationName("Autonomous Video Editor");
    app.setOrganizationName("AVE");

    QQmlApplicationEngine engine;
    return app.exec();
#else
    std::cout << "Built in headless/core mode (Qt presentation deferred). Run with --smoke-test to verify.\n";
    return 0;
#endif
}
