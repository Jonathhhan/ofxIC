# ofxIC

ofxIC means "Inference Connector". This repository develops one small
openFrameworks addon that connects to external inference endpoints without
embedding their model runtimes.

## Product boundary

The supported path is:

1. connect to or inspect a local `llama-server` or hosted endpoint;
2. hold a chat session;
3. search local documents through an allowlisted tool;
4. return a grounded answer.

An independently configured media endpoint may expose OpenAI-compatible image
generation, native asynchronous `stable-diffusion.cpp` image/video jobs, or
explicit Hugging Face fal-ai media routing. These are protocol adapters, not
embedded runtimes.

All four steps are covered by deterministic injected-transport tests and have
passed through the real example GUI against a hosted model. Keep deterministic
tests and marker-gated live evidence distinct.

## Rules

- Do not add a shared ggml runtime, tensor API, graph API, or generic model base.
- Keep native model runtimes out of this addon. The process boundary is intentional.
- Every public class must be used by an example.
- Add abstractions only after two real consumers need the same non-trivial code.
- Do not add new addons, ecosystem manifests, readiness scores, or cross-repo automation.
- Keep model files, generated projects, binaries, caches, and runtime output out of Git.
- Distinguish deterministic tests from real model-backed smoke evidence.

## Validation

Run:

```sh
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```
