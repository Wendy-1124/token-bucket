/*
 * basic_usage.cpp
 * Demonstrates basic usage of the TokenBucket rate limiter.
 */

#include <iostream>
#include <thread>
#include <chrono>

#include "token_bucket.hpp"

// Example 1: Simple rate limiting
void example_simple_rate_limiting() {
  std::cout << "\n=== Example 1: Simple Rate Limiting ===" << std::endl;

  // Create a limiter that allows 10 requests per second
  // with a burst capacity of 5 requests
  token_bucket::TokenBucket limiter(10.0, 5.0);

  for (int i = 0; i < 12; ++i) {
    auto [success, busy] = limiter.Acquire(1.0, 100);  // Try to acquire 1 token, wait up to 100ms

    if (success) {
      std::cout << "Request " << i << ": Allowed";
      if (busy) {
        std::cout << " (busy)";
      }
      std::cout << std::endl;
    } else {
      std::cout << "Request " << i << ": Rejected (overloaded)" << std::endl;
    }
  }
}

// Example 2: Server-side request throttling
void example_server_throttling() {
  std::cout << "\n=== Example 2: Server Throttling ===" << std::endl;

  // Simulate a server with max 100 QPS, burst=20, busy threshold=80
  token_bucket::TokenBucket server_limiter(100.0, 20.0, 80.0);

  auto process_request = [&]() {
    auto [success, busy] = server_limiter.Acquire(1.0, 10);

    if (!success) {
      // Hard reject: server is overloaded
      std::cout << "503 Service Unavailable" << std::endl;
      return;
    }

    if (busy) {
      // Soft signal: server is busy, client should backoff
      std::cout << "200 OK (Server: Busy, consider backoff)" << std::endl;
    } else {
      // Normal operation
      std::cout << "200 OK" << std::endl;
    }
  };

  // Simulate incoming requests
  for (int i = 0; i < 10; ++i) {
    std::cout << "Processing request " << i << ": ";
    process_request();
  }
}

// Example 3: Custom token consumption
void example_custom_token_consumption() {
  std::cout << "\n=== Example 3: Custom Token Consumption ===" << std::endl;

  // Different operations consume different number of tokens
  token_bucket::TokenBucket limiter(1000.0, 50.0);

  struct Operation {
    std::string name;
    double tokens;
  };

  Operation ops[] = {
      {"GET /api/users", 1.0},         // Cheap operation
      {"POST /api/data", 5.0},         // Moderate operation
      {"BATCH /import", 20.0},         // Expensive operation
  };

  for (const auto& op : ops) {
    auto [success, busy] = limiter.Acquire(op.tokens, 50);

    if (success) {
      std::cout << op.name << ": Allowed (consumed " << op.tokens << " tokens)"
                << std::endl;
    } else {
      std::cout << op.name << ": Rejected (insufficient tokens)" << std::endl;
    }
  }
}

// Example 4: Adaptive backoff
void example_adaptive_backoff() {
  std::cout << "\n=== Example 4: Adaptive Backoff ===" << std::endl;

  token_bucket::TokenBucket limiter(50.0, 10.0, 40.0);

  auto backoff_delay = [](int attempt) {
    // Exponential backoff: 10ms, 20ms, 40ms, 80ms...
    return (1 << attempt) * 10;
  };

  for (int attempt = 0; attempt < 5; ++attempt) {
    auto [success, busy] = limiter.Acquire(8.0, 100);

    if (success) {
      std::cout << "Attempt " << attempt << ": Success" << std::endl;
      if (busy) {
        std::cout << "  Server is busy, slowing down client..." << std::endl;
      }
      break;
    } else {
      int delay = backoff_delay(attempt);
      std::cout << "Attempt " << attempt << ": Failed, waiting " << delay
                << "ms before retry..." << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
  }
}

// Example 5: Multi-tier rate limiting
void example_multi_tier_limiting() {
  std::cout << "\n=== Example 5: Multi-Tier Rate Limiting ===" << std::endl;

  // Different rate limits for different user tiers
  token_bucket::TokenBucket free_tier(10.0, 5.0);      // 10 QPS
  token_bucket::TokenBucket premium_tier(100.0, 20.0); // 100 QPS

  enum class UserTier { Free, Premium };

  auto handle_request = [&free_tier, &premium_tier](UserTier tier) {
    auto& limiter = (tier == UserTier::Free) ? free_tier : premium_tier;
    auto [success, busy] = limiter.Acquire(1.0, 10);

    std::string tier_name = (tier == UserTier::Free) ? "Free" : "Premium";
    if (success) {
      std::cout << tier_name << " user: Request allowed" << std::endl;
    } else {
      std::cout << tier_name << " user: Rate limit exceeded" << std::endl;
    }
  };

  // Simulate requests from different tiers
  handle_request(UserTier::Free);
  handle_request(UserTier::Premium);
  handle_request(UserTier::Free);
  handle_request(UserTier::Premium);
}

// Example 6: Token bucket visualization
void example_visualization() {
  std::cout << "\n=== Example 6: Token Bucket Behavior ===" << std::endl;
  std::cout << "Limiter: rate=5/sec, capacity=3, threshold=2/sec" << std::endl;
  std::cout << "\nTimeline:" << std::endl;

  token_bucket::TokenBucket limiter(5.0, 3.0, 2.0);

  std::cout << "t=0:   Initial state (3 tokens, full capacity)" << std::endl;
  auto [s1, b1] = limiter.Acquire(1.0, 0);
  std::cout << "       After Acquire(1): " << (s1 ? "✓" : "✗") << std::endl;

  auto [s2, b2] = limiter.Acquire(1.0, 0);
  std::cout << "       After Acquire(1): " << (s2 ? "✓" : "✗") << std::endl;

  auto [s3, b3] = limiter.Acquire(1.0, 0);
  std::cout << "       After Acquire(1): " << (s3 ? "✓" : "✗") << std::endl;

  auto [s4, b4] = limiter.Acquire(1.0, 0);
  std::cout << "       After Acquire(1): " << (s4 ? "✓" : "✗") << " (exhausted)"
            << std::endl;

  std::cout << "\nt=0.2s: Tokens refilled (1 token added = 5/sec * 0.2s)" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto [s5, b5] = limiter.Acquire(1.0, 100);
  std::cout << "       After Acquire(1, timeout=100ms): " << (s5 ? "✓" : "✗")
            << std::endl;
}

int main() {
  std::cout << "Token Bucket Rate Limiter - Examples" << std::endl;
  std::cout << "====================================" << std::endl;

  example_simple_rate_limiting();
  example_server_throttling();
  example_custom_token_consumption();
  example_adaptive_backoff();
  example_multi_tier_limiting();
  example_visualization();

  std::cout << "\n\nAll examples completed!" << std::endl;
  return 0;
}
