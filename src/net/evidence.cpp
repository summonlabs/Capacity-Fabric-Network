// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/net/evidence.hpp"

#include <algorithm>
#include <utility>

#include "cfn/persist/serialize.hpp"

namespace cfn::net {

namespace {

void write_kind(std::vector<std::byte>& out, EvidenceMessageKind kind) {
  serialize::Writer writer;
  writer.u16(static_cast<std::uint16_t>(kind));
  std::vector<std::byte> prefix = writer.take();
  out.insert(out.begin(), prefix.begin(), prefix.end());
}

void encode_record(serialize::Writer& writer, const EvidenceRecord& record) {
  writer.id(record.resource);
  writer.generation(record.resource_generation);
  writer.generation(record.evidence_generation);
  writer.capacity(record.capacity);
  writer.u8(static_cast<std::uint8_t>(record.evidence_class));
  writer.provenance(record.provenance);
}

[[nodiscard]] bool decode_record(serialize::Reader& reader, EvidenceRecord& record) {
  std::uint8_t klass = 0;
  if (!reader.id(record.resource) || !reader.generation(record.resource_generation) ||
      !reader.generation(record.evidence_generation) || !reader.capacity(record.capacity) ||
      !reader.u8(klass) || !reader.provenance(record.provenance)) {
    return false;
  }
  if (klass > static_cast<std::uint8_t>(EvidenceClass::Imported)) {
    return false;
  }
  record.evidence_class = static_cast<EvidenceClass>(klass);
  return true;
}

[[nodiscard]] Outcome<EvidenceMessageKind> split_kind(std::span<const std::byte> payload,
                                                      std::span<const std::byte>& body) {
  serialize::Reader reader(payload);
  std::uint16_t raw = 0;
  if (!reader.u16(raw)) {
    return Error(ErrorCode::MalformedInput, "message is shorter than its kind prefix");
  }
  if (raw > static_cast<std::uint16_t>(EvidenceMessageKind::Failure)) {
    return Error(ErrorCode::MalformedInput, "message declares an unknown kind");
  }
  body = payload.subspan(2);
  return static_cast<EvidenceMessageKind>(raw);
}

}  // namespace

std::string_view to_string(EvidenceMessageKind kind) noexcept {
  switch (kind) {
    case EvidenceMessageKind::Invalid: return "invalid";
    case EvidenceMessageKind::Hello: return "hello";
    case EvidenceMessageKind::HelloAck: return "hello-ack";
    case EvidenceMessageKind::EvidenceRequest: return "evidence-request";
    case EvidenceMessageKind::EvidenceResponse: return "evidence-response";
    case EvidenceMessageKind::Bye: return "bye";
    case EvidenceMessageKind::Failure: return "failure";
  }
  return "invalid";
}

void encode(std::vector<std::byte>& out, const EvidenceHello& message) {
  serialize::Writer writer;
  writer.u16(message.protocol_version);
  writer.epoch(message.client_epoch);
  writer.boot(message.client_boot);
  writer.id(message.client_id);
  out = writer.take();
  write_kind(out, EvidenceMessageKind::Hello);
}

void encode(std::vector<std::byte>& out, const EvidenceHelloAck& message) {
  serialize::Writer writer;
  writer.u16(message.protocol_version);
  writer.epoch(message.server_epoch);
  writer.boot(message.server_boot);
  writer.generation(message.evidence_generation);
  writer.u32(message.record_count);
  writer.provenance(message.provenance);
  out = writer.take();
  write_kind(out, EvidenceMessageKind::HelloAck);
}

void encode(std::vector<std::byte>& out, const EvidenceRequestMessage& message) {
  serialize::Writer writer;
  writer.generation(message.since_generation);
  writer.u32(message.max_records);
  out = writer.take();
  write_kind(out, EvidenceMessageKind::EvidenceRequest);
}

void encode(std::vector<std::byte>& out, const EvidenceResponseMessage& message, const Limits& limits) {
  const std::size_t count = std::min<std::size_t>(message.records.size(),
                                                  limits.max_evidence_records_per_response);
  serialize::Writer writer;
  writer.epoch(message.server_epoch);
  writer.boot(message.server_boot);
  writer.generation(message.evidence_generation);
  writer.u32(static_cast<std::uint32_t>(count));
  for (std::size_t index = 0; index < count; ++index) {
    encode_record(writer, message.records[index]);
  }
  out = writer.take();
  write_kind(out, EvidenceMessageKind::EvidenceResponse);
}

void encode(std::vector<std::byte>& out, const EvidenceFailure& message) {
  serialize::Writer writer;
  writer.u16(message.code);
  writer.text(message.text.view());
  out = writer.take();
  write_kind(out, EvidenceMessageKind::Failure);
}

Outcome<EvidenceHello> decode_hello(std::span<const std::byte> payload) {
  std::span<const std::byte> body;
  const Outcome<EvidenceMessageKind> kind = split_kind(payload, body);
  if (!kind) {
    return kind.error();
  }
  if (*kind != EvidenceMessageKind::Hello) {
    return Error(ErrorCode::MalformedInput, "expected a hello message");
  }
  serialize::Reader reader(body);
  EvidenceHello message;
  if (!reader.u16(message.protocol_version) || !reader.epoch(message.client_epoch) ||
      !reader.boot(message.client_boot) || !reader.id(message.client_id) || !reader.at_end()) {
    return Error(ErrorCode::MalformedInput, "hello message is malformed");
  }
  return message;
}

Outcome<EvidenceHelloAck> decode_hello_ack(std::span<const std::byte> payload) {
  std::span<const std::byte> body;
  const Outcome<EvidenceMessageKind> kind = split_kind(payload, body);
  if (!kind) {
    return kind.error();
  }
  if (*kind != EvidenceMessageKind::HelloAck) {
    return Error(ErrorCode::MalformedInput, "expected a hello acknowledgement");
  }
  serialize::Reader reader(body);
  EvidenceHelloAck message;
  if (!reader.u16(message.protocol_version) || !reader.epoch(message.server_epoch) ||
      !reader.boot(message.server_boot) || !reader.generation(message.evidence_generation) ||
      !reader.u32(message.record_count) || !reader.provenance(message.provenance) ||
      !reader.at_end()) {
    return Error(ErrorCode::MalformedInput, "hello acknowledgement is malformed");
  }
  return message;
}

Outcome<EvidenceRequestMessage> decode_evidence_request(std::span<const std::byte> payload) {
  std::span<const std::byte> body;
  const Outcome<EvidenceMessageKind> kind = split_kind(payload, body);
  if (!kind) {
    return kind.error();
  }
  if (*kind != EvidenceMessageKind::EvidenceRequest) {
    return Error(ErrorCode::MalformedInput, "expected an evidence request");
  }
  serialize::Reader reader(body);
  EvidenceRequestMessage message;
  if (!reader.generation(message.since_generation) || !reader.u32(message.max_records) ||
      !reader.at_end()) {
    return Error(ErrorCode::MalformedInput, "evidence request is malformed");
  }
  return message;
}

Outcome<EvidenceResponseMessage> decode_evidence_response(std::span<const std::byte> payload,
                                                          const Limits& limits) {
  std::span<const std::byte> body;
  const Outcome<EvidenceMessageKind> kind = split_kind(payload, body);
  if (!kind) {
    return kind.error();
  }
  if (*kind != EvidenceMessageKind::EvidenceResponse) {
    return Error(ErrorCode::MalformedInput, "expected an evidence response");
  }
  serialize::Reader reader(body);
  EvidenceResponseMessage message;
  std::uint32_t count = 0;
  if (!reader.epoch(message.server_epoch) || !reader.boot(message.server_boot) ||
      !reader.generation(message.evidence_generation) || !reader.u32(count)) {
    return Error(ErrorCode::MalformedInput, "evidence response is malformed");
  }
  if (count > limits.max_evidence_records_per_response) {
    return Error(ErrorCode::LimitExceeded, "evidence response declares too many records");
  }
  message.records.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    EvidenceRecord record;
    if (!decode_record(reader, record)) {
      return Error(ErrorCode::MalformedInput, "evidence record is malformed");
    }
    message.records.push_back(record);
  }
  if (!reader.at_end()) {
    return Error(ErrorCode::MalformedInput, "evidence response carries trailing bytes");
  }
  return message;
}

Outcome<EvidenceFailure> decode_failure(std::span<const std::byte> payload) {
  std::span<const std::byte> body;
  const Outcome<EvidenceMessageKind> kind = split_kind(payload, body);
  if (!kind) {
    return kind.error();
  }
  if (*kind != EvidenceMessageKind::Failure) {
    return Error(ErrorCode::MalformedInput, "expected a failure message");
  }
  serialize::Reader reader(body);
  EvidenceFailure message;
  std::string text;
  if (!reader.u16(message.code) || !reader.text(text, decltype(message.text)::capacity) ||
      !reader.at_end()) {
    return Error(ErrorCode::MalformedInput, "failure message is malformed");
  }
  if (!message.text.assign(text)) {
    return Error(ErrorCode::LimitExceeded, "failure text exceeds its bound");
  }
  return message;
}

Outcome<void> send_message(const Socket& socket, EvidenceMessageKind kind,
                           std::span<const std::byte> body, const Limits& limits, Duration timeout) {
  std::vector<std::byte> payload;
  payload.reserve(body.size() + 2);
  serialize::Writer writer;
  writer.u16(static_cast<std::uint16_t>(kind));
  payload = writer.take();
  payload.insert(payload.end(), body.begin(), body.end());
  return send_frame(socket, payload, limits, timeout);
}

Outcome<std::vector<std::byte>> receive_message(const Socket& socket, FrameReader& reader,
                                                EvidenceMessageKind& kind_out, Duration timeout,
                                                const Limits& limits) {
  const Outcome<std::vector<std::byte>> frame = receive_frame(socket, reader, timeout, limits);
  if (!frame) {
    return frame.error();
  }
  std::span<const std::byte> body;
  const Outcome<EvidenceMessageKind> kind = split_kind(*frame, body);
  if (!kind) {
    return kind.error();
  }
  kind_out = *kind;
  return std::vector<std::byte>(body.begin(), body.end());
}

Outcome<EvidenceFetchResult> fetch_evidence(const EvidenceClientOptions& options) {
  CFN_RETURN_IF_ERROR(options.limits.validate());
  if (options.port == 0) {
    return Error(ErrorCode::InvalidArgument, "evidence client needs a port");
  }
  Outcome<Socket> connected = connect_tcp(options.host, options.port, options.connect_timeout);
  if (!connected) {
    return connected.error();
  }
  Socket socket = std::move(*connected);
  FrameReader reader(options.limits);
  EvidenceFetchResult result;

  {
    EvidenceHello hello;
    hello.client_epoch = options.required_epoch;
    hello.client_boot = options.client_boot;
    hello.client_id = options.client_id;
    std::vector<std::byte> payload;
    encode(payload, hello);
    CFN_RETURN_IF_ERROR(send_frame(socket, payload, options.limits, options.io_timeout));
  }

  EvidenceHelloAck ack;
  {
    EvidenceMessageKind kind = EvidenceMessageKind::Invalid;
    const Outcome<std::vector<std::byte>> body =
        receive_message(socket, reader, kind, options.io_timeout, options.limits);
    if (!body) {
      return body.error();
    }
    if (kind == EvidenceMessageKind::Failure) {
      std::vector<std::byte> framed;
      serialize::Writer writer;
      writer.u16(static_cast<std::uint16_t>(kind));
      framed = writer.take();
      framed.insert(framed.end(), body->begin(), body->end());
      const Outcome<EvidenceFailure> failure = decode_failure(framed);
      if (!failure) {
        return failure.error();
      }
      return Error(ErrorCode::TransportError, "the evidence peer refused the session",
                   failure->text.view());
    }
    if (kind != EvidenceMessageKind::HelloAck) {
      return Error(ErrorCode::TransportError, "the evidence peer did not acknowledge the session");
    }
    std::vector<std::byte> framed;
    serialize::Writer writer;
    writer.u16(static_cast<std::uint16_t>(kind));
    framed = writer.take();
    framed.insert(framed.end(), body->begin(), body->end());
    const Outcome<EvidenceHelloAck> decoded = decode_hello_ack(framed);
    if (!decoded) {
      return decoded.error();
    }
    ack = *decoded;
  }
  if (ack.protocol_version != kEvidenceProtocolVersion) {
    return Error(ErrorCode::VersionMismatch, "the evidence peer speaks a different protocol version");
  }
  if (options.required_epoch.valid() && ack.server_epoch.is_older_than(options.required_epoch)) {
    return Error(ErrorCode::StaleEvidence,
                 "the evidence peer reports an older fabric epoch than required");
  }
  if (options.required_generation.valid() &&
      ack.evidence_generation.is_older_than(options.required_generation)) {
    return Error(ErrorCode::StaleEvidence,
                 "the evidence peer reports an older evidence generation than required");
  }

  {
    EvidenceRequestMessage request;
    request.since_generation = options.since_generation;
    request.max_records = options.max_records;
    std::vector<std::byte> payload;
    encode(payload, request);
    CFN_RETURN_IF_ERROR(send_frame(socket, payload, options.limits, options.io_timeout));
  }

  {
    EvidenceMessageKind kind = EvidenceMessageKind::Invalid;
    const Outcome<std::vector<std::byte>> body =
        receive_message(socket, reader, kind, options.io_timeout, options.limits);
    if (!body) {
      return body.error();
    }
    if (kind != EvidenceMessageKind::EvidenceResponse) {
      return Error(ErrorCode::TransportError, "the evidence peer did not answer the request");
    }
    std::vector<std::byte> framed;
    serialize::Writer writer;
    writer.u16(static_cast<std::uint16_t>(kind));
    framed = writer.take();
    framed.insert(framed.end(), body->begin(), body->end());
    const Outcome<EvidenceResponseMessage> decoded = decode_evidence_response(framed, options.limits);
    if (!decoded) {
      return decoded.error();
    }
    result.response = *decoded;
  }
  if (result.response.server_epoch != ack.server_epoch || result.response.server_boot != ack.server_boot) {
    return Error(ErrorCode::EpochMismatch,
                 "the evidence response came from a different incarnation than the acknowledgement");
  }
  if (options.required_epoch.valid() &&
      result.response.server_epoch.is_older_than(options.required_epoch)) {
    return Error(ErrorCode::StaleEvidence, "the evidence response carries an older fabric epoch");
  }

  for (const EvidenceRecord& record : result.response.records) {
    if (!may_be_authoritative(record.evidence_class) || !record.provenance.authoritative) {
      result.records_rejected_unauthoritative += 1U;
      continue;
    }
    if (options.required_generation.valid() &&
        record.evidence_generation.is_older_than(options.required_generation)) {
      result.records_rejected_stale += 1U;
      continue;
    }
    const auto known = options.known_resource_generations.find(record.resource);
    if (known != options.known_resource_generations.end() &&
        record.resource_generation.is_older_than(known->second)) {
      result.records_rejected_stale += 1U;
      continue;
    }
    result.records_accepted += 1U;
  }
  result.rounds = 1;

  {
    std::vector<std::byte> payload;
    serialize::Writer writer;
    (void)writer;
    EvidenceMessageKind kind = EvidenceMessageKind::Bye;
    const Outcome<void> sent = send_message(socket, kind, {}, options.limits, options.io_timeout);
    (void)sent;
  }
  return result;
}

EvidenceProvider::~EvidenceProvider() = default;

EvidenceServer::EvidenceServer(Options options, EvidenceProvider& provider)
    : options_(std::move(options)), provider_(&provider) {}

EvidenceServer::~EvidenceServer() { stop(); }

Outcome<std::unique_ptr<EvidenceServer>> EvidenceServer::start(const Options& options,
                                                               EvidenceProvider& provider) {
  CFN_RETURN_IF_ERROR(options.limits.validate());
  Outcome<std::pair<Socket, std::uint16_t>> listening =
      listen_tcp(options.bind_host, options.port, options.backlog);
  if (!listening) {
    return listening.error();
  }
  std::unique_ptr<EvidenceServer> server(new EvidenceServer(options, provider));
  server->listener_ = std::move(listening->first);
  server->port_ = listening->second;
  return server;
}

void EvidenceServer::stop() noexcept { stopping_.store(true, std::memory_order_release); }

Outcome<void> EvidenceServer::run() {
  const Duration poll = Duration::from_millis(50);
  while (!stopping_.load(std::memory_order_acquire)) {
    bool would_block = false;
    Outcome<Socket> accepted = accept_tcp(listener_, poll, would_block);
    if (!accepted) {
      if (stopping_.load(std::memory_order_acquire)) {
        return Outcome<void>();
      }
      return accepted.error();
    }
    if (would_block) {
      continue;
    }
    Socket connection = std::move(*accepted);
    connections_served_ += 1U;
    FrameReader reader(options_.limits);
    bool finished = false;
    while (!finished) {
      EvidenceMessageKind kind = EvidenceMessageKind::Invalid;
      const Outcome<std::vector<std::byte>> body =
          receive_message(connection, reader, kind, options_.io_timeout, options_.limits);
      if (!body) {
        if (body.code() == ErrorCode::Disconnected || body.code() == ErrorCode::TransportError) {
          break;
        }
        messages_rejected_ += 1U;
        break;
      }
      switch (kind) {
        case EvidenceMessageKind::Hello: {
          std::vector<std::byte> framed;
          serialize::Writer prefix;
          prefix.u16(static_cast<std::uint16_t>(kind));
          framed = prefix.take();
          framed.insert(framed.end(), body->begin(), body->end());
          const Outcome<EvidenceHello> hello = decode_hello(framed);
          if (!hello) {
            messages_rejected_ += 1U;
            finished = true;
            break;
          }
          if (hello->protocol_version != kEvidenceProtocolVersion) {
            EvidenceFailure failure;
            failure.code = 1;
            (void)failure.text.assign("protocol version mismatch");
            std::vector<std::byte> payload;
            encode(payload, failure);
            (void)send_frame(connection, payload, options_.limits, options_.io_timeout);
            messages_rejected_ += 1U;
            finished = true;
            break;
          }
          EvidenceHelloAck ack;
          ack.server_epoch = provider_->epoch();
          ack.server_boot = provider_->boot();
          ack.evidence_generation = provider_->evidence_generation();
          ack.record_count = provider_->record_count();
          ack.provenance = provider_->provenance();
          std::vector<std::byte> payload;
          encode(payload, ack);
          CFN_RETURN_IF_ERROR(send_frame(connection, payload, options_.limits, options_.io_timeout));
          break;
        }
        case EvidenceMessageKind::EvidenceRequest: {
          std::vector<std::byte> framed;
          serialize::Writer prefix;
          prefix.u16(static_cast<std::uint16_t>(kind));
          framed = prefix.take();
          framed.insert(framed.end(), body->begin(), body->end());
          const Outcome<EvidenceRequestMessage> request = decode_evidence_request(framed);
          if (!request) {
            messages_rejected_ += 1U;
            finished = true;
            break;
          }
          const std::uint32_t limit = std::min(request->max_records,
                                               options_.limits.max_evidence_records_per_response);
          EvidenceResponseMessage response;
          response.server_epoch = provider_->epoch();
          response.server_boot = provider_->boot();
          response.evidence_generation = provider_->evidence_generation();
          response.records = provider_->fetch(request->since_generation, limit);
          requests_served_ += 1U;
          std::vector<std::byte> payload;
          encode(payload, response, options_.limits);
          CFN_RETURN_IF_ERROR(send_frame(connection, payload, options_.limits, options_.io_timeout));
          break;
        }
        case EvidenceMessageKind::Bye:
          finished = true;
          break;
        default:
          messages_rejected_ += 1U;
          finished = true;
          break;
      }
    }
  }
  return Outcome<void>();
}

}  // namespace cfn::net
