Isolated legacy ABI compatibility core
=====================================

This is an in-progress implementation of docs/legacy-plugin-compat-plan.txt.
It builds a reusable C++ library and executes actual old SDK code against fake
engine services. It is not linked into RED4ext, packaged, or enabled for plugins.
The historical research bundles under docs/ remain unchanged.

Production integration now targets official RED4ext v1.31.0 at
9f135ac339b00c55a68d06ce7106e960dd82962a. The release has identical runtime
sources and the same SDK pin as the previous 47ac01c baseline. No profile,
historical fixture, layout or operation identity changes are required by that
release. See docs/red4ext-1.31.0-update.txt and its machine-readable evidence.

Implemented contracts
---------------------

schema/2.31-to-2.50.json and tools/generate_schema.py:
  Explicit type/field policies, source/target ABI, pointer types/nullability,
  primary/secondary operation identities, signature labels and address kind.
  Generation rejects ambiguous identities, overlapping/out-of-range fields,
  untyped pointers, unknown policies and incomplete VALUE coverage. The JSON
  Schema describes structure; the generator also enforces semantic rules.
  C++ Schema::Validate repeats structural/closure checks for runtime descriptors.
  Generated descriptors are immutable. Both actual SDKs are compiled against
  field/size assertions. Signatures are manually reviewed C++ declarations;
  signature labels do not infer a prototype from a raw address or prove that
  an arbitrary cast matches the declaration.

Domain/DomainRegistry/CallRouter:
  One atomic state couples terminal Close with active calls and retained owners.
  New external calls are rejected after Close; already admitted nested work and
  retained-owner cleanup may finish. TLS is a nested domain stack. Typed compiled
  slots retain the domain independently of the calling thread and preserve the
  compiler's argument/aggregate-return/unwind ABI.
  Retired slots are never reused. A concurrent invocation holds its binding
  until the call and callable cleanup finish. Publish marks an entry permanent:
  it cannot be retired without a reviewed engine detach contract. Capacity is
  bounded per signature/tag bank. Banks intentionally retain published bindings
  through process termination; static destructors do not call guest DLL code
  after an unknown module-teardown order. Registration handles have one control-plane
  owner; concurrent entry invocation/retirement is supported, concurrent mutation
  of the same Registration handle is not.
  DomainRegistry validates module ranges and domain identity on the control
  plane. It is NOT the preallocated loader-notification publication mechanism.

EarlyModules/ModuleNotifications/ResolverRouter:
  Preconfigured ABI directory roots select domains before DLL initializers run.
  Roots currently require absolute ASCII drive paths; notification filenames may
  contain Unicode. Policies must be canonical, disjoint, immutable and installed
  before loading selected modules. This is not automatic ABI inference from Query.
  Notifications publish into preallocated, never-reused records without calling
  OS services, taking application locks or allocating. Unloads leave tombstones;
  native module records prevent address reuse from inheriting a legacy domain.
  Overlap/capacity errors latch instead of allowing native fallback.

  ResolverRouter combines those records with sealed, identity-checked catalogs.
  Native callers use the supplied original resolver, even inside a legacy TLS
  scope. Legacy callers select a catalog by their actual module; cached typed
  entries retain their domain when invoked later from worker threads. Unknown
  hashes, missing domains and closed/retired domains return structured failures.
  Bounded append-only diagnostics preserve caller/hash/domain/error and report
  overflow; formatting and persistence belong outside critical paths.
  A preparation hook retains defining-module ownership before exposing an entry.
  The integration still owns failure policy, target verification, root selection,
  Query reconciliation and plugin Unload suppression. No production loader is
  connected to this router yet.

CallbackBridge:
  Copies old fixed callbacks through their handler ABI and destroys payloads in
  the defining domain while retained ownership still blocks unload. A failed
  copy releases provisional ownership. Handler destruction must not throw.
  The caller synchronizes payload access as required by the callback itself.
  Logical ownership does not acquire an OS module reference. Loader integration
  must keep code mapped and must not run plugin Unload until CanUnload is true.

RttiAdapter/PropertyAdapter/ModulePin:
  Compiler-built historical virtual tables now cover RTTI type/class/async-ID
  lookup, script-name lookup/conversion, common IType metadata, and register /
  post-register callbacks. The old destructor slot remains at 0x138; the native
  2.5 table moved it to 0x158. The by-value old callback has a 32-byte payload
  and 0x28 total size. Its payload is copied through the old handler and consumed
  inside the retained domain before the compiler destroys the parameter.
  Native callback sinks receive an owned closure for constructing their 0x48
  callback, including when a sink publishes before throwing.
  RegisterType forwards only already established canonical identities; publication
  is recorded before the native sink can retain or throw. An arbitrary class
  descriptor made with guest C++ new remains unsupported. BindNativeRtti supplies
  current-SDK virtual calls and current callback construction; its receiver is
  still synthetic in the tests, not a live game singleton.

  Guest vptr reads return immutable compiler-built tables, never native vptrs.
  Class and common type views share canonical zero-offset IType identities.
  No direct access to private CRTTISystem maps is granted. Unsupported virtual
  operations throw UnknownOperation; deleting borrowed singletons/types is a
  fatal contract violation. Tables use a bounded, never-reused bank. Unpublished
  retirement requires drained callers and tombstones cached entries; callback
  publication retains the adapter and backing for the process lifetime.

  PropertyAdapter promotes a constructed native property before old inline
  construction writes its fields. The allocator boundary must identify the
  RTTIProperty pool explicitly; size equality does not identify a property.
  Construction/mapping failures destroy provisional native backing. Publish
  must precede returning the guest allocation: inline construction can expose
  it through a class array without another engine call. Published properties
  remain resident because no native removal/drain contract is established.
  Property cleanup services must not throw; adapter handles have one owner on
  the control plane.

  ModulePin acquires a real OS module reference from an address. Keep pins in
  the shared lifetime owner until all defining-module calls/destructors drain.
  Acquisition/release is a control-plane operation, not notification/VEH work.
  It does not suppress plugin Unload or automatically discover dependencies;
  both still require loader integration. Do not confuse an OS reference with
  preservation of the plugin's own application state.
  ModulePins retains every defining module reached through the resolver, before
  a callback can be published during DllMain. Concurrent first callers acquire
  independent OS references and atomically keep one, with no application mutex
  held across a loader call. Tests balance duplicate references and release the
  ordinary LoadLibrary reference before invoking the retained fixture. A failing
  initializer and arbitrary dependency/module sharing policies remain untested.

CallbackAdapter/FrameAdapters/FunctionAdapter:
  Reusable adapters now own the old-header chain's callback, frame, opcode and
  class/static-function construction boundaries. The fake engine supplies native
  services only. Registration callbacks construct the current 0x48 callback from
  a retained old handler/payload without invoking the SDK's broken copy template.
  Script entries marshal borrowed frames and translate context identities;
  opcode entries synchronize the active frame around reviewed native operations.
  Results require explicit synchronous scalar contracts (1/2/4/8 bytes) or
  ScalarResult::Void(), which requires a null output buffer. Frame synchronization
  still runs for void methods, including exception copy-out. Opcode auxiliary
  pointers must be null. Handles, pointer results, escaping buffers and arbitrary
  opcode semantics are not implemented. Result contracts are not inferred from
  the function-constructor signature or defaulted safely for arbitrary functions.
  Unknown opcode slots reject calls. Table storage is bounded and never reused;
  cached tables remain safely indexable after their routes become tombstones.
  SharedPointers requires an owner for translator state, backing and services.

  FunctionAdapter preserves the old Create allocation address, validates its
  domain, allocates the larger native object, binds the script entry and promotes
  the guest view BEFORE calling a constructor that may publish that entry.
  Preparation failure frees unconstructed storage and quarantines the guest.
  After constructor entry, a throw or unexpected return retains the entire state:
  partial engine publication has no established rollback. Missing/failed detach
  similarly retains the callback, backing and services for the session.
  Optional detach must guarantee all native references and calls have drained;
  only then may the adapter retire views, destroy backing and retire the route.
  That capability is supplied by tests, not established for the game. Cleanup
  services must not throw. The adapter handle has one control-plane owner.
  This profile currently covers the class/static function shape; base/global
  function objects and polymorphic dispatch require separate contracts.

ArrayAdapter:
  Binds empty or already populated arrays through reviewed owning Class/Function
  fields. Adoption validates native element identities and allocator metadata;
  an optional preparation service establishes missing Function/Property views.
  It borrows the existing buffer without copying or prematurely freeing it.
  It supports function/property pointer elements and mediates the actual old
  DynArray_Realloc boundary. Element type comes from that field, not byte size.
  Empty arrays store an allocator vtable in their data slot; allocated arrays
  store it after the element buffer. The schema describes that union explicitly.
  Compiler-built allocator virtual members handle the SDK's copied-vptr Free
  and GetHandle calls. Other allocator methods reject calls. Vtable slots are
  bounded, domain-owned and never reused; cached retired entries reject calls.

  Growth/shrink prepares native backing and both element/tail views before
  changing the owner. Allocation or mapping failure preserves the old header
  and contents. Retired buffers keep guest-address tombstones. Empty shrink
  restores the allocator identity; the old inline destructor frees once through
  the adapter, including retained-owner cleanup after domain admission closes.
  Oversized requests fail before native allocation. A busy adapter retains its
  services/backing instead of destroying reachable arrays.

  This requires exclusive access against guest AND native observers during
  mutation, valid native owner lifetimes, fresh native allocations and nonthrowing
  free services. Owner binding/retirement is a control-plane operation; retirement
  additionally requires callers to stop using its allocator identity. Native
  mutation can be reconciled through Synchronize at an explicit, quiescent
  boundary. It adopts a replacement buffer and retires old guest aliases without
  freeing storage already disposed by the native mutator. Same-buffer element
  changes are validated; in-place capacity changes are rejected because their
  allocation lifetime cannot be established. This is not automatic interception
  of every native mutation. Arbitrary stack/private arrays, array copy/move,
  nontrivial elements and general pool allocation remain unsupported. Non-null
  move callbacks are rejected before allocation: the actual old SDK callback
  uses assignment into fresh storage, whose nontrivial lifetime problem is not
  repaired by translating pointers. No universal element constructor is inferred.

AddressCatalog:
  Resolves only a sealed catalog for the active, matching source/target domain.
  Binding checks primary plus secondary identity, signature and function/table
  kind. Unknown relocations do not fall through to raw native addresses. Target
  executable/database verification is the integration caller's responsibility.
  The table case is exercised through actual old OpcodeHandlers::Get/Run.

ObjectRegistry:
  Bounded canonical mappings distinguish domain, type, lifetime generation and
  guest/native extent. Linear byte ranges must be explicitly registered; other
  interior pointers are rejected. Retirement waits for ReadGuards, leaves guest
  address tombstones, and requires a new generation/address for native reuse.
  No backing memory or OS pages are allocated or freed here. The caller must
  preserve native lifetimes for any use of translated pointers beyond a guard,
  retire mappings before freeing backing, and reserve guest addresses against
  reuse. This synchronized control-plane registry is NOT a VEH-safe lookup index.
  Reviewed zero-offset opaque base aliases share the concrete mapping. A base
  alias does not inherit a concrete type's linear-byte interior permission.

ValueMarshaller/BorrowedValue:
  Conversion validates in bounded scratch space before writing destination
  fields; unknown pointers/nonzero opaque fields fail without partial writes.
  Pointer-array conversion similarly validates the entire bounded input first.
  Caller ownership/synchronization is still required: these are not atomic
  multi-field transactions against concurrent engine readers.
  Borrowed frame bindings preserve native-only state, translate function/parent
  identities, maintain nested frame identity, and synchronize around nested
  native calls and exception unwinding. They never destroy the native frame.
  If synchronization itself fails during exception handling, the compatibility
  error takes precedence. No throwing destructor performs implicit copy-out.

LiveMemory/ViewArena/ExceptionBackend/InstructionPlan (P3 prototype):
  Fixed-capacity immutable mappings and atomic access leases form a separate
  handler data plane. Transfer preflights reviewed fields and pointer values;
  unmapped fields and partial pointer stores are rejected. Retirement waits
  for admitted accesses. Native backing lifetime remains the caller's contract.
  The Windows arena reserves bounded page ranges per allocation, commits only
  requested provisional pages, then promotes the original address to a protected
  view. One page remains the default limit; explicit larger limits support arrays.
  Compound element/allocator mappings publish together with no visible prefix
  on validation failure. The handler still performs no allocation or locking.
  Retired addresses remain reserved until the arena is destroyed. Live views
  retain their domain; backend removal/arena destruction require drained callers.
  Function/class/property metadata and typed pointer-array elements now have
  explicit field rules. This is incomplete graph/virtual-table coverage.
  The VEH mediates only owned view access violations; it uses bounded code
  decoding, fixed diagnostic cells, and no page unprotect/single-step window.
  Scalar moves/extensions, selected integer arithmetic/flags, conditional moves,
  legacy SSE moves and aligned scalar atomic forms have implementations.
  Unsupported encodings/accesses continue exception dispatch with a diagnostic.
  A hardware differential oracle now checks supported encodings; contention,
  retirement, worker/unwind, foreign-exception and nested native-fault controls
  also pass. Windows native accesses use non-inlined SEH probes with /EHa.
  A native access failure releases leases and handler reentrancy state, reports
  NativeFault, and preserves normal exception dispatch. Semantic preflight does
  not promise rollback if native memory becomes invalid during a multi-part write.
  Atomic operations require suitably aligned live scalar objects and a compatible
  native concurrency contract; alignment alone does not establish that contract.
  This remains experimental. Trap-flag execution is rejected, and no general
  debugger compatibility, instruction coverage or performance result is claimed.
  Bulk/string/AVX instructions, general virtual dispatch, nontrivial/generic
  containers and broader object lifetimes are still required.

The profile's Frame VALUE policy classifies every historical byte. Unknown
fields must be zero; bytecode/storage pointers need explicit bounded mappings.
This deliberately rejects states that have not been reviewed. Unclassified
LIVE_VIEW fields are rejected, even when record sizes are equal.
Schema backing storage must remain immutable and outlive catalogs, registries
and value plans; the generated constexpr profile satisfies that requirement.

Build and test from the repository root
--------------------------------------

Portable algorithm/concurrency checks (Clang, no Windows ABI claim):
  python3 -m unittest discover -s tests -p test_compatibility.py -v
  python3 scripts/test-compatibility.py --platform portable --label linux

Microsoft x64 build using the existing xwin SDK and pinned historical Git object:
  python3 scripts/test-compatibility.py --build-only
  python3 scripts/test-compatibility.py --run-only --label system-wine

Unchanged-plugin startup probe (a local DLL, copied into a fresh private tree):
  python3 scripts/test-compatibility.py --run-only --probe-only \
    --probe-plugin '<path-to-mod_settings.dll>'
The paired bridged/unbridged cases use a test RED4ext.dll resolver and synthetic
engine receiver. They stop at script registration; they do not invoke the
plugin's registration callbacks, hook game code, compile scripts or test teardown.
Startup-only serial-counter/handle scaffolds are test services, not supported
production operations. Unknown resolutions yield inaccessible diagnostic pages;
first use records the hash/access and fails. That diagnostic behavior is confined
to the probe and is never ResolverRouter's production fallback. A checkpoint
terminates the private host without DLL detach, whose engine services are absent.

GE/Steam Runtime, substituting actual tool locations:
  python3 scripts/test-compatibility.py --run-only --label ge-proton \
    --wine '<GE-Proton-root>/files/bin/wine' \
    --runtime '<SteamLinuxRuntime_4-root>/_v2-entry-point'

Native Windows with an existing historical SDK repository:
  python scripts/test-compatibility.py --old-sdk '<historical-sdk-repository>' \
    --generator "Visual Studio 17 2022" --label native-windows

Historical headers always come from git archive at the recorded commit, even
when --old-sdk is supplied. No downloads, initialization, Git mutations, source
header edits or game launches occur in the local driver. Cross builds use the
existing Microsoft SDK/CRT; no MinGW runtime is used. Wine may need permission
outside a restricted sandbox for its local IPC.

Generated sources, binaries, build records, logs and isolated runtime prefixes
are under build/compat/{portable,win64}. --run-only rejects changed inputs or
binaries. Each runtime gets a separate results-<label>.json referencing the build
record hash. Runtime timeouts are failures and terminate only the test process
tree/group. Neither old research snapshots nor production packages are rewritten.

The native-Windows workflow is .github/workflows/compatibility.yml. Its Linux
job prepares the pinned SDK with the existing patch helper; the Windows job
consumes that exact header artifact, verifies its checksum and lockfile identity,
then uses ClangCL with Microsoft's SDK/CRT. This avoids relying on the patch
helper's Linux temporary-file behavior on Windows. CI results remain pending
until the workflow is actually run; adding the job is not Windows validation.
Runner/toolset reference:
https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md

Test coverage and remaining implementation
-----------------------------------------

On 2026-10-25, all 15 Win64 suite runs and both unchanged-DLL startup controls
passed under Wine 11.20 and GE Wine 11.0 through Steam Runtime. Eleven Python
schema tests, two portable Release tests and two ASan/UBSan/LSan tests also
passed. E33 records the exact inputs, results and limits; E30-E32 are historical.
Current build and run identities are under build/compat/{portable,win64}; the
sanitizer CTest record is under build/compat/asan/Testing/Temporary/LastTest.log.
See evidence E33 for current build identities. The earlier official-release
validation remains historical in docs/red4ext-1.31.0-update.txt.
Native Windows CI is configured but has not run in this session.

Core tests exercise schema rejection, cross-domain resolution, aggregate returns,
mixed arguments, worker callbacks, captured-payload cleanup, closure races,
published-entry retention, stale slots, generation/address reuse, transactional
pointer arrays, nested frame identity, failed copy-out and native-tail retention.
The old-header DLL tests execute actual SDK allocation/Create, inline class
insertion/lookup, callback handling and GetParameter/opcode lookup with optimized
and unoptimized code. Class and array storage are now protected live views:
inline insertion publishes the translated native function pointer immediately,
and retained metadata writes/native changes are observed in both directions.
The explicit fake publication barrier has been removed from this implementation
fixture. Fake native constructors and detach remain test services. The original
snapshot/retained-write negative control remains preserved in the research lab.
Platform controls retain the earlier dependency/parent notification and small
generated-bridge C++/SEH/unwind tests. No general DBI backend is selected.

live_tests.cpp executes 171 instruction encodings with 96 inputs each (16,416
cases) both on the CPU and through InstructionPlan, comparing memory, registers,
XMM state and defined flags. It caught and now covers false CMOV r32 zeroing the
upper destination bits. It also checks pointer-array preflight, native-address
reuse, contended atomic updates/retirement, unsupported instruction rejection,
protected-page promotion, bounded canonical borrowing and diagnostic overflow.
Windows controls forward unsupported/foreign/retired faults to a second VEH,
preserve unrelated single-step exceptions, unwind worker C++ exceptions, and
recover cleanly after injected faults in native backing. The initial optimized
native-fault probe failed; non-inlined probes with /EHa passed the same controls.
The failing log is retained locally as build/compat/win64/native-fault-diagnostic.log.
Finite differential cases are evidence for these forms, not a full x64 proof.

function_tests.cpp checks rejection before allocation across domains, rollback
before native publication, original-pointer preservation, explicit detach and
exactly-once destruction. Separate process cases verify retention when a native
constructor publishes then throws, when detach is unavailable, and when detach
fails. Published state is not destroyed during static process teardown.

array_tests.cpp executes the actual historical header through empty allocation,
growth beyond one page, repeated inline insertion, shrinking, empty shrink and
destruction. It checks translated pointer contents, allocator tail/empty-slot
dispatch, oversized/allocation/mapping failure preservation, stale aliases and
retired cached vtables. Each run balances six native allocations and frees.
Independent portable checks cover compound publication failure and vector stores
that change both an array pointer and its capacity discriminator.

registration_tests.cpp adds actual old CRTTISystem::Get, virtual lookup,
callback installation, pool lookup, CProperty::Create, function creation and
inline class-array registration in one chain. It checks common type metadata
and CName returns, two callback phases, delayed callback execution, publication
followed by a sink exception, script invocation, and cleanup after Close. The
loader's DLL reference is released before registration; retained OS pins keep
the guest callable. Independent controls verify final module release, canonical
base aliases, cross-domain rejection, retired cached virtual entries, and
property construction/mapping rollback. Native RTTI services, pool registry,
function constructor and allocator are test services. Separate array adoption
checks cover populated native buffers, native growth/clear, allocator mismatch,
canonical element preparation and subsequent old-header growth/destruction.
This is not a test of existing populated game classes or game constructors.

plugin_probe.cpp executes actual old-header initialization and worker resolution
through the RED4ext_ResolveAddress export, the notification registry and sealed
catalog router. It registers a callback in DllMain and pins the defining DLL
before LoadLibrary returns. The unchanged Mod Settings DLL also reaches the
script-registration checkpoint through both adapted callback slots. Without the
adapter, the same DLL faults when the native-layout copy reads handler +0x40.
See E33 for the counter/handle scaffold limits and the exact installed DLL hash.

This supplies P0/P1/P2 components and a P3 live-view prototype. It does not
complete their acceptance gates. Next implementation work, in order:
  1. Support guest-created class descriptors and their reverse virtual/lifetime
     contracts. RegisterType currently rejects these ordinary C++ allocations;
     this is essential for the body of Mod Settings' registration callbacks.
  2. Complete broader function/result/property/object/handle lifetimes, nontrivial
     arrays and copy/move, and native engine services. Wire explicit native-array
     synchronization into every admitted mutating operation. Verify coverage
     with additional old-header fixtures and snapshots.
  3. Connect the early routing/pinning components to RED4ext only after those
     pointer contracts are complete; preserve native behavior and add admission,
     diagnostics and lifecycle handling. Then implement reviewed hook contracts.
  4. Complete G1-G5 engine/coverage/Windows/performance gates in
     docs/legacy-plugin-compat-readiness.txt before runtime distribution.
No loader runtime-version override, general plugin admission, engine detach,
gameplay, save/load, game performance or production hot unload is established.
