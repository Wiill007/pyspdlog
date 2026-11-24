// Background threads: the async thread pool, the periodic flusher, and what
// happens to them on shutdown() and os.fork().

#include "common.h"

#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/sink.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace pyspdlog {
namespace {

using thread_pool = spdlog::details::thread_pool;

thread_local bool t_on_pool_thread = false;
thread_local bool t_on_flusher_thread = false;

// Thread pools are numbered as they are created, starting at 1. Both are
// guarded by the registry's tp_mutex.
std::uint64_t g_pool_number = 0;
std::size_t g_pool_threads = 1;

// AsyncLoggers bound to a pool numbered below this one cannot have their
// messages processed anymore: shutdown() stopped the pool, or the process
// forked and the pool's threads only exist in the parent.
std::atomic<std::uint64_t> g_first_live_pool{1};
// Pools numbered below this one were inherited across os.fork().
std::atomic<std::uint64_t> g_first_pool_after_fork{0};
std::atomic<bool> g_warned_about_fork{false};

// The pool each async logger created by pyspdlog was created with.
// spdlog::async_logger is final and keeps its pool private, so this lives on
// the side. The weak_ptr tells a live entry from a stale one whose address was
// reused by a newer logger.
//
// Globals used by background threads are allocated once and never freed, so
// they stay valid for threads still running during static destruction.
struct async_entry {
    std::weak_ptr<spdlog::async_logger> logger;
    std::uint64_t pool_number;
};

std::mutex &async_table_mutex() {
    static auto *mutex = new std::mutex();
    return *mutex;
}

std::unordered_map<const spdlog::async_logger *, async_entry> &async_table() {
    static auto *table = new std::unordered_map<const spdlog::async_logger *, async_entry>();
    return *table;
}

void remember_pool(const async_logger_ptr &logger, std::uint64_t pool_number) {
    std::lock_guard<std::mutex> lock(async_table_mutex());
    auto &table = async_table();
    static std::size_t prune_at = 64;
    if (table.size() >= prune_at) {
        for (auto it = table.begin(); it != table.end();) {
            it = it->second.logger.expired() ? table.erase(it) : std::next(it);
        }
        prune_at = std::max<std::size_t>(64, 2 * table.size());
    }
    table[logger.get()] = {logger, pool_number};
}

std::optional<std::uint64_t> pool_number_of(const spdlog::async_logger &logger) {
    std::lock_guard<std::mutex> lock(async_table_mutex());
    auto &table = async_table();
    auto it = table.find(&logger);
    if (it == table.end() || it->second.logger.expired()) {
        return std::nullopt;
    }
    return it->second.pool_number;
}

// Whether `logger` is bound to a pool whose threads are gone, and if so whether
// that is because of a fork.
struct pool_state {
    bool dead;
    bool forked;
};

pool_state state_of(spdlog::logger &logger) {
    if (g_first_live_pool.load(std::memory_order_relaxed) <= 1) {
        return {false, false};  // no pool has been stopped yet: skip the lookup
    }
    auto *async = dynamic_cast<spdlog::async_logger *>(&logger);
    if (!async) {
        return {false, false};
    }
    auto number = pool_number_of(*async);
    if (!number || *number >= g_first_live_pool.load(std::memory_order_relaxed)) {
        return {false, false};
    }
    return {true, *number < g_first_pool_after_fork.load(std::memory_order_relaxed)};
}

struct pool_handle {
    std::shared_ptr<thread_pool> pool;
    std::uint64_t number;
};

void check_not_on_background_thread(const char *what) {
    if (t_on_pool_thread || t_on_flusher_thread) {
        throw std::runtime_error(std::string(what) +
                                 " cannot be called from a sink running on a background thread");
    }
}

std::shared_ptr<thread_pool> make_pool(std::size_t queue_size, std::size_t thread_count) {
    return std::make_shared<thread_pool>(queue_size, thread_count, [] { t_on_pool_thread = true; });
}

// Each worker that dequeues one of the barrier's messages blocks until all
// `parties` messages have been dequeued. A blocked worker cannot take another
// message, so the messages end up on distinct workers, and once all of them
// arrive every message queued before them has been processed.
class barrier_sink final : public spdlog::sinks::sink {
public:
    explicit barrier_sink(std::size_t parties) : parties_(parties) {}

    void log(const spdlog::details::log_msg &) override {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrived_;
        cv_.notify_all();
        cv_.wait(lock, [this] { return arrived_ >= parties_; });
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return arrived_ >= parties_; });
    }

    void flush() override {}
    void set_pattern(const std::string &) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

private:
    std::size_t parties_;
    std::size_t arrived_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
};

void wait_for_pool(const std::shared_ptr<thread_pool> &pool, std::size_t parties) {
    auto barrier = std::make_shared<barrier_sink>(parties);
    {
        auto logger = std::make_shared<spdlog::async_logger>("pyspdlog-barrier", barrier, pool,
                                                             spdlog::async_overflow_policy::block);
        for (std::size_t i = 0; i < parties; ++i) {
            logger->log(spdlog::level::critical, spdlog::string_view_t(""));
        }
    }
    barrier->wait();
}

// --- Periodic flusher -------------------------------------------------------
//
// Used instead of spdlog::flush_every(): spdlog's flusher holds the registry
// lock while flushing (so a flush callback calling back into the registry
// would deadlock), and it cannot be abandoned in a forked child.

// Held by the flusher while it flushes, and across fork() so that the child
// never inherits a registry lock taken by the flusher.
std::mutex &flush_cycle_mutex() {
    static auto *mutex = new std::mutex();
    return *mutex;
}

std::mutex &flusher_mutex() {
    static auto *mutex = new std::mutex();
    return *mutex;
}

void flush_registered_loggers() {
    std::lock_guard<std::mutex> cycle(flush_cycle_mutex());
    std::vector<std::shared_ptr<spdlog::logger>> loggers;
    spdlog::apply_all([&](std::shared_ptr<spdlog::logger> logger) { loggers.push_back(std::move(logger)); });
    for (auto &logger : loggers) {
        if (accepts_messages_nogil(*logger)) {
            logger->flush();
        }
    }
}

class periodic_flusher {
public:
    explicit periodic_flusher(std::chrono::milliseconds interval)
        : interval_(interval), thread_([this] { run(); }) {}

    ~periodic_flusher() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

private:
    void run() {
        t_on_flusher_thread = true;
        std::unique_lock<std::mutex> lock(mutex_);
        while (!cv_.wait_for(lock, interval_, [this] { return stop_; })) {
            lock.unlock();
            flush_registered_loggers();
            lock.lock();
        }
    }

    std::chrono::milliseconds interval_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread thread_;  // last, so it starts after the members it uses exist
};

periodic_flusher *g_flusher = nullptr;  // guarded by flusher_mutex()

void stop_flusher() {
    std::lock_guard<std::mutex> lock(flusher_mutex());
    delete g_flusher;
    g_flusher = nullptr;
}

}  // namespace

// --- Async loggers ----------------------------------------------------------

bool accepts_messages_nogil(spdlog::logger &logger) { return !state_of(logger).dead; }

bool accepts_messages(spdlog::logger &logger) {
    // No GIL release needed: the table lock is never held while waiting for the GIL.
    pool_state state = state_of(logger);
    if (!state.dead) {
        return true;
    }
    if (state.forked && !g_warned_about_fork.exchange(true)) {
        const std::string message =
            "pyspdlog: AsyncLogger '" + logger.name() +
            "' was created before os.fork(); the thread pool it uses does not exist in the child "
            "process, so its messages are dropped. Create async loggers after forking, or use the "
            "'spawn' or 'forkserver' multiprocessing start method.";
        if (PyErr_WarnEx(PyExc_RuntimeWarning, message.c_str(), 1) != 0) {
            throw nb::python_error();
        }
    }
    return false;
}

namespace {

pool_handle ensure_thread_pool() {
    auto &registry = spdlog::details::registry::instance();
    std::lock_guard<std::recursive_mutex> lock(registry.tp_mutex());
    auto pool = registry.get_tp();
    if (!pool) {
        pool = make_pool(spdlog::details::default_async_q_size, 1);
        ++g_pool_number;
        g_pool_threads = 1;
        registry.set_tp(pool);
    }
    return {std::move(pool), g_pool_number};
}

}  // namespace

void init_thread_pool(std::size_t queue_size, std::size_t thread_count) {
    check_not_on_background_thread("init_thread_pool()");
    if (queue_size == 0) {
        throw nb::value_error("queue_size must be at least 1");
    }
    auto pool = make_pool(queue_size, thread_count);
    auto &registry = spdlog::details::registry::instance();
    std::lock_guard<std::recursive_mutex> lock(registry.tp_mutex());
    ++g_pool_number;
    g_pool_threads = thread_count;
    // Replacing the pool joins the old one after it drains its queue.
    registry.set_tp(std::move(pool));
}

async_logger_ptr make_async_logger(std::string name, std::vector<spdlog::sink_ptr> sinks,
                                   spdlog::async_overflow_policy policy) {
    pool_handle pool = ensure_thread_pool();
    auto logger = std::make_shared<spdlog::async_logger>(std::move(name), sinks.begin(), sinks.end(),
                                                         pool.pool, policy);
    remember_pool(logger, pool.number);
    return logger;
}

std::shared_ptr<spdlog::logger> clone_logger(spdlog::logger &logger, std::string name) {
    auto cloned = logger.clone(std::move(name));
    if (auto *async = dynamic_cast<spdlog::async_logger *>(&logger)) {
        if (auto number = pool_number_of(*async)) {
            remember_pool(std::static_pointer_cast<spdlog::async_logger>(cloned), *number);
        }
    }
    return cloned;
}

void flush_async_logger(spdlog::async_logger &logger) {
    if (state_of(logger).dead) {
        return;
    }
    auto number = pool_number_of(logger);
    logger.flush();
    if (t_on_pool_thread || !number) {
        return;  // a worker waiting for its own pool would deadlock
    }
    std::shared_ptr<thread_pool> pool;
    std::size_t parties = 0;
    {
        auto &registry = spdlog::details::registry::instance();
        std::lock_guard<std::recursive_mutex> lock(registry.tp_mutex());
        if (g_pool_number != *number) {
            return;  // its pool was replaced by init_thread_pool()
        }
        pool = registry.get_tp();
        parties = g_pool_threads;
    }
    if (pool) {
        wait_for_pool(pool, parties);
    }
}

void set_flush_interval(std::chrono::milliseconds interval) {
    check_not_on_background_thread("flush_every()");
    std::lock_guard<std::mutex> lock(flusher_mutex());
    delete g_flusher;
    g_flusher = interval.count() > 0 ? new periodic_flusher(interval) : nullptr;
}

void shutdown_spdlog() {
    check_not_on_background_thread("shutdown()");
    stop_flusher();
    {
        auto &registry = spdlog::details::registry::instance();
        std::lock_guard<std::recursive_mutex> lock(registry.tp_mutex());
        g_first_live_pool = g_pool_number + 1;
    }
    spdlog::shutdown();
}

void before_fork() {
    flusher_mutex().lock();
    flush_cycle_mutex().lock();
}

void after_fork_in_parent() {
    flush_cycle_mutex().unlock();
    flusher_mutex().unlock();
}

void after_fork_in_child() {
    flush_cycle_mutex().unlock();
    flusher_mutex().unlock();

    // Only the forking thread exists in the child. Destroying the objects
    // that own the parent's threads would join them and hang forever, so
    // they are leaked instead.
    {
        std::lock_guard<std::mutex> lock(flusher_mutex());
        g_flusher = nullptr;
    }
    auto &registry = spdlog::details::registry::instance();
    std::lock_guard<std::recursive_mutex> lock(registry.tp_mutex());
    if (auto pool = registry.get_tp()) {
        new std::shared_ptr<thread_pool>(std::move(pool));
        registry.set_tp(nullptr);
    }
    g_first_live_pool = g_pool_number + 1;
    g_first_pool_after_fork = g_pool_number + 1;
}

}  // namespace pyspdlog
