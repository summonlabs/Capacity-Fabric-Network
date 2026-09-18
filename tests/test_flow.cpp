// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_support.hpp"

#include <vector>

#include "cfn/cfn.hpp"

using namespace cfn;

namespace {

struct Built {
  FlowNetwork network;
  std::uint32_t source = 0;
  std::uint32_t sink = 0;
  std::vector<std::uint32_t> arcs;
};

[[nodiscard]] Built build_diamond(const Limits& limits) {
  Built built{FlowNetwork(limits), 0, 0, {}};
  for (int index = 0; index < 4; ++index) {
    (void)built.network.add_node();
  }
  built.source = 0;
  built.sink = 3;
  built.arcs.push_back(*built.network.add_arc(0, 1, 3, 100));
  built.arcs.push_back(*built.network.add_arc(0, 2, 2, 101));
  built.arcs.push_back(*built.network.add_arc(1, 3, 2, 102));
  built.arcs.push_back(*built.network.add_arc(2, 3, 3, 103));
  built.arcs.push_back(*built.network.add_arc(1, 2, 1, 104));
  return built;
}

}  // namespace

CFN_TEST(flow, max_flow_on_a_known_graph) {
  Limits limits;
  Built built = build_diamond(limits);
  CFN_REQUIRE_OK(cfn_ctx, built.network.build());
  const auto value = built.network.max_flow(built.source, built.sink);
  CFN_REQUIRE_OK(cfn_ctx, value);
  CFN_CHECK_EQ(*value, 5ULL);
  // Flow conservation: everything that leaves the source arrives at the sink.
  std::uint64_t leaving = 0;
  std::uint64_t entering = 0;
  for (std::uint32_t index = 0; index < built.network.arc_count(); ++index) {
    if (built.network.arc_tail(index) == built.source) {
      leaving += built.network.arc_flow(index);
    }
    if (built.network.arc_head(index) == built.sink) {
      entering += built.network.arc_flow(index);
    }
  }
  CFN_CHECK_EQ(leaving, 5ULL);
  CFN_CHECK_EQ(entering, 5ULL);
}

CFN_TEST(flow, max_flow_respects_a_limit) {
  Limits limits;
  Built built = build_diamond(limits);
  CFN_REQUIRE_OK(cfn_ctx, built.network.build());
  const auto value = built.network.max_flow(built.source, built.sink, 2);
  CFN_REQUIRE_OK(cfn_ctx, value);
  CFN_CHECK_EQ(*value, 2ULL);
  const auto remainder = built.network.max_flow(built.source, built.sink);
  CFN_REQUIRE_OK(cfn_ctx, remainder);
  CFN_CHECK_EQ(*remainder, 3ULL);
}

CFN_TEST(flow, residual_reachability_marks_the_source_side) {
  Limits limits;
  Built built = build_diamond(limits);
  CFN_REQUIRE_OK(cfn_ctx, built.network.build());
  CFN_REQUIRE_OK(cfn_ctx, built.network.max_flow(built.source, built.sink));
  const std::vector<std::uint8_t>& reachable = built.network.residual_reachable();
  CFN_CHECK_EQ(reachable.size(), 4U);
  CFN_CHECK_EQ(reachable[0], static_cast<std::uint8_t>(1));
  CFN_CHECK_EQ(reachable[3], static_cast<std::uint8_t>(0));
}

CFN_TEST(flow, zero_capacity_arc_blocks_flow) {
  Limits limits;
  FlowNetwork network(limits);
  (void)network.add_node();
  (void)network.add_node();
  const auto arc = network.add_arc(0, 1, 5, 0);
  CFN_CHECK(arc.has_value());
  CFN_REQUIRE_OK(cfn_ctx, network.build());
  CFN_REQUIRE_OK(cfn_ctx, network.max_flow(0, 1));
  network.set_arc_capacity(*arc, 0);
  network.reset_flows();
  const auto value = network.max_flow(0, 1);
  CFN_REQUIRE_OK(cfn_ctx, value);
  CFN_CHECK_EQ(*value, 0ULL);
}

CFN_TEST(flow, identical_endpoints_yield_zero) {
  Limits limits;
  FlowNetwork network(limits);
  (void)network.add_node();
  CFN_REQUIRE_OK(cfn_ctx, network.build());
  const auto value = network.max_flow(0, 0);
  CFN_REQUIRE_OK(cfn_ctx, value);
  CFN_CHECK_EQ(*value, 0ULL);
}

CFN_TEST(flow, out_of_range_endpoint_is_rejected) {
  Limits limits;
  FlowNetwork network(limits);
  (void)network.add_node();
  CFN_REQUIRE_OK(cfn_ctx, network.build());
  CFN_REQUIRE_ERROR(cfn_ctx, network.max_flow(0, 5), ErrorCode::InvalidArgument);
}

CFN_TEST(flow, unbounded_network_is_rejected) {
  Limits limits;
  FlowNetwork network(limits);
  (void)network.add_node();
  CFN_REQUIRE_ERROR(cfn_ctx, network.max_flow(0, 0), ErrorCode::InvalidState);
  CFN_REQUIRE_OK(cfn_ctx, network.build());
  CFN_REQUIRE_ERROR(cfn_ctx, network.build(), ErrorCode::InvalidState);
}

CFN_TEST(flow, work_budget_is_enforced) {
  Limits limits;
  limits.max_flow_work_units = 1;
  FlowNetwork network(limits);
  for (int index = 0; index < 8; ++index) {
    (void)network.add_node();
  }
  for (int index = 0; index + 1 < 8; ++index) {
    CFN_CHECK(network.add_arc(static_cast<std::uint32_t>(index),
                              static_cast<std::uint32_t>(index + 1), 10, 0)
                  .has_value());
  }
  CFN_REQUIRE_OK(cfn_ctx, network.build());
  CFN_REQUIRE_ERROR(cfn_ctx, network.max_flow(0, 7), ErrorCode::LimitExceeded);
}

CFN_TEST(flow, cancellation_stops_the_solve) {
  Limits limits;
  CancellationToken token;
  token.cancel();
  FlowNetwork network(limits, token);
  (void)network.add_node();
  (void)network.add_node();
  CFN_CHECK(network.add_arc(0, 1, 5, 0).has_value());
  CFN_REQUIRE_OK(cfn_ctx, network.build());
  CFN_REQUIRE_ERROR(cfn_ctx, network.max_flow(0, 1), ErrorCode::Cancelled);
}

CFN_TEST(flow, arc_budget_is_bounded) {
  Limits limits;
  limits.max_topology_edges = 1;
  limits.max_topology_nodes = 1;
  limits.max_resources = 1;
  FlowNetwork network(limits);
  (void)network.add_node();
  (void)network.add_node();
  int accepted = 0;
  for (int index = 0; index < 100; ++index) {
    if (network.add_arc(0, 1, 1, 0).has_value()) {
      ++accepted;
    }
  }
  CFN_CHECK(accepted < 100);
  // Four arcs per declared edge, node and resource, plus a fixed allowance.
  CFN_CHECK(network.arc_count() <= (4U * (1U + 1U + 1U)) + 64U);
}