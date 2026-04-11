#include "net/event_loop.hpp"

#include <cerrno>
#include <exception>
#include <poll.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace net {

EventLoop::Task::Task(handle_type handle) noexcept : handle_(handle) {}

EventLoop::Task::Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

EventLoop::Task& EventLoop::Task::operator=(Task&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            handle_.destroy();
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

EventLoop::Task::~Task() {
    if (handle_ != nullptr) {
        handle_.destroy();
    }
}

void EventLoop::Task::start(EventLoop& loop) {
    if (handle_ == nullptr) {
        return;
    }

    handle_.promise().loop = &loop;
    loop.schedule(handle_);
    handle_ = nullptr;
}

EventLoop::Task EventLoop::Task::promise_type::get_return_object() noexcept {
    return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
}

std::suspend_always EventLoop::Task::promise_type::initial_suspend() noexcept {
    return {};
}

bool EventLoop::Task::promise_type::FinalAwaiter::await_ready() const noexcept {
    return false;
}

EventLoop::Task::promise_type::FinalAwaiter EventLoop::Task::promise_type::final_suspend() noexcept {
    return {};
}

void EventLoop::Task::promise_type::return_void() noexcept {}

void EventLoop::Task::promise_type::unhandled_exception() {
    std::terminate();
}

bool EventLoop::ReadAwaiter::await_ready() const noexcept {
    return false;
}

void EventLoop::ReadAwaiter::await_suspend(std::coroutine_handle<> handle) const noexcept {
    loop->wait_read(fd, handle);
}

bool EventLoop::WriteAwaiter::await_ready() const noexcept {
    return false;
}

void EventLoop::WriteAwaiter::await_suspend(std::coroutine_handle<> handle) const noexcept {
    loop->wait_write(fd, handle);
}

EventLoop::ReadAwaiter EventLoop::readable(int fd) noexcept {
    return ReadAwaiter{this, fd};
}

EventLoop::WriteAwaiter EventLoop::writable(int fd) noexcept {
    return WriteAwaiter{this, fd};
}

void EventLoop::spawn(Task&& task) {
    task.start(*this);
}

void EventLoop::schedule(std::coroutine_handle<> handle) {
    ready_.push_back(handle);
}

void EventLoop::stop() {
    stopping_ = true;
}

void EventLoop::complete(std::coroutine_handle<> handle) {
    handle.destroy();
}

void EventLoop::wait_read(int fd, std::coroutine_handle<> handle) {
    read_waiters_[fd] = handle;
}

void EventLoop::wait_write(int fd, std::coroutine_handle<> handle) {
    write_waiters_[fd] = handle;
}

void EventLoop::run() {
    while (!stopping_) {
        while (!ready_.empty()) {
            auto handle = ready_.front();
            ready_.pop_front();
            if (handle != nullptr && !handle.done()) {
                handle.resume();
            }
        }

        if (stopping_) {
            break;
        }

        if (read_waiters_.empty() && write_waiters_.empty()) {
            break;
        }

        std::vector<pollfd> poll_fds;
        poll_fds.reserve(read_waiters_.size() + write_waiters_.size());

        for (const auto& [fd, _] : read_waiters_) {
            pollfd pfd {};
            pfd.fd = fd;
            pfd.events |= POLLIN;
            if (write_waiters_.find(fd) != write_waiters_.end()) {
                pfd.events |= POLLOUT;
            }
            poll_fds.push_back(pfd);
        }

        for (const auto& [fd, _] : write_waiters_) {
            if (read_waiters_.find(fd) != read_waiters_.end()) {
                continue;
            }
            pollfd pfd {};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            poll_fds.push_back(pfd);
        }

        if (poll_fds.empty()) {
            break;
        }

        const int rc = ::poll(poll_fds.data(), poll_fds.size(), -1);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("poll failed");
        }

        std::vector<std::coroutine_handle<>> ready_handles;

        for (const pollfd& pfd : poll_fds) {
            if (pfd.revents == 0) {
                continue;
            }

            const bool readable = (pfd.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0;
            const bool writable = (pfd.revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL)) != 0;

            if (readable) {
                auto it = read_waiters_.find(pfd.fd);
                if (it != read_waiters_.end()) {
                    ready_handles.push_back(it->second);
                    read_waiters_.erase(it);
                }
            }

            if (writable) {
                auto it = write_waiters_.find(pfd.fd);
                if (it != write_waiters_.end()) {
                    ready_handles.push_back(it->second);
                    write_waiters_.erase(it);
                }
            }
        }

        for (auto handle : ready_handles) {
            schedule(handle);
        }
    }
}

}  // namespace net
