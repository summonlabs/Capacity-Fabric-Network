// Capacity Fabric Network - portable TCP sockets.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Thin RAII wrapper over Winsock2 and BSD sockets. Readiness is always waited
// for with select() and a caller supplied deadline, never with an indefinite
// block, so a wedged peer surfaces as an error instead of a hang.
#ifndef CFN_NET_SOCKET_HPP
#define CFN_NET_SOCKET_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "cfn/core/api.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/time.hpp"

namespace cfn::net {

class CFN_API Socket {
 public:
  Socket() = default;
  ~Socket();

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::intptr_t native() const noexcept { return handle_; }
  void reset(std::intptr_t handle = -1) noexcept;
  void close() noexcept;
  [[nodiscard]] std::intptr_t release() noexcept;

 private:
  std::intptr_t handle_ = -1;
};

/// One-time process level networking start-up. Idempotent and thread safe.
[[nodiscard]] CFN_API Outcome<void> initialize_networking();
[[nodiscard]] CFN_API bool networking_initialized() noexcept;

[[nodiscard]] CFN_API Outcome<Socket> connect_tcp(const std::string& host, std::uint16_t port,
                                                  Duration timeout);
/// Binds and listens. Port zero asks the operating system for a free port; the
/// chosen port is returned alongside the listener.
[[nodiscard]] CFN_API Outcome<std::pair<Socket, std::uint16_t>> listen_tcp(const std::string& host,
                                                                          std::uint16_t port,
                                                                          int backlog);
/// Waits up to the deadline for a pending connection. Sets would_block when the
/// deadline expires with nothing to accept.
[[nodiscard]] CFN_API Outcome<Socket> accept_tcp(const Socket& listener, Duration timeout,
                                                 bool& would_block);
[[nodiscard]] CFN_API Outcome<bool> wait_readable(const Socket& socket, Duration timeout);
[[nodiscard]] CFN_API Outcome<bool> wait_writable(const Socket& socket, Duration timeout);
[[nodiscard]] CFN_API Outcome<void> send_all(const Socket& socket, std::span<const std::byte> data,
                                             Duration timeout);
/// Receives whatever is available, up to the buffer size. A zero result with
/// ok() true means the peer closed cleanly.
[[nodiscard]] CFN_API Outcome<std::size_t> recv_some(const Socket& socket, std::span<std::byte> buffer,
                                                     Duration timeout);
[[nodiscard]] CFN_API Outcome<void> shutdown_send(const Socket& socket);

}  // namespace cfn::net

#endif  // CFN_NET_SOCKET_HPP
