# ofxIC

**Inference Connector for openFrameworks.** `ofxIC` connects an openFrameworks
application to external model endpoints. Its compact surface covers chat with
one allowlisted document tool, explicit OpenAI and `whisper.cpp` transcription
protocols, explicit SAM bridge point segmentation, OpenAI-compatible image
generation, and native asynchronous image/video jobs exposed by
`stable-diffusion.cpp`. A separate Hugging Face
media path routes text-to-image and text-to-video through fal-ai. Dedicated
music clients connect either to a local ACE-Step server or to hosted Stability
Audio 3 jobs. Inference always remains in a separate local or hosted process.

The addon does **not** embed ggml, llama.cpp, CUDA, SAM, or another native model
runtime. Run `llama-server` separately and update it independently.

## Install

Clone the repository into the openFrameworks addons directory:

```sh
cd path/to/openFrameworks/addons
git clone https://github.com/Jonathhhan/ofxIC.git
```

Open `example-chat` with the openFrameworks Project Generator for the minimal
OF lifecycle example. It uses `ofxIC` and `ofxImGui`. Add `ofxIC` to an existing
project in the usual `addons.make` form. No model runtime or model file is
installed with the addon.

Open `example-documents` for the complete grounded-document path: explicitly
load a text file, expose only `search_documents`, and require cited source
identifiers without blocking the openFrameworks frame thread.

The complete `ofxICExample` endpoint workbench additionally uses a pinned
`ofxImGui` revision:

```sh
cd path/to/openFrameworks/addons
git clone https://github.com/jvcleave/ofxImGui.git
git -C ofxImGui checkout 4b5d04a3e73bbae34d564c9b78143e021f67377a
```

### Optional Windows CUDA server

The addon remains an HTTP client, but these scripts can install pinned,
external CUDA runtimes into
`%LOCALAPPDATA%\ofxIC\servers`.
Executables, CUDA runtime files, logs, and models remain outside the repository.
Pinned prebuilt archives are accepted only when their recorded digest matches;
source-based environments pin their upstream release or commit.

```powershell
.\scripts\install-llama-server.ps1
.\scripts\start-llama-server.ps1 -Model C:\models\model.gguf -FlashAttention
.\scripts\install-stable-diffusion-server.ps1
.\scripts\start-stable-diffusion-server.ps1 -DiffusionModel C:\models\image-model.gguf -FlashAttention
.\scripts\install-whisper-server.ps1
.\scripts\install-acestep-server.ps1
.\scripts\install-sam-server.ps1 -ExistingModel G:\Models\sam_vit_b_01ec64.pth
```

Use `-Plan` to inspect downloads or launch arguments without installing or
starting anything. The pinned package is llama.cpp `b10516`, Windows x64,
CUDA 13.3. Other backends may be added later; models remain a separate choice.
The official stable-diffusion.cpp Windows package currently targets CUDA 12;
its bundled runtime works with a compatible newer NVIDIA driver. Its server
defaults to port `8081`, so it can run beside llama-server on port `8080`.
The ACE-Step installer builds a pinned native `acestep.cpp` CUDA server and
uses an existing complete GGUF set such as `G:\Models`; it does not copy model
weights into the addon.
The Windows example discovers versioned installations below that directory at
runtime, lets a manually selected executable take precedence, and starts the
external process from the GUI. Detection is version-independent and belongs to
the example application; no runtime is linked into the addon.
The Overview tab can run the Whisper, ACE-Step, and SAM installers without a
separate terminal. Whisper installs a verified `base-q5_1` model by default;
ACE-Step uses a separately selected complete GGUF model directory; the SAM
installer can reuse a selected Meta `sam_vit_b`, `vit_l`, or `vit_h` checkpoint
and creates an isolated PyTorch CUDA 13 environment.

The addon does not override the C++ language standard selected by the generated
openFrameworks project. Its standalone deterministic CMake tests require C++20;
the canonical example is built with the standard supplied by the current
openFrameworks platform template. This keeps the addon aligned with
openFrameworks instead of imposing a separate compiler mode.

### Professional Windows build and validation

Use the repository scripts instead of relying on Visual Studio's ambiguous
incremental-build message:

```powershell
.\scripts\build-example.ps1
.\scripts\validate-windows.ps1
```

`build-example.ps1` verifies that every addon/example `.cpp` belongs to the
generated project and performs a fast incremental Release/x64 build by default,
then reports whether the exact EXE was rebuilt or already current. Use
`-Rebuild` for an unconditional full rebuild; `validate-windows.ps1` does this
automatically for release validation. `-Incremental` remains accepted for
compatibility. Use `-UpdateProject` only when source files were added and the
local openFrameworks Project Generator needs to refresh the solution. The
validation script runs deterministic CMake tests, rebuilds both focused
examples and the complete workbench, and then executes the model-free GUI
lifecycle smoke plus a no-start plan of all five local runtime configurations.
Current development builds expose the version string `0.2.1-dev` through
`ofxIC::versionString`.

The default target is the full `ofxICExample`. The same verified path builds the
minimal example after generating its ignored platform project once:

```powershell
.\scripts\build-example.ps1 -Example example-chat -UpdateProject
.\scripts\build-example.ps1 -Example example-documents -UpdateProject
```

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

Long-running endpoint inspection, chat, tool-loop, and transcription requests
accept an operation-scoped `RequestControl`. Run the blocking call on a worker
thread and set the flag from the openFrameworks thread; the example exposes
this as **Cancel request**:

```cpp
std::atomic<bool> cancelRequested{ false };
ofxIC::RequestControl control;
control.timeoutSeconds = 30; // zero keeps the operation's bounded default
control.shouldCancel = [&]() {
	return cancelRequested.load();
};
const auto result = loop.run("Summarize the loaded sources", 4, control);
```

Cancellation is forwarded to the HTTP transport, rolls the incomplete turn
back out of chat history, and is reported separately from timeout, transport,
provider, and invalid-response failures through `RequestFailure`. The previous
standalone cancellation-predicate overloads remain available during `0.2.x`.

The example's **Stream direct chat** option renders text chunks while a plain
chat request is still running. It deliberately bypasses document tools because
streaming tool calls are not part of the supported protocol yet; leave it off
for grounded `search_documents` answers.

The complete document workflow remains small:

```cpp
ofxIC::DocumentIndex documents;
documents.addFile("notes/architecture.md");

ofxIC::ToolRegistry tools;
tools.addDocumentSearch(documents);

ofxIC::ToolLoop loop(chat, tools);
const auto answer = loop.run("Why is llama-server a separate process?");
```

`ToolLoop::run` can also report typed progress when it requests the model or
executes an allowlisted tool. The example uses this to expose multi-request
grounding work without leaking worker-thread state into the GUI thread.

These objects deliberately use non-owning references: keep `Endpoint` alive while
its `ChatSession` is used, keep both `ChatSession` and `ToolRegistry` alive while
using `ToolLoop`, and keep `DocumentIndex` alive after registering document
search.

`search_documents` receives only a query. It cannot choose a file path or run
an arbitrary function; it searches only text the application loaded first.
The index accepts at most 128 documents, 16,384 chunks, and 8 MiB per document;
rejected additions do not partially change the index. Tool results label source
text as untrusted evidence. This supports safer prompting but does not turn
arbitrary web content into trusted instructions.

### Controlled web snapshots

Web ingestion stays outside the addon and outside the model tool loop. The
bundled utility fetches one URL selected by the user and writes a provenance-
bearing `.txt` or `.md` snapshot that can be loaded through the regular example:

```powershell
python scripts\web_snapshot.py https://example.com/ `
  --output "$env:TEMP\ofxic-article.txt"
```

Then choose **Load .md / .txt** in `ofxICExample`, or set
`OFXIC_DOCUMENT_PATH` to that output file before launch. The snapshot records
the final URL, retrieval time, title when present, and a SHA-256 hash of the
cleaned text.

This utility is intentionally not a crawler: it does not follow page links,
execute JavaScript, use authenticated browser state, or run as a model-selected
tool. It revalidates redirect destinations, rejects hosts resolving to private
or otherwise non-public addresses, accepts only HTML and plain text, limits the
download to 4 MiB, and defaults to a 15-second timeout. Dynamic sites that need
a browser renderer should be exported by a separate user-controlled process
and loaded as a local `.txt` or `.md` file.

Inspect the endpoint and advertised model identity before use:

```cpp
const auto status = endpoint.inspect();
if (status && !status.models.empty()) {
	ofxIC::ChatOptions options;
	options.model = status.models.front();
	chat.setOptions(options);
}
```

Inspection proves endpoint reachability and parses the advertised model list.
It does not by itself prove that a token is valid, that provider credit is
available, or that a selected model can complete the intended request.
The example reports `Inference completed` only after a chat request has
actually returned successfully.

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

Before submitting local video, `MediaClient` reads
`/sdcpp/v1/capabilities`. Submission stops with an actionable error when the
model currently loaded by `sd-server` does not advertise `vid_gen`; common
image models such as SD-Turbo cannot generate video. Start the server with a
video-capable model such as a compatible WAN, LTX-Video, or AnimateDiff model.
When no output format is requested explicitly, the client selects a format
advertised by that server, preferring AVI for broad Windows playback support.

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

Music generation is intentionally provider-specific rather than another value
in `MediaKind`. The model runtime stays in a separate process:

```cpp
ofxIC::Endpoint aceStep("http://127.0.0.1:8085");
ofxIC::AceStepMusicClient music(aceStep);

ofxIC::AceStepMusicRequest request;
request.caption = "Warm modular synthesizer, instrumental, no vocals";
request.durationSeconds = 30;
request.outputFormat = "wav";
request.protocol = ofxIC::AceStepMusicProtocol::NativeCpp;

auto job = music.submit(request);
// Direct servers may complete immediately; async servers use /job polling.
if (!job.terminal()) job = music.poll(job);
```

The client supports both the official ACE-Step 1.5 `/release_task`,
`/query_result`, `/v1/audio` flow and native `acestep.cpp` `/lm`, `/synth`,
`/job` jobs. It bounds responses, validates task IDs, extracts native multipart
audio, and verifies the returned WAV or MP3 container. It never sends a
configured bearer token to either local route.

Stability Audio 3 has its own asynchronous API and credential:

```cpp
ofxIC::Endpoint stability("https://api.stability.ai");
stability.setBearerToken(stabilityApiKey);
ofxIC::StabilityAudioClient music(stability);

ofxIC::StabilityAudioRequest request;
request.prompt = "Warm modular synthesizer, instrumental, no vocals";
request.durationSeconds = 30;
request.outputFormat = "mp3";

auto job = music.submit(request);
// Poll later. A completed job carries validated MP3 or WAV bytes.
job = music.poll(job);
```

The client accepts only `stable-audio-3`, validates the provider's documented
duration and generation controls, bounds downloads, and checks the requested
audio container before reporting completion. This is an external paid provider
operation; deterministic tests do not spend credits.

Audio transcription is also an external endpoint operation. Select the OpenAI
protocol for `/v1/audio/transcriptions`, or point the custom endpoint at a
separately running `whisper.cpp` server and select its `/inference` protocol:

```cpp
ofxIC::TranscriptionClient transcription(endpoint);
ofxIC::TranscriptionRequest request;
request.audioBytes = wavBytes;
request.filename = "recording.wav";
request.model = "whisper-1"; // OpenAI only; whisper.cpp selects at server start.

const auto transcript = transcription.transcribeOpenAI(request);
```

The two methods are deliberately separate because these endpoints do not share
one universal transcription contract.

Prompted image segmentation uses the explicitly named `ofxIC SAM bridge v1`,
not a claimed provider standard. The client uploads a PPM image plus normalized
positive or negative points to `/v1/segmentations` and receives one PGM mask.
The regular example can queue multiple positive and negative prompts; an empty
queue preserves the single positive point workflow. Left-clicking its input
preview adds a positive prompt, right-clicking adds a negative prompt, and the
rendered markers can be undone or cleared:

```cpp
ofxIC::SegmentationClient segmentation(endpoint);
ofxIC::SegmentationRequest request;
request.imageBytes = ppmBytes;
request.points.push_back({ 0.5f, 0.5f, true });
const auto mask = segmentation.segmentSamBridge(request);
```

`scripts\sam-bridge-server.py` implements the localhost process boundary. Its
real mode invokes an independently supplied `sam-runner` with the existing
`--model`, `--image`, `--output`, and repeated point-flag contract. For runners
that expose it, `--backend cpu|cuda` is forwarded explicitly instead of silently
falling back to a slow CPU default. Its fixture
mode exists only for deterministic GUI evidence and performs no inference. The
client and bridge require `X-ofxIC-SAM-Bridge-Version: 1`, accept at most 64
points, bound request and mask sizes, validate complete PPM/PGM payloads, and
report stable text error codes. The bridge rejects concurrent model runs and
supports `--runner-timeout <seconds>` rather than allowing an orphaned request
to wait indefinitely. `scripts\install-sam-server.ps1` provides one concrete
external consumer of this contract: an isolated Meta SAM Python runner with a
CUDA 13 PyTorch environment. Its bounded `GET /health` response reports bridge version,
fixture/runner mode, and selected backend; the regular example exposes this as
**Check bridge** before an image needs to be submitted.

## Scope

Implemented:

- `/v1/models` endpoint inspection
- `/v1/chat/completions`
- `/v1/images/generations`
- OpenAI `/v1/audio/transcriptions` and `whisper.cpp` `/inference`
- explicit `/v1/segmentations` SAM bridge v1 point segmentation
- native `stable-diffusion.cpp` image/video submission and job polling
- Hugging Face text-to-image and queued text-to-video through fal-ai routing
- local ACE-Step 1.5 task-API text-to-music generation
- Stability Audio 3 text-to-music submission, polling, and MP3/WAV download
- conversational history
- optional streaming transport when openFrameworks exposes curl
- injected HTTP transport for deterministic tests
- local document index
- controlled one-URL web-to-text snapshot ingestion outside the addon
- one allowlisted `search_documents` tool
- bounded tool loop with source citations

Proven end to end:

- deterministic protocol tests on Linux and Windows
- compilation and GUI launch against the current openFrameworks Linux nightly
- automated keyboard input through the real example GUI
- local `whisper.cpp` transcription through the regular Windows GUI using
  `ggml-tiny.en` and the `jfk.wav` speech sample
- local SAM2.1 point segmentation through the regular Windows GUI, explicit
  localhost bridge, and independently built `ofxGgmlSam` runner
- marker-gated local ACE-Step music generation through the ofxIC client,
  including asynchronous jobs and multipart WAV extraction
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
LM Studio, Hugging Face, OpenAI, and a custom endpoint. A separate tabbed
**Inference tasks** window gives transcription, image/video, music, and SAM one
consistent visible workspace each. It selects independent backends, generates
images, automatically follows asynchronous image/video or music jobs, displays returned media, saves and
plays completed MP3/WAV output with date-and-time filenames, and sends an
explicitly selected audio file to either supported transcription protocol.
Chat, transcription, SAM, image/video media, and music have separate endpoint
URL fields so their external processes can remain available at the same time;
**Use chat URL** is a deliberate convenience action rather than an implicit
protocol assumption.

| Media backend | Image | Video | Behavior |
| --- | --- | --- | --- |
| OpenAI | Yes | No | `/v1/images/generations`; video is intentionally not offered |
| Hugging Face / fal-ai | Yes | Yes | Hosted task routes; provider credit may be required |
| `stable-diffusion.cpp` | Yes | Model-dependent | External asynchronous jobs; the loaded model must advertise `vid_gen` for video |

| Music backend | Local | Behavior |
| --- | --- | --- |
| ACE-Step | Yes | External server on `http://127.0.0.1:8085`; native `acestep.cpp` jobs or the official 1.5 task API |
| Stability Audio 3 | No | Hosted asynchronous provider API; token and credit required |

The GUI shows these capabilities and does not allow unsupported combinations.
An OpenAI video adapter is deliberately absent: it would require paid API
access, while the current Sora video API is deprecated and scheduled to shut
down on September 24, 2026. ChatGPT application access is not API credit and
cannot be used as an `ofxIC` endpoint.

- Choose an endpoint preset or enter a custom base URL.
- Inspect `/v1/models` and select or enter the model ID.
- Use **Save settings** to remember non-secret chat, media, music, local server
  paths, model paths, and launch parameters in `.ofxICExample.settings` in the
  user profile. **Reset saved settings** removes
  that file and restores built-in defaults; active environment overrides remain
  authoritative.
- For a script-installed runtime, select its local backend and press **Use
  detected llama-server** or **Use detected sd-server**. A valid manually
  selected executable remains authoritative when starting. The GUI reports the
  child PID, waits for its configured TCP port, and distinguishes a running
  process from a ready listener.
- Local actions are one-click tasks: if their configured runtime is not ready,
  the GUI starts it, waits asynchronously for its loopback port, and continues
  the original chat, inspection, transcription, SAM, image/video, or music task
  automatically. The queued task remains visible and cancellable in **Overview**.
- The **Overview** tab supervises all five local processes in one table. Each
  row shows installation/configuration readiness independently from the colored
  `stopped`, `starting`, `ready`, `exited`, or `failed` process state. The tab
  also prints the exact workbench executable and build timestamp, making a stale
  example binary immediately visible. Each task tab provides a bounded live
  stdout/stderr view, and server output is mirrored to the openFrameworks
  console. A listening expected port is adopted as an
  externally managed runtime and is never terminated by the example; otherwise
  launch uses the executable directory as the child working directory and
  preserves exact Windows process-creation errors.
- Windows script-install discovery first uses portable filesystem traversal and
  falls back to native Win32 enumeration under
  `%LOCALAPPDATA%\ofxIC\servers`. If a start still cannot resolve an executable,
  the console reports the configured path, startup snapshot, root state,
  matching runtime directories/files, and native fallback result.
- Externally attached Llama and stable-diffusion servers also follow the
  canonical `%LOCALAPPDATA%\ofxIC\logs\<server>-<port>.stdout|stderr.log`
  files produced by the bundled start scripts. The GUI labels the exact sources,
  shows their update age, tails replacements safely, and retains only the most
  recent 64 KiB. **Clear view** clears only the in-memory display; the GUI never
  edits or removes externally owned logs.
- **Install missing runtimes** in Overview installs Whisper, ACE-Step, or SAM
  asynchronously, shows bounded live installer output, and refreshes detected
  paths on success. Every launched process tree is assigned to a Windows job so
  cancellation and application shutdown do not leave installer descendants.
- For a GUI-owned `sd-server`, the example distinguishes the selected model
  from the checkpoint actually loaded by the running process. Changing from
  SD-Turbo to WAN causes a controlled restart on the next generation and
  selects a matching WAN VAE and UMT5 encoder when found. An externally managed
  server is never restarted implicitly.
- **Export diagnostics...** in Overview writes a bounded, privacy-aware support
  snapshot with addon/build identity, configured endpoint hosts, task state, and
  runtime lifecycle metadata. It deliberately omits credentials, prompts,
  document content, response/generated payloads, server output, and exact
  runtime/model paths. URL user info and query strings are removed. Automation
  can select the output with `OFXIC_DIAGNOSTICS_PATH`.
- Settings, private History, and diagnostics use crash-safe atomic replacement:
  an incomplete write cannot truncate the previously valid destination, and
  failed temporary files are cleaned automatically.
- The **History** tab retains at most 100 recent task outcomes and saved output
  paths beside the non-secret settings file. Prompts, document contents,
  credentials, generated payloads, and provider response bodies are not stored;
  history can be cleared in one action.
- Run `scripts\smoke-one-click-local.ps1` after building the Release example to
  exercise GUI-managed ACE-Step and SAM fixture processes, automatic task
  continuation, private History persistence, owned-child shutdown, and the
  never-ready runtime timeout without models or provider credit. The production
  startup timeout is 900 seconds; deterministic automation may override it with
  `OFXIC_RUNTIME_START_TIMEOUT_SECONDS` (valid range: 1–3600 seconds).
- Run `scripts\smoke-local-runtime-matrix.ps1` to launch the real workbench five
  times in plan mode and report the executable/model configuration selected for
  Llama, stable-diffusion.cpp, ACE-Step, Whisper, and SAM without starting a
  server. For explicit local-live lifecycle evidence, set
  `OFXIC_RUN_LIVE_RUNTIME_MATRIX=1` and add `-Live`. The script starts the five
  runtimes sequentially through the same code paths as the GUI buttons, requires
  GUI ownership and port readiness, exits the workbench to stop its complete
  child process tree, and verifies that the port was released. This proves
  discovery, launch, readiness, and shutdown—not model inference; the separate
  marker-gated inference smokes remain the evidence for generated content.
- Local `stable-diffusion.cpp`, Hugging Face/fal-ai media, ACE-Step, and
  Stability Audio jobs are polled automatically. Completed image, video, and
  audio payloads are saved and loaded into their preview/player without a
  separate **Poll job** action; manual polling is retained only as a timeout or
  diagnostic fallback.
- `scripts\smoke-video-live.ps1` exercises the regular Release GUI against an
  already running video-capable `sd-server`. It first rejects an image-only
  loaded model, then verifies the asynchronous job and saved WebM/WebP/AVI
  output. Real inference is deliberately gated: set
  `OFXIC_RUN_LIVE_SDCPP_VIDEO=1` before running it. The default 512x512,
  17-frame test is intended as a small WAN validation, not a quality preset.
- Start the separate ACE-Step server (the default is `http://127.0.0.1:8085`),
  then select **ACE-Step local**. For a headless,
  explicitly opt-in model-backed check, set `OFXIC_RUN_LIVE_ACESTEP=1` and run
  `scripts/smoke-acestep-live.ps1`; generated audio remains under the ignored
  `tests/build/live` directory.
- `scripts/smoke-music.ps1` drives the regular GUI through an asynchronous
  deterministic ACE-Step fixture and verifies the generated
  `ofxIC-music-YYYYMMDD-HHMMSS-mmm.wav` name. Add `-Live` with the same
  `OFXIC_RUN_LIVE_ACESTEP=1` marker to exercise the regular GUI against the
  real local server. The script removes its generated audio after validation.
- Load deliberately selected `.md` or `.txt` files with the GUI button or by
  dropping them on the window. The example sends only their file names as
  source identifiers, not their full local paths.
- Use `OFXIC_API_KEY` as the universal token override. The Hugging Face and
  OpenAI presets also recognize `HF_TOKEN` and `OPENAI_API_KEY` respectively.
- If the bundled libcurl cannot validate your Windows certificate chain, set
  `OFXIC_CA_BUNDLE` to a maintained PEM CA bundle. `CURL_CA_BUNDLE` and
  `SSL_CERT_FILE` are also recognized. Certificate verification remains enabled.
- Environment tokens remain the highest-priority override. On Windows, the GUI
  can optionally save a masked token in Windows Credential Manager and remove
  it again with **Forget saved token**. Tokens are never written to the example
  settings file, displayed after entry, or committed with the project.
- `OFXIC_ENDPOINT_URL`, `OFXIC_MODEL`, and `OFXIC_API_KEY` configure
  the initial state for scripts and CI.
- `OFXIC_DIAGNOSTICS_PATH` exports the same privacy-aware Overview support
  snapshot once setup and configured autoruns have been initialized.
- `OFXIC_INSPECT_AUTORUN=1` starts endpoint inspection after setup; it is
  intended for launch and cancellation checks.
- `scripts\smoke-huggingface-inspection.ps1` exercises the Windows GUI,
  Schannel, HF model listing, and inspection evidence wording with an explicit
  fake token. It does not validate credentials or consume inference credit.
- `scripts\smoke-https-cancellation.ps1` explicitly uses a delayed public HTTPS
  route to verify that closing the Windows GUI interrupts a blocking WinHTTP
  request. It uses a fake token and is separate from deterministic tests.
- `OFXIC_DOCUMENT_PATH` deliberately loads one document at startup;
  `OFXIC_DOCUMENT_RESULT_PATH` records its GUI automation status.
- `OFXIC_AUDIO_PATH` and `OFXIC_TRANSCRIPTION_AUTORUN=openai|whisper-cpp`
  exercise a deliberately selected audio file through the real GUI lifecycle.
  `OFXIC_TRANSCRIPTION_ENDPOINT_URL` selects its endpoint independently;
  `OFXIC_TRANSCRIPTION_MODEL` overrides the OpenAI audio model. The deterministic
  `scripts\smoke-transcription.ps1` fixture verifies this path without claiming
  that a real speech model ran.
- `scripts\smoke-whisper-cpp-live.ps1 -Server <whisper-server.exe> -Model
  <ggml-model.bin> -Audio <speech.wav>` is the separate marker-gated live proof.
  Set `OFXIC_RUN_LIVE_WHISPER=1` before running it. It starts the external
  runtime with CUDA by default (`-Cpu` is an explicit fallback), exercises the
  regular GUI, requires a nonempty model-produced transcript, and keeps
  binaries, models, audio, and output out of Git.
- `scripts\smoke-llama-server-live.ps1 -Server <llama-server.exe> -Model
  <model.gguf> -Document <source.md>` is the marker-gated local proof for the
  core endpoint path. Set `OFXIC_RUN_LIVE_LLAMA_SERVER=1` explicitly before
  running it. The script starts only the supplied external server, waits for
  `/health`, drives the regular Release GUI through `/v1/models`, a two-request
  document-tool loop, and a cited final answer, then stops only the processes it
  launched. Use `-EvidenceDirectory <dir>` to retain logs and the GUI result;
  otherwise temporary evidence is removed. The selected local model must support
  the llama.cpp tool-call protocol. The script never downloads a model. The
  complete path was verified with Qwen3.6-27B on 2026-08-27.
- `scripts\smoke-segmentation.ps1` is the deterministic SAM bridge fixture and
  performs no inference. Set `OFXIC_RUN_LIVE_SAM=1`, then run
  `scripts\smoke-sam-live.ps1 -Python <installed-python.exe> -Runner <sam-runner.py>
  -Model <sam_vit_*.pth> -Image <input.png>` as the separate model-backed proof. It
  exercises the external runner through the bridge and regular GUI, requires a
  returned PGM mask, and keeps the runner, model, inputs, and output out of Git.
- For interactive use, select the installed Python, `sam-python-runner.py`, and
  Meta SAM `.pth` checkpoint in the SAM tab; the GUI starts the bundled bridge
  on `http://127.0.0.1:18085`. `scripts\start-sam-bridge.ps1` remains available
  for explicitly external runners.
- `OFXIC_SEGMENTATION_ENDPOINT_URL` selects the SAM bridge independently.
  `OFXIC_SEGMENTATION_NEGATIVE_POINT_X` and
  `OFXIC_SEGMENTATION_NEGATIVE_POINT_Y` add a negative prompt to GUI automation.
  `OFXIC_ENDPOINT_URL` remains a backward-compatible default for audio and SAM
  automation when their task-specific variables are absent.
- `OFXIC_MEDIA_BACKEND`, `OFXIC_MEDIA_ENDPOINT_URL`,
  `OFXIC_MEDIA_IMAGE_MODEL`, `OFXIC_MEDIA_VIDEO_MODEL`, and
  `OFXIC_MEDIA_API_KEY` configure media independently from chat.
- `OFXIC_MEDIA_KIND`, `OFXIC_MEDIA_WIDTH`, `OFXIC_MEDIA_HEIGHT`,
  `OFXIC_MEDIA_FRAMES`, and `OFXIC_MEDIA_FPS` override saved media values for
  scripts and CI. `OFXIC_SETTINGS_PATH` selects a different settings file.
- `OFXIC_MUSIC_ENDPOINT_URL`, `OFXIC_MUSIC_DURATION`, and
  `OFXIC_MUSIC_OUTPUT_FORMAT=mp3|wav` override saved Stability Audio choices.
  `OFXIC_MUSIC_API_KEY` overrides `STABILITY_API_KEY`; both remain outside the
  settings file and the Windows example can store `STABILITY_API_KEY` securely.
- `OFXIC_ACESTEP_SERVER`, `OFXIC_ACESTEP_SERVER_ARGS`, and
  `OFXIC_ACESTEP_MODELS` override the native server executable, advanced
  arguments, and GGUF model directory without moving model files.
- `OFXIC_LLAMA_SERVER`, `OFXIC_LLAMA_MODEL`, `OFXIC_LLAMA_MODELS`,
  `OFXIC_SD_SERVER`, `OFXIC_SD_MODEL`, `OFXIC_SD_MODELS`, and
  `OFXIC_WHISPER_SERVER`, `OFXIC_WHISPER_MODEL` provide the same explicit,
  non-persistent path overrides for reproducible local lifecycle checks. SD
  component overrides additionally accept `OFXIC_SD_VAE`,
  `OFXIC_SD_TEXT_ENCODER`, `OFXIC_SD_CLIP_L`, and `OFXIC_SD_CLIP_G`;
  `OFXIC_SD_COMPLETE_CHECKPOINT=1` selects the complete-checkpoint launch form.
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

The default GUI workflow additionally exercises both transcription protocols,
SAM segmentation with positive and negative points, and video download and
playback against local fixture servers. Each task uses its own endpoint while
the chat endpoint is deliberately unavailable. This validates the complete
client and rendering paths without a GPU, provider account, token, or payment
method.

On Windows, a built Release example can exercise cancellation through the real
libcurl and GUI shutdown path against a deliberately slow local endpoint:

```powershell
scripts\smoke-cancellation.ps1
```

The request-lifecycle GUI smoke uses that same slow local endpoint twice and
checks the result written by the regular example: a one-second bounded request
must report `Inspection timed out`, while a separately marker-triggered cancel
must report `Inspection cancelled`:

```powershell
scripts\smoke-request-lifecycle.ps1
```

This is deterministic local transport and GUI evidence; it does not claim that
a hosted provider or model was contacted.

Direct-chat streaming has its own deterministic GUI smoke. A local HTTP/1.1
fixture delays its second SSE event and the script requires the regular example
to expose the first chunk before the completed response:

```powershell
scripts\smoke-streaming.ps1
```

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
[release notes](docs/RELEASE_NOTES.md) for the exact boundary and the
[roadmap](docs/ROADMAP.md) for the prioritized path to `0.2.0` and beyond.
