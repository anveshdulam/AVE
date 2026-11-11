#include "ave/command.hpp"
#include "ave/ffmpeg_probe.hpp"
#include "ave/media_asset.hpp"
#include "ave/media_cache.hpp"
#include "ave/media_extractor.hpp"
#include "ave/media_import_queue.hpp"
#include "ave/media_pool.hpp"
#include "ave/media_pool_model.hpp"
#include "ave/playback_session.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/project.hpp"
#include "ave/stream_audio_decoder.hpp"
#include "ave/stream_decoder.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPointer>
#include <QSignalSpy>
#include <QUrl>

namespace {

std::filesystem::path fixture_file(const char* name) {
#ifdef AVE_TEST_FIXTURES_DIR
    return std::filesystem::path(AVE_TEST_FIXTURES_DIR) / name;
#else
    return std::filesystem::path("tests/fixtures") / name;
#endif
}

ave::MediaAsset make_synthetic_asset(const std::filesystem::path& path, const std::string& name) {
    std::vector<ave::SourceStream> streams;
    ave::SourceStream vs(0, ave::StreamType::Video, "mpeg4", ave::Rational{1, 24}, 24);
    ave::VideoStreamInfo vi;
    vi.width = 320;
    vi.height = 240;
    vi.frame_rate = ave::Rational{24, 1};
    vs.set_video_info(vi);
    streams.push_back(vs);

    return ave::MediaAsset(ave::UUID::generate(),
                           path,
                           name,
                           1024,
                           "mp4",
                           ave::Rational{1, 1},
                           std::move(streams));
}

template <typename Pred>
bool pump_events_until(Pred&& pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        if (pred()) {
            return true;
        }
        std::this_thread::yield();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    return pred();
}

struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

TEST(Phase10ImportTest, QueueInitialStateAndInvalidCancel) {
    ave::MediaImportQueue queue;
    const auto counts = queue.counts();
    EXPECT_EQ(counts.active_count, 0u);
    EXPECT_EQ(counts.pending_count, 0u);
    EXPECT_FALSE(counts.is_importing());
    EXPECT_FALSE(queue.is_shutdown());
    EXPECT_FALSE(queue.cancel(""));
    EXPECT_FALSE(queue.cancel("non-existent-job"));

    std::string err;
    EXPECT_TRUE(queue.enqueue(std::filesystem::path{}, &err).empty());
    EXPECT_FALSE(err.empty());
}

TEST(Phase10ImportTest, MetaObjectPropertiesAndRolesMatchSpec) {
    ave::Project project("Meta Spec Project");
    auto queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel model(project, queue);

    const QMetaObject& mo = ave::desktop::MediaPoolModel::staticMetaObject;
    const char* expected_props[] = {
        "assetCount",
        "isImporting",
        "activeImportCount",
        "pendingImportCount",
        "lastImportError"
    };

    for (const char* prop_name : expected_props) {
        const int idx = mo.indexOfProperty(prop_name);
        ASSERT_GE(idx, 0) << "Missing Q_PROPERTY: " << prop_name;
        const QMetaProperty prop = mo.property(idx);
        EXPECT_TRUE(prop.isReadable()) << prop_name;
        EXPECT_TRUE(prop.hasNotifySignal()) << prop_name;
    }

    EXPECT_EQ(model.assetCount(), 0);
    EXPECT_FALSE(model.isImporting());
    EXPECT_EQ(model.activeImportCount(), 0);
    EXPECT_EQ(model.pendingImportCount(), 0);
    EXPECT_TRUE(model.lastImportError().isEmpty());

    const auto roles = model.roleNames();
    EXPECT_EQ(roles.size(), 11);
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::AssetIdRole), QByteArray("assetId"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::NameRole), QByteArray("name"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::FilePathRole), QByteArray("filePath"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::DurationTextRole), QByteArray("durationText"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::DurationSecondsRole), QByteArray("durationSeconds"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::ResolutionTextRole), QByteArray("resolutionText"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::FpsTextRole), QByteArray("fpsText"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::CodecTextRole), QByteArray("codecText"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::MediaTypeRole), QByteArray("mediaType"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::IsOnlineRole), QByteArray("isOnline"));
    EXPECT_EQ(roles.value(ave::desktop::MediaPoolModel::ThumbnailDataUrlRole), QByteArray("thumbnailDataUrl"));
}

TEST(Phase10ImportTest, RealSampleImportPopulatesModelProjectAndCache) {
    const std::filesystem::path sample = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(sample)) << "Missing fixture: " << sample;

    ave::Project project("Real Import Project");
    auto cache = std::make_shared<ave::MediaCache>(std::filesystem::temp_directory_path() / "ave_phase10_import_cache");
    cache->clear();
    auto queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel model(project, queue, nullptr, cache);

    QSignalSpy asset_spy(&model, &ave::desktop::MediaPoolModel::assetCountChanged);
    QSignalSpy mutated_spy(&model, &ave::desktop::MediaPoolModel::projectMediaMutated);
    QSignalSpy importing_spy(&model, &ave::desktop::MediaPoolModel::isImportingChanged);

    const QString job_id = model.importMediaFile(QString::fromStdWString(sample.wstring()));
    EXPECT_FALSE(job_id.isEmpty());

    ASSERT_TRUE(pump_events_until([&]() {
        return model.assetCount() == 1 && !model.isImporting();
    }));

    EXPECT_EQ(model.assetCount(), 1);
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(project.media_pool().asset_count(), 1u);
    EXPECT_TRUE(model.lastImportError().isEmpty());
    EXPECT_FALSE(model.isImporting());
    EXPECT_EQ(model.activeImportCount(), 0);
    EXPECT_EQ(model.pendingImportCount(), 0);
    EXPECT_EQ(asset_spy.count(), 1);
    EXPECT_EQ(mutated_spy.count(), 1);
    EXPECT_GE(importing_spy.count(), 2);

    const QModelIndex idx = model.index(0, 0);
    ASSERT_TRUE(idx.isValid());
    const QString asset_id = model.data(idx, ave::desktop::MediaPoolModel::AssetIdRole).toString();
    EXPECT_FALSE(asset_id.isEmpty());
    EXPECT_EQ(model.data(idx, ave::desktop::MediaPoolModel::NameRole).toString(), QStringLiteral("sample_test.mp4"));
    EXPECT_EQ(model.data(idx, ave::desktop::MediaPoolModel::ResolutionTextRole).toString(), QStringLiteral("320x240"));
    EXPECT_EQ(model.data(idx, ave::desktop::MediaPoolModel::FpsTextRole).toString(), QStringLiteral("24.00 fps"));
    EXPECT_EQ(model.data(idx, ave::desktop::MediaPoolModel::CodecTextRole).toString(), QStringLiteral("mpeg4"));
    EXPECT_EQ(model.data(idx, ave::desktop::MediaPoolModel::MediaTypeRole).toString(), QStringLiteral("Video"));
    EXPECT_TRUE(model.data(idx, ave::desktop::MediaPoolModel::IsOnlineRole).toBool());

    const double dur_sec = model.data(idx, ave::desktop::MediaPoolModel::DurationSecondsRole).toDouble();
    EXPECT_GT(dur_sec, 0.8);
    EXPECT_LT(dur_sec, 1.3);

    const QString thumb_url = model.data(idx, ave::desktop::MediaPoolModel::ThumbnailDataUrlRole).toString();
    EXPECT_TRUE(thumb_url.startsWith(QStringLiteral("data:image/png;base64,")));

    EXPECT_EQ(cache->thumbnail_count(), 1u);
    EXPECT_EQ(cache->waveform_count(), 1u);
}

TEST(Phase10ImportTest, BoundedQueueAccepts18Rejects19thAndCapsConcurrencyAtTwo) {
    std::mutex mu;
    std::condition_variable cv;
    int active_now = 0;
    int max_active = 0;
    int entered_total = 0;
    bool release_workers = false;

    auto queue = std::make_shared<ave::MediaImportQueue>(
        [&](const std::filesystem::path& path, const std::shared_ptr<std::atomic<bool>>& cancel) {
            {
                std::unique_lock<std::mutex> lock(mu);
                ++active_now;
                ++entered_total;
                if (active_now > max_active) {
                    max_active = active_now;
                }
                cv.notify_all();
                while (!release_workers && !cancel->load(std::memory_order_acquire)) {
                    cv.wait_for(lock, std::chrono::milliseconds(2), [&]() {
                        return release_workers || cancel->load(std::memory_order_acquire);
                    });
                }
                --active_now;
            }
            ave::MediaImportQueue::WorkOutput out;
            if (cancel->load(std::memory_order_acquire)) {
                out.terminal_status = ave::ImportJobStatus::Cancelled;
                return out;
            }
            out.terminal_status = ave::ImportJobStatus::Succeeded;
            out.asset = make_synthetic_asset(path, path.filename().string());
            return out;
        });

    ave::Project project("Capacity Project");
    ave::desktop::MediaPoolModel model(project, queue);

    // Fill first 2 active slots and wait deterministically for both workers to engage
    const QString j1 = model.importMediaFile(QStringLiteral("D:/media/clip_01.mp4"));
    const QString j2 = model.importMediaFile(QStringLiteral("D:/media/clip_02.mp4"));
    ASSERT_FALSE(j1.isEmpty());
    ASSERT_FALSE(j2.isEmpty());

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]() {
            return entered_total == 2 && active_now == 2;
        }));
    }

    QCoreApplication::processEvents();
    EXPECT_TRUE(model.isImporting());
    EXPECT_EQ(model.activeImportCount(), 2);
    EXPECT_EQ(model.pendingImportCount(), 0);

    // Enqueue 16 pending items -> total 18 accepted
    std::vector<QString> ids{j1, j2};
    for (int i = 3; i <= 18; ++i) {
        const QString id = model.importMediaFile(QStringLiteral("D:/media/clip_%1.mp4").arg(i));
        EXPECT_FALSE(id.isEmpty()) << "Expected job #" << i << " within 18 capacity";
        ids.push_back(id);
    }

    EXPECT_EQ(model.activeImportCount(), 2);
    EXPECT_EQ(model.pendingImportCount(), 16);
    EXPECT_TRUE(model.isImporting());

    // 19th submission MUST reject immediately without blocking caller and set lastImportError
    QSignalSpy err_spy(&model, &ave::desktop::MediaPoolModel::lastImportErrorChanged);
    const QString rejected_id = model.importMediaFile(QStringLiteral("D:/media/clip_19.mp4"));
    EXPECT_TRUE(rejected_id.isEmpty());
    EXPECT_FALSE(model.lastImportError().isEmpty());
    EXPECT_EQ(err_spy.count(), 1);
    EXPECT_EQ(model.activeImportCount(), 2);
    EXPECT_EQ(model.pendingImportCount(), 16);

    // Release all jobs cleanly
    {
        std::lock_guard<std::mutex> lock(mu);
        release_workers = true;
    }
    cv.notify_all();

    ASSERT_TRUE(pump_events_until([&]() {
        return model.assetCount() == 18 && !model.isImporting();
    }));

    EXPECT_EQ(max_active, 2);
    EXPECT_EQ(model.activeImportCount(), 0);
    EXPECT_EQ(model.pendingImportCount(), 0);
    EXPECT_FALSE(model.isImporting());
}

TEST(Phase10ImportTest, CancelPendingJobPreventsExecutionWhileRunningJobCompletes) {
    std::mutex mu;
    std::condition_variable cv;
    int active_now = 0;
    bool release_workers = false;
    std::atomic<bool> third_executed{false};

    auto queue = std::make_shared<ave::MediaImportQueue>(
        [&](const std::filesystem::path& path, const std::shared_ptr<std::atomic<bool>>& cancel) {
            if (path.filename() == "job3.mp4") {
                third_executed.store(true, std::memory_order_release);
            }
            {
                std::unique_lock<std::mutex> lock(mu);
                ++active_now;
                cv.notify_all();
                while (!release_workers && !cancel->load(std::memory_order_acquire)) {
                    cv.wait_for(lock, std::chrono::milliseconds(2), [&]() {
                        return release_workers || cancel->load(std::memory_order_acquire);
                    });
                }
                --active_now;
            }
            ave::MediaImportQueue::WorkOutput out;
            out.terminal_status = ave::ImportJobStatus::Succeeded;
            out.asset = make_synthetic_asset(path, path.filename().string());
            return out;
        });

    ave::Project project("Pending Cancel Project");
    ave::desktop::MediaPoolModel model(project, queue);

    const QString j1 = model.importMediaFile(QStringLiteral("D:/media/job1.mp4"));
    const QString j2 = model.importMediaFile(QStringLiteral("D:/media/job2.mp4"));
    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]() {
            return active_now == 2;
        }));
    }

    const QString j3 = model.importMediaFile(QStringLiteral("D:/media/job3.mp4"));
    ASSERT_FALSE(j3.isEmpty());
    EXPECT_EQ(model.pendingImportCount(), 1);

    model.cancelImport(j3);
    EXPECT_EQ(model.pendingImportCount(), 0);

    // Releasing j1 & j2 frees slots; j3 must never execute
    {
        std::lock_guard<std::mutex> lock(mu);
        release_workers = true;
    }
    cv.notify_all();

    ASSERT_TRUE(pump_events_until([&]() {
        return !model.isImporting() && model.assetCount() == 2;
    }));

    EXPECT_FALSE(third_executed.load());
    EXPECT_TRUE(model.lastImportError().isEmpty());
    EXPECT_EQ(model.assetCount(), 2);
}

TEST(Phase10ImportTest, CancelAllImportsAbortsRunningAndPendingWithoutMutatingPool) {
    std::mutex mu;
    std::condition_variable cv;
    int entered = 0;

    auto queue = std::make_shared<ave::MediaImportQueue>(
        [&](const std::filesystem::path&, const std::shared_ptr<std::atomic<bool>>& cancel) {
            {
                std::lock_guard<std::mutex> lock(mu);
                ++entered;
                cv.notify_all();
            }
            while (!cancel->load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait_for(lock, std::chrono::milliseconds(2), [&]() {
                    return cancel->load(std::memory_order_acquire);
                });
            }
            ave::MediaImportQueue::WorkOutput out;
            out.terminal_status = ave::ImportJobStatus::Cancelled;
            return out;
        });

    ave::Project project("CancelAll Project");
    ave::desktop::MediaPoolModel model(project, queue);
    QSignalSpy mutated_spy(&model, &ave::desktop::MediaPoolModel::projectMediaMutated);

    for (int i = 1; i <= 6; ++i) {
        ASSERT_FALSE(model.importMediaFile(QStringLiteral("D:/media/c%1.mp4").arg(i)).isEmpty());
    }

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]() { return entered == 2; }));
    }

    model.cancelAllImports();
    cv.notify_all();

    ASSERT_TRUE(pump_events_until([&]() { return !model.isImporting(); }));

    EXPECT_EQ(model.activeImportCount(), 0);
    EXPECT_EQ(model.pendingImportCount(), 0);
    EXPECT_EQ(model.assetCount(), 0);
    EXPECT_EQ(project.media_pool().asset_count(), 0u);
    EXPECT_EQ(mutated_spy.count(), 0);
    EXPECT_TRUE(model.lastImportError().isEmpty());
}

TEST(Phase10ImportTest, CasRaceCancelWinsBeforeCommitDiscardsPayloadAndFiresOnce) {
    std::promise<void> ready_to_commit_promise;
    std::future<void> ready_to_commit_future = ready_to_commit_promise.get_future();
    std::promise<void> allow_return_promise;
    std::shared_future<void> allow_return_future = allow_return_promise.get_future().share();

    ave::MediaImportQueue queue(
        [&](const std::filesystem::path& path, const std::shared_ptr<std::atomic<bool>>&) {
            ave::MediaImportQueue::WorkOutput out;
            out.terminal_status = ave::ImportJobStatus::Succeeded;
            out.asset = make_synthetic_asset(path, "raced.mp4");
            ready_to_commit_promise.set_value();
            allow_return_future.wait();
            return out; // Tries CAS Running -> Succeeded AFTER cancel won Running -> Cancelled
        });

    std::mutex res_mu;
    std::condition_variable res_cv;
    int completion_count = 0;
    ave::ImportJobStatus observed_status = ave::ImportJobStatus::Queued;
    bool has_asset = false;

    queue.set_callbacks(
        [&](ave::ImportJobResult res) {
            std::lock_guard<std::mutex> lock(res_mu);
            ++completion_count;
            observed_status = res.status;
            has_asset = res.asset.has_value();
            res_cv.notify_all();
        },
        nullptr);

    const std::string job_id = queue.enqueue("D:/media/race_cancel.mp4");
    ASSERT_FALSE(job_id.empty());

    ready_to_commit_future.wait();
    EXPECT_TRUE(queue.cancel(job_id)); // Cancel wins CAS Running -> Cancelled!

    allow_return_promise.set_value();

    // Wait for worker count to drain cleanly to zero so we prove post-work CAS did not emit a second callback
    ASSERT_TRUE(pump_events_until([&]() {
        return !queue.counts().is_importing();
    }));

    std::lock_guard<std::mutex> lock(res_mu);
    EXPECT_EQ(completion_count, 1);
    EXPECT_EQ(observed_status, ave::ImportJobStatus::Cancelled);
    EXPECT_FALSE(has_asset);
}

TEST(Phase10ImportTest, CasRaceCommitWinsBeforeReentrantCancelKeepsAsset) {
    std::mutex res_mu;
    std::condition_variable res_cv;
    int completion_count = 0;
    bool reentrant_cancel_result = true;
    ave::ImportJobStatus observed_status = ave::ImportJobStatus::Queued;

    ave::MediaImportQueue queue(
        [](const std::filesystem::path& path, const std::shared_ptr<std::atomic<bool>>&) {
            ave::MediaImportQueue::WorkOutput out;
            out.terminal_status = ave::ImportJobStatus::Succeeded;
            out.asset = make_synthetic_asset(path, "winner.mp4");
            return out;
        });

    queue.set_callbacks(
        [&](ave::ImportJobResult res) {
            // Re-entrancy: calling cancel() from inside completion callback must not deadlock and must lose CAS
            const bool cancelled = queue.cancel(res.job_id);
            std::lock_guard<std::mutex> lock(res_mu);
            ++completion_count;
            reentrant_cancel_result = cancelled;
            observed_status = res.status;
            res_cv.notify_all();
        },
        nullptr);

    const std::string job_id = queue.enqueue("D:/media/race_commit.mp4");
    ASSERT_FALSE(job_id.empty());

    std::unique_lock<std::mutex> lock(res_mu);
    ASSERT_TRUE(res_cv.wait_for(lock, std::chrono::seconds(2), [&]() {
        return completion_count == 1;
    }));
    EXPECT_EQ(observed_status, ave::ImportJobStatus::Succeeded);
    EXPECT_FALSE(reentrant_cancel_result);
}

TEST(Phase10ImportTest, MissingAndCorruptMediaFilesReportErrorsCleanly) {
    ave::Project project("Error Path Project");
    auto queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel model(project, queue);

    const std::string missing_job =
        model.importMediaFile(QStringLiteral("D:/definitely_missing_ave_asset_12345.mp4")).toStdString();
    EXPECT_FALSE(missing_job.empty());

    ASSERT_TRUE(pump_events_until([&]() { return !model.isImporting(); }));
    EXPECT_EQ(model.assetCount(), 0);
    EXPECT_FALSE(model.lastImportError().isEmpty());

    // Write invalid bytes to a temp .mp4 and verify graceful FFmpeg demux error
    const std::filesystem::path bad_file =
        std::filesystem::temp_directory_path() / "ave_phase10_corrupt_test.mp4";
    TempFileGuard cleanup{bad_file};
    {
        std::ofstream ofs(bad_file, std::ios::binary);
        ofs << "NOT_A_VALID_MP4_CONTAINER_HEADER_DATA_00000000";
    }

    const QString prev_err = model.lastImportError();
    const QString corrupt_job = model.importMediaFile(QString::fromStdWString(bad_file.wstring()));
    EXPECT_FALSE(corrupt_job.isEmpty());

    ASSERT_TRUE(pump_events_until([&]() { return !model.isImporting(); }));
    EXPECT_EQ(model.assetCount(), 0);
    EXPECT_NE(model.lastImportError(), prev_err);
    EXPECT_FALSE(model.lastImportError().isEmpty());
}

TEST(Phase10ImportTest, AvioInterruptCbAbortsCustomIoAndKeepsCancelStateAlivePastClose) {
    const std::filesystem::path sample = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(sample));

    std::ifstream ifs(sample, std::ios::binary);
    ASSERT_TRUE(ifs.good());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 4096u);

    // Test 1: Pre-cancelled real fixture probe aborts cleanly without succeeding
    {
        auto cancel = std::make_shared<std::atomic<bool>>(true);
        const auto res = ave::FFmpegProbe::probe(sample, cancel);
        EXPECT_FALSE(res.success);
        EXPECT_TRUE(res.cancelled);
        EXPECT_FALSE(res.asset.has_value());
    }

    // Test 2: Genuine mid-demux FFmpeg AVIOInterruptCB interruption via chunked custom AVIOContext
    struct CustomIoState {
        const std::vector<uint8_t>* data{nullptr};
        std::size_t pos{0};
        std::size_t read_calls{0};
        std::promise<void> reached_stream_info;
        bool signaled_reached{false};
        std::promise<void> resume_reads;
        std::shared_future<void> resume_future;
    } io_state;

    io_state.data = &bytes;
    io_state.resume_future = io_state.resume_reads.get_future().share();

    auto read_cb = [](void* opaque, uint8_t* buf, int buf_size) -> int {
        auto* st = static_cast<CustomIoState*>(opaque);
        ++st->read_calls;
        // Wait once initial probe bytes are buffered so main thread flips cancel_state mid-demux
        if (st->pos >= 2048u && !st->signaled_reached) {
            st->signaled_reached = true;
            st->reached_stream_info.set_value();
            st->resume_future.wait();
        }
        if (st->pos >= st->data->size()) {
            return -541478725; // AVERROR_EOF without pulling ffmpeg headers into test
        }
        const std::size_t chunk = std::min<std::size_t>({static_cast<std::size_t>(buf_size), 512u, st->data->size() - st->pos});
        std::memcpy(buf, st->data->data() + st->pos, chunk);
        st->pos += chunk;
        return static_cast<int>(chunk);
    };

    auto seek_cb = [](void* opaque, int64_t offset, int whence) -> int64_t {
        auto* st = static_cast<CustomIoState*>(opaque);
        constexpr int kAvSeekSize = 0x10000;
        if (whence == kAvSeekSize) {
            return static_cast<int64_t>(st->data->size());
        }
        int64_t new_pos = 0;
        if (whence == SEEK_SET) {
            new_pos = offset;
        } else if (whence == SEEK_CUR) {
            new_pos = static_cast<int64_t>(st->pos) + offset;
        } else if (whence == SEEK_END) {
            new_pos = static_cast<int64_t>(st->data->size()) + offset;
        } else {
            return -1;
        }
        if (new_pos < 0 || static_cast<std::size_t>(new_pos) > st->data->size()) {
            return -1;
        }
        st->pos = static_cast<std::size_t>(new_pos);
        return new_pos;
    };

    auto cancel_state = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<std::atomic<bool>> weak_cancel = cancel_state;

    ave::CustomProbeIo custom_io;
    custom_io.opaque = &io_state;
    custom_io.read_packet = read_cb;
    custom_io.seek = seek_cb;

    std::future<ave::ProbeResult> probe_future = std::async(
        std::launch::async,
        [&]() {
            return ave::FFmpegProbe::probe_custom_io(custom_io, "sample_test.mp4", cancel_state);
        });

    io_state.reached_stream_info.get_future().wait();
    // cancel_state must still be referenced inside probe_internal past open/stream_info/close
    EXPECT_FALSE(weak_cancel.expired());
    cancel_state->store(true, std::memory_order_release);
    io_state.resume_reads.set_value();

    const ave::ProbeResult custom_res = probe_future.get();
    EXPECT_FALSE(custom_res.success);
    EXPECT_TRUE(custom_res.cancelled);
    EXPECT_FALSE(custom_res.asset.has_value());
    EXPECT_GE(io_state.read_calls, 2u);

    // Caller still holds last strong ref; resetting it drops use_count cleanly after avformat_close_input finished
    EXPECT_EQ(cancel_state.use_count(), 1);
    cancel_state.reset();
    EXPECT_TRUE(weak_cancel.expired());
}

TEST(Phase10ImportTest, ExtractorsAndStreamDecodersRespectCooperativeCancellation) {
    const std::filesystem::path sample = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(sample));

    auto pre_cancelled = std::make_shared<std::atomic<bool>>(true);
    EXPECT_FALSE(ave::ThumbnailExtractor::extract(sample, ave::Rational{0, 1}, 160, 90, pre_cancelled).has_value());
    EXPECT_FALSE(ave::WaveformExtractor::extract(sample, 100, pre_cancelled).has_value());

    // StreamDecoder cancellation + move safety
    {
        auto dec_cancel = std::make_shared<std::atomic<bool>>(true);
        ave::StreamDecoder cancelled_open;
        EXPECT_FALSE(cancelled_open.open(sample, dec_cancel));
        EXPECT_FALSE(cancelled_open.is_open());

        auto live_cancel = std::make_shared<std::atomic<bool>>(false);
        ave::StreamDecoder decoder;
        ASSERT_TRUE(decoder.open(sample, live_cancel));
        EXPECT_TRUE(decoder.decode_next_frame(1, 160, 120).has_value());

        ave::StreamDecoder moved_decoder = std::move(decoder);
        EXPECT_TRUE(moved_decoder.is_open());
        EXPECT_FALSE(moved_decoder.is_cancelled());

        moved_decoder.request_cancel();
        EXPECT_TRUE(moved_decoder.is_cancelled());
        EXPECT_TRUE(live_cancel->load());
        EXPECT_FALSE(moved_decoder.seek_pts(ave::Rational{0, 1}));
        EXPECT_FALSE(moved_decoder.decode_next_frame(2, 160, 120).has_value());
        moved_decoder.close();
        EXPECT_FALSE(moved_decoder.is_open());
    }

    // StreamAudioDecoder cancellation
    {
        auto aud_cancel = std::make_shared<std::atomic<bool>>(true);
        ave::StreamAudioDecoder cancelled_audio;
        EXPECT_FALSE(cancelled_audio.open(sample, 48000, 2, aud_cancel));
        EXPECT_FALSE(cancelled_audio.is_open());

        auto live_cancel = std::make_shared<std::atomic<bool>>(false);
        ave::StreamAudioDecoder audio_decoder;
        ASSERT_TRUE(audio_decoder.open(sample, 48000, 2, live_cancel));
        EXPECT_TRUE(audio_decoder.decode_next_audio().has_value());

        audio_decoder.request_cancel();
        EXPECT_TRUE(audio_decoder.is_cancelled());
        EXPECT_FALSE(audio_decoder.seek_pts(ave::Rational{0, 1}));
        EXPECT_FALSE(audio_decoder.decode_next_audio().has_value());
        audio_decoder.close();
    }
}

TEST(Phase10ImportTest, MediaPoolModelDestructionMidImportAndStaleQueuedEventAreSafe) {
    ave::Project project("Safe Teardown Project");

    std::mutex mu;
    std::condition_variable cv;
    int started = 0;

    auto queue = std::make_shared<ave::MediaImportQueue>(
        [&](const std::filesystem::path&, const std::shared_ptr<std::atomic<bool>>& cancel) {
            {
                std::lock_guard<std::mutex> lock(mu);
                ++started;
                cv.notify_all();
            }
            while (!cancel->load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait_for(lock, std::chrono::milliseconds(2), [&]() {
                    return cancel->load(std::memory_order_acquire);
                });
            }
            ave::MediaImportQueue::WorkOutput out;
            out.terminal_status = ave::ImportJobStatus::Cancelled;
            return out;
        });

    auto model = std::make_unique<ave::desktop::MediaPoolModel>(project, queue);
    const std::shared_ptr<std::atomic<bool>> alive_flag = queue->model_alive_flag();
    QPointer<ave::desktop::MediaPoolModel> weak_model(model.get());

    // Queue BOTH:
    // 1) a direct queued call onto model (Qt removes on ~QObject), and
    // 2) a queued call onto QCoreApplication executing after model destruction to verify explicit weak_model + alive_flag guards.
    std::atomic<bool> stale_callback_ran{false};
    std::atomic<bool> touched_dead_model{false};
    QMetaObject::invokeMethod(
        model.get(),
        [&touched_dead_model]() {
            touched_dead_model.store(true, std::memory_order_release);
        },
        Qt::QueuedConnection);
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [weak_model, alive_flag, &stale_callback_ran, &touched_dead_model]() {
            stale_callback_ran.store(true, std::memory_order_release);
            if (!alive_flag->load(std::memory_order_acquire) || weak_model.isNull()) {
                return;
            }
            touched_dead_model.store(true, std::memory_order_release);
        },
        Qt::QueuedConnection);

    // Also submit real in-flight active + pending jobs to the import queue
    for (int i = 1; i <= 4; ++i) {
        ASSERT_FALSE(model->importMediaFile(QStringLiteral("D:/media/teardown_%1.mp4").arg(i)).isEmpty());
    }

    {
        std::unique_lock<std::mutex> lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]() { return started == 2; }));
    }

    // Deleting model mid-import MUST set alive_flag=false, shut down & join workers synchronously
    model.reset();
    cv.notify_all();

    EXPECT_FALSE(alive_flag->load(std::memory_order_acquire));
    EXPECT_TRUE(weak_model.isNull());
    EXPECT_TRUE(queue->is_shutdown());
    EXPECT_EQ(project.media_pool().asset_count(), 0u);

    // Draining queued lambdas after model destruction must observe !alive_flag && QPointer.isNull() and return safely
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    EXPECT_TRUE(stale_callback_ran.load());
    EXPECT_FALSE(touched_dead_model.load());
}

TEST(Phase10ImportTest, ReloadFromProjectPopulatesDeterministically) {
    ave::Project project("Reload Project");
    project.media_pool_mut().register_asset(make_synthetic_asset("D:/media/z_clip.mp4", "z_clip.mp4"));
    project.media_pool_mut().register_asset(make_synthetic_asset("D:/media/a_clip.mp4", "a_clip.mp4"));

    auto queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel model(project, queue);

    EXPECT_EQ(model.assetCount(), 2);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(0, 0), ave::desktop::MediaPoolModel::NameRole).toString(),
              QStringLiteral("a_clip.mp4"));
    EXPECT_EQ(model.data(model.index(1, 0), ave::desktop::MediaPoolModel::NameRole).toString(),
              QStringLiteral("z_clip.mp4"));

    project.media_pool_mut().register_asset(make_synthetic_asset("D:/media/m_clip.mp4", "m_clip.mp4"));
    QSignalSpy count_spy(&model, &ave::desktop::MediaPoolModel::assetCountChanged);
    model.reloadFromProject();

    EXPECT_EQ(count_spy.count(), 1);
    EXPECT_EQ(model.assetCount(), 3);
    EXPECT_EQ(model.data(model.index(1, 0), ave::desktop::MediaPoolModel::NameRole).toString(),
              QStringLiteral("m_clip.mp4"));
}

TEST(Phase10ImportTest, CompletedImportRepublishesSnapshotForPlaybackSession) {
    const std::filesystem::path sample = fixture_file("sample_test.mp4");
    ASSERT_TRUE(std::filesystem::exists(sample)) << "Missing fixture: " << sample;

    ave::Project project("Import Snapshot Bridge Project");
    auto snapshot_store = std::make_shared<ave::PlaybackSnapshotStore>(
        project.timeline(), project.media_pool());
    ASSERT_EQ(snapshot_store->current_version(), 1u);

    ave::PlaybackSession session(snapshot_store);
    ave::ProjectSnapshotCoordinator coordinator(project, snapshot_store, &session);

    auto cache = std::make_shared<ave::MediaCache>(
        std::filesystem::temp_directory_path() / "ave_phase10_g08_import_cache");
    cache->clear();
    auto queue = std::make_shared<ave::MediaImportQueue>();
    ave::desktop::MediaPoolModel model(project, queue, nullptr, cache);

    QObject::connect(&model, &ave::desktop::MediaPoolModel::projectMediaMutated, &model,
                     [&coordinator]() { coordinator.notify_media_mutated(); });

    const ave::PlaybackSnapshotPtr initial_snap = snapshot_store->load();
    ASSERT_TRUE(initial_snap->assets_by_id.empty());

    const QString job_id = model.importMediaFile(QString::fromStdWString(sample.wstring()));
    ASSERT_FALSE(job_id.isEmpty());

    ASSERT_TRUE(pump_events_until([&]() {
        return model.assetCount() == 1 && !model.isImporting();
    }));

    EXPECT_EQ(snapshot_store->current_version(), 2u);
    EXPECT_TRUE(initial_snap->assets_by_id.empty());

    const std::string imported_id =
        model.data(model.index(0, 0), ave::desktop::MediaPoolModel::AssetIdRole).toString().toStdString();
    ASSERT_FALSE(imported_id.empty());

    const ave::PlaybackSnapshotPtr after_import_snap = snapshot_store->load();
    ASSERT_NE(after_import_snap->find_asset(imported_id), nullptr);

    const std::string v1_id = project.timeline().video_tracks()[0].id();
    const std::string a1_id = project.timeline().audio_tracks()[0].id();
    ASSERT_TRUE(project.execute_command(std::make_unique<ave::InsertClipCommand>(
        v1_id, ave::TimelineItem("g08-v1", "g08-vc1", imported_id, "Sample V1",
                                 ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1}))).is_success());
    ASSERT_TRUE(project.execute_command(std::make_unique<ave::InsertClipCommand>(
        a1_id, ave::TimelineItem("g08-a1", "g08-ac1", imported_id, "Sample A1",
                                 ave::Rational{0, 1}, ave::Rational{1, 1}, ave::Rational{0, 1}))).is_success());
    EXPECT_EQ(snapshot_store->current_version(), 4u);

    session.seek(ave::Rational{0, 1});
    if (session.audio_device()) {
        session.audio_device()->stop();
    }
    session.pump_audio(1024);
    const auto pres = session.update();
    EXPECT_TRUE(pres.frame.has_value());
    EXPECT_EQ(pres.snapshot_version, 4u);
    EXPECT_GT(session.audio_ring_buffer().available_read_frames(), 0u);

    session.shutdown();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
