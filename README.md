# ofxIC

**Inference Connector for openFrameworks.** `ofxIC` connects an openFrameworks
application to external model endpoints. Its compact surface covers chat with
one allowlisted document tool, OpenAI-compatible image generation, and native
asynchronous image/video jobs exposed by `stable-diffusion.cpp`. Inference
always remains in a separate local or hosted process.

The addon does **not** embed ggml, llama.cpp, CUDA, SAM, or another native model
runtime. Run `llama-server` separately and update it independently.

## Install

Clone the repository into the openFrameworks addons directory:

```sh
cd path/to/openFrameworks/addons
git clone https://github.com/Jonathhhan/ofxIC.git
git clone --branch develop https://github.com/jvcleave/ofxImGui.git
```

Open `ofxICExample` with the openFrameworks Project Generator, or add
`ofxIC` to an existing project. No model runtime or model file is installed
with the addon.

## Current API

```cpp
#include "ofxIC.h"

ofxIC::Endpoint endpoint("http://127.0.0.1:8080");
ofxIC::ChatSession chat(endpoint);

chat.setSystemPrompt("Be concise.");
const auto result = chat.send("Hello");

if (result) {
	ofLogNotice() << result.text;
} else {
	ofLogError() << result.error;
}
```

The complete document workflow remains small:

```cpp
ofxIC::DocumentIndex documents;
documents.addFile("notes/architecture.md");

ofxIC::ToolRegistry tools;
tools.addDocumentSearch(documents);

ofxIC::ToolLoop loop(chat, tools);
const auto answer = loop.run("Why is llama-server a separate process?");
```

These objects deliberately use non-owning references: keep `Endpoint` alive while
its `ChatSession` is used, keep both `ChatSession` and `ToolRegistry` alive while
using `ToolLoop`, and keep `DocumentIndex` alive after registering document
search.

`search_documents` receives only a query. It cannot choose a file path or run
an arbitrary function; it searches only text the application loaded first.

Inspect the endpoint and advertised model identity before use:

```cpp
const auto status = endpoint.inspect();
if (status && !status.models.empty()) {
	ofxIC::ChatOptions options;
	options.model = status.models.front();
	chat.setOptions(options);
}
```

Image generation uses the same endpoint and token configuration:

```cpp
ofxIC::MediaClient media(endpoint);
ofxIC::ImageRequest image;
image.prompt = "A paper sculpture in soft studio light";
image.width = 1024;
image.height = 1024;

const auto result = media.generateImage(image);
// result.imagesBase64 or result.urls contains the generated output.
```

The native `stable-diffusion.cpp` video path is asynchronous:

```cpp
ofxIC::MediaJobRequest video;
video.kind = ofxIC::MediaKind::Video;
video.prompt = "The paper sculpture slowly turns";
video.videoFrames = 33;
video.fps = 16;

auto job = media.submit(video);
// Poll later; do not block the openFrameworks draw thread.
job = media.poll(job);
```

## Scope

Implemented:

- `/v1/models` endpoint inspection
- `/v1/chat/completions`
- `/v1/images/generations`
- native `stable-diffusion.cpp` image/video submission and job polling
- conversational history
- optional streaming transport when openFrameworks exposes curl
- injected HTTP transport for deterministic tests
- local document index
- one allowlisted `search_documents` tool
- bounded tool loop with source citations

Proven end to end:

- deterministic protocol tests on Linux and Windows
- compilation and GUI launch against the current openFrameworks Linux nightly
- automated keyboard input through the real example GUI
- a marker-gated Hugging Face run with two model requests, local
  `search_documents` execution, and a cited final answer

The latest recorded live proof used `openai/gpt-oss-120b` and returned
`architecture.md#chunk-1`. This evidence was recorded while the reduced code
still lived on the historical `ofxGgml` V2 branch; new runs are recorded in
this repository.

Not part of the supported surface yet:

- embedded native model runtimes
- generic tensor, graph, or model abstractions
- multi-agent orchestration
- embedded Whisper, SAM, Stable Diffusion, music, or video runtimes
- Hugging Face task routing for image and video generation
- image editing and provider-specific media controls

## Example

`ofxICExample` uses `ofxImGui` to switch between `llama-server`,
LM Studio, Hugging Face, OpenAI, and a custom endpoint. The same GUI also
generates images, submits native `stable-diffusion.cpp` video jobs, polls them,
and displays returned image or video payloads.

- Choose an endpoint preset or enter a custom base URL.
- Inspect `/v1/models` and select or enter the model ID.
- Use `OFXIC_API_KEY` as the universal token override. The Hugging Face and
  OpenAI presets also recognize `HF_TOKEN` and `OPENAI_API_KEY` respectively.
- Tokens are read from the environment; the GUI displays only whether one was
  loaded and never displays or stores its value.
- `OFXIC_ENDPOINT_URL`, `OFXIC_MODEL`, and `OFXIC_API_KEY` configure
  the initial state for scripts and CI.
- `F1` and `F2` remain shortcuts for inspect and clear.

### Testing without a local GPU

Hugging Face Inference Providers exposes an OpenAI-compatible endpoint and
supports tools. Configure the example without putting the token in source:

```sh
export OFXIC_ENDPOINT_URL=https://router.huggingface.co/v1
export OFXIC_API_KEY=hf_your_token
export OFXIC_MODEL=your-tool-capable-model:provider
```

Create a token with the `Inference Providers` permission and choose a currently
available tool-capable model in the
[Hugging Face playground](https://huggingface.co/playground). Provider usage can
consume monthly credit or incur pay-as-you-go charges; see
[HF pricing](https://huggingface.co/docs/inference-providers/pricing).

The same addon code works with local `llama-server`; only environment values
change. Do not commit tokens or paste them into issue logs.

Hugging Face's OpenAI-compatible router currently covers chat, not its image
and video task APIs. Those media tasks therefore need a separate provider
adapter; `ofxIC` does not silently send them to the chat router.

Before building the openFrameworks example, verify that the selected hosted
model supports the required two-request tool protocol:

```powershell
scripts\smoke-huggingface.bat
```

This live smoke asks the model to call `search_documents`, supplies one fixed
read-only result, and requires the final response to contain its citation. It
tests the hosted model/provider contract. The openFrameworks example then tests
the same protocol through the addon itself.

For a GitHub-hosted run, add a repository Actions secret named `HF_TOKEN`.
Optionally set repository variables `HF_MODEL` and `HF_SERVER_URL`; otherwise
the workflows use `openai/gpt-oss-120b` and the Hugging Face router. On `main`,
`[hf-smoke]` runs the provider protocol check and `[hf-gui-smoke]` runs the
compiled openFrameworks GUI through the same hosted tool path. Normal pushes
run neither live inference test, so they cannot consume provider credit
accidentally.

## Tests

```sh
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

The deterministic tests use an injected transport; live inference remains a
separate, explicitly triggered check so protocol failures and provider costs
cannot be confused with unit-test failures.

GitHub Actions also builds `ofxICExample` against the current official
openFrameworks Linux nightly. The workflow records the resolved archive name,
opens the GUI on a virtual display, and uploads its screenshot and log. With
`[hf-gui-smoke]`, it additionally types a question, waits for the model-backed
tool loop, verifies the source identifier, and captures the rendered answer.

## Repository status

`ofxIC` starts with an independent history and a deliberately small public
surface. It is derived from the tested endpoint-first rewrite previously
developed on the historical `ofxGgml` V2 branch, but it does not carry that
repository's embedded-runtime or multi-addon history. See the
[release notes](docs/RELEASE_NOTES.md) for the exact boundary.
