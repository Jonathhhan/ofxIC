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
