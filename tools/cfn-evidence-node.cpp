// Capacity Fabric Network - capacity evidence node.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A real OS process that serves capacity evidence over real framed TCP. The
// population it serves is SYNTHETIC: it is generated from a seed and models no
// physical network. It exists so the evidence protocol, the epoch and
// generation binding, and process kill/restart behaviour can be exercised
// against genuine separate processes.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "cfn/cfn.hpp"

namespace {

class Provider final : public cfn::net::EvidenceProvider {
 public:
  cfn::FabricEpoch fabric_epoch;
  cfn::BootIncarnation incarnation;
  cfn::Generation evidence_generation_value;
  cfn::Provenance provenance_value;
  std::vector<cfn::net::EvidenceRecord> population;

  [[nodiscard]] cfn::FabricEpoch epoch() const override { return fabric_epoch; }
  [[nodiscard]] cfn::BootIncarnation boot() const override { return incarnation; }
  [[nodiscard]] cfn::Generation evidence_generation() const override {
    return evidence_generation_value;
  }
  [[nodiscard]] std::uint32_t record_count() const override {
    return static_cast<std::uint32_t>(population.size());
  }
  [[nodiscard]] cfn::Provenance provenance() const override { return provenance_value; }

  [[nodiscard]] std::vector<cfn::net::EvidenceRecord> fetch(cfn::Generation since,
                                                            std::uint32_t max_records) const override {
    std::vector<cfn::net::EvidenceRecord> selected;
    for (const cfn::net::EvidenceRecord& record : population) {
      if (record.resource_generation.is_older_than(since)) {
        continue;
      }
      if (selected.size() >= max_records) {
        break;
      }
      selected.push_back(record);
    }
    return selected;
  }
};

[[nodiscard]] std::uint64_t argument(int argc, char** argv, const char* name, std::uint64_t fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::strcmp(argv[index], name) == 0) {
      return std::strtoull(argv[index + 1], nullptr, 10);
    }
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  const cfn::Outcome<void> networking = cfn::net::initialize_networking();
  if (!networking) {
    std::printf("error=%s\n", networking.error().to_text().c_str());
    return 1;
  }
  const std::uint64_t port = argument(argc, argv, "--port", 0);
  const std::uint64_t epoch = argument(argc, argv, "--epoch", 1);
  const std::uint64_t generation = argument(argc, argv, "--generation", 1);
  const std::uint64_t resources = argument(argc, argv, "--resources", 8);
  const std::uint64_t stop_after = argument(argc, argv, "--stop-after", 0);
  const std::uint64_t capacity = argument(argc, argv, "--capacity", 1000);

  Provider provider;
  provider.fabric_epoch = cfn::FabricEpoch::from_value(epoch == 0 ? 1 : epoch);
  provider.incarnation = cfn::BootIncarnation::initial();
  provider.evidence_generation_value = cfn::Generation::from_value(generation == 0 ? 1 : generation);
  const auto source = cfn::EvidenceSourceId::parse("evidence-node");
  if (source.has_value()) {
    provider.provenance_value =
        cfn::observed_provenance(*source, cfn::ProvenanceSource::TelemetryCollector,
                                 cfn::Timestamp::from_unix_seconds(1767225600LL), cfn::Duration{});
  }
  for (std::uint64_t index = 0; index < resources; ++index) {
    cfn::net::EvidenceRecord record;
    const auto id = cfn::ResourceId::parse("ev-" + std::to_string(index));
    if (!id.has_value()) {
      return 1;
    }
    record.resource = *id;
    record.resource_generation = cfn::Generation::from_value(generation == 0 ? 1 : generation);
    record.evidence_generation = provider.evidence_generation_value;
    record.capacity = cfn::Capacity::from_units(capacity + (index * 10));
    record.evidence_class = cfn::EvidenceClass::Measured;
    record.provenance = provider.provenance_value;
    provider.population.push_back(record);
  }

  cfn::net::EvidenceServer::Options options;
  options.bind_host = "127.0.0.1";
  options.port = static_cast<std::uint16_t>(port);
  cfn::Outcome<std::unique_ptr<cfn::net::EvidenceServer>> server =
      cfn::net::EvidenceServer::start(options, provider);
  if (!server) {
    std::printf("error=%s\n", server.error().to_text().c_str());
    return 1;
  }
  std::printf("LISTENING %u epoch=%llu generation=%llu records=%llu\n", (*server)->port(),
              static_cast<unsigned long long>(provider.fabric_epoch.value()),
              static_cast<unsigned long long>(provider.evidence_generation_value.value()),
              static_cast<unsigned long long>(provider.population.size()));
  std::fflush(stdout);

  if (stop_after != 0) {
    std::thread watcher([&]() {
      while ((*server)->requests_served() < stop_after) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      (*server)->stop();
    });
    const cfn::Outcome<void> served = (*server)->run();
    watcher.join();
    if (!served) {
      std::printf("error=%s\n", served.error().to_text().c_str());
      return 1;
    }
  } else {
    const cfn::Outcome<void> served = (*server)->run();
    if (!served) {
      std::printf("error=%s\n", served.error().to_text().c_str());
      return 1;
    }
  }
  std::printf("SERVED connections=%llu requests=%llu rejected=%llu\n",
              static_cast<unsigned long long>((*server)->connections_served()),
              static_cast<unsigned long long>((*server)->requests_served()),
              static_cast<unsigned long long>((*server)->messages_rejected()));
  return 0;
}
