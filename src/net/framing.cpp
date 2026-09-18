// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/net/framing.hpp"

#include <array>

#include "cfn/core/checked.hpp"
#include "cfn/persist/crc32c.hpp"

namespace cfn::net {

namespace {

void put_u32(std::byte* out, std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    out[index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
  }
}

[[nodiscard]] std::uint32_t get_u32(const std::byte* in) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[index])) << (8U * index);
  }
  return value;
}

}  // namespace

Outcome<std::vector<std::byte>> encode_frame(std::span<const std::byte> payload, const Limits& limits) {
  if (payload.size() > limits.max_frame_bytes) {
    return Error(ErrorCode::LimitExceeded, "frame payload exceeds the configured maximum");
  }
  std::vector<std::byte> frame(kFrameHeaderBytes + payload.size());
  put_u32(frame.data(), static_cast<std::uint32_t>(payload.size()));
  put_u32(frame.data() + 4, crc32c::compute(payload));
  for (std::size_t index = 0; index < payload.size(); ++index) {
    frame[kFrameHeaderBytes + index] = payload[index];
  }
  return frame;
}

FrameReader::FrameReader(const Limits& limits) : limits_(limits) {}

void FrameReader::reset() {
  buffer_.clear();
  consumed_ = 0;
  frames_decoded_ = 0;
  failed_ = false;
}

Outcome<void> FrameReader::feed(std::span<const std::byte> chunk,
                                std::vector<std::vector<std::byte>>& frames_out) {
  if (failed_) {
    return Error(ErrorCode::InvalidState, "the frame reader has already failed");
  }
  if (consumed_ != 0 && consumed_ == buffer_.size()) {
    buffer_.clear();
    consumed_ = 0;
  } else if (consumed_ > 0 && consumed_ * 2U > buffer_.size()) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed_));
    consumed_ = 0;
  }
  if (!chunk.empty()) {
    buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
  }
  if (buffer_.size() > limits_.max_frame_bytes + kFrameHeaderBytes + (1U << 16)) {
    failed_ = true;
    return Error(ErrorCode::LimitExceeded, "framed receive buffer exceeds the configured maximum");
  }

  for (;;) {
    if (buffer_.size() - consumed_ < kFrameHeaderBytes) {
      break;
    }
    const std::uint32_t length = get_u32(buffer_.data() + consumed_);
    if (length > limits_.max_frame_bytes) {
      failed_ = true;
      return Error(ErrorCode::MalformedInput, "incoming frame declares an oversized payload");
    }
    if (buffer_.size() - consumed_ < kFrameHeaderBytes + length) {
      break;
    }
    const std::byte* payload = buffer_.data() + consumed_ + kFrameHeaderBytes;
    const std::uint32_t expected = get_u32(buffer_.data() + consumed_ + 4);
    if (crc32c::compute(payload, length) != expected) {
      failed_ = true;
      return Error(ErrorCode::ChecksumMismatch, "incoming frame payload failed its checksum");
    }
    frames_out.emplace_back(payload, payload + length);
    frames_decoded_ += 1;
    consumed_ += kFrameHeaderBytes + length;
  }
  return Outcome<void>();
}

Outcome<void> send_frame(const Socket& socket, std::span<const std::byte> payload, const Limits& limits,
                         Duration timeout) {
  const Outcome<std::vector<std::byte>> frame = encode_frame(payload, limits);
  if (!frame) {
    return frame.error();
  }
  return send_all(socket, *frame, timeout);
}

Outcome<std::vector<std::byte>> receive_frame(const Socket& socket, FrameReader& reader,
                                              Duration timeout, const Limits& limits) {
  std::vector<std::vector<std::byte>> frames;
  std::array<std::byte, 4096> buffer{};
  for (;;) {
    frames.clear();
    // The reader may already hold a complete frame before any new bytes arrive.
    CFN_RETURN_IF_ERROR(reader.feed({}, frames));
    if (!frames.empty()) {
      return frames.front();
    }
    const std::size_t chunk = std::min(buffer.size(), static_cast<std::size_t>(limits.max_frame_bytes));
    const Outcome<std::size_t> received = recv_some(socket, std::span<std::byte>(buffer.data(), chunk), timeout);
    if (!received) {
      return received.error();
    }
    if (*received == 0) {
      return Error(ErrorCode::Disconnected, "the peer closed the connection");
    }
    CFN_RETURN_IF_ERROR(reader.feed(std::span<const std::byte>(buffer.data(), *received), frames));
    if (!frames.empty()) {
      return frames.front();
    }
  }
}

}  // namespace cfn::net
