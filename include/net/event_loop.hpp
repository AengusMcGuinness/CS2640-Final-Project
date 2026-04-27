#pragma once

// Coroutines are the core abstraction used by the event loop.
#include <coroutine>
// Fixed-width integer types keep socket-related APIs explicit.
#include <cstdint>
// The scheduler keeps ready coroutines in FIFO order.
#include <deque>
// Waiter maps let us look up which coroutine is waiting on which file descriptor.
#include <unordered_map>

// Everything in this file lives under the networking namespace.
namespace net {

// EventLoop is a tiny cooperative scheduler built on top of poll(2).
class EventLoop {
public:
    // Task is the coroutine type that the scheduler knows how to run.
    class Task {
    public:
        // promise_type defines how the coroutine behaves at creation, suspension, and completion.
        struct promise_type {
            // The coroutine stores a pointer to its owning loop so final suspension can hand control back.
            EventLoop* loop = nullptr;

            // Build the Task object that wraps this coroutine frame.
            Task get_return_object() noexcept;
            // Start suspended so the caller can register the task with the scheduler first.
            std::suspend_always initial_suspend() noexcept;
            // FinalAwaiter is the last suspension point before the coroutine is destroyed.
            struct FinalAwaiter {
                // This awaiter never completes synchronously because the scheduler needs to decide cleanup.
                bool await_ready() const noexcept;
                // If the coroutine is attached to a loop, let the loop destroy it; otherwise destroy directly.
                template <typename Promise>
                void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
                    if (handle.promise().loop != nullptr) {
                        handle.promise().loop->complete(handle);
                    } else {
                        handle.destroy();
                    }
                }
                // Nothing special happens when final suspension resumes.
                void await_resume() const noexcept {}
            };
            // Return the final awaiter from the coroutine so cleanup can happen in one controlled place.
            FinalAwaiter final_suspend() noexcept;
            // This coroutine returns no value, only control flow.
            void return_void() noexcept;
            // Any uncaught exception inside a task terminates the process.
            void unhandled_exception();
        };

        // Standard alias for the coroutine handle type that controls this task.
        using handle_type = std::coroutine_handle<promise_type>;

        // Default construction yields an empty, non-owning task.
        Task() noexcept = default;
        // Construct a task from a coroutine handle produced by the compiler.
        explicit Task(handle_type handle) noexcept;
        // Move construction transfers ownership of the coroutine frame.
        Task(Task&& other) noexcept;
        // Move assignment also transfers ownership and destroys the previous frame if needed.
        Task& operator=(Task&& other) noexcept;
        // Destroy the coroutine frame if it still exists.
        ~Task();

        // Tasks are unique owners of coroutine frames, so copying is disabled.
        Task(const Task&) = delete;
        // Copy assignment is also disabled for the same reason.
        Task& operator=(const Task&) = delete;

        // Register the task with a loop and enqueue it for execution.
        void start(EventLoop& loop);

    private:
        // The underlying coroutine handle, or null if the task has been moved or started.
        handle_type handle_ = nullptr;
    };

    // ReadAwaiter suspends a coroutine until a file descriptor becomes readable.
    struct ReadAwaiter {
        // The event loop that owns this wait.
        EventLoop* loop = nullptr;
        // The file descriptor being watched.
        int fd = -1;

        // Nonblocking I/O means suspension is needed whenever the descriptor is not ready immediately.
        bool await_ready() const noexcept;
        // If the descriptor is not ready, store the coroutine in the read-waiter table.
        void await_suspend(std::coroutine_handle<> handle) const noexcept;
        // Once resumed, there is no extra value to fetch from the awaiter.
        void await_resume() const noexcept {}
    };

    // WriteAwaiter suspends a coroutine until a file descriptor becomes writable.
    struct WriteAwaiter {
        // The loop that will wake the coroutine back up.
        EventLoop* loop = nullptr;
        // The descriptor we are waiting to write to.
        int fd = -1;

        // Writes are also readiness-based, so the coroutine suspends unless the kernel says otherwise.
        bool await_ready() const noexcept;
        // Record the coroutine in the write-waiter table.
        void await_suspend(std::coroutine_handle<> handle) const noexcept;
        // There is no payload to return from the awaiter.
        void await_resume() const noexcept {}
    };

    // Produce an awaiter for readability on the given descriptor.
    ReadAwaiter readable(int fd) noexcept;
    // Produce an awaiter for writability on the given descriptor.
    WriteAwaiter writable(int fd) noexcept;

    // Start a coroutine task and take ownership of it.
    void spawn(Task&& task);
    // Put a coroutine back on the ready queue so the run loop can resume it.
    void schedule(std::coroutine_handle<> handle);
    // Drive the loop until there is no more work or stop() is called.
    void run();
    // Ask the loop to exit after it finishes the current ready work.
    void stop();

    // Destroy a completed coroutine frame.
    void complete(std::coroutine_handle<> handle);

private:
    // The awaiters need access to the wait queues, so the loop grants friendship.
    friend struct ReadAwaiter;
    // The awaiters need access to the wait queues, so the loop grants friendship.
    friend struct WriteAwaiter;

    // Internal helper that registers a coroutine as waiting for readability.
    void wait_read(int fd, std::coroutine_handle<> handle);
    // Internal helper that registers a coroutine as waiting for writability.
    void wait_write(int fd, std::coroutine_handle<> handle);

    // Coroutines that are ready to resume immediately.
    std::deque<std::coroutine_handle<>> ready_;
    // Coroutines blocked on read readiness, keyed by file descriptor.
    std::unordered_map<int, std::coroutine_handle<>> read_waiters_;
    // Coroutines blocked on write readiness, keyed by file descriptor.
    std::unordered_map<int, std::coroutine_handle<>> write_waiters_;
    // Set to true when the loop should stop after draining ready work.
    bool stopping_ = false;
};

// End of the networking namespace.
}  // namespace net
