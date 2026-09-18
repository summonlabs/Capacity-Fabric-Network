// Capacity Fabric Network - capacity evidence protocol.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The protocol carries authoritative capacity evidence from one real process
// to another over real framed TCP. Every exchange is bound to a fabric epoch,
// a boot incarnation, and an evidence generation; a peer that answers with an
// older epoch, or a record bound to an older resource generation, is rejected
// rather than merged.
#ifndef CFN_NET_EVIDENCE_HPP
#define CFN_NET_EVIDENCE_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cfn/core/bounded.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/core/time.hpp"
#include "cfn/model/resource.hpp"
#include "cfn/net/framing.hpp"
#include "cfn/net/socket.hpp"

namespace cfn::net {

inline constexpr std::uint16_t kEvidenceProtocolVersion = 1;

enum class EvidenceMessageKind : std::uint16_t {
  Invalid = 0,
  Hello = 1,
  HelloAck = 2,
  EvidenceRequest = 3,
  EvidenceResponse = 4,
  Bye = 5,
  Failure = 6,
};

[[nodiscard]] CFN_API std::string_view to_string(EvidenceMessageKind kind) noexcept;

struct EvidenceHello {
  std::uint16_t protocol_version = kEvidenceProtocolVersion;
  FabricEpoch client_epoch;
  BootIncarnation client_boot;
  PublisherId client_id;
};

struct EvidenceHelloAck {
  std::uint16_t protocol_version = kEvidenceProtocolVersion;
  FabricEpoch server_epoch;
  BootIncarnation server_boot;
  Generation evidence_generation;
  std::uint32_t record_count = 0;
  Provenance provenance;
};

struct EvidenceRequestMessage {
  Generation since_generation;
  std::uint32_t max_records = 0;
};

struct EvidenceRecord {
  ResourceId resource;
  Generation resource_generation;
  Generation evidence_generation;
  Capacity capacity;
  EvidenceClass evidence_class = EvidenceClass::Unknown;
  Provenance provenance;
};

struct EvidenceResponseMessage {
  FabricEpoch server_epoch;
  BootIncarnation server_boot;
  Generation evidence_generation;
  std::vector<EvidenceRecord> records;
};

struct EvidenceFailure {
  std::uint16_t code = 0;
  BoundedString<192> text;
};

CFN_API void encode(std::vector<std::byte>& out, const EvidenceHello& message);
CFN_API void encode(std::vector<std::byte>& out, const EvidenceHelloAck& message);
CFN_API void encode(std::vector<std::byte>& out, const EvidenceRequestMessage& message);
CFN_API void encode(std::vector<std::byte>& out, const EvidenceResponseMessage& message, const Limits& limits);
CFN_API void encode(std::vector<std::byte>& out, const EvidenceFailure& message);

[[nodiscard]] CFN_API Outcome<EvidenceHello> decode_hello(std::span<const std::byte> payload);
[[nodiscard]] CFN_API Outcome<EvidenceHelloAck> decode_hello_ack(std::span<const std::byte> payload);
[[nodiscard]] CFN_API Outcome<EvidenceRequestMessage> decode_evidence_request(std::span<const std::byte> payload);
[[nodiscard]] CFN_API Outcome<EvidenceResponseMessage> decode_evidence_response(std::span<const std::byte> payload,
                                                                                const Limits& limits);
[[nodiscard]] CFN_API Outcome<EvidenceFailure> decode_failure(std::span<const std::byte> payload);

/// Sends a message with the kind prefix and framing applied.
[[nodiscard]] CFN_API Outcome<void> send_message(const Socket& socket, EvidenceMessageKind kind,
                                                 std::span<const std::byte> body, const Limits& limits,
                                                 Duration timeout);
[[nodiscard]] CFN_API Outcome<std::vector<std::byte>> receive_message(const Socket& socket, FrameReader& reader,
                                                                     EvidenceMessageKind& kind_out,
                                                                     Duration timeout, const Limits& limits);

struct EvidenceClientOptions {
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;
  Limits limits;
  Duration connect_timeout = Duration::from_seconds(10);
  Duration io_timeout = Duration::from_seconds(10);
  std::uint32_t max_records = 1024;
  Generation since_generation;
  /// Reject any response whose fabric epoch is older than this.
  FabricEpoch required_epoch;
  /// Reject any response whose evidence generation is older than this.
  Generation required_generation;
  PublisherId client_id;
  BootIncarnation client_boot;
  /// Resource generations already known to the client. A record bound to an
  /// older generation is stale and is rejected.
  std::map<ResourceId, Generation> known_resource_generations;
};

struct EvidenceFetchResult {
  EvidenceResponseMessage response;
  std::uint32_t records_accepted = 0;
  std::uint32_t records_rejected_stale = 0;
  std::uint32_t records_rejected_unauthoritative = 0;
  std::uint32_t rounds = 0;
};

/// Fetches evidence in one bounded request/response round trip, validating the
/// epoch, the generation, and every record binding before returning.
[[nodiscard]] CFN_API Outcome<EvidenceFetchResult> fetch_evidence(const EvidenceClientOptions& options);

class CFN_API EvidenceProvider {
 public:
  virtual ~EvidenceProvider();
  [[nodiscard]] virtual FabricEpoch epoch() const = 0;
  [[nodiscard]] virtual BootIncarnation boot() const = 0;
  [[nodiscard]] virtual Generation evidence_generation() const = 0;
  [[nodiscard]] virtual std::uint32_t record_count() const = 0;
  [[nodiscard]] virtual Provenance provenance() const = 0;
  [[nodiscard]] virtual std::vector<EvidenceRecord> fetch(Generation since_generation,
                                                          std::uint32_t max_records) const = 0;
};

class CFN_API EvidenceServer {
 public:
  struct Options {
    std::string bind_host = "127.0.0.1";
    std::uint16_t port = 0;
    Limits limits;
    Duration io_timeout = Duration::from_seconds(10);
    int backlog = 8;
  };

  static Outcome<std::unique_ptr<EvidenceServer>> start(const Options& options, EvidenceProvider& provider);
  ~EvidenceServer();
  EvidenceServer(const EvidenceServer&) = delete;
  EvidenceServer& operator=(const EvidenceServer&) = delete;

  /// Serves until stop() is called. Returns an error only on a listener fault.
  [[nodiscard]] Outcome<void> run();
  void stop() noexcept;

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] std::uint64_t connections_served() const noexcept { return connections_served_; }
  [[nodiscard]] std::uint64_t requests_served() const noexcept { return requests_served_; }
  [[nodiscard]] std::uint64_t messages_rejected() const noexcept { return messages_rejected_; }

 private:
  EvidenceServer(Options options, EvidenceProvider& provider);

  Options options_;
  EvidenceProvider* provider_;
  Socket listener_;
  std::uint16_t port_ = 0;
  std::atomic<bool> stopping_{false};
  std::uint64_t connections_served_ = 0;
  std::uint64_t requests_served_ = 0;
  std::uint64_t messages_rejected_ = 0;
};

}  // namespace cfn::net

#endif  // CFN_NET_EVIDENCE_HPP
