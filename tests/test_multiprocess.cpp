// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Real OS process tests. Every claim here is produced by spawning an actual
// child process, killing it, restarting it, and observing what the parent
// sees. The bounded waits below are transport and process deadlines that turn
// a wedged child into a reported failure with diagnostics; they are not test
// timeouts and they never mask a hang by passing.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

#ifndef CFN_STORE_PROBE_PATH
#error "CFN_STORE_PROBE_PATH must be defined by the build"
#endif
#ifndef CFN_EVIDENCE_NODE_PATH
#error "CFN_EVIDENCE_NODE_PATH must be defined by the build"
#endif

namespace {

constexpr Duration kChildDeadline = Duration::from_seconds(60);

[[nodiscard]] std::uint64_t field_of(const std::string& line, const std::string& key) {
  const std::string needle = key + "=";
  const std::size_t position = line.find(needle);
  if (position == std::string::npos) {
    return 0;
  }
  return std::strtoull(line.c_str() + position + needle.size(), nullptr, 10);
}

[[nodiscard]] bool run_probe(TestContext& context, const std::vector<std::string>& arguments,
                             std::string& output, int& exit_code) {
  std::fflush(stdout);
  auto child = test::ChildProcess::spawn(CFN_STORE_PROBE_PATH, arguments);
  std::fflush(stdout);
  if (!child) {
    context.add_note(child.error().to_text());
    return false;
  }
  std::string line;
  output.clear();
  std::fflush(stdout);
  while (child->read_line(line, kChildDeadline)) {
    output.append(line);
    output.push_back('\n');
  }
  std::fflush(stdout);
  if (!child->wait_for_exit(kChildDeadline, exit_code)) {
    context.add_note("child process did not exit within the deadline");
    return false;
  }
  std::fflush(stdout);
  return true;
}

[[nodiscard]] std::uint16_t parse_port(const std::string& line) {
  const std::string needle = "LISTENING ";
  const std::size_t position = line.find(needle);
  if (position == std::string::npos) {
    return 0;
  }
  return static_cast<std::uint16_t>(std::strtoul(line.c_str() + position + needle.size(), nullptr, 10));
}

}  // namespace

CFN_TEST(multiprocess, probe_writes_and_a_fresh_process_reads) {
  cfn::test::TempDirectory directory("cfn-probe");
  std::string output;
  int exit_code = 0;
  CFN_CHECK(run_probe(cfn_ctx, {"write", directory.path().string(), "3", "clean"}, output, exit_code));
  CFN_CHECK_EQ(exit_code, 0);
  CFN_CHECK(output.find("wrote=3") != std::string::npos);

  CFN_CHECK(run_probe(cfn_ctx, {"open", directory.path().string()}, output, exit_code));
  CFN_CHECK_EQ(exit_code, 0);
  CFN_CHECK_EQ(field_of(output, "models"), 3ULL);
  CFN_CHECK_EQ(field_of(output, "history"), 3ULL);
  CFN_CHECK_EQ(field_of(output, "policies"), 1ULL);
  CFN_CHECK_EQ(field_of(output, "epoch_advanced"), 0ULL);
  CFN_NOTE(output);
}

CFN_TEST(multiprocess, killed_writer_advances_the_epoch_and_recovers_committed_records) {
  cfn::test::TempDirectory directory("cfn-probe-crash");
  std::string output;
  int exit_code = 0;

  CFN_CHECK(run_probe(cfn_ctx, {"write", directory.path().string(), "2", "clean"}, output, exit_code));
  CFN_CHECK_EQ(exit_code, 0);
  CFN_CHECK(run_probe(cfn_ctx, {"open", directory.path().string()}, output, exit_code));
  CFN_CHECK_EQ(field_of(output, "current_epoch"), 1ULL);

  // This child dies without closing the store.
  (void)run_probe(cfn_ctx, {"write", directory.path().string(), "4", "crash"}, output, exit_code);
  CFN_CHECK(exit_code != 0);

  CFN_CHECK(run_probe(cfn_ctx, {"open", directory.path().string()}, output, exit_code));
  CFN_CHECK_EQ(exit_code, 0);
  CFN_CHECK_EQ(field_of(output, "epoch_advanced"), 1ULL);
  CFN_CHECK_EQ(field_of(output, "previous_epoch"), 1ULL);
  CFN_CHECK_EQ(field_of(output, "current_epoch"), 2ULL);
  CFN_CHECK_EQ(field_of(output, "models"), 4ULL);
  CFN_CHECK(field_of(output, "previous_boot") < field_of(output, "current_boot"));
  CFN_NOTE(output);
}

CFN_TEST(multiprocess, live_authority_does_not_survive_a_process_kill) {
  cfn::test::TempDirectory directory("cfn-probe-authority");
  std::string output;
  int exit_code = 0;
  (void)run_probe(cfn_ctx, {"grant", directory.path().string(), "publisher-1", "crash"}, output, exit_code);
  CFN_CHECK(exit_code != 0);

  CFN_CHECK(run_probe(cfn_ctx, {"check", directory.path().string(), "publisher-1"}, output, exit_code));
  CFN_CHECK_EQ(exit_code, 0);
  CFN_CHECK_EQ(field_of(output, "persisted"), 1ULL);
  CFN_CHECK_EQ(field_of(output, "valid"), 0ULL);
  CFN_CHECK_EQ(field_of(output, "fenced"), 1ULL);
  CFN_NOTE(output);
}

CFN_TEST(multiprocess, repeated_restarts_keep_advancing_the_epoch) {
  cfn::test::TempDirectory directory("cfn-probe-epochs");
  std::string output;
  int exit_code = 0;
  for (int round = 0; round < 3; ++round) {
    (void)run_probe(cfn_ctx, {"write", directory.path().string(), "1", "crash"}, output, exit_code);
    CFN_CHECK(exit_code != 0);
  }
  CFN_CHECK(run_probe(cfn_ctx, {"open", directory.path().string()}, output, exit_code));
  CFN_CHECK_EQ(exit_code, 0);
  CFN_CHECK_EQ(field_of(output, "epoch_advanced"), 1ULL);
  CFN_CHECK(field_of(output, "current_epoch") >= 3ULL);
  // Each round rewrites the same model identity, so the map holds one entry;
  // every round appends a history record.
  CFN_CHECK(field_of(output, "models") >= 1ULL);
  CFN_CHECK_EQ(field_of(output, "history"), 3ULL);
  CFN_NOTE(output);
}

CFN_TEST(multiprocess, evidence_is_fetched_over_real_framed_transport) {
  auto child = test::ChildProcess::spawn(CFN_EVIDENCE_NODE_PATH,
                                         {"--port", "0", "--epoch", "1", "--generation", "1",
                                          "--resources", "6"});
  CFN_REQUIRE_OK(cfn_ctx, child);
  std::string line;
  CFN_CHECK(child->read_line(line, kChildDeadline));
  const std::uint16_t port = parse_port(line);
  CFN_CHECK(port != 0);
  CFN_NOTE(line);

  net::EvidenceClientOptions options;
  options.port = port;
  options.max_records = 32;
  options.required_epoch = FabricEpoch::from_value(1);
  options.required_generation = Generation::from_value(1);
  const auto fetched = net::fetch_evidence(options);
  CFN_REQUIRE_OK(cfn_ctx, fetched);
  CFN_CHECK_EQ(fetched->records_accepted, 6U);
  CFN_CHECK_EQ(fetched->records_rejected_stale, 0U);
  CFN_CHECK_EQ(fetched->response.records.size(), 6U);
  CFN_CHECK_EQ(fetched->response.server_epoch.value(), 1ULL);

  child->kill();
  int exit_code = 0;
  CFN_CHECK(child->wait_for_exit(kChildDeadline, exit_code));
}

CFN_TEST(multiprocess, evidence_client_rejects_an_older_fabric_epoch) {
  auto child = test::ChildProcess::spawn(CFN_EVIDENCE_NODE_PATH,
                                         {"--port", "0", "--epoch", "1", "--generation", "1",
                                          "--resources", "4"});
  CFN_REQUIRE_OK(cfn_ctx, child);
  std::string line;
  CFN_CHECK(child->read_line(line, kChildDeadline));
  const std::uint16_t port = parse_port(line);
  CFN_CHECK(port != 0);

  net::EvidenceClientOptions options;
  options.port = port;
  options.required_epoch = FabricEpoch::from_value(2);
  CFN_REQUIRE_ERROR(cfn_ctx, net::fetch_evidence(options), ErrorCode::StaleEvidence);

  net::EvidenceClientOptions advanced;
  advanced.port = port;
  advanced.required_epoch = FabricEpoch::from_value(1);
  advanced.required_generation = Generation::from_value(2);
  CFN_REQUIRE_ERROR(cfn_ctx, net::fetch_evidence(advanced), ErrorCode::StaleEvidence);

  net::EvidenceClientOptions current;
  current.port = port;
  current.required_epoch = FabricEpoch::from_value(1);
  current.required_generation = Generation::from_value(1);
  CFN_REQUIRE_OK(cfn_ctx, net::fetch_evidence(current));

  child->kill();
  int exit_code = 0;
  CFN_CHECK(child->wait_for_exit(kChildDeadline, exit_code));
}

CFN_TEST(multiprocess, evidence_records_bound_to_older_resource_generations_are_rejected) {
  auto child = test::ChildProcess::spawn(CFN_EVIDENCE_NODE_PATH,
                                         {"--port", "0", "--epoch", "1", "--generation", "1",
                                          "--resources", "4"});
  CFN_REQUIRE_OK(cfn_ctx, child);
  std::string line;
  CFN_CHECK(child->read_line(line, kChildDeadline));
  const std::uint16_t port = parse_port(line);
  CFN_CHECK(port != 0);

  net::EvidenceClientOptions options;
  options.port = port;
  const auto known = ResourceId::parse("ev-0");
  CFN_CHECK(known.has_value());
  options.known_resource_generations[*known] = Generation::from_value(5);
  const auto fetched = net::fetch_evidence(options);
  CFN_REQUIRE_OK(cfn_ctx, fetched);
  CFN_CHECK_EQ(fetched->records_rejected_stale, 1U);
  CFN_CHECK_EQ(fetched->records_accepted, 3U);

  child->kill();
  int exit_code = 0;
  CFN_CHECK(child->wait_for_exit(kChildDeadline, exit_code));
}

CFN_TEST(multiprocess, killed_evidence_node_surfaces_as_a_transport_error) {
  auto child = test::ChildProcess::spawn(CFN_EVIDENCE_NODE_PATH,
                                         {"--port", "0", "--epoch", "1", "--resources", "2"});
  CFN_REQUIRE_OK(cfn_ctx, child);
  std::string line;
  CFN_CHECK(child->read_line(line, kChildDeadline));
  const std::uint16_t port = parse_port(line);
  CFN_CHECK(port != 0);
  child->kill();
  int exit_code = 0;
  CFN_CHECK(child->wait_for_exit(kChildDeadline, exit_code));

  net::EvidenceClientOptions options;
  options.port = port;
  options.connect_timeout = Duration::from_seconds(5);
  const auto fetched = net::fetch_evidence(options);
  CFN_CHECK(!fetched);
}

CFN_TEST(multiprocess, evidence_round_trip_survives_a_restart_with_a_new_epoch) {
  cfn::test::TempDirectory directory("cfn-evidence-restart");
  (void)directory;
  std::string line;
  {
    auto child = test::ChildProcess::spawn(CFN_EVIDENCE_NODE_PATH,
                                           {"--port", "0", "--epoch", "3", "--generation", "7",
                                            "--resources", "3"});
    CFN_REQUIRE_OK(cfn_ctx, child);
    CFN_CHECK(child->read_line(line, kChildDeadline));
    const std::uint16_t port = parse_port(line);
    CFN_CHECK(port != 0);
    net::EvidenceClientOptions options;
    options.port = port;
    options.required_epoch = FabricEpoch::from_value(3);
    options.required_generation = Generation::from_value(7);
    const auto fetched = net::fetch_evidence(options);
    CFN_REQUIRE_OK(cfn_ctx, fetched);
    CFN_CHECK_EQ(fetched->records_accepted, 3U);
    child->kill();
    int exit_code = 0;
    CFN_CHECK(child->wait_for_exit(kChildDeadline, exit_code));
  }
  {
    auto restarted = test::ChildProcess::spawn(CFN_EVIDENCE_NODE_PATH,
                                               {"--port", "0", "--epoch", "4", "--generation", "8",
                                                "--resources", "3"});
    CFN_REQUIRE_OK(cfn_ctx, restarted);
    CFN_CHECK(restarted->read_line(line, kChildDeadline));
    const std::uint16_t port = parse_port(line);
    CFN_CHECK(port != 0);
    net::EvidenceClientOptions options;
    options.port = port;
    options.required_epoch = FabricEpoch::from_value(4);
    options.required_generation = Generation::from_value(8);
    const auto fetched = net::fetch_evidence(options);
    CFN_REQUIRE_OK(cfn_ctx, fetched);
    CFN_CHECK_EQ(fetched->response.server_epoch.value(), 4ULL);
    CFN_CHECK_EQ(fetched->response.evidence_generation.value(), 8ULL);
    restarted->kill();
    int exit_code = 0;
    CFN_CHECK(restarted->wait_for_exit(kChildDeadline, exit_code));
  }
}

CFN_TEST(multiprocess, aggregate_evidence_ingestion_validates_bindings) {
  auto child = test::ChildProcess::spawn(CFN_EVIDENCE_NODE_PATH,
                                         {"--port", "0", "--epoch", "2", "--generation", "5",
                                          "--resources", "5", "--capacity", "250"});
  CFN_REQUIRE_OK(cfn_ctx, child);
  std::string line;
  CFN_CHECK(child->read_line(line, kChildDeadline));
  const std::uint16_t port = parse_port(line);
  CFN_CHECK(port != 0);

  net::EvidenceClientOptions options;
  options.port = port;
  options.required_epoch = FabricEpoch::from_value(2);
  options.required_generation = Generation::from_value(5);
  const auto fetched = net::fetch_evidence(options);
  CFN_REQUIRE_OK(cfn_ctx, fetched);
  CFN_CHECK_EQ(fetched->records_accepted, 5U);
  std::uint64_t total = 0;
  for (const net::EvidenceRecord& record : fetched->response.records) {
    total += record.capacity.units;
    CFN_CHECK(record.provenance.authoritative);
    CFN_CHECK_EQ(static_cast<int>(record.evidence_class), static_cast<int>(EvidenceClass::Measured));
  }
  CFN_CHECK_EQ(total, 250ULL * 5ULL + 10ULL * 10ULL);

  child->kill();
  int exit_code = 0;
  CFN_CHECK(child->wait_for_exit(kChildDeadline, exit_code));
}