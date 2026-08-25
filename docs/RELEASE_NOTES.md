# Release Notes

## Unreleased

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
