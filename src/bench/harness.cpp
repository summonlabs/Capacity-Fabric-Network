// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/bench/harness.hpp"

#include <chrono>
#include <cstdio>

#include "cfn/text/json.hpp"

namespace cfn::bench {

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t now_nanos() {
  const auto value = Clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(value).count());
}

[[nodiscard]] std::string format_double(double value, int decimals) {
  char buffer[64] = {};
  const int written = std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  if (written <= 0) {
    return "0";
  }
  return std::string(buffer, static_cast<std::size_t>(written));
}

}  // namespace

double Measurement::ns_per_iteration() const noexcept {
  if (iterations == 0) {
    return 0.0;
  }
  return static_cast<double>(total_nanos) / static_cast<double>(iterations);
}

double Measurement::work_units_per_second() const noexcept {
  if (total_nanos == 0) {
    return 0.0;
  }
  return (static_cast<double>(work_units) * 1e9) / static_cast<double>(total_nanos);
}

Suite::Suite(std::string title, bool json_output) : title_(std::move(title)), json_(json_output) {}

Measurement& Suite::measure(std::string name, std::string population, std::uint64_t iterations,
                            const std::function<std::uint64_t(std::uint64_t)>& body) {
  Measurement measurement;
  measurement.name = std::move(name);
  measurement.population = std::move(population);
  measurement.iterations = iterations;
  const std::uint64_t begin = now_nanos();
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    measurement.checksum = measurement.checksum * 1099511628211ULL + body(iteration) + 1ULL;
  }
  const std::uint64_t end = now_nanos();
  measurement.total_nanos = end - begin;
  measurements_.push_back(std::move(measurement));
  return measurements_.back();
}

std::string Suite::render() const {
  if (json_) {
    text::JsonWriter writer(true);
    writer.begin_object();
    writer.field("title", title_);
    writer.field("kind", "synthetic");
    writer.key("measurements");
    writer.begin_array();
    for (const Measurement& measurement : measurements_) {
      writer.begin_object();
      writer.field("name", measurement.name);
      writer.field("population", measurement.population);
      writer.field("iterations", measurement.iterations);
      writer.field("total_nanos", measurement.total_nanos);
      writer.field("work_units", measurement.work_units);
      writer.field("checksum", measurement.checksum);
      writer.field("ns_per_iteration", format_double(measurement.ns_per_iteration(), 1));
      writer.field("work_units_per_second", format_double(measurement.work_units_per_second(), 1));
      writer.end_object();
    }
    writer.end_array();
    writer.end_object();
    return writer.take();
  }

  std::string out = title_;
  out.append("\n");
  out.append("all populations are synthetic\n\n");
  for (const Measurement& measurement : measurements_) {
    out.append(measurement.name);
    out.append("\n  population        : ");
    out.append(measurement.population);
    out.append("\n  iterations        : ");
    out.append(std::to_string(measurement.iterations));
    out.append("\n  total             : ");
    out.append(format_double(static_cast<double>(measurement.total_nanos) / 1e6, 3));
    out.append(" ms");
    out.append("\n  ns / iteration    : ");
    out.append(format_double(measurement.ns_per_iteration(), 1));
    out.append("\n  work units / s    : ");
    out.append(format_double(measurement.work_units_per_second(), 1));
    out.append("\n  checksum          : ");
    out.append(std::to_string(measurement.checksum));
    out.append("\n\n");
  }
  return out;
}

std::uint64_t run_once(const std::function<std::uint64_t()>& body) { return body(); }

}  // namespace cfn::bench
