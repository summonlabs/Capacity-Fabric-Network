// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/net/socket.hpp"

#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cfn::net {

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

std::once_flag g_startup_once;
bool g_started = false;

[[nodiscard]] NativeSocket to_native(std::intptr_t handle) noexcept {
  return static_cast<NativeSocket>(handle);
}

[[nodiscard]] std::intptr_t from_native(NativeSocket handle) noexcept {
  return static_cast<std::intptr_t>(handle);
}

void close_native(NativeSocket handle) noexcept {
  if (handle == kInvalidSocket) {
    return;
  }
#if defined(_WIN32)
  (void)closesocket(handle);
#else
  (void)::close(handle);
#endif
}

[[nodiscard]] bool would_block_error() noexcept {
#if defined(_WIN32)
  const int code = WSAGetLastError();
  return code == WSAEWOULDBLOCK || code == WSAEINTR || code == WSAEINPROGRESS;
#else
  return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR || errno == EINPROGRESS;
#endif
}

[[nodiscard]] Outcome<NativeSocket> create_tcp_socket() {
  const NativeSocket handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (handle == kInvalidSocket) {
    return Error(ErrorCode::TransportError, "failed to create a TCP socket");
  }
  int enabled = 1;
  (void)::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled),
                     static_cast<int>(sizeof(enabled)));
  return handle;
}

[[nodiscard]] Outcome<sockaddr_in> resolve(const std::string& host, std::uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (host.empty() || host == "localhost") {
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
  }
  if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    return Error(ErrorCode::InvalidArgument, "only IPv4 literal addresses are supported", host);
  }
  return address;
}

[[nodiscard]] bool wait_on(const Socket& socket, Duration timeout, bool for_write) {
  fd_set set;
  FD_ZERO(&set);
  const NativeSocket handle = to_native(socket.native());
  FD_SET(handle, &set);
  timeval value{};
  const std::int64_t nanos = timeout.nanos > 0 ? timeout.nanos : 0;
  value.tv_sec = static_cast<long>(nanos / 1000000000LL);
  value.tv_usec = static_cast<long>((nanos % 1000000000LL) / 1000LL);
#if defined(_WIN32)
  const int ready = ::select(0, for_write ? nullptr : &set, for_write ? &set : nullptr, nullptr, &value);
#else
  const int ready = ::select(static_cast<int>(handle) + 1, for_write ? nullptr : &set,
                             for_write ? &set : nullptr, nullptr, &value);
#endif
  return ready > 0;
}

}  // namespace

Outcome<void> initialize_networking() {
  std::call_once(g_startup_once, []() {
#if defined(_WIN32)
    WSADATA data{};
    g_started = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    g_started = true;
#endif
  });
  if (!g_started) {
    return Error(ErrorCode::TransportError, "failed to initialise the platform networking stack");
  }
  return Outcome<void>();
}

bool networking_initialized() noexcept { return g_started; }

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) { other.handle_ = -1; }

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = -1;
  }
  return *this;
}

bool Socket::valid() const noexcept { return handle_ != -1; }

void Socket::reset(std::intptr_t handle) noexcept {
  close();
  handle_ = handle;
}

void Socket::close() noexcept {
  if (handle_ != -1) {
    close_native(to_native(handle_));
    handle_ = -1;
  }
}

std::intptr_t Socket::release() noexcept {
  const std::intptr_t handle = handle_;
  handle_ = -1;
  return handle;
}

Outcome<Socket> connect_tcp(const std::string& host, std::uint16_t port, Duration timeout) {
  CFN_RETURN_IF_ERROR(initialize_networking());
  const Outcome<sockaddr_in> address = resolve(host, port);
  if (!address) {
    return address.error();
  }
  Outcome<NativeSocket> created = create_tcp_socket();
  if (!created) {
    return created.error();
  }
  Socket socket;
  socket.reset(from_native(*created));
  const int result = ::connect(to_native(socket.native()), reinterpret_cast<const sockaddr*>(&*address),
                               static_cast<int>(sizeof(sockaddr_in)));
  if (result != 0) {
#if defined(_WIN32)
    const int code = WSAGetLastError();
    const bool in_progress = code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
#else
    const bool in_progress = errno == EINPROGRESS || errno == EWOULDBLOCK;
#endif
    if (!in_progress) {
      return Error(ErrorCode::TransportError, "failed to connect to the peer", host);
    }
    if (!wait_on(socket, timeout, true)) {
      return Error(ErrorCode::TransportError, "connection attempt did not complete", host);
    }
    int error = 0;
#if defined(_WIN32)
    int length = static_cast<int>(sizeof(error));
#else
    socklen_t length = sizeof(error);
#endif
    if (::getsockopt(to_native(socket.native()), SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&error), &length) != 0 ||
        error != 0) {
      return Error(ErrorCode::TransportError, "connection attempt failed", host);
    }
  }
  return socket;
}

Outcome<std::pair<Socket, std::uint16_t>> listen_tcp(const std::string& host, std::uint16_t port,
                                                     int backlog) {
  CFN_RETURN_IF_ERROR(initialize_networking());
  const Outcome<sockaddr_in> address = resolve(host, port);
  if (!address) {
    return address.error();
  }
  Outcome<NativeSocket> created = create_tcp_socket();
  if (!created) {
    return created.error();
  }
  Socket socket;
  socket.reset(from_native(*created));
  int reuse = 1;
  (void)::setsockopt(to_native(socket.native()), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), static_cast<int>(sizeof(reuse)));
  if (::bind(to_native(socket.native()), reinterpret_cast<const sockaddr*>(&*address),
             static_cast<int>(sizeof(sockaddr_in))) != 0) {
    return Error(ErrorCode::TransportError, "failed to bind the listening socket", host);
  }
  if (::listen(to_native(socket.native()), backlog) != 0) {
    return Error(ErrorCode::TransportError, "failed to listen on the bound socket", host);
  }
  sockaddr_in bound{};
#if defined(_WIN32)
  int bound_length = static_cast<int>(sizeof(bound));
#else
  socklen_t bound_length = sizeof(bound);
#endif
  if (::getsockname(to_native(socket.native()), reinterpret_cast<sockaddr*>(&bound), &bound_length) != 0) {
    return Error(ErrorCode::TransportError, "failed to determine the bound port", host);
  }
  return std::make_pair(std::move(socket), static_cast<std::uint16_t>(ntohs(bound.sin_port)));
}

Outcome<Socket> accept_tcp(const Socket& listener, Duration timeout, bool& would_block) {
  would_block = false;
  if (!listener.valid()) {
    return Error(ErrorCode::Closed, "the listening socket is closed");
  }
  if (!wait_on(listener, timeout, false)) {
    would_block = true;
    return Socket{};
  }
  sockaddr_in peer{};
#if defined(_WIN32)
  int peer_length = static_cast<int>(sizeof(peer));
#else
  socklen_t peer_length = sizeof(peer);
#endif
  const NativeSocket accepted = ::accept(to_native(listener.native()),
                                         reinterpret_cast<sockaddr*>(&peer), &peer_length);
  if (accepted == kInvalidSocket) {
    if (would_block_error()) {
      would_block = true;
      return Socket{};
    }
    return Error(ErrorCode::TransportError, "failed to accept a connection");
  }
  Socket socket;
  socket.reset(from_native(accepted));
  int enabled = 1;
  (void)::setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled),
                     static_cast<int>(sizeof(enabled)));
  return socket;
}

Outcome<bool> wait_readable(const Socket& socket, Duration timeout) {
  if (!socket.valid()) {
    return Error(ErrorCode::Closed, "the socket is closed");
  }
  return wait_on(socket, timeout, false);
}

Outcome<bool> wait_writable(const Socket& socket, Duration timeout) {
  if (!socket.valid()) {
    return Error(ErrorCode::Closed, "the socket is closed");
  }
  return wait_on(socket, timeout, true);
}

Outcome<void> send_all(const Socket& socket, std::span<const std::byte> data, Duration timeout) {
  if (!socket.valid()) {
    return Error(ErrorCode::Closed, "the socket is closed");
  }
  std::size_t sent = 0;
  while (sent < data.size()) {
    if (!wait_on(socket, timeout, true)) {
      return Error(ErrorCode::TransportError, "timed out while sending");
    }
    const std::size_t chunk = data.size() - sent;
    const int amount = static_cast<int>(chunk > 1u << 20 ? 1u << 20 : chunk);
    const int written = ::send(to_native(socket.native()),
                               reinterpret_cast<const char*>(data.data() + sent), amount, 0);
    if (written <= 0) {
      if (would_block_error()) {
        continue;
      }
      return Error(ErrorCode::Disconnected, "the peer closed the connection while sending");
    }
    sent += static_cast<std::size_t>(written);
  }
  return Outcome<void>();
}

Outcome<std::size_t> recv_some(const Socket& socket, std::span<std::byte> buffer, Duration timeout) {
  if (!socket.valid()) {
    return Error(ErrorCode::Closed, "the socket is closed");
  }
  const Outcome<bool> ready = wait_readable(socket, timeout);
  if (!ready) {
    return ready.error();
  }
  if (!*ready) {
    return Error(ErrorCode::TransportError, "timed out while receiving");
  }
  const int amount = static_cast<int>(buffer.size() > 1u << 20 ? 1u << 20 : buffer.size());
  const int received =
      ::recv(to_native(socket.native()), reinterpret_cast<char*>(buffer.data()), amount, 0);
  if (received < 0) {
    if (would_block_error()) {
      return static_cast<std::size_t>(0);
    }
    return Error(ErrorCode::TransportError, "failed to receive from the peer");
  }
  return static_cast<std::size_t>(received);
}

Outcome<void> shutdown_send(const Socket& socket) {
  if (!socket.valid()) {
    return Error(ErrorCode::Closed, "the socket is closed");
  }
#if defined(_WIN32)
  if (::shutdown(to_native(socket.native()), SD_SEND) != 0) {
    return Error(ErrorCode::TransportError, "failed to shut down the send direction");
  }
#else
  if (::shutdown(to_native(socket.native()), SHUT_WR) != 0) {
    return Error(ErrorCode::TransportError, "failed to shut down the send direction");
  }
#endif
  return Outcome<void>();
}

}  // namespace cfn::net
