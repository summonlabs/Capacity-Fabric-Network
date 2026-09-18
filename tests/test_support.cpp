// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_support.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace cfn::test {

namespace {

std::uint64_t current_process_id() {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

}  // namespace

TempDirectory::TempDirectory(const char* tag) {
  static std::atomic<std::uint64_t> counter{0};
  const std::uint64_t ordinal = counter.fetch_add(1, std::memory_order_relaxed);
  std::error_code error;
  std::filesystem::path base = std::filesystem::temp_directory_path(error);
  if (error) {
    base = std::filesystem::current_path();
  }
  path_ = base / (std::string(tag) + "-" + std::to_string(ordinal) + "-" +
                  std::to_string(current_process_id()));
  std::filesystem::remove_all(path_, error);
  std::filesystem::create_directories(path_, error);
}

TempDirectory::~TempDirectory() {
  std::error_code error;
  std::filesystem::remove_all(path_, error);
}

void TempDirectory::reset() {
  std::error_code error;
  std::filesystem::remove_all(path_, error);
  std::filesystem::create_directories(path_, error);
}

void TestContext::check(bool condition, std::string_view expression, const char* file, int line) {
  checks_ += 1;
  if (condition) {
    return;
  }
  failures_ += 1;
  std::string message = file;
  message.append(":");
  message.append(std::to_string(line));
  message.append(": ");
  message.append(name_);
  message.append(": ");
  message.append(expression);
  messages_.push_back(std::move(message));
}

void TestContext::fail(std::string_view message, const char* file, int line) {
  check(false, message, file, line);
}

void TestContext::add_note(std::string_view note) {
  std::string message = "note: ";
  message.append(note);
  messages_.push_back(std::move(message));
}

std::vector<Registration>& registrations() {
  static std::vector<Registration> values;
  return values;
}

Registrar::Registrar(const char* suite, const char* name, TestBody body) {
  Registration registration;
  registration.suite = suite;
  registration.name = name;
  registration.body = body;
  registrations().push_back(std::move(registration));
}

namespace {

std::uint64_t now_nanos() {
  const auto value = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(value).count());
}

}  // namespace

ChildProcess::~ChildProcess() {
  std::fflush(stdout);
  if (running()) {
    kill();
    int code = 0;
    (void)wait_for_exit(Duration::from_seconds(10), code);
  }
#if defined(_WIN32)
  if (read_handle_ != -1) {
    ::CloseHandle(reinterpret_cast<HANDLE>(read_handle_));
  }
  if (handle_ != -1) {
    ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
  }
#else
  if (read_handle_ != -1) {
    ::close(static_cast<int>(read_handle_));
  }
#endif
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : pid_(other.pid_),
      handle_(other.handle_),
      read_handle_(other.read_handle_),
      exited_(other.exited_),
      exit_code_(other.exit_code_),
      buffer_(std::move(other.buffer_)) {
  other.pid_ = 0;
  other.handle_ = -1;
  other.read_handle_ = -1;
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
  if (this != &other) {
    this->~ChildProcess();
    pid_ = other.pid_;
    handle_ = other.handle_;
    read_handle_ = other.read_handle_;
    exited_ = other.exited_;
    exit_code_ = other.exit_code_;
    buffer_ = std::move(other.buffer_);
    other.pid_ = 0;
    other.handle_ = -1;
    other.read_handle_ = -1;
  }
  return *this;
}

Outcome<ChildProcess> ChildProcess::spawn(const std::string& executable,
                                          const std::vector<std::string>& arguments) {
  std::fflush(stdout);
  ChildProcess child;
#if defined(_WIN32)
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  if (::CreatePipe(&read_end, &write_end, &attributes, 0) == 0) {
    return Error(ErrorCode::IoError, "failed to create a pipe for the child process");
  }
  ::SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  std::string command = "\"" + executable + "\"";
  for (const std::string& argument : arguments) {
    command.append(" \"");
    command.append(argument);
    command.append("\"");
  }
  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_end;
  startup.hStdError = write_end;
  startup.hStdInput = nullptr;
  PROCESS_INFORMATION information{};
  std::vector<char> mutable_command(command.begin(), command.end());
  mutable_command.push_back('\0');
  const BOOL created = ::CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, 0,
                                        nullptr, nullptr, &startup, &information);
  ::CloseHandle(write_end);
  if (created == 0) {
    ::CloseHandle(read_end);
    return Error(ErrorCode::IoError, "failed to create the child process", executable);
  }
  ::CloseHandle(information.hThread);
  child.pid_ = static_cast<std::uint64_t>(information.dwProcessId);
  child.handle_ = reinterpret_cast<std::intptr_t>(information.hProcess);
  child.read_handle_ = reinterpret_cast<std::intptr_t>(read_end);
#else
  int descriptors[2] = {-1, -1};
  if (::pipe(descriptors) != 0) {
    return Error(ErrorCode::IoError, "failed to create a pipe for the child process");
  }
  std::vector<std::string> storage;
  storage.push_back(executable);
  for (const std::string& argument : arguments) {
    storage.push_back(argument);
  }
  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (std::string& value : storage) {
    argv.push_back(value.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, descriptors[0]);
  const int result = posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(descriptors[1]);
  if (result != 0) {
    ::close(descriptors[0]);
    return Error(ErrorCode::IoError, "failed to spawn the child process", executable);
  }
  child.pid_ = static_cast<std::uint64_t>(pid);
  child.handle_ = static_cast<std::intptr_t>(pid);
  child.read_handle_ = static_cast<std::intptr_t>(descriptors[0]);
#endif
  std::fflush(stdout);
  ChildProcess staging = std::move(child);
  std::fflush(stdout);
  Outcome<ChildProcess> result(std::move(staging));
  std::fflush(stdout);
  return result;
}

bool ChildProcess::running() {
  if (exited_ || handle_ == -1) {
    return false;
  }
#if defined(_WIN32)
  const DWORD status = ::WaitForSingleObject(reinterpret_cast<HANDLE>(handle_), 0);
  if (status == WAIT_TIMEOUT) {
    return true;
  }
  DWORD code = 0;
  if (::GetExitCodeProcess(reinterpret_cast<HANDLE>(handle_), &code) != 0) {
    exit_code_ = static_cast<int>(code);
  }
  exited_ = true;
  return false;
#else
  int status = 0;
  const pid_t result = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
  if (result == 0) {
    return true;
  }
  if (result > 0) {
    exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    exited_ = true;
    return false;
  }
  return true;
#endif
}

void ChildProcess::kill() {
  if (exited_ || handle_ == -1) {
    return;
  }
#if defined(_WIN32)
  (void)::TerminateProcess(reinterpret_cast<HANDLE>(handle_), 1);
#else
  (void)::kill(static_cast<pid_t>(pid_), SIGKILL);
#endif
  exited_ = true;
  exit_code_ = -1;
}

bool ChildProcess::wait_for_exit(Duration timeout, int& exit_code) {
  if (handle_ == -1) {
    exit_code = exit_code_;
    return true;
  }
  const std::uint64_t deadline = now_nanos() + static_cast<std::uint64_t>(timeout.nanos > 0 ? timeout.nanos : 0);
  for (;;) {
    if (!running()) {
      exit_code = exit_code_;
      return true;
    }
    if (now_nanos() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

bool ChildProcess::read_line(std::string& line, Duration timeout) {
  std::fflush(stdout);
  const std::uint64_t deadline = now_nanos() + static_cast<std::uint64_t>(timeout.nanos > 0 ? timeout.nanos : 0);
  for (;;) {
    const std::size_t newline = buffer_.find('\n');
    if (newline != std::string::npos) {
      line = buffer_.substr(0, newline);
      buffer_.erase(0, newline + 1);
      std::fflush(stdout);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      return true;
    }
    if (now_nanos() >= deadline) {
      return false;
    }
#if defined(_WIN32)
    DWORD available = 0;
    if (::PeekNamedPipe(reinterpret_cast<HANDLE>(read_handle_), nullptr, 0, nullptr, &available,
                        nullptr) == 0) {
      return false;
    }
    if (available == 0) {
      if (!running() && buffer_.empty()) {
        std::fflush(stdout);
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    char chunk[512] = {};
    DWORD read = 0;
    if (::ReadFile(reinterpret_cast<HANDLE>(read_handle_), chunk, sizeof(chunk), &read, nullptr) == 0 ||
        read == 0) {
      return false;
    }
    buffer_.append(chunk, read);
    std::fflush(stdout);
#else
    char chunk[512] = {};
    const ssize_t read = ::read(static_cast<int>(read_handle_), chunk, sizeof(chunk));
    if (read > 0) {
      buffer_.append(chunk, static_cast<std::size_t>(read));
      continue;
    }
    if (read == 0) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  }
}

int run_all(int argc, char** argv) {
  // Output is flushed after every case so progress is visible even when the
  // stream is redirected: a stuck case must be diagnosable from its log.
  std::string filter;
  bool list_only = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--list") {
      list_only = true;
    } else if (argument.rfind("--filter=", 0) == 0) {
      filter = std::string(argument.substr(9));
    }
  }

  std::vector<Registration>& all = registrations();
  std::sort(all.begin(), all.end(), [](const Registration& lhs, const Registration& rhs) {
    if (lhs.suite != rhs.suite) {
      return lhs.suite < rhs.suite;
    }
    return lhs.name < rhs.name;
  });

  std::uint64_t executed = 0;
  std::uint64_t failed = 0;
  std::uint64_t checks = 0;
  std::size_t registration_count = 0;
  for (const Registration& registration : all) {
    const std::string full = registration.suite + "." + registration.name;
    if (!filter.empty() && full.find(filter) == std::string::npos) {
      continue;
    }
    ++registration_count;
    if (list_only) {
      std::printf("%s\n", full.c_str());
      continue;
    }
    std::fflush(stdout);
    TestContext context(full);
    registration.body(context);
    ++executed;
    checks += context.checks();
    if (context.failed()) {
      ++failed;
      std::printf("FAIL %s\n", full.c_str());
      for (const std::string& message : context.messages()) {
        std::printf("     %s\n", message.c_str());
      }
    } else {
      std::printf("ok   %s (%llu checks)\n", full.c_str(),
                  static_cast<unsigned long long>(context.checks()));
    }
    std::fflush(stdout);
  }

  if (list_only) {
    std::printf("%zu cases\n", registration_count);
    return 0;
  }
  std::printf("\n%llu cases, %llu failed, %llu assertions\n",
              static_cast<unsigned long long>(executed), static_cast<unsigned long long>(failed),
              static_cast<unsigned long long>(checks));
  return failed == 0 ? 0 : 1;
}

}  // namespace cfn::test