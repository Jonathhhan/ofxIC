# Architecture

`ofxIC` means **Inference Connector**. The addon owns client-side protocol and
workflow state; inference remains in a separate local or hosted process.

## Product path

```text
openFrameworks app
  -> chat: ToolLoop -> DocumentIndex -> ChatSession
  -> audio: TranscriptionClient -> explicit OpenAI or whisper.cpp route
  -> segmentation: SegmentationClient -> explicit SAM bridge v1
  -> media: MediaClient -> image response or async media job
  -> music: AceStepMusicClient -> official ACE-Step 1.5 or native acestep.cpp API
         or StabilityAudioClient -> hosted async Stability Audio 3 job
  -> ofxIC::Endpoint
  -> external llama-server, whisper.cpp, SAM runner, stable-diffusion.cpp,
     ACE-Step, Stability AI, or another hosted provider
```

Each client may reference a separately configured `Endpoint`; the regular
example does so for chat, transcription, segmentation, media, and music. The process
boundary is architectural. It prevents unrelated native runtimes
from forcing one ggml version, one CUDA build, or one release cycle on the
addon.

## Public surface

- `Endpoint` owns endpoint configuration and HTTP transport.
- `ChatSession` owns conversation history.
- `TranscriptionClient` maps binary multipart requests onto either OpenAI
  `/v1/audio/transcriptions` or `whisper.cpp` `/inference`; it owns no decoder
  or model runtime and does not treat the two protocols as interchangeable.
- `SegmentationClient` owns no SAM runtime. It maps PPM images and normalized
  point prompts onto the explicit localhost SAM bridge v1 and accepts PGM masks.
  The bridge may invoke an independently installed runner; fixture success is
  never reported as model-backed segmentation evidence. Version negotiation,
  bounded images/prompts/masks, strict portable-map validation, single-runner
  admission, and a configurable runner timeout protect that process boundary.
- `MediaClient` owns no runtime; it maps typed OpenAI image requests, native
  `stable-diffusion.cpp` jobs, and explicit Hugging Face fal-ai media jobs onto
  configured endpoints.
- `StabilityAudioClient` owns no synthesizer or generic audio abstraction. It
  maps the documented Stable Audio 3 multipart submit/result protocol onto a
  separately configured Stability AI endpoint, validates job IDs and controls,
  bounds downloads, and accepts only matching MP3 or WAV containers.
- `AceStepMusicClient` owns no ACE-Step runtime. It maps both the official 1.5
  task API and native `acestep.cpp` two-stage `/lm`, `/synth`, `/job` jobs onto
  explicit request/job values. It never forwards bearer credentials to a local
  server, bounds responses, extracts multipart audio, and validates completed
  WAV or MP3 bytes.
- `DocumentIndex` chunks explicitly loaded text and performs deterministic
  lexical search. It does not watch directories or accept model-selected paths.
  Per-document, document-count, and total-chunk limits are checked before an
  addition becomes visible, so a rejected document cannot leave a partial index.
- `ToolRegistry` is the execution allowlist; `ToolLoop` bounds repeated calls.
  Document-search output labels retrieved content as untrusted evidence; the
  regular example also tells the model never to treat source text as instructions.
- `ToolLoopProgress` exposes only its two observable phases—model request and
  allowlisted tool execution—without introducing a general agent event model.
- Endpoint inspection, chat, and tool-loop cancellation is cooperative: the
  application owns the cancellation flag, while `ofxIC` forwards it through
  each HTTP request and rolls an incomplete conversational turn back.
- On headless Windows builds, non-streaming HTTP(S) uses WinHTTP. In an
  openFrameworks build, non-streaming HTTPS uses WinHTTP/Schannel so certificate
  and proxy handling follow native Windows trust, while streaming and
  cancellable local HTTP remain on the curl/openFrameworks transport.
- Optional credential persistence belongs to the application layer. The
  Windows example uses Credential Manager; the addon core receives only the
  resulting bearer token and has no credential-vault dependency.
- Optional local process supervision also belongs to the example application.
  One internal supervisor is shared by llama-server, stable-diffusion.cpp,
  ACE-Step, whisper.cpp, and the SAM bridge. It owns only processes it starts,
  captures bounded stdout/stderr, checks loopback-port readiness, and terminates
  owned children during explicit stop or application shutdown. The addon core
  still sees only endpoint URLs and protocol responses.
- Example-layer task orchestration keeps one pending local task at a time. It
  can start or attach to the task's external loopback runtime, wait for explicit
  readiness without blocking the UI, and dispatch the original request through
  the existing protocol client. It does not select providers or silently fall
  back to another backend. Startup waiting has a bounded deadline and propagates
  failure to GUI status, automation evidence, and private task history.
- The example's bounded local task history stores only timestamp, task type,
  normalized outcome/status, and a saved output path. Prompts, document content,
  credentials, provider bodies, and generated payloads are deliberately absent.
- Support diagnostics also remain an example-layer concern. The exported
  snapshot records version/build identity, sanitized endpoints, task state, and
  process lifecycle metadata. It reports executable/model existence rather than
  their locations, strips URL user info and query data, and omits credentials,
  prompts, documents, payloads, free-form runtime status, and server output.
- Settings, task history, and support diagnostics share one example-internal
  atomic text writer. It creates a unique same-directory temporary file and
  replaces the destination only after a complete write, preserving an existing
  file on replacement failure and cleaning temporary artifacts. This is not an
  addon storage API; the abstraction exists because three real application
  consumers need the same crash-safety behavior.
- `ChatRequest`, `ChatOptions`, and `ChatResult` are explicit value types.
- `HttpTransport` is injectable so protocol behavior can be tested without a
  model or network service.
- Endpoint inspection establishes reachability and advertised model identity;
  token validity, provider credit, and inference capability remain properties
  of an exercised inference request.
- HTTP responses are bounded before parsing: inspection and chat use tighter
  JSON limits, while explicit binary media downloads opt into a larger bound.

## Examples

- `example-chat` is the focused OF-style integration. It uses ImGui but has no
  local runtime dependency, keeps blocking HTTP off the update/draw thread, forwards
  cancellation, and joins its worker during `exit()`.
- `example-documents` adds the allowlisted grounded-document path without adding
  process management, credential persistence, or model-selected filesystem access.
- `ofxICExample` is the full integration workbench. It exercises every public
  client and keeps credential storage, settings, installers, process control,
  previews, and support diagnostics outside the addon core.

## Deliberately absent

- No Core addon or common native runtime.
- No tensor, graph, or universal model abstraction.
- No runtime discovery in the addon core. The Windows example may discover
  external script-installed executables below `%LOCALAPPDATA%\ofxIC\servers`
  and launch them as independent child processes; it never links their runtime.
  HF model metadata is queried only to resolve the requested model's current
  fal-ai route.
- No general agent framework around the one bounded document tool loop.
- No in-addon crawler, browser renderer, live web-search tool, or model-selected
  URL fetch. `scripts/web_snapshot.py` is a separate, user-invoked ingestion
  utility: one public URL becomes one bounded, provenance-bearing local text
  file, which then enters the same explicit document-loading path.
- No provider SDK or claim that chat, image, video, and music share one
  universal API.
- No ecosystem manifest or cross-repository control plane.

## Growth rule

A public class must be used by an example. A shared abstraction needs two real
consumers. A new addon boundary must solve an observed installation, build,
link, or independent-release problem.
