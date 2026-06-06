#include "dbmesh/routing/routing_policy.h"

#include <atomic>
#include <limits>
#include <random>

namespace dbmesh::routing {

namespace {

std::mt19937& rng() {
  static thread_local std::mt19937 gen{std::random_device{}()};
  return gen;
}

// ── round_robin ───────────────────────────────────────────────────────────
class RoundRobinPolicy final : public RoutingPolicy {
 public:
  BackendID select(const std::vector<std::shared_ptr<BackendEntry>>& c,
                   const RoutingContext&) const override {
    if (c.empty()) return {};
    std::size_t i = counter_.fetch_add(1, std::memory_order_relaxed) % c.size();
    return c[i]->id;
  }
  std::string_view name() const override { return "round_robin"; }

 private:
  mutable std::atomic<std::size_t> counter_{0};
};

// ── random ────────────────────────────────────────────────────────────────
class RandomPolicy final : public RoutingPolicy {
 public:
  BackendID select(const std::vector<std::shared_ptr<BackendEntry>>& c,
                   const RoutingContext&) const override {
    if (c.empty()) return {};
    std::uniform_int_distribution<std::size_t> d(0, c.size() - 1);
    return c[d(rng())]->id;
  }
  std::string_view name() const override { return "random"; }
};

// ── weighted ──────────────────────────────────────────────────────────────
class WeightedPolicy final : public RoutingPolicy {
 public:
  BackendID select(const std::vector<std::shared_ptr<BackendEntry>>& c,
                   const RoutingContext&) const override {
    if (c.empty()) return {};
    long total = 0;
    for (const auto& e : c) total += std::max(1, e->weight);
    std::uniform_int_distribution<long> d(0, total - 1);
    long pick = d(rng());
    for (const auto& e : c) {
      pick -= std::max(1, e->weight);
      if (pick < 0) return e->id;
    }
    return c.back()->id;
  }
  std::string_view name() const override { return "weighted"; }
};

// ── least_connections ─────────────────────────────────────────────────────
class LeastConnectionsPolicy final : public RoutingPolicy {
 public:
  BackendID select(const std::vector<std::shared_ptr<BackendEntry>>& c,
                   const RoutingContext& ctx) const override {
    if (c.empty()) return {};
    if (!ctx.active_connections) {  // no signal — fall back to round-robin
      std::size_t i = counter_.fetch_add(1, std::memory_order_relaxed) % c.size();
      return c[i]->id;
    }
    BackendID best;
    std::size_t best_n = std::numeric_limits<std::size_t>::max();
    for (const auto& e : c) {
      std::size_t n = ctx.active_connections(e->id);
      if (n < best_n) { best_n = n; best = e->id; }
    }
    return best;
  }
  std::string_view name() const override { return "least_connections"; }

 private:
  mutable std::atomic<std::size_t> counter_{0};
};

// ── latency ───────────────────────────────────────────────────────────────
class LatencyPolicy final : public RoutingPolicy {
 public:
  BackendID select(const std::vector<std::shared_ptr<BackendEntry>>& c,
                   const RoutingContext& ctx) const override {
    if (c.empty()) return {};
    if (!ctx.latency_us) {
      std::size_t i = counter_.fetch_add(1, std::memory_order_relaxed) % c.size();
      return c[i]->id;
    }
    BackendID best;
    uint64_t best_lat = std::numeric_limits<uint64_t>::max();
    for (const auto& e : c) {
      uint64_t lat = ctx.latency_us(e->id);
      if (lat < best_lat) { best_lat = lat; best = e->id; }
    }
    return best;
  }
  std::string_view name() const override { return "latency"; }

 private:
  mutable std::atomic<std::size_t> counter_{0};
};

// ── failover ──────────────────────────────────────────────────────────────
// Candidates are in config order; pick the first that is not FAILED/DRAINING.
class FailoverPolicy final : public RoutingPolicy {
 public:
  BackendID select(const std::vector<std::shared_ptr<BackendEntry>>& c,
                   const RoutingContext&) const override {
    if (c.empty()) return {};
    for (const auto& e : c) {
      auto st = e->state.load(std::memory_order_acquire);
      if (st != BackendState::FAILED && st != BackendState::DRAINING)
        return e->id;
    }
    return c.front()->id;  // all degraded — return the highest-priority one
  }
  std::string_view name() const override { return "failover"; }
};

} // namespace

std::unique_ptr<RoutingPolicy> PolicyFactory::create(
    dbmesh::RoutingPolicy policy) {
  switch (policy) {
    case dbmesh::RoutingPolicy::ROUND_ROBIN:
      return std::make_unique<RoundRobinPolicy>();
    case dbmesh::RoutingPolicy::WEIGHTED:
      return std::make_unique<WeightedPolicy>();
    case dbmesh::RoutingPolicy::LEAST_CONNECTIONS:
      return std::make_unique<LeastConnectionsPolicy>();
    case dbmesh::RoutingPolicy::LATENCY:
      return std::make_unique<LatencyPolicy>();
    case dbmesh::RoutingPolicy::RANDOM:
      return std::make_unique<RandomPolicy>();
    case dbmesh::RoutingPolicy::FAILOVER:
      return std::make_unique<FailoverPolicy>();
    case dbmesh::RoutingPolicy::CONSISTENT_HASHING:
      // Planned (future roadmap) — fall back to round-robin for now.
      return std::make_unique<RoundRobinPolicy>();
  }
  return std::make_unique<RoundRobinPolicy>();
}

} // namespace dbmesh::routing
