/*
 * token_bucket.hpp
 *
 * A high-performance token bucket (leaky bucket) rate limiter.
 * Header-only implementation with zero external dependencies.
 *
 * Usage:
 *   token_bucket::TokenBucket limiter(1000, 10, 500);
 *   auto [success, busy] = limiter.Acquire(1, 10);
 *   if (!success) {
 *       // Server is overloaded, backoff and retry
 *   }
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <algorithm>

namespace token_bucket {

/**
 * High-performance token bucket rate limiter.
 *
 * Implements the token bucket algorithm for traffic shaping and rate limiting.
 * Supports concurrent access from multiple threads via std::mutex.
 *
 * Algorithm:
 * - Tokens are added to the bucket at a fixed rate (rate_per_sec).
 * - The bucket has a maximum capacity (burst_size).
 * - When a request arrives, it tries to consume tokens.
 * - If enough tokens exist, the request proceeds immediately.
 * - If not enough tokens, the caller can wait up to timeout_ms.
 * - If waiting time exceeds timeout, the request is rejected.
 *
 * Additionally, the implementation tracks throughput over the last second
 * and returns a "busy" signal if throughput exceeds busy_threshold.
 * This allows clients to implement adaptive backoff strategies.
 */
class TokenBucket {
 public:
  /**
   * Constructor
   *
   * @param rate_per_sec    Tokens added per second (default: 1000)
   * @param capacity        Maximum burst capacity (default: 10)
   * @param busy_threshold  Throughput threshold to signal "busy" (default: 500)
   *
   * Example:
   *   // Allow 1000 QPS, with 10 token burst, mark busy when exceeds 500 QPS
   *   TokenBucket limiter(1000.0, 10.0, 500.0);
   */
  explicit TokenBucket(double rate_per_sec = 1000.0, double capacity = 10.0,
                       double busy_threshold = 500.0) noexcept
      : rate_us_(std::max(1.0, rate_per_sec) / 1000000.0),
        capacity_(std::max(1.0, capacity)),
        busy_threshold_(std::max(1.0, busy_threshold)),
        cur_water_mark_(std::max(1.0, capacity)),
        last_update_time_us_(now_us()),
        rate_in_last_second_(0.0) {}

  /**
   * Try to acquire tokens from the bucket.
   *
   * @param size        Number of tokens to acquire (default: 1.0)
   * @param timeout_ms  Maximum time to wait in milliseconds (default: 0)
   *
   * @return std::pair<bool, bool> where:
   *   - first (success): true if tokens were acquired, false if rejected
   *   - second (busy):   true if throughput exceeds busy_threshold
   *
   * Return values:
   *   {true, false}  - Tokens acquired, server is not busy (normal case)
   *   {true, true}   - Tokens acquired, but server is busy (backoff recommended)
   *   {false, *}     - Tokens not acquired, server is overloaded (must retry)
   *
   * Example:
   *   auto [success, busy] = limiter.Acquire(1, 10);
   *   if (!success) {
   *       // Reject request, server too busy
   *       return kServiceBusy;
   *   }
   *   if (busy) {
   *       // Accept request but inform client to consider backing off
   *       response->set_load(kBusy);
   *   }
   *   // Process the request
   */
  std::pair<bool, bool> Acquire(double size = 1.0,
                                int64_t timeout_ms = 0) noexcept {
    size = std::max(0.0, size);
    int64_t dt_us = 0;

    // Phase 1: Update water mark and check if we need to wait
    {
      std::lock_guard<std::mutex> lg(mutex_);
      int64_t now_us_val = now_us();
      Update(now_us_val);

      if (cur_water_mark_ < 0) {
        // Water mark is negative, tokens are over-committed
        // Calculate how long we need to wait to accumulate enough tokens
        dt_us = static_cast<int64_t>((-cur_water_mark_) / rate_us_) + 1;

        if (dt_us > timeout_ms * 1000LL) {
          // Timeout exceeded, reject the request
          return std::make_pair(false, true);
        }
      }

      // Consume the tokens
      cur_water_mark_ -= size;
    }

    // Phase 2: Wait if necessary (unlocked to allow other threads to proceed)
    if (dt_us > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(dt_us));
    }

    // Phase 3: Check throughput and determine if server is busy
    double rate = 0.0;
    {
      std::lock_guard<std::mutex> lg(mutex_);
      int64_t now_us_val = now_us();
      Update(now_us_val);
      RefreshRate(now_us_val);

      // Record this request in the current 100ms time bucket
      int64_t key = now_us_val / 100000;
      records_[key] += size;

      // Calculate throughput: previous 1-second data + current 100ms bucket
      rate = rate_in_last_second_ + records_[key];
    }

    // Return success with busy flag
    bool is_busy = (rate > busy_threshold_);
    return std::make_pair(true, is_busy);
  }

 private:
  /**
   * Get current time in microseconds using steady_clock.
   * Monotonically increasing and not affected by system clock adjustments.
   */
  static inline int64_t now_us() noexcept {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
               now.time_since_epoch())
        .count();
  }

  /**
   * Update the current water mark by adding tokens accumulated over time.
   * Called within lock_guard to ensure thread safety.
   */
  void Update(int64_t now_us_val) noexcept {
    int64_t dt_us = now_us_val - last_update_time_us_;
    if (dt_us <= 0) {
      return;
    }

    // Add tokens: rate_us_ tokens per microsecond * elapsed time
    // Cap at capacity to prevent overflow
    cur_water_mark_ =
        std::min(capacity_, cur_water_mark_ + rate_us_ * dt_us);
    last_update_time_us_ = now_us_val;
  }

  /**
   * Refresh the throughput calculation over the last 1 second (10 * 100ms buckets).
   * Removes old buckets and recalculates rate_in_last_second_.
   * Called within lock_guard.
   */
  void RefreshRate(int64_t now_us_val) noexcept {
    int64_t key = now_us_val / 100000;
    bool need_refresh_rate = false;

    // Remove buckets older than 1 second (10 buckets * 100ms)
    while (!records_.empty() && records_.begin()->first < key - 10) {
      records_.erase(records_.begin());
      need_refresh_rate = true;
    }

    // Ensure current bucket exists
    if (records_.find(key) == records_.end()) {
      records_[key] = 0.0;
      need_refresh_rate = true;
    }

    // Recalculate throughput if buckets were modified
    if (need_refresh_rate) {
      rate_in_last_second_ = 0.0;
      for (const auto& record : records_) {
        if (record.first < key) {
          rate_in_last_second_ += record.second;
        }
      }
    }
  }

  // Configuration parameters
  double rate_us_;          // Tokens per microsecond
  double capacity_;         // Maximum bucket capacity
  double busy_threshold_;   // Throughput threshold to mark as busy

  // Current state
  double cur_water_mark_;         // Current tokens in bucket
  int64_t last_update_time_us_;   // Last time we updated water mark

  // Throughput tracking (sliding window: 10 buckets of 100ms each)
  std::map<int64_t, double> records_;      // Per-bucket token consumption
  double rate_in_last_second_;              // Sum of last-second buckets

  // Synchronization
  std::mutex mutex_;  // Protects all above state
};

}  // namespace token_bucket
