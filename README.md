# ofxIC

**Inference Connector for openFrameworks.** `ofxIC` connects an openFrameworks
application to external model endpoints. Its compact surface covers chat with
one allowlisted document tool, OpenAI-compatible image generation, and native
asynchronous image/video jobs exposed by `stable-diffusion.cpp`. A separate
Hugging Face media path routes text-to-image and text-to-video through fal-ai.
Inference always remains in a separate local or hosted process.

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

Hugging Face media deliberately uses its task protocol instead of pretending
that the chat router implements OpenAI image/video routes. The first compact
adapter supports fal-ai for both image and video:

```cpp
ofxIC::Endpoint hf("https://router.huggingface.co");
hf.setBearerToken(environmentToken);
ofxIC::MediaClient hostedMedia(hf);

ofxIC::MediaJobRequest video;
video.kind = ofxIC::MediaKind::Video;
video.model = "Wan-AI/Wan2.2-TI2V-5B";
video.prompt = "A paper sculpture slowly turns";

auto job = hostedMedia.submitHuggingFaceFal(video);
// fal-ai video is queued; call poll(job) later until it is terminal.
```

The client resolves the HF model's current fal-ai mapping, keeps the HF token
on router requests, and downloads the returned media bytes without forwarding
that token to the output CDN.

## Scope

Implemented:

- `/v1/models` endpoint inspection
- `/v1/chat/completions`
- `/v1/images/generations`
- native `stable-diffusion.cpp` image/video submission and job polling
- Hugging Face text-to-image and queued text-to-video through fal-ai routing
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
- additional Hugging Face media providers beyond fal-ai
- image editing and broader provider-specific media controls

## Example

`ofxICExample` uses `ofxImGui` to switch between `llama-server`,
LM Studio, Hugging Face, OpenAI, and a custom endpoint. The same GUI also
selects an independent media backend, generates images, submits native or
Hugging Face video jobs, polls them, and displays returned media.

| Media backend | Image | Video | Behavior |
| --- | --- | --- | --- |
| OpenAI | Yes | No | `/v1/images/generations`; video is intentionally not offered |
| Hugging Face / fal-ai | Yes | Yes | Hosted task routes; provider credit may be required |
| `stable-diffusion.cpp` | Yes | Yes | External asynchronous image/video jobs |

The GUI shows these capabilities and does not allow unsupported combinations.
An OpenAI video adapter is deliberately absent: it would require paid API
access, while the current Sora video API is deprecated and scheduled to shut
down on September 24, 2026. ChatGPT application access is not API credit and
cannot be used as an `ofxIC` endpoint.

- Choose an endpoint preset or enter a custom base URL.
- Inspect `/v1/models` and select or enter the model ID.
- Use `OFXIC_API_KEY` as the universal token override. The Hugging Face and
  OpenAI presets also recognize `HF_TOKEN` and `OPENAI_API_KEY` respectively.
- Tokens are read from the environment; the GUI displays only whether one was
  loaded and never displays or stores its value.
- `OFXIC_ENDPOINT_URL`, `OFXIC_MODEL`, and `OFXIC_API_KEY` configure
  the initial state for scripts and CI.
- `OFXIC_MEDIA_BACKEND`, `OFXIC_MEDIA_ENDPOINT_URL`,
  `OFXIC_MEDIA_IMAGE_MODEL`, `OFXIC_MEDIA_VIDEO_MODEL`, and
  `OFXIC_MEDIA_API_KEY` configure media independently from chat.
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
and video task APIs. `ofxIC` therefore uses an explicit fal-ai task adapter for
HF media and never silently sends media to the chat route. A live media request
can consume provider credit.

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

To exercise media explicitly, open the `openFrameworks example` workflow,
choose **Run workflow**, and select `image` or `video` for `hf_media_task`.
This manual run uses `HF_TOKEN`, launches the compiled GUI, waits for the fal-ai
job, saves and opens the returned media, and uploads its screenshot, result,
and log. A selected media task can incur provider charges; `none` is the
default.

## Tests

```sh
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

The deterministic tests use an injected transport; live inference remains a
separate, explicitly triggered check so protocol failures and provider costs
cannot be confused with unit-test failures.

The default GUI workflow additionally exercises video download and playback
against a local fixture server. It therefore validates the complete client and
rendering path without a GPU, provider account, token, or payment method.

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
