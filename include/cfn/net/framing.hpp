// Capacity Fabric Network - length and checksum framed transport.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Frame layout: 4 byte little endian payload length, 4 byte CRC32C of the
// payload, then the payload. A frame longer than the configured maximum is a
// protocol violation, not something to buffer and hope for.
#ifndef CFN_NET_FRAMING_HPP
#define CFN_NET_FRAMING_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/time.hpp"
#include "cfn/net/socket.hpp"

namespace cfn::net {

inline constexpr std::size_t kFrameHeaderBytes = 8;

[[nodiscard]] CFN_API Outcome<std::vector<std::byte>> encode_frame(std::span<const std::byte> payload,
                                                                   const Limits& limits);

/// Incremental decoder. Bytes may arrive in any fragmentation; the decoder
/// emits whole frames only, rejects oversized declared lengths, and rejects a
/// payload whose checksum does not match.
class CFN_API FrameReader {
 public:
  explicit FrameReader(const Limits& limits);

  [[nodiscard]] Outcome<void> feed(std::span<const std::byte> chunk,
                                   std::vector<std::vector<std::byte>>& frames_out);

  [[nodiscard]] bool failed() const noexcept { return failed_; }
  [[nodiscard]] std::size_t buffered() const noexcept { return buffer_.size(); }
  [[nodiscard]] std::uint64_t frames_decoded() const noexcept { return frames_decoded_; }
  void reset();

 private:
  Limits limits_;
  std::vector<std::byte> buffer_;
  std::size_t consumed_ = 0;
  std::uint64_t frames_decoded_ = 0;
  bool failed_ = false;
};

/// Sends one frame, retrying partial writes. Returns only when the whole frame
/// is on the wire or an error is reported.
[[nodiscard]] CFN_API Outcome<void> send_frame(const Socket& socket, std::span<const std::byte> payload,
                                               const Limits& limits, Duration timeout);

/// Receives exactly one frame.
[[nodiscard]] CFN_API Outcome<std::vector<std::byte>> receive_frame(const Socket& socket,
                                                                    FrameReader& reader,
                                                                    Duration timeout,
                                                                    const Limits& limits);

}  // namespace cfn::net

#endif  // CFN_NET_FRAMING_HPP
