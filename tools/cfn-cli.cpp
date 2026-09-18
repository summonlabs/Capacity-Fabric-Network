// Capacity Fabric Network - command line interface.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cfn/bench/synthetic.hpp"
#include "cfn/cfn.hpp"

namespace {

struct Arguments {
  std::vector<std::string> positional;
  std::vector<std::pair<std::string, std::string>> options;

  [[nodiscard]] bool has(const std::string& name) const {
    for (const auto& option : options) {
      if (option.first == name) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::string get(const std::string& name, const std::string& fallback) const {
    for (const auto& option : options) {
      if (option.first == name) {
        return option.second;
      }
    }
    return fallback;
  }

  [[nodiscard]] std::uint64_t get_u64(const std::string& name, std::uint64_t fallback) const {
    const std::string value = get(name, std::string());
    if (value.empty()) {
      return fallback;
    }
    std::uint64_t parsed = 0;
    if (!cfn::text::parse_u64(value, parsed)) {
      return fallback;
    }
    return parsed;
  }
};

[[nodiscard]] Arguments parse_arguments(int argc, char** argv, int from) {
  Arguments arguments;
  for (int index = from; index < argc; ++index) {
    const std::string value = argv[index];
    if (value.rfind("--", 0) == 0) {
      const std::size_t equals = value.find('=');
      if (equals != std::string::npos) {
        arguments.options.emplace_back(value.substr(2, equals - 2), value.substr(equals + 1));
      } else if (index + 1 < argc && std::strncmp(argv[index + 1], "--", 2) != 0) {
        arguments.options.emplace_back(value.substr(2), argv[index + 1]);
        ++index;
      } else {
        arguments.options.emplace_back(value.substr(2), "true");
      }
    } else {
      arguments.positional.push_back(value);
    }
  }
  return arguments;
}

[[nodiscard]] int fail(const cfn::Error& error) {
  std::fprintf(stderr, "error: %s\n", error.to_text().c_str());
  return 1;
}

[[nodiscard]] int report(const cfn::Outcome<cfn::Explanation>& explanation, bool json) {
  if (!explanation) {
    return fail(explanation.error());
  }
  std::cout << (json ? explanation->to_json() : explanation->to_text());
  return 0;
}

[[nodiscard]] cfn::Limits limits_from(const Arguments& arguments) {
  cfn::Limits limits;
  const std::uint64_t width = arguments.get_u64("max-resources", limits.max_resources);
  if (width > 0 && width <= 0xFFFFFFFFULL) {
    limits.max_resources = static_cast<std::uint32_t>(width);
  }
  return limits;
}

[[nodiscard]] int run_demo(const Arguments& arguments) {
  cfn::bench::SyntheticParams params;
  params.resource_count = static_cast<std::uint32_t>(arguments.get_u64("resources", 256));
  params.path_width = static_cast<std::uint32_t>(arguments.get_u64("paths", 4));
  params.seed = arguments.get_u64("seed", 1);
  params.reservation_density_ppm = static_cast<std::uint32_t>(arguments.get_u64("reservations", 100000));
  params.failure_domain_count = static_cast<std::uint32_t>(arguments.get_u64("domains", 4));
  params.flow_count = static_cast<std::uint32_t>(arguments.get_u64("flows", 2));
  params.degraded_count = static_cast<std::uint32_t>(arguments.get_u64("degraded", 0));
  params.unhealthy_domain_count = static_cast<std::uint32_t>(arguments.get_u64("unhealthy", 0));
  params.headroom_ppm = static_cast<std::uint32_t>(arguments.get_u64("headroom-ppm", 50000));
  const std::string resilience = arguments.get("resilience", "none");

  cfn::Limits limits = limits_from(arguments);
  cfn::text::Scenario scenario = cfn::bench::make_synthetic_scenario(params);
  if (resilience == "n1") {
    scenario.policy.resilience = cfn::ResilienceMode::DomainN1;
  } else if (resilience == "n1n1") {
    scenario.policy.resilience = cfn::ResilienceMode::DomainN1N1;
  }

  cfn::FabricOptions options;
  options.limits = limits;
  const auto fabric = cfn::Fabric::open(options);
  if (!fabric) {
    return fail(fabric.error());
  }
  const auto resources = (*fabric)->set_resource_catalog(scenario.resources);
  if (!resources) {
    return fail(resources.error());
  }
  const auto topology = (*fabric)->set_topology(scenario.topology);
  if (!topology) {
    return fail(topology.error());
  }
  const auto domains = (*fabric)->set_failure_domains(scenario.domains);
  if (!domains) {
    return fail(domains.error());
  }
  const auto reservations = (*fabric)->set_reservations(scenario.reservations);
  if (!reservations) {
    return fail(reservations.error());
  }
  const auto degradation = (*fabric)->set_degradation(scenario.degradation);
  if (!degradation) {
    return fail(degradation.error());
  }
  const auto policy = (*fabric)->register_policy(scenario.policy);
  if (!policy) {
    return fail(policy.error());
  }
  const auto shape = (*fabric)->register_demand_shape(scenario.demand_shape);
  if (!shape) {
    return fail(shape.error());
  }
  const auto model = (*fabric)->register_model(scenario.model);
  if (!model) {
    return fail(model.error());
  }
  const auto snapshot = (*fabric)->compute(scenario.model.id);
  if (!snapshot) {
    return fail(snapshot.error());
  }
  std::fprintf(stderr, "# population: %s\n", cfn::bench::describe(params).c_str());
  std::fprintf(stderr, "# synthetic population; no physical network is involved\n");
  return report(cfn::explain(*snapshot, limits), arguments.has("json"));
}

[[nodiscard]] int run_scenario(const Arguments& arguments) {
  if (arguments.positional.empty()) {
    std::fprintf(stderr, "scenario needs a file path\n");
    return 2;
  }
  const cfn::Limits limits = limits_from(arguments);
  cfn::SystemClock clock;
  const auto scenario = cfn::text::load_scenario(arguments.positional.front(), limits, clock.wall_now());
  if (!scenario) {
    return fail(scenario.error());
  }
  cfn::FabricOptions options;
  options.limits = limits;
  const auto fabric = cfn::Fabric::open(options);
  if (!fabric) {
    return fail(fabric.error());
  }
  {
    const auto result = (*fabric)->set_resource_catalog(scenario->resources);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_topology(scenario->topology);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_failure_domains(scenario->domains);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_reservations(scenario->reservations);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_degradation(scenario->degradation);
    if (!result) {
      return fail(result.error());
    }
  }
  const auto policy = (*fabric)->register_policy(scenario->policy);
  if (!policy) {
    return fail(policy.error());
  }
  if (scenario->has_demand_shape) {
    const auto shape = (*fabric)->register_demand_shape(scenario->demand_shape);
    if (!shape) {
      return fail(shape.error());
    }
  }
  const auto model = (*fabric)->register_model(scenario->model);
  if (!model) {
    return fail(model.error());
  }
  const auto snapshot = (*fabric)->compute(scenario->model.id);
  if (!snapshot) {
    return fail(snapshot.error());
  }
  return report(cfn::explain(*snapshot, limits), arguments.has("json"));
}

[[nodiscard]] int run_roundtrip(const Arguments& arguments) {
  if (arguments.positional.empty()) {
    std::fprintf(stderr, "roundtrip needs a file path\n");
    return 2;
  }
  const cfn::Limits limits = limits_from(arguments);
  cfn::SystemClock clock;
  const auto scenario = cfn::text::load_scenario(arguments.positional.front(), limits, clock.wall_now());
  if (!scenario) {
    return fail(scenario.error());
  }
  const std::string formatted = cfn::text::format_scenario(*scenario);
  const auto reparsed = cfn::text::parse_scenario(formatted, limits, clock.wall_now());
  if (!reparsed) {
    return fail(reparsed.error());
  }
  const std::string again = cfn::text::format_scenario(*reparsed);
  if (again != formatted) {
    std::fprintf(stderr, "error: round trip is not stable\n");
    return 1;
  }
  std::cout << formatted;
  return 0;
}

[[nodiscard]] int run_fit(const Arguments& arguments) {
  if (arguments.positional.empty()) {
    std::fprintf(stderr, "fit needs a scenario file path\n");
    return 2;
  }
  const cfn::Limits limits = limits_from(arguments);
  cfn::SystemClock clock;
  const auto scenario = cfn::text::load_scenario(arguments.positional.front(), limits, clock.wall_now());
  if (!scenario) {
    return fail(scenario.error());
  }
  cfn::FabricOptions options;
  options.limits = limits;
  const auto fabric = cfn::Fabric::open(options);
  if (!fabric) {
    return fail(fabric.error());
  }
  {
    const auto result = (*fabric)->register_policy(scenario->policy);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_resource_catalog(scenario->resources);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_topology(scenario->topology);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_failure_domains(scenario->domains);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_reservations(scenario->reservations);
    if (!result) {
      return fail(result.error());
    }
  }
  {
    const auto result = (*fabric)->set_degradation(scenario->degradation);
    if (!result) {
      return fail(result.error());
    }
  }
  cfn::FitQuery query;
  const auto source = cfn::ResourceId::parse(arguments.get("source", "src"));
  const auto sink = cfn::ResourceId::parse(arguments.get("sink", "dst"));
  if (!source.has_value() || !sink.has_value()) {
    std::fprintf(stderr, "fit needs valid --source and --sink identities\n");
    return 2;
  }
  query.source = *source;
  query.sink = *sink;
  query.magnitude = cfn::Capacity::from_units(arguments.get_u64("magnitude", 0));
  const std::string resilience = arguments.get("resilience", "none");
  if (resilience == "n1") {
    query.resilience = cfn::ResilienceMode::DomainN1;
  } else if (resilience == "n1n1") {
    query.resilience = cfn::ResilienceMode::DomainN1N1;
  }
  const auto answer = (*fabric)->fit(scenario->policy.id, query);
  if (!answer) {
    return fail(answer.error());
  }
  return report(cfn::explain(query, *answer, limits), arguments.has("json"));
}

[[nodiscard]] int run_predict(const Arguments& arguments) {
  if (arguments.positional.empty()) {
    std::fprintf(stderr, "predict needs a scenario file path\n");
    return 2;
  }
  const cfn::Limits limits = limits_from(arguments);
  cfn::SystemClock clock;
  const auto scenario = cfn::text::load_scenario(arguments.positional.front(), limits, clock.wall_now());
  if (!scenario) {
    return fail(scenario.error());
  }
  cfn::PredictionRequest request;
  const std::string model = arguments.get("model", "bounded-linear-trend");
  if (!cfn::prediction_model_from_string(model, request.kind)) {
    std::fprintf(stderr, "unknown prediction model\n");
    return 2;
  }
  request.min_samples = static_cast<std::uint32_t>(arguments.get_u64("min-samples", 3));
  const std::uint64_t horizon = arguments.get_u64("horizon-seconds", 600);
  request.max_extrapolation = cfn::Duration::from_seconds(
      static_cast<std::int64_t>(arguments.get_u64("max-extrapolation-seconds", 3600)));
  request.max_sample_age = cfn::Duration::from_seconds(
      static_cast<std::int64_t>(arguments.get_u64("max-sample-age-seconds", 86400)));
  request.history = scenario->observations;
  cfn::Timestamp newest{};
  for (const cfn::CapacityObservation& observation : request.history) {
    if (observation.at > newest) {
      newest = observation.at;
    }
  }
  cfn::Timestamp horizon_at{};
  if (!cfn::checked::add_i64(newest.unix_nanos,
                             static_cast<std::int64_t>(horizon) * 1000000000LL,
                             horizon_at.unix_nanos)) {
    std::fprintf(stderr, "horizon is out of range\n");
    return 2;
  }
  request.horizon = horizon_at;
  const auto prediction = cfn::predict(request, limits, newest);
  if (!prediction) {
    return fail(prediction.error());
  }
  return report(cfn::explain(*prediction, limits), arguments.has("json"));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: cfn-cli <version|demo|scenario|fit|predict|roundtrip> [options]\n");
    return 2;
  }
  const std::string command = argv[1];
  const Arguments arguments = parse_arguments(argc, argv, 2);
  if (command == "version") {
    std::cout << cfn::build_banner() << "\n";
    return 0;
  }
  if (command == "demo") {
    return run_demo(arguments);
  }
  if (command == "scenario") {
    return run_scenario(arguments);
  }
  if (command == "roundtrip") {
    return run_roundtrip(arguments);
  }
  if (command == "fit") {
    return run_fit(arguments);
  }
  if (command == "predict") {
    return run_predict(arguments);
  }
  std::fprintf(stderr, "unknown command: %s\n", command.c_str());
  return 2;
}