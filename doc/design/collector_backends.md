<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Collector backend integration

The initialization phases in `system_phase_init_discovery.mmd` remain unchanged.
`BuildOrchestrator` passes the same backend registry to topology, collector, and
metric construction. Each builder also accepts an injected registry for tests.

`CollectorBackend` is a stateless descriptor containing identity, name, discovery,
collector construction, metric registration, and optional target preparation.
Existing backend functions need no virtual adapter objects: targets and collectors
already own their resources, including shared libsensors API state. Registry spans
are borrowed only during construction; collectors must not retain pointers into them.

`BuiltinCollectorBackends()` explicitly assembles the descriptors. Its order controls
discovery and duplicate-name precedence. There is no static-constructor registration.
A null discovery callback marks a known but unavailable backend, allowing collector
selection to distinguish a compiled-out backend from an unknown name. Live backends
must provide construction and registration callbacks. Registry validation rejects
duplicate names/IDs and incomplete live descriptors; discovery checks target ownership.

Backend-specific implementation lives under `src/impl/scmi/`, `src/impl/procfs/`,
`src/impl/libsensors/`, and `src/impl/windows/`. Each folder groups its discovery,
targets, operations, collectors, metrics, and configuration parsing where applicable.
Shared interfaces and orchestration remain in the common layer folders.

To add a backend:

1. Implement its targets, collectors, metrics, and operation builders inside its module.
2. Define a descriptor in that module; keep platform APIs and casts there. Collector
   creation returns one collector or a null pointer when no live collector is needed.
3. Add the descriptor to the explicit assembly and its source to CMake. Retain a
   descriptor with null discovery when compiled out if its selection name is supported.
4. Add the stable collector identity and persistence handling deliberately. Existing
   wire values and target-key prefixes are compatibility contracts, not registry indices.
5. Test discovery, lifecycle, disabled builds, and saved-session replay. The injected
   backend test demonstrates the common path without adding a production enum member.

`AnyOperationBuilder` owns a copyable strategy through `std::function`, preserving
independent concrete types and value-copy semantics. It has no hard object-size limit;
large builders may allocate. `Target<T>()` exists for backend-local metadata inspection
(currently SCMI availability filtering), not common dispatch. Default/empty strategies
return NOT_IMPLEMENTED and the common invocation helper translates operation-ID exhaustion.

This refactor does not change serialization or remove collector-side operation casts.
Offline replay must not depend on live discovery. Correcting historical enum mappings
requires a separate compatibility plan. Configuration-time casts remain useful validation;
repeated sampling casts can be addressed separately with typed collection plans.
