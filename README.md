# Capacity Fabric Network

Capacity Fabric Network (CFN) is an open-source, vendor-neutral C++20 runtime for
**predictive, generation-bound usable network-capacity modeling**. It answers one
question, exactly, and refuses to answer any other:

> Given authoritative network resources, topology and path structure, measured or
> declared capacities, reservations, degradation, headroom, fragmentation, demand
> shape, and current generations, how much network capacity is actually usable now
> or predictively available, where is it stranded, and when must the capacity model
> be invalidated or revalidated?

---

## Contents

- [What this library owns, and what it does not](#what-this-library-owns-and-what-it-does-not)
- [The accounting identity](#the-accounting-identity)
- [Fragmentation and fit queries](#fragmentation-and-fit-queries)
- [Prediction is not observation](#prediction-is-not-observation)
- [Generations, epochs, and staleness](#generations-epochs-and-staleness)
- [Durability and recovery](#durability-and-recovery)
- [Evidence ingestion over framed TCP](#evidence-ingestion-over-framed-tcp)
- [Quick start](#quick-start)
- [Building](#building)
- [Command line tools](#command-line-tools)
- [Benchmarks](#benchmarks)
- [Tests](#tests)
- [Validation status](#validation-status)
- [Design decisions that constrain the model](#design-decisions-that-constrain-the-model)
- [License](#license)

---

## What this library owns, and what it does not

**In boundary.** Usable-capacity modeling and prediction: consuming authoritative
resource, topology, reservation, degradation, and failure-domain evidence; deriving an
exactly closing capacity account; measuring fragmentation against a demand shape;
answering deterministic fit queries; producing bounded deterministic predictions with
provenance and confidence; persisting owned models, configuration, and history; and
revalidating dynamic external evidence after restart.

**Out of boundary, permanently.** This library does not discover physical links, does
not define link-state truth, does not create or release reservations, does not admit
traffic, does not arbitrate bandwidth, does not place or schedule flows, does not
perform TE allocation, and does not enforce rates. Those systems publish; CFN models
what was published.

Consequently there is no notion of a resource being "up" inside CFN. A resource is
present or withdrawn, its capacity is authoritative or not, and it is degraded by an
explicit record or by the loss of its failure domain. Everything else is somebody
else's truth.

---

## The accounting identity

For every resource, with all arithmetic checked:

    available = raw - reserved - protected_headroom - degraded

and, summed over the whole resource population:

    raw_total = reserved_total + headroom_total + degraded_total + available_total

When a demand shape is bound to the model, available capacity is split exactly:

    available_total = usable_total + spare_total + stranded_total
    raw_total       = reserved_total + headroom_total + degraded_total
                      + usable_total + spare_total + stranded_total

Every term is an exact integer sum. A combination that does not close is a **rejected
evaluation** (`ErrorCode::Contradictory`), never a rounded or clamped number. The
check runs twice - once on the rollup and once on the finished snapshot - and
`verify_closure` re-derives it on demand.

Classification is deliberate and separate:

| Bucket | Meaning |
| --- | --- |
| raw | authoritative capacity, as declared or measured |
| unknown | reported magnitude with no authority. **Never** spare capacity |
| reserved | committed, protected, or pinned capacity held by an external authority |
| protected headroom | the policy floor: `min(absolute + ppm x raw, raw)` per resource |
| degraded | capacity lost to degradation records or to a lost failure domain |
| available | raw minus the three deductions above |
| usable | demand the shape actually places |
| spare | capacity the shape endpoints could carry but the demand does not ask for |
| stranded | capacity that cannot reach the shape endpoints at all |

A lost failure domain subsumes the reservations and headroom on its resources rather
than double counting them: `raw = degraded` for those resources, and the identity
still closes.

---

## Fragmentation and fit queries

Fragmentation is capacity that exists in aggregate but cannot serve a demand shape.
CFN measures it exactly, by maximum flow over a network built from the authoritative
topology:

    available_total = useful + stranded_total
    useful          = deliverable capacity for the shape endpoints
    stranded_total  = available_total - deliverable

Stranding is attributed to three causes that are required to sum back to the exact
total:

- **Segmentation** - the capacity lies on no admissible path between the shape
  endpoints: disconnected resources, resources referenced by no arc, endpoints that
  cannot reach each other.
- **Bottleneck** - the capacity is reachable from the source but separated from the
  sink by a saturated minimum cut.
- **Failure-domain resilience** - the capacity only exists if a failure domain is
  allowed to fail, under `DomainN1` or `DomainN1N1`.

Attribution is self-checking: if segmentation plus resilience ever exceeded the
stranded total, the evaluation would be rejected rather than reported.

**Exactness is stated, not assumed.** A single-flow shape is a plain maximum flow and
is reported as exact. A multi-flow shape is a multi-commodity problem, so CFN reports
a *feasible allocation* (deterministic, priority-then-identity ordered sequential
maximum flow) as a lower bound and the single-commodity relaxation as an upper bound,
and marks the result inexact.

Determinism comes from canonical ordering: nodes, edges, resources, and flows are all
processed in sorted-identity order, so the same inputs produce the same answer, bit
for bit.

---

## Prediction is not observation

Prediction lives in a separate product type with its own provenance. It reads history
and the observed answer; it never writes either.
`PredictionResult::usable_as_observation()` is `constexpr false`, and the predicted
figure lives in a different structure from any observed figure, so there is no code
path that can promote one into the other.

Three bounded deterministic models are implemented, all in integer fixed point so a
prediction reproduces bit for bit on every platform:

- `LastValue` - repeat the newest authoritative observation.
- `BoundedLinearTrend` - integer least squares with a clamped slope, a bounded
  horizon, and a documented fallback when the fit would overflow.
- `EwmaLevel` - exponentially weighted level with a bounded smoothing factor.

Every prediction carries its model, its assumptions, its evidence window, the number
of records rejected and why, and a confidence figure derived deterministically from
sample count, sample age, and extrapolation distance. When the evidence is
insufficient the result is **UNKNOWN** - not zero, and not a guess. Future-dated,
stale, non-authoritative, and over-horizon evidence is excluded and counted.

---

## Generations, epochs, and staleness

Every authoritative input carries a generation. A snapshot records the complete
generation vector it was computed from, plus a digest of the exact resource
population:

    fabric_epoch, model, policy, topology, resource_catalog, resource_set digest,
    resource_count, reservation_snapshot, failure_domain_catalog, degradation,
    demand_shape

`validate_binding` rejects a snapshot whose binding no longer matches, with a stable
precedence order and a specific error code per input (`StaleTopology`,
`StaleReservation`, `StalePolicy`, `StaleEvidence`, `StaleResource`,
`StaleFailureDomain`, `StaleDemandShape`, `EpochMismatch`).
`classify_staleness` returns the same distinction as a value when a caller wants to
explain rather than reject.

Identity is derived, not invented: `make_snapshot_id` hashes the model identity and
the generation vector, so identical inputs always produce an identical snapshot
identity.

---

## Durability and recovery

Durable state lives in a directory:

    state.bin        compacted configuration and history, CRC32C framed
    state.bin.tmp    temporary during compaction; never read
    journal.log      append-only record stream since the last checkpoint

Every durable mutation follows one order:

    validate -> bind authority -> plan -> journal -> fsync -> apply -> publish

A mutation is never acknowledged before its record is durable. Compaction writes a
temporary file, syncs it, renames it over the checkpoint, syncs the directory, and
only then resets the journal; the new journal continues the checkpoint's sequence
number so a record can always be told apart from one the checkpoint already folded in.

Recovery classifies what it finds and reports all of it:

- committed state accepted from the checkpoint;
- committed transactions replayed from the journal;
- unfinished attempts - a `Begin` without a valid `Commit`, discarded, never applied;
- corrupt or torn records - detected by record header CRC and payload CRC;
- ambiguous outcomes - an unreadable journal header or version mismatch, which rejects
  the open instead of guessing;
- **stale live authority** - publisher, worker, lease, and telemetry-freshness claims
  are kept for audit and are **never restored**. They are reported as fenced, and
  `live_authority_valid` returns false for every claim from an earlier boot
  incarnation or fabric epoch;
- **evidence requiring revalidation** - every external evidence reference is marked for
  re-observation on restart.

Every open advances the boot incarnation. The fabric epoch advances only after an
unclean shutdown, so a clean restart keeps generation-bound answers valid while a crash
invalidates them. A corrupt checkpoint is refused unless the caller explicitly consents
to discarding it, and the refusal is still reported.

The durable store is thread safe: mutations are serialised, and reads return copies
rather than references into state a concurrent writer could be rewriting.

---

## Evidence ingestion over framed TCP

Capacity evidence can be consumed from another real OS process over real framed TCP:

    frame: [u32 length][u32 CRC32C(payload)][payload]
    body:  [u16 kind][message]

The session is bound to a fabric epoch, a boot incarnation, and an evidence generation.
A peer that answers with an older epoch or an older generation is rejected; a record
bound to a resource generation the client has already moved past is rejected and
counted. Protocol version mismatches are reported as failures rather than parsed
hopefully. Frames larger than the configured maximum are a protocol violation. All
socket waits are bounded by a caller-supplied deadline, so a wedged peer surfaces as an
error rather than a hang.

---

## Quick start

`@cpp
#include <cfn/cfn.hpp>

cfn::FabricOptions options;                 // in-memory; no durable store
auto opened = cfn::Fabric::open(options);
cfn::Fabric& fabric = **opened;

fabric.set_resource_catalog(resources);     // authoritative inputs first
fabric.set_topology(topology);
fabric.set_failure_domains(domains);
fabric.set_reservations(reservations);
fabric.set_degradation(degradation);
fabric.register_policy(policy);             // then owned configuration
fabric.register_demand_shape(shape);
fabric.register_model(model);

auto snapshot = fabric.compute(model.id);   // generation-bound, exactly closing
auto explanation = cfn::explain(*snapshot, cfn::Limits());
`@

The resource catalog must precede a demand shape, because a shape's endpoints are
resolved against it. `examples/quickstart.cpp` is the complete, runnable version.
`examples/find_package_consumer/` is an independent downstream project that depends
on the installed package through `find_package(cfn CONFIG REQUIRED)` only.

---

## Building

Requirements: CMake 3.20 or newer, a C++20 compiler. No third-party dependencies.

`@sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
`@

| Option | Default | Effect |
| --- | --- | --- |
| `CFN_BUILD_TESTS` | ON | build the test suite |
| `CFN_BUILD_TOOLS` | ON | build `cfn-cli`, `cfn-evidence-node`, `cfn-store-probe` |
| `CFN_BUILD_BENCH` | ON | build `cfn-bench` |
| `CFN_BUILD_EXAMPLES` | ON | build `cfn-quickstart` |
| `CFN_WARNINGS_AS_ERRORS` | ON | `/WX` on MSVC, `-Werror` elsewhere |
| `CFN_ENABLE_ASAN` | OFF | AddressSanitizer where the toolchain supports it |
| `CFN_NATIVE_ARCH` | OFF | tune code generation for the building machine |

Installed package:

`@cmake
find_package(cfn 1.0 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE cfn::cfn)
`@

---

## Command line tools

`cfn-cli`

- `cfn-cli version`
- `cfn-cli demo [--resources N] [--paths W] [--seed S] [--reservations PPM] [--domains D] [--flows F] [--resilience none|n1|n1n1] [--json]`
- `cfn-cli scenario <file> [--json]` - parse a `.cfnscene` file and explain the snapshot
- `cfn-cli fit <file> [--source ID] [--sink ID] [--magnitude N] [--resilience MODE] [--json]`
- `cfn-cli predict <file> --model last-value|bounded-linear-trend|ewma-level [--horizon-seconds N] [--json]`
- `cfn-cli roundtrip <file>` - parse, format, re-parse, and prove the rendering is stable

The scenario format is line oriented and deliberately unforgiving. An unknown
directive, a missing required field, an unparsable number, a duplicate identity, a
forward reference, or an oversized population is rejected with the line number
attached. Nothing is guessed and nothing is silently ignored.

`cfn-store-probe` performs one durable operation in a real process and then either
closes cleanly or dies without closing. `cfn-evidence-node` serves capacity evidence
over framed TCP from a real process.

---

## Benchmarks

`cfn-bench` measures **completed** capacity computation: full evaluations from
authoritative inputs through to a verified snapshot, plus standalone fit queries. Each
measurement reports iterations, wall time, work units per second, and a checksum that
proves the work was actually performed.

Every population is **SYNTHETIC**: generated in memory from a seed, modelling no
physical network. The dimensions varied are resource count, path width, reservation
density, failure-domain complexity, degradation, and fit queries. Results are labelled
synthetic in both the text and JSON renderings.

---

## Tests

`@sh
ctest --test-dir build --output-on-failure      # 15 suites
./build/tests/cfn-tests --list                  # every case
./build/tests/cfn-tests --filter=property.      # one suite
`@

| Suite | Covers |
| --- | --- |
| core | identities, checked arithmetic, bounded values, RNG determinism, text, time, provenance rules |
| accounting | deduction chains, unknown capacity, lost domains, contradictions, overflow, closure verifier |
| flow | maximum flow on known graphs, limits, min cut, work budget, cancellation |
| fragmentation | exactness, segmentation, bottleneck, resilience, granularity, multi-flow intervals |
| snapshot | generation vector, deterministic identity, confidence, evaluator rejection paths |
| invalidation | every bound generation enforced individually, precedence, classification names |
| prediction | model behaviour, UNKNOWN paths, clamps, determinism, prediction/observation separation |
| persistence | journal round trip, torn tails, corrupt payloads, compaction, recovery classification, authority fencing |
| scenario | parsing, round-trip stability, malformed input, limits |
| explain | field contents, budgets, JSON well-formedness, boundary statements |
| property | seeded randomized fabrics, identity reconciliation, capacity drops, cyclic topologies |
| adversarial | oversized, contradictory, stale, structurally invalid, and ambiguous inputs |
| concurrency | readers with writers, shutdown during work, concurrent writers, atomic publication |
| multiprocess | real child processes: kill, restart, epoch advance, stale epoch rejection, framed TCP |

**No test carries a timeout.** A test that hangs is a defect to diagnose. The
multiprocess tests do use bounded *process* and *transport* deadlines, which is a
different thing: when a deadline expires the case **fails** with diagnostics, so a
wedged child is reported rather than hidden.

---

## Validation status

Labels are used precisely.

**REAL** - performed in this environment against the actual artefact:

- Release, Debug, and AddressSanitizer builds, all with `/W4 /WX` on MSVC 19.44.
- The complete test suite in all three configurations.
- Real OS processes: child processes spawned, killed without closing, restarted, and
  observed to advance the boot incarnation and fabric epoch, to reject stale work, and
  to fence persisted live authority.
- Real framed TCP between two processes, including a peer killed mid-session.
- Real durable files: fsync barriers, atomic rename, journal truncation and repair.
- Installed CMake package consumed by an independent downstream `find_package`
  project.
- MSVC `/analyze` over the first-party sources.

**SYNTHETIC** - generated in memory, models no physical network:

- Every benchmark population and every randomized property/adversarial population.
- The `cfn-cli demo` population and the `cfn-evidence-node` population.

**UNSUPPORTED** - not claimed, not implemented, not tested:

- No physical network, switch, NIC, DPU, RDMA, NVLink, or optical validation of any
  kind was performed, and no result here should be read as one.
- The POSIX socket path in `src/net/socket.cpp` is written against BSD sockets but
  was not exercised in this environment; only the Windows path was validated.
- No GCC or Clang build was performed here. The CMake configuration supports them and
  the build flags are defined, but only MSVC 19.44 was validated.
- Multi-commodity flow is not solved optimally. Multi-flow shapes report a feasible
  lower bound and a relaxed upper bound, and say so.
- `DomainN1N1` is exact but bounded: the failure-domain pair count must fit the
  configured resilience budget, otherwise the evaluation is rejected with
  `LimitExceeded` rather than approximated.

---

## Design decisions that constrain the model

These are deliberate, and each one is enforced rather than documented and hoped for.

1. **A capacity resource backs exactly one topology arc.** If two edges, or an edge
   and a node transit, drew on the same resource, the resource would become a hub that
   lets flow leave along a different arc from the one it entered. That is not a
   property the authoritative topology declares, so it is rejected as contradictory.
   Aggregate capacity is expressed by publishing an aggregate resource.

2. **Node transit capacity constrains transit only.** A flow's own endpoints are not
   charged their node transit capacity, because they are not transiting.

3. **UNKNOWN is never spare.** A resource without authoritative capacity contributes
   only to the unknown bucket; its deductions are not invented, and it can never
   appear as available or usable capacity. A policy may instead refuse to evaluate at
   all while any present resource lacks authority (`reject_unknown_capacity`).

4. **Evidence dated after the evaluation instant is not fresh.** A clock cannot vouch
   for the future.

5. **Everything that can grow is bounded.** Resource, topology, reservation, flow,
   failure-domain, explanation, history, journal, frame, and sample populations all
   have configured limits, and exceeding one is an error, never a truncation.

6. **All externally influenced arithmetic is checked.** Capacities are summed through
   an accumulator that records overflow instead of wrapping, and a snapshot whose
   totals overflow is rejected.

7. **No callback, evaluation, or persistence step ever runs while a registry lock is
   held.** Readers take one immutable view and work outside the lock, which removes
   the read-write re-entry and shutdown-deadlock failure modes rather than mitigating
   them.

8. **Cancelled work publishes nothing.** Cancellation is cooperative and checked at
   bounded intervals; a cancelled evaluation returns `Cancelled` and produces no
   snapshot.

---

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
