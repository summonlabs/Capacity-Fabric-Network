// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include <cstdio>

#include "test_support.hpp"

int main(int argc, char** argv) {
  std::fflush(stdout);
  const int result = cfn::test::run_all(argc, argv);
  std::fflush(stdout);
  return result;
}