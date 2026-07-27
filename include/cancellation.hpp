#ifndef CANCELLATION_HPP
#define CANCELLATION_HPP

#include <atomic>
#include <csignal>

namespace opencode {

class CancellationToken {
public:
    explicit CancellationToken(
        volatile std::sig_atomic_t* signal_flag = nullptr
    ) : signal_flag_(signal_flag) {}

    void request() noexcept {
        requested_.store(true);
        if (signal_flag_) *signal_flag_ = 1;
    }
    void reset() noexcept {
        requested_.store(false);
        if (signal_flag_) *signal_flag_ = 0;
    }
    bool requested() const noexcept {
        return requested_.load() || (signal_flag_ && *signal_flag_ != 0);
    }

private:
    std::atomic<bool> requested_{false};
    volatile std::sig_atomic_t* signal_flag_;
};

} // namespace opencode

#endif
