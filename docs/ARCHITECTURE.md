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
  -> music: AceStepMusicClient -> local /lm, /synth, optional /job
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
- `AceStepMusicClient` owns no ACE-Step runtime. It maps the existing local
  server's `/lm`, `/synth`, and optional `/job` state transitions onto explicit
  request/job values, never forwards bearer credentials to the local server,
  bounds responses, and validates completed WAV or MP3 bytes.
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
- `ChatRequest`, `ChatOptions`, and `ChatResult` are explicit value types.
- `HttpTransport` is injectable so protocol behavior can be tested without a
  model or network service.
- Endpoint inspection establishes reachability and advertised model identity;
  token validity, provider credit, and inference capability remain properties
  of an exercised inference request.
- HTTP responses are bounded before parsing: inspection and chat use tighter
  JSON limits, while explicit binary media downloads opt into a larger bound.

## Deliberately absent

- No Core addon or common native runtime.
- No tensor, graph, or universal model abstraction.
- No automatic runtime discovery; HF model metadata is queried only to resolve
  the requested model's current fal-ai route.
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
