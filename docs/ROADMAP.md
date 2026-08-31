# Roadmap

## Goal

`ofxIC` should let an openFrameworks application configure and inspect one
external inference endpoint, hold a chat session, search explicitly loaded local
documents through the allowlisted tool, and return either a grounded answer with
source identifiers or an actionable failure.

The addon remains an inference connector. Model runtimes, model downloads,
provider billing, and unrestricted tool execution stay outside its boundary.

## Definition of done for the core path

The core path is complete for a release when the regular `ofxICExample` can:

1. configure a local `llama-server` or hosted OpenAI-compatible endpoint without
   storing its token in the settings file;
2. inspect `/v1/models` and make the difference between reachability and usable
   inference explicit;
3. complete a multi-turn chat session;
4. load a deliberately selected `.md` or `.txt` file, invoke only
   `search_documents`, and render a grounded answer with source identifiers;
5. preserve cancellation, timeout, transport, provider, and invalid-response
   failures through the API, GUI status, and console log.

Deterministic injected-transport and GUI-fixture tests prove protocol and
lifecycle behavior. A separately opted-in model-backed run is required before
claiming that a particular endpoint, model, token, or document workflow works
end to end.

## Current baseline

The five core capabilities above are implemented and covered by deterministic
tests on Windows and Linux. The real GUI has also completed the grounded hosted
tool path in a separately gated run. Provider-credit failures are now preserved
with their bounded response detail and active endpoint/model route.

The supported secondary protocol clients—transcription, SAM bridge, image/video,
and music—remain explicit adapters to independently configured processes. They
do not widen the core addon into a generic runtime or provider framework.

## Prioritized milestones

### M1 — Reproducible local core path

Status: completed. The Release GUI path passed against a local `llama-server`
with a tool-capable Qwen3.6-27B GGUF on 2026-08-27: endpoint inspection,
two model requests, allowlisted document search, and an
`ARCHITECTURE.md#chunk-2` citation were all observed. The model and retained
runtime evidence remain outside Git.

Provide one marker-gated Windows smoke command for a user-supplied
`llama-server` executable and model. It must launch the server outside the addon,
drive the regular GUI through endpoint inspection, chat, local document search,
and a cited final answer, then stop only the process it launched.

Completion evidence:

- the deterministic suite remains green;
- the script rejects missing opt-in, executable, model, or document inputs
  before launch;
- one recorded run exercises the regular Release GUI against the supplied local
  model and records the selected model plus source identifier;
- no model, server binary, generated project, token, or runtime output enters
  Git.

### M2 — `0.2.0` release closure

Status: completed. All public client classes are exercised by the canonical
example; deterministic Windows and Linux tests plus the Linux nightly GUI passed
from commit `c92fdb7`; release notes distinguish fixture and model-backed
evidence; installation pins the CI-tested `ofxImGui` revision; and tracked-file
inspection found no runtime binaries, models, caches, generated projects, or
live evidence.

Close the release around the demonstrated surface rather than adding another
backend.

Completion evidence:

- public headers and canonical example agree on every public class;
- Windows deterministic tests and Linux nightly GUI workflow pass from the
  release commit;
- release notes distinguish deterministic, fixture, and model-backed evidence;
- installation, C++ standard expectations, endpoint configuration, credential
  handling, and local-live-smoke instructions are reproducible from a clean
  openFrameworks checkout;
- the release tag contains no binaries, models, caches, generated projects, or
  runtime output.

### M3 — `0.2.x` evidence-triggered hardening

Keep the public surface stable while real applications exercise `0.2.0`. Accept
patch work only when a supported workflow supplies a reproducer. Likely
candidates are streaming response compatibility across a second real endpoint,
provider-specific media controls requested by an example, or remote-job
cancellation when a supported provider exposes a concrete cancel contract.

Each candidate needs a reproducer, the responsible protocol boundary, and a
claim-matched test before it becomes scheduled work.

#### M3.1 — Local runtime and asynchronous-job lifecycle

Status: completed from repeated GUI reproductions on Windows. Versioned
`llama-server` and `sd-server` installations were present but stale cached or
hard-coded paths made detection and launch unreliable; asynchronous media jobs
also required a manual poll during normal GUI use.

The canonical example now uses a version-independent, deterministically tested
runtime-path resolver, preserves valid manual executable choices, persists
non-secret runtime/model/launch settings with backward-compatible schema
migration, and automatically follows supported media and music jobs until a
terminal state. Finished payloads enter the existing preview/player path.
These changes remain application-layer process orchestration and do not move a
native runtime into the addon.

#### M3.2 — Observable local runtime supervision

Status: completed from the repeated GUI failures that previously surfaced only
as disabled controls, indefinite “starting” text, or opaque process errors.

The canonical example now supervises all five local runtime processes through
one internal component with explicit stopped/starting/ready/exited/failed
states, port-conflict preflight, executable-relative working directories,
bounded stdout/stderr capture, console mirroring, PID and elapsed startup
status, and owned-process shutdown. A central Overview tab provides start/stop
control while task tabs retain configuration and detailed server output.
Deterministic tests exercise initial/failure state and a real model-free Windows
child process through output capture and clean exit. This remains example-layer
orchestration and does not expand the public addon API.

#### M3.3 — One-click task continuity and private local history

Status: completed from the repeated GUI friction where users had to understand,
start, and poll a separate runtime before they could perform the task they had
already requested.

Local chat and inspection, stable-diffusion.cpp media, whisper.cpp
transcription, ACE-Step music, and localhost SAM actions now use one deferred
task path: start or attach to the configured external process, wait
asynchronously for explicit port readiness, and resume the original action.
Only one task may wait at a time; it is visible and cancellable, has a bounded
startup deadline, and never triggers provider fallback. Externally managed
listeners are distinguished from owned child processes and are disconnected,
not terminated.

The example also keeps a bounded, user-clearable history of 100 task outcomes.
It records timestamp, task type, normalized outcome/status, and saved output
path, but no prompt, document content, credential, provider body, or generated
payload. Serialization, retention, unsafe-field rejection, external-listener
attachment, process lifecycle, and the existing protocol clients remain covered
by deterministic tests. This is application-layer orchestration; no runtime or
generic job framework enters the addon API.

The model-free Windows vertical smoke `scripts/smoke-one-click-local.ps1`
additionally proves that the regular Release GUI starts two different fixture
runtimes, continues ACE-Step and SAM tasks, persists privacy-aware History,
stops its owned children, and reports a never-ready child through automation and
History after a bounded test timeout.

#### M3.4 — Reproducible release engineering and support diagnostics

Status: completed as the supportability gate for `0.2.1-dev`.

Windows development now has one canonical rebuild command that verifies source
membership in the generated openFrameworks project, uses `Rebuild` by default,
verifies that the expected EXE was rewritten, and reports the exact artifact.
Project regeneration remains an explicit option because it is needed only after
adding sources and can fail independently of compilation. A second command
combines the deterministic CMake suite, canonical example rebuild, and
model-free GUI lifecycle smoke into one release-candidate check.

The public headers expose explicit semantic development-version metadata, and
the Overview tab exports a privacy-aware diagnostic snapshot. The report keeps
support-relevant endpoint, task, and process state while excluding credentials,
prompts, document content, payloads, server output, URL secrets, and exact
runtime/model paths. The regular GUI smoke verifies both the report schema and
those privacy exclusions.

#### M3.5 — Crash-safe application persistence

Status: completed after auditing the three existing local writers.

Example settings previously truncated their destination directly, History
removed its old destination before rename, and diagnostics carried a separate
platform-specific implementation. They now share one example-internal atomic
writer because all three are real consumers of the same non-trivial behavior.
The writer uses a unique temporary file beside the destination, creates missing
parent directories, replaces only after a complete write, preserves existing
data when replacement fails, and removes failed temporary artifacts.

Deterministic tests exercise initial creation, complete replacement, destination
preservation, error reporting, and temporary-file cleanup. The helper remains
outside the public addon surface and does not introduce a storage framework.

#### M3.6 — Conventional openFrameworks package surface

Status: completed after treating the original all-in-one GUI as an integration
workbench rather than the only onboarding example.

The addon now relies on normal recursive `src` discovery from `addon_config.mk`
and provides `example-chat` as a focused Project-Generator-compatible example
using `ofxIC` with `ofxImGui`. Blocking inference stays off the
openFrameworks frame thread, cancellation is operation-scoped, and the worker
is joined during `exit()`. The existing `ofxICExample` remains the complete
workbench for every public client, installers, independently managed runtimes,
logs, and diagnostics. CI and Windows release validation build all examples
against the same pinned `ofxImGui` revision.

#### M3.7 — Whole-workbench local runtime lifecycle evidence

Status: completed after individually working installations repeatedly appeared
missing or could not be started from their GUI controls.

The regular workbench now exposes one automation-only lifecycle probe that uses
the exact same resolver, launch arguments, process supervisor, and shutdown path
as its runtime buttons. `scripts/smoke-local-runtime-matrix.ps1` runs Llama,
stable-diffusion.cpp, ACE-Step, Whisper, and SAM sequentially so CUDA memory is
not contested. Its default plan mode starts no server and reports the selected
configuration; the explicitly marker-gated live mode requires each process to
be GUI-owned, reach its declared localhost port, terminate with the workbench,
and release that port. Exact paths and server logs remain temporary evidence.

Windows discovery also has a native directory-enumeration fallback and emits a
structured search diagnostic on failure. The Overview displays the running
workbench build/executable and keeps installation/configuration readiness
separate from process state, so a stale binary or incomplete model selection is
distinguishable from a process launch failure.

This closes discovery/start/stop reproducibility without treating an open port
as proof of inference. Model-backed chat, media, transcription, music, and SAM
claims continue to require their separate request/output smokes. No runtime is
linked into the addon and no generic runtime API is added.

#### M3.8 — Grounded-document onboarding example

Status: completed after the minimal chat example exposed only the first half of
the addon's supported core path.

`example-documents` is now a Project-Generator-compatible example dedicated to
explicit local document loading, allowlisted `search_documents` execution,
source-identifier citations, cancellation, and non-blocking frame-thread use.
It uses only the public `ofxIC` surface and treats retrieved document text as
untrusted evidence. Windows release validation and Linux nightly CI build it
alongside `example-chat` and the complete workbench, so both core onboarding
paths remain reproducible without turning the workbench into introductory API
documentation.

Completion evidence for each patch:

- the first meaningful failure is retained in an issue, test, or release note;
- the smallest responsible adapter changes without widening the addon boundary;
- deterministic regression coverage passes on Windows and Linux;
- model-backed or provider-backed claims remain separately marker-gated.

### M4 — `0.3` only after real convergence

Do not schedule `0.3` by date or by backend count. Start it only after two real
consumers need the same non-trivial behavior and `0.2.x` compatibility cannot
express it cleanly. A candidate may include a deliberate public-API cleanup or a
shared protocol helper, but never an embedded runtime or generic model base.

Admission evidence:

- two named consumers exercise the duplicated behavior;
- both failures or maintenance costs are reproducible;
- the proposed common boundary removes that duplication without admitting
  arbitrary filesystem, URL, provider, or runtime access;
- migration and source-compatibility consequences are documented before code;
- the canonical example can exercise every resulting public class.

Until those conditions hold, the long-term action is maintenance, compatibility
observation, and removal of proven friction—not speculative framework growth.

## Explicitly deferred

- embedded model runtimes or automatic model downloads;
- tensor, graph, universal model, or generic provider abstractions;
- automatic provider selection or silent fallback after a primary failure;
- unrestricted tools, multi-agent orchestration, or model-selected file/URL
  access;
- another addon, ecosystem manifest, readiness score, or cross-repository
  automation;
- new media providers without a concrete second consumer and exercised protocol.

## Planning rule

Roadmap order may change when evidence changes. A completed declaration, build,
or endpoint inspection does not substitute for exercising the intended user
path. The first meaningful failure remains visible even if a separate fallback
succeeds.
