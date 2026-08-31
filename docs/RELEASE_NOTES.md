# Release Notes

## Unreleased

- On 2026-08-31, separate marker-gated local runs exercised all five external
  runtime adapters with real models: Llama returned a chat completion,
  stable-diffusion.cpp loaded `sd_turbo` on an RTX 3090, Whisper transcribed a
  generated WAV through the regular GUI, SAM produced a mask through the GUI
  and CUDA bridge runner, and native ACE-Step completed a 1,920,044-byte WAV.
  These observations are model-backed evidence and remain distinct from the
  deterministic suite and model-free GUI lifecycle smoke.
- Hardened local runtime starts with immutable startup-detection fallbacks, so
  an empty or stale editable GUI path can no longer hide an installation that
  was successfully discovered at startup. On Windows, executable discovery now
  uses native Win32 enumeration before `std::filesystem`, including when the
  latter reports `ERROR_PATH_NOT_FOUND` for an existing
  `%LOCALAPPDATA%\ofxIC\servers` tree. Failed starts report the configured path,
  startup snapshot, root state, matching family directories/files, and native
  resolver result instead of only saying `not found`.
- The Overview now exposes the exact workbench executable/build identity and a
  separate configuration-ready state for every local runtime. This makes stale
  example binaries and missing executable/model configuration visible before a
  Start button is pressed.
- Added native `acestep.cpp` DiT selection by exact GGUF registry filename,
  retained official ACE-Step model aliases separately, and verified the full
  GUI-managed CUDA path with a real 10-second 48 kHz stereo WAV generation.
- Added one sequential local-runtime matrix that drives the regular GUI start
  paths for Llama, stable-diffusion.cpp, ACE-Step, Whisper, and SAM, captures
  structured ownership/readiness evidence, and verifies owned-tree shutdown and
  port release. Its default plan mode starts no server; real starts require an
  explicit marker and remain distinct from model-inference evidence.

- Local ACE-Step now prefers a pinned native `acestep.cpp` CUDA server, exposes
  the external GGUF model directory in the GUI, detects complete LM + embedding
  + DiT + VAE sets, and supports native asynchronous `/lm`, `/synth`, `/job`
  generation alongside the official ACE-Step 1.5 task API.

### Added

- `example-chat` provides a focused `ofBaseApp` integration using `ofxIC` and
  `ofxImGui`; it keeps blocking HTTP off the frame thread,
  forwards cancellation, and joins its worker during shutdown;
- Linux example CI and Windows validation build focused chat and document
  examples plus the complete endpoint workbench;
- public `ofxIC::versionMajor`, `versionMinor`, `versionPatch`, and
  `versionString` metadata identify the current `0.2.1-dev` build;
- `scripts/build-example.ps1` verifies generated-project source membership and
  performs a fast incremental Windows Release/x64 build by default, with
  unconditional `-Rebuild` and project regeneration available explicitly; its
  `-Example` parameter applies the same validation to minimal or full examples.
  `scripts/validate-windows.ps1` retains unconditional rebuilds for its release
  path and combines them with the deterministic suite and model-free GUI smoke;
- `scripts/smoke-local-runtime-matrix.ps1` reports the five selected local
  executable/model configurations in no-start plan mode and can explicitly run
  the GUI-owned lifecycle matrix; Windows validation includes the safe plan;
- the Overview tab exports a privacy-aware diagnostic snapshot containing
  sanitized endpoint, task, and runtime lifecycle metadata without credentials,
  prompts, documents, payloads, server output, or exact runtime/model paths;
- the Windows example now discovers versioned script-installed `llama-server`
  and `sd-server` executables without hard-coded build-directory names; the
  resolver is deterministically tested and remains outside the addon core;
- example settings schema v3 persists non-secret local runtime paths, model
  paths, launch parameters, and explicit SAM runner/checkpoint/backend fields
  while continuing to load versions 1 and 2;
- the canonical example has a central runtime Overview and one internal process
  supervisor for llama-server, sd-server, ACE-Step, whisper.cpp, and SAM, with
  explicit lifecycle states and bounded live stdout/stderr display.
- a focused `example-chat` follows the standard openFrameworks project layout,
  uses ofxIC with ofxImGui, keeps blocking requests off the render thread, and
  demonstrates cancellation and shutdown independently of the full workbench;
- local task actions now start or attach to their external runtime, wait for
  readiness without blocking the GUI, and automatically continue the original
  chat/inspection, media, transcription, music, or SAM task;
- a bounded, clearable History tab persists privacy-aware task metadata and
  saved output paths without prompts, documents, credentials, response bodies,
  or generated payloads.
- a model-free Windows vertical smoke verifies GUI-owned ACE-Step and SAM
  startup, automatic task continuation, History privacy, owned-child shutdown,
  a bounded never-ready runtime failure, and diagnostics privacy through the
  regular Release example.
- an explicit marker-gated WAN video smoke validates the real Release GUI,
  loaded `sd-server` capabilities, asynchronous polling, and the saved video
  artifact without mixing model-backed evidence into deterministic tests;
- the Overview can install missing Whisper, ACE-Step 1.5, and Meta SAM external
  runtimes asynchronously; Whisper includes a verified compact default model,
  while SAM uses an isolated CUDA 13 PyTorch environment and can reuse an
  existing checkpoint;

### Changed

- runtime-matrix cleanup now waits for both workbench exit and redirected-stream
  drain, fixing the first Windows plan run where the final temporary log was
  still locked after the process had already reported exit;
- `addon_config.mk` now uses conventional recursive `src` discovery instead of
  maintaining a duplicate source list, and installation documents `ofxImGui`
  as an optional workbench-only dependency;

- example settings, private task History, and privacy-aware diagnostics now use
  one crash-safe atomic writer with unique same-directory temporary files;
  failed replacements preserve existing data and clean temporary artifacts;
- supported asynchronous media and music jobs are now followed automatically
  during normal GUI use; completed payloads are saved and loaded into the
  existing image/video/audio preview, with manual polling retained as fallback;
- local process status distinguishes process creation, TCP-port readiness,
  exit, and explicit stop, and logs the resolved executable path used to launch
  `llama-server`.
- local process launch now attaches to an already listening expected port as an
  explicitly external process, uses the executable directory as its working
  directory when launching, mirrors captured output to the console, and stops
  only processes owned by the example.
- externally attached Llama and stable-diffusion runtimes now tail the bounded
  stdout/stderr files produced by the bundled start scripts, show their exact
  source paths and update age, handle truncation or replacement, and mirror new
  lines to the console without taking ownership of either the process or log
  files; clearing the view never truncates the source.
- deferred runtime-start failures now reach the same automation result channel
  as completed tasks and use stable History task identifiers; startup waiting
  defaults to 900 seconds and has a bounded test override.

### Fixed

- changing the selected stable-diffusion.cpp checkpoint no longer leaves a
  GUI-owned server silently running the previous SD-Turbo model; generation
  restarts the owned process with the new checkpoint, distinguishes external
  processes, and pairs detected WAN models with a suitable VAE and UMT5;
- native `stable-diffusion.cpp` video submission now checks the loaded server
  model's `vid_gen` capability, negotiates an advertised AVI/WebM/WebP output
  format, and surfaces server-provided HTTP error details; the example no
  longer presents an image-only model such as SD-Turbo as video-capable and
  provides pause, resume, and restart controls for completed video previews;
- the `sdcpp` environment preset now targets the same dedicated media port
  `8081` as the GUI profile instead of the llama-server default port `8080`;
- the Whisper installer uses retrying `curl.exe` downloads with a temporary
  partial file before the existing cryptographic verification, avoiding a
  Windows PowerShell `Invoke-WebRequest` transfer that can remain at zero bytes;
- the real Whisper GUI smoke now uses the installed CUDA backend by default,
  exposes `-Cpu` only as an explicit fallback, and enforces its documented live
  inference marker; it also waits for the stopped server to release redirected
  log files before removing temporary evidence on Windows;
- the model-backed SAM smoke now requires its explicit live marker, accepts the
  installed Python executable so `.py` adapters use the CUDA environment, and
  waits for bridge and GUI process handles before cleaning temporary evidence;
- stale or missing GUI executable paths are repaired from the current external
  installation before launch instead of producing an opaque Windows error 3;
- local server start buttons remain actionable and report missing model or
  executable inputs as explicit status messages instead of silently staying
  disabled;
- the local `llama-server` GUI smoke now waits for processes it force-stops
  before deleting temporary evidence, preventing a successful inference run
  from failing because redirected server logs are still locked on Windows.

## 0.2.0 — 2026-08-27

### Changed

- image, native/Hugging Face media, Stability Audio, and ACE-Step operations
  now accept the same operation-scoped `RequestControl` used by chat and
  transcription, expose `RequestFailure`, and keep local HTTP cancellation
  distinct from provider-side remote-job cancellation;
- the regular example can cancel its active media or music HTTP request.
- a deterministic OpenAI-compatible image fixture now exercises the regular
  GUI through Base64 decoding and verifies the saved PNG without provider
  credits.
- SAM bridge inspection and segmentation now use the same operation-scoped
  `RequestControl` and `RequestFailure` classification as the other endpoint
  clients while retaining callback overloads for `0.2.x` source compatibility.
- the regular example can now render direct-chat SSE chunks while a request is
  still running, with an explicit boundary that keeps document-tool requests
  on the existing non-streaming tool loop.
- chat user messages, streaming chunks, completed assistant messages, and
  request failures are mirrored to the openFrameworks console log for diagnosis.
- chat provider failures now retain a bounded JSON error detail, and the regular
  example logs the active endpoint and model without exposing credentials;
- deterministic Windows and Linux GUI smokes preserve a representative HTTP 402
  credit failure through the real example status and console paths.
- endpoint inspection now mirrors its non-secret route and result to the console;
- a marker-gated Windows live smoke can start a user-supplied `llama-server` and
  GGUF model, then require the regular GUI to inspect the endpoint and complete
  the cited two-request local document-tool path without downloading a runtime
  or model. This path passed with a tool-capable Qwen3.6-27B GGUF on 2026-08-27;
  the model and live evidence were kept outside Git.
- clean-checkout installation now pins the same `ofxImGui` revision exercised
  by CI, and the documented language-standard contract keeps standalone tests
  on C++20 while generated addon projects retain the openFrameworks standard.
- GitHub Actions now use `actions/checkout@v6`, `actions/cache@v6`, and
  `actions/upload-artifact@v7`, removing the Node.js 20 deprecation warnings
  observed during final release validation.

## 0.2.0-rc.4 — 2026-08-26

### Changed

- standalone deterministic tests now use C++20, matching the common modern
  openFrameworks baseline while generated platform projects retain their
  platform-specific OF standard settings;
- endpoint inspection, chat/tool-loop, and transcription now share an
  operation-scoped `RequestControl` and distinguish cancellation, timeout,
  transport, provider, and invalid-response failures while preserving the
  `0.2.x` cancellation-callback overloads;
- the regular example now supports a marker-gated slow-request GUI fixture that
  independently observes timeout and cancellation through the real local HTTP
  transport path;

### Fixed

- pressing Enter in the regular example chat now sends the complete message,
  including spaces; Ctrl+Enter remains available for inserting a new line;
- selecting the OpenAI image backend now resets the shared media dimensions to
  the supported `1024x1024` GPT Image size, and image HTTP failures retain the
  provider's bounded JSON error detail instead of showing only the status code;
- regular endpoint inspection now keeps its default timeout when the optional
  GUI-fixture timeout marker is unset;

## 0.2.0-rc.3 — 2026-08-26

### Added

- a user-invoked, one-URL web snapshot utility that produces `.txt` or `.md`
  documents with final-URL, retrieval-time, title, and SHA-256 provenance;
- deterministic tests for public-address validation, bounded reads, HTML text
  extraction, provenance formatting, and safe output replacement behavior;
- a provider-specific Stability Audio 3 client for asynchronous text-to-music
  submission, polling, and bounded MP3/WAV retrieval;
- a separate local ACE-Step music client for the 1.5 `/release_task`,
  `/query_result`, and `/v1/audio` flow, with bounded payloads and MP3/WAV
  validation;
- regular-example music controls with a separate endpoint, non-secret saved
  backend selection, ACE-Step localhost defaults, migration of older Stability
  settings, optional Windows Credential Manager storage for `STABILITY_API_KEY`,
  and saving/playback of completed audio with date-and-time filenames;
- marker-driven regular-example music automation with automatic asynchronous
  job polling, machine-readable completion evidence, and a deterministic GUI
  fixture that verifies the timestamped output path;
- a bounded SAM bridge `/health` route and regular-example **Check bridge** action
  that report protocol version, fixture/runner mode, and selected backend before
  submitting a segmentation request;
- injected-transport tests for the Stability multipart contract, validation,
  job lifecycle, provider errors, and binary audio signatures.

### Hardened

- `DocumentIndex` now atomically enforces an 8 MiB per-document limit, 2,048-byte
  source identifiers, 128 documents, and 16,384 total chunks;
- chunk-boundary lookup is limited to the active chunk window, avoiding
  quadratic processing of large documents without whitespace;
- document tool results explicitly label source content as untrusted evidence,
  and the regular example instructs the model not to execute source text.

### Fixed

- the regular example now groups transcription, image/video, music, and SAM in
  one consistent tabbed **Inference tasks** window; transcription no longer
  occupies the document-chat panel, and SAM is no longer hidden below unrelated
  media controls;
- image, video, and SAM previews now use a stable bounded height rather than the
  remaining vertical space in a long scrollable ImGui window; SAM input and mask
  images therefore no longer collapse to an effectively invisible one-pixel row;
- the SAM bridge can now explicitly forward `--backend cpu|cuda`; this prevents
  CUDA-capable external SAM3 runners from silently using CPU and exceeding the
  regular GUI smoke deadline;
- transcription failures now preserve the HTTP status while showing a bounded,
  single-line provider or decoder error detail instead of hiding the response
  behind a generic status such as `transcription endpoint returned HTTP 500`.

The snapshot utility remains outside the addon and model tool loop. It is not a
crawler, JavaScript renderer, authenticated browser, or live web-search agent.
Music tests prove both provider protocols with injected transports without
contacting the paid provider. The marker-gated local ACE-Step smoke additionally
generated and validated an 844,844-byte WAV through the actual ofxIC client.
The regular GUI path also completed a real local ACE-Step run, automatically
polled both phases, and verified its timestamped WAV path; that model-backed
evidence remains distinct from the deterministic fixture suite.

## 0.2.0-rc.2 — 2026-08-25

### Fixed

- the regular example now selects the OpenAI audio endpoint on first startup
  and when loading settings that predate the audio fields, while preserving
  explicit custom and environment endpoint URLs.

## 0.2.0-rc.1 — 2026-08-25

The endpoint-first boundary remains unchanged. Audio transcription and prompted
image segmentation have been added as explicit protocol clients; neither adds
an embedded model runtime or restores the former generic runtime APIs.

### Added

- explicit OpenAI `/v1/audio/transcriptions` and `whisper.cpp` `/inference`
  clients, both used by the regular example;
- explicit `ofxIC SAM bridge v1` point segmentation with normalized positive
  and negative prompts and a PGM mask response;
- bridge-v1 compatibility headers, bounded image/prompt/mask input, strict
  PPM/PGM validation, single-runner admission, stable error codes, and a
  configurable external-runner timeout;
- multiple positive and negative SAM prompts in the regular example;
- a localhost reference bridge that invokes an independently supplied SAM
  runner without embedding or installing it;
- bounded HTTP responses and cooperative cancellation through the Windows
  WinHTTP transport;
- optional Windows Credential Manager storage at the example layer, while
  environment tokens remain authoritative and secrets stay out of settings;
- deterministic protocol and GUI fixture smokes for transcription and
  segmentation;
- separate model-backed Windows GUI smokes for local `whisper.cpp` and an
  external `ofxGgmlSam` runner.

### Validation boundary

Injected-transport and fixture tests establish request, response, cancellation,
and GUI lifecycle behavior without claiming inference. Model-backed Whisper and
SAM evidence is recorded only by their separately invoked live scripts. Models,
runners, selected input files, generated masks, transcripts, and runtime output
remain outside Git.

### Still excluded

- embedded native model runtimes and automatic model downloads;
- tensor, graph, universal model, or provider-base abstractions;
- automatic runtime discovery, ecosystem manifests, and cross-repository
  control planes;
- general agent orchestration or unrestricted tool execution.

## 0.1.0 — 2026-08-24

This is the first release in the independent `ofxIC` repository. It
starts with the reduced endpoint-first implementation tested on the historical
`ofxGgml` V2 branch, without carrying that repository's history or claiming API
compatibility with its earlier, broader runtime design.

The addon is renamed from `ofxGgml` to `ofxIC`, short for **Inference
Connector**. The previous name implied an embedded ggml runtime even though the
new architecture deliberately owns only the client side of a process boundary.
The public namespace is now `ofxIC`, `Server` is now `Endpoint`, and environment
variables use the `OFXIC_` prefix.

### Included

- one openFrameworks addon with no embedded ggml or model runtime;
- OpenAI-compatible endpoint inspection and chat completions;
- OpenAI-compatible image generation;
- native asynchronous `stable-diffusion.cpp` image/video jobs and polling;
- Hugging Face text-to-image and queued text-to-video through fal-ai;
- conversation history and optional response streaming;
- explicitly loaded local documents and one allowlisted search tool;
- a bounded tool loop that returns source identifiers;
- one `ofxImGui` openFrameworks example with selectable endpoint profiles;
- explicit media capability display that prevents unsupported backend/kind
  combinations, including automation requests;
- injectable-transport tests on Linux and Windows;
- compilation and GUI launch against the current openFrameworks Linux nightly;
- an explicitly triggered Hugging Face tool-path smoke test.

### Breaking boundary

The tensor, graph, embedded-runtime, generic model, embedding, segmentation,
SAM, and multi-addon APIs from `v2.0.0-rewrite.0` are intentionally absent.
Local `llama-server` and hosted providers use the same endpoint-facing API.

### Release validation

Pushes to `main` and `v*` tags run deterministic tests and the openFrameworks nightly
GUI smoke. Live provider inference remains separately gated so publishing a
release cannot consume Hugging Face credit accidentally.

### Deferred

- an embedded native runtime;
- embedded Whisper, SAM, Stable Diffusion, music, and video runtimes;
- Hugging Face media providers beyond the initial fal-ai adapter;
- OpenAI video, whose current paid Sora API is deprecated;
- generic agents or unrestricted tool execution;
- automatic model downloads or provider selection.
