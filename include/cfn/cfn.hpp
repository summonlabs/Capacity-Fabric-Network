// Capacity Fabric Network - umbrella header.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CFN_HPP
#define CFN_CFN_HPP

#include "cfn/version.hpp"

#include "cfn/core/bounded.hpp"
#include "cfn/core/cancel.hpp"
#include "cfn/core/checked.hpp"
#include "cfn/core/error.hpp"
#include "cfn/core/hash.hpp"
#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/core/random.hpp"
#include "cfn/core/text.hpp"
#include "cfn/core/time.hpp"

#include "cfn/model/capacity_model.hpp"
#include "cfn/model/degradation.hpp"
#include "cfn/model/demand_shape.hpp"
#include "cfn/model/policy.hpp"
#include "cfn/model/reservation.hpp"
#include "cfn/model/resource.hpp"
#include "cfn/model/topology.hpp"

#include "cfn/engine/accounting.hpp"
#include "cfn/engine/evaluator.hpp"
#include "cfn/engine/flow.hpp"
#include "cfn/engine/fragmentation.hpp"
#include "cfn/engine/invalidation.hpp"
#include "cfn/engine/prediction.hpp"
#include "cfn/engine/snapshot.hpp"

#include "cfn/explain/explanation.hpp"

#include "cfn/persist/crc32c.hpp"
#include "cfn/persist/journal.hpp"
#include "cfn/persist/records.hpp"
#include "cfn/persist/serialize.hpp"
#include "cfn/persist/store.hpp"

#include "cfn/fabric/fabric.hpp"
#include "cfn/fabric/registry.hpp"

#include "cfn/net/evidence.hpp"
#include "cfn/net/framing.hpp"
#include "cfn/net/socket.hpp"

#include "cfn/text/json.hpp"
#include "cfn/text/scenario.hpp"

#endif  // CFN_CFN_HPP
