# Release Notes

## 0.1.0 — candidate

This is the first release candidate in the independent `ofxIC` repository. It
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
- injectable-transport tests on Linux and Windows;
- compilation and GUI launch against the current openFrameworks Linux nightly;
- an explicitly triggered Hugging Face tool-path smoke test.

### Breaking boundary

The tensor, graph, embedded-runtime, generic model, embedding, segmentation,
SAM, and multi-addon APIs from `v2.0.0-rewrite.0` are intentionally absent.
Local `llama-server` and hosted providers use the same endpoint-facing API.

### Candidate validation

Pushes to `main` and `v*` tags run deterministic tests and the openFrameworks nightly
GUI smoke. Live provider inference remains separately gated so publishing a
candidate cannot consume Hugging Face credit accidentally.

### Deferred

- an embedded native runtime;
- embedded Whisper, SAM, Stable Diffusion, music, and video runtimes;
- Hugging Face media providers beyond the initial fal-ai adapter;
- generic agents or unrestricted tool execution;
- automatic model downloads or provider selection.
