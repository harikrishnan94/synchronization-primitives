#pragma once

#include "common.h"

namespace sync_prim::mutex {
template <bool EnableDeadlockDetection> class MutexImpl;

using Mutex = MutexImpl<false>;
using DeadlockSafeMutex = MutexImpl<true>;

template <bool EnableDeadlockDetection> class MutexImpl {
private:
  static inline auto parkinglot = folly::ParkingLot<std::nullptr_t>{};
  static inline auto dead_lock_verify_mutex = std::mutex{};
  static inline auto thread_waiting_on =
      EnableDeadlockDetection
          ? std::make_unique<std::atomic<const MutexImpl *>[]>(
                sync_prim::ThreadRegistry::MAX_THREADS)
          : nullptr;

  using thread_id_t = ThreadRegistry::thread_id_t;

  class LockWord {
    enum class LockState : int8_t { LS_UNLOCKED, LS_LOCKED, LS_CONTENTED };

    static constexpr thread_id_t M_CONTENDED_MASK =
        1 << (sizeof(thread_id_t) * CHAR_BIT - 1);
    static constexpr thread_id_t M_UNLOCKED = -1 & ~M_CONTENDED_MASK;

  public:
    using WordType =
        std::conditional_t<EnableDeadlockDetection, thread_id_t, LockState>;

  private:
    LockWord(WordType a_word) : word(a_word) {}

  public:
    WordType word;

    static LockWord get_unlocked_word() {
      if constexpr (EnableDeadlockDetection)
        return M_UNLOCKED;
      else
        return LockState::LS_UNLOCKED;
    }

    WordType get_value() const { return word; }

    bool is_locked() const { return word != get_unlocked_word().get_value(); }

    bool is_lock_contented() const {
      if constexpr (EnableDeadlockDetection)
        return (word & M_CONTENDED_MASK) == 0;
      else
        return word == LockState::LS_CONTENTED;
    }

    LockWord as_uncontented_word() {
      if constexpr (EnableDeadlockDetection)
        return word & ~M_CONTENDED_MASK;
      else
        return LockState::LS_LOCKED;
    }

    static LockWord get_contented_word() {
      if constexpr (EnableDeadlockDetection)
        return ThreadRegistry::ThreadID() | M_CONTENDED_MASK;
      else
        return LockState::LS_CONTENTED;
    }

    static LockWord get_lock_word() {
      if constexpr (EnableDeadlockDetection)
        return ThreadRegistry::ThreadID();
      else
        return LockState::LS_LOCKED;
    }
  };

  std::atomic<LockWord> word{LockWord::get_unlocked_word()};

  bool check_deadlock() const {
    if constexpr (EnableDeadlockDetection) {
      std::unordered_map<thread_id_t, const DeadlockSafeMutex *> waiters;

      auto detect_deadlock = [&]() {
        const DeadlockSafeMutex *waiting_on = this;

        waiters[ThreadRegistry::ThreadID()] = waiting_on;

        while (true) {
          auto lock_holder = waiting_on->word.load().as_uncontented_word();

          /* Lock was just released.. */
          if (!lock_holder.is_locked())
            return false;

          waiting_on = thread_waiting_on[lock_holder.get_value()];

          /* lock holder is live, so not a dead lock */
          if (waiting_on == nullptr)
            return false;

          /* Found a cycle, so deadlock */
          if (waiters.count(lock_holder.get_value()) != 0)
            return true;

          waiters[lock_holder.get_value()] = waiting_on;
        }
      };

      auto verify_deadlock = [&]() {
        std::lock_guard<std::mutex> dead_lock_verify_lock{
            dead_lock_verify_mutex};

        for (const auto &waiter : waiters) {
          if (waiter.second != thread_waiting_on[waiter.first])
            return false;
        }

        denounce_wait();

        return true;
      };

      return detect_deadlock() && verify_deadlock();
    }
    return false;
  }

  bool park() const {
    if constexpr (EnableDeadlockDetection) {
      using namespace std::chrono_literals;
      static auto DEADLOCK_DETECT_TIMEOUT = 1s;

      announce_wait();

      auto res = parkinglot.park_for(this, nullptr,
                                     [&]() { return is_lock_contented(); },
                                     []() {}, DEADLOCK_DETECT_TIMEOUT);

      if (res == folly::ParkResult::Timeout && check_deadlock())
        return true;

      denounce_wait();
    } else {
      parkinglot.park(this, nullptr, [&]() { return is_lock_contented(); },
                      []() {});
    }

    return false;
  }

  void announce_wait() const {
    if constexpr (EnableDeadlockDetection)
      thread_waiting_on[ThreadRegistry::ThreadID()] = this;
  }

  void denounce_wait() const {
    if constexpr (EnableDeadlockDetection)
      thread_waiting_on[ThreadRegistry::ThreadID()] = nullptr;
  }

  bool is_lock_contented() const { return word.load().is_lock_contented(); }

  bool uncontended_path_available() {
    while (true) {
      auto old = word.load();

      if (!old.is_locked())
        return true;

      if (old.is_lock_contented() ||
          word.compare_exchange_strong(old, old.get_contented_word())) {
        return false;
      }

      _mm_pause();
    }
  }

  bool try_lock_contended() {
    auto old = LockWord::get_unlocked_word();

    return word.compare_exchange_strong(old, LockWord::get_contented_word());
  }

  MutexLockResult lock_contended() {
    while (!try_lock_contended()) {
      if (park())
        return MutexLockResult::DEADLOCKED;
    };

    return MutexLockResult::LOCKED;
  }

public:
  static constexpr bool DEADLOCK_SAFE = EnableDeadlockDetection;

  MutexImpl() = default;
  MutexImpl(MutexImpl &&) = delete;
  MutexImpl(const MutexImpl &) = delete;

  bool try_lock() {
    auto old = LockWord::get_unlocked_word();

    return word.compare_exchange_strong(old, LockWord::get_lock_word());
  }

  bool is_locked() const { return word.load().is_locked(); }

  MutexLockResult lock() {
    while (!try_lock()) {
      if (!uncontended_path_available())
        return lock_contended();

      _mm_pause();
    }

    assert(is_locked());

    return MutexLockResult::LOCKED;
  }

  void unlock() {
    auto old = word.exchange(LockWord::get_unlocked_word());

    if (old.is_lock_contented())
      parkinglot.unpark(this,
                        [](auto) { return folly::UnparkControl::RemoveBreak; });
  }
};

} // namespace sync_prim::mutex
