# Architecture

`ofxIC` means **Inference Connector**. The addon owns client-side protocol and
workflow state; inference remains in a separate local or hosted process.

## Product path

```text
openFrameworks app
  -> chat: ToolLoop -> DocumentIndex -> ChatSession
  -> media: MediaClient -> image response or async media job
  -> ofxIC::Endpoint
  -> external llama-server, stable-diffusion.cpp, or hosted provider
```

The process boundary is architectural. It prevents unrelated native runtimes
from forcing one ggml version, one CUDA build, or one release cycle on the
addon.

## Public surface

- `Endpoint` owns endpoint configuration and HTTP transport.
- `ChatSession` owns conversation history.
- `MediaClient` owns no runtime; it maps typed OpenAI image requests, native
  `stable-diffusion.cpp` jobs, and explicit Hugging Face fal-ai media jobs onto
  configured endpoints.
- `DocumentIndex` chunks explicitly loaded text and performs deterministic
  lexical search. It does not watch directories or accept model-selected paths.
- `ToolRegistry` is the execution allowlist; `ToolLoop` bounds repeated calls.
- `ToolLoopProgress` exposes only its two observable phases—model request and
  allowlisted tool execution—without introducing a general agent event model.
- Endpoint inspection, chat, and tool-loop cancellation is cooperative: the
  application owns the cancellation flag, while `ofxIC` forwards it through
  each HTTP request and rolls an incomplete conversational turn back.
- On Windows, non-streaming HTTPS uses WinHTTP/Schannel so certificate and
  proxy handling follow native Windows trust. Streaming and local HTTP remain
  on the cancellable libcurl transport.
- Optional credential persistence belongs to the application layer. The
  Windows example uses Credential Manager; the addon core receives only the
  resulting bearer token and has no credential-vault dependency.
- `ChatRequest`, `ChatOptions`, and `ChatResult` are explicit value types.
- `HttpTransport` is injectable so protocol behavior can be tested without a
  model or network service.
- Endpoint inspection establishes reachability and advertised model identity;
  token validity, provider credit, and inference capability remain properties
  of an exercised inference request.

## Deliberately absent

- No Core addon or common native runtime.
- No tensor, graph, or universal model abstraction.
- No automatic runtime discovery; HF model metadata is queried only to resolve
  the requested model's current fal-ai route.
- No general agent framework around the one bounded document tool loop.
- No provider SDK or claim that chat, image, and video share one universal API.
- No ecosystem manifest or cross-repository control plane.

## Growth rule

A public class must be used by an example. A shared abstraction needs two real
consumers. A new addon boundary must solve an observed installation, build,
link, or independent-release problem.
