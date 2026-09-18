// Capacity Fabric Network - test harness.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Minimal self-contained harness: registration, assertions, filtering, and a
// real OS process helper for the multiprocess tests. No test in this project
// carries a timeout; a hang is a defect to diagnose, not something to mask.
#ifndef CFN_TEST_SUPPORT_HPP
#define CFN_TEST_SUPPORT_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cfn/core/outcome.hpp"
#include "cfn/core/time.hpp"

namespace cfn::test {

class TestContext {
 public:
  explicit TestContext(std::string name) : name_(std::move(name)) {}

  void check(bool condition, std::string_view expression, const char* file, int line);

  template <class A, class B>
  void check_equal(const A& lhs, const B& rhs, std::string_view left_text,
                   std::string_view right_text, const char* file, int line) {
    if (!(lhs == rhs)) {
      std::string message = "expected ";
      message.append(left_text);
      message.append(" == ");
      message.append(right_text);
      message.append(" but got ");
      message.append(describe(lhs));
      message.append(" vs ");
      message.append(describe(rhs));
      check(false, message, file, line);
    } else {
      checks_ += 1;
    }
  }

  void fail(std::string_view message, const char* file, int line);
  void add_note(std::string_view note);

  [[nodiscard]] bool failed() const noexcept { return failures_ != 0; }
  [[nodiscard]] std::uint64_t checks() const noexcept { return checks_; }
  [[nodiscard]] std::uint64_t failures() const noexcept { return failures_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] const std::vector<std::string>& messages() const noexcept { return messages_; }

 private:
  template <class T>
  [[nodiscard]] static std::string describe(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
      return value;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
      return std::string(value);
    } else if constexpr (std::is_same_v<T, bool>) {
      return value ? "true" : "false";
    } else if constexpr (std::is_integral_v<T>) {
      return std::to_string(value);
    } else {
      return "<value>";
    }
  }

  std::string name_;
  std::uint64_t checks_ = 0;
  std::uint64_t failures_ = 0;
  std::vector<std::string> messages_;
};

using TestBody = void (*)(TestContext&);

class Registrar {
 public:
  Registrar(const char* suite, const char* name, TestBody body);
};

struct Registration {
  std::string suite;
  std::string name;
  TestBody body;
};

[[nodiscard]] std::vector<Registration>& registrations();
[[nodiscard]] int run_all(int argc, char** argv);

/// Asserts that an Outcome succeeded, reporting the error when it did not.
#define CFN_REQUIRE_OK(context, expression)                                          \
  do {                                                                               \
    auto&& cfn_required = (expression);                                              \
    if (!cfn_required) {                                                             \
      (context).add_note(std::string("error: ") + cfn_required.error().to_text());   \
    }                                                                                \
    (context).check(static_cast<bool>(cfn_required), #expression, __FILE__, __LINE__); \
    if (!cfn_required) {                                                             \
      return;                                                                        \
    }                                                                                \
  } while (false)

/// Asserts that an Outcome failed with a specific code.
#define CFN_REQUIRE_ERROR(context, expression, expected)                       \
  do {                                                                         \
    auto&& cfn_required = (expression);                                          \
    (context).check(!cfn_required, #expression " must fail", __FILE__, __LINE__); \
    if (cfn_required) {                                                          \
      return;                                                                    \
    }                                                                            \
    (context).check_equal(static_cast<int>(cfn_required.error().code()),          \
                          static_cast<int>(expected), #expression " code",        \
                          #expected, __FILE__, __LINE__);                         \
  } while (false)

/// Scoped temporary directory for the persistence tests.
class TempDirectory {
 public:
  explicit TempDirectory(const char* tag = "cfn");
  ~TempDirectory();
  TempDirectory(const TempDirectory&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  /// Removes and recreates the directory, so a test can simulate a clean slate.
  void reset();

 private:
  std::filesystem::path path_;
};

/// Real child process helper used by the multiprocess tests.
class ChildProcess {
 public:
  ChildProcess() = default;
  ~ChildProcess();
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;
  ChildProcess(ChildProcess&& other) noexcept;
  ChildProcess& operator=(ChildProcess&& other) noexcept;

  static Outcome<ChildProcess> spawn(const std::string& executable,
                                     const std::vector<std::string>& arguments);

  /// Reads one line of standard output. Returns false when the deadline passes
  /// or the child closed its output.
  [[nodiscard]] bool read_line(std::string& line, Duration timeout);
  /// Waits for exit, or terminates the child when the deadline passes.
  [[nodiscard]] bool wait_for_exit(Duration timeout, int& exit_code);
  /// Unconditional hard termination, equivalent to a crash for the child.
  void kill();
  [[nodiscard]] bool running();
  [[nodiscard]] std::uint64_t pid() const noexcept { return pid_; }

 private:
  std::uint64_t pid_ = 0;
  std::intptr_t handle_ = -1;
  std::intptr_t read_handle_ = -1;
  bool exited_ = false;
  int exit_code_ = -1;
  std::string buffer_;
};

}  // namespace cfn::test

#define CFN_TEST(suite_name, case_name)                                            \
  static void cfn_test_body_##suite_name##_##case_name(::cfn::test::TestContext&); \
  namespace {                                                                      \
  const ::cfn::test::Registrar cfn_test_registrar_##suite_name##_##case_name(      \
      #suite_name, #case_name, &cfn_test_body_##suite_name##_##case_name);         \
  }                                                                                \
  static void cfn_test_body_##suite_name##_##case_name(::cfn::test::TestContext& cfn_ctx)

#define CFN_CHECK(expr) cfn_ctx.check((expr), #expr, __FILE__, __LINE__)
#define CFN_CHECK_EQ(lhs, rhs) cfn_ctx.check_equal((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)
#define CFN_NOTE(text) cfn_ctx.add_note(text)

#endif  // CFN_TEST_SUPPORT_HPP