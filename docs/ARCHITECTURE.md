# Architecture

`ofxIC` means **Inference Connector**. The addon owns client-side protocol and
workflow state; inference remains in a separate local or hosted process.

## Product path

```text
openFrameworks app
  -> ofxIC::ToolLoop
  -> DocumentIndex + allowlisted search_documents
  -> ofxIC::ChatSession
  -> ofxIC::Endpoint
  -> OpenAI-compatible HTTP endpoint
  -> external llama-server or hosted inference provider
```

The process boundary is architectural. It prevents unrelated native runtimes
from forcing one ggml version, one CUDA build, or one release cycle on the
addon.

## Public surface

- `Endpoint` owns endpoint configuration and HTTP transport.
- `ChatSession` owns conversation history.
- `DocumentIndex` chunks explicitly loaded text and performs deterministic
  lexical search. It does not watch directories or accept model-selected paths.
- `ToolRegistry` is the execution allowlist; `ToolLoop` bounds repeated calls.
- `ChatRequest`, `ChatOptions`, and `ChatResult` are explicit value types.
- `HttpTransport` is injectable so protocol behavior can be tested without a
  model or network service.

## Deliberately absent

- No Core addon or common native runtime.
- No tensor, graph, or universal model abstraction.
- No automatic backend discovery beyond the configured endpoint.
- No general agent framework around the one bounded document tool loop.
- No ecosystem manifest or cross-repository control plane.

## Growth rule

A public class must be used by an example. A shared abstraction needs two real
consumers. A new addon boundary must solve an observed installation, build,
link, or independent-release problem.
