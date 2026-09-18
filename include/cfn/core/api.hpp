// Capacity Fabric Network - shared library export decoration.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CORE_API_HPP
#define CFN_CORE_API_HPP

#if defined(CFN_SHARED)
#include <cfn/export.hpp>
#else
#define CFN_API
#endif

#endif  // CFN_CORE_API_HPP
