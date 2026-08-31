# example-chat

This is the minimal openFrameworks integration for `ofxIC`. It demonstrates
the regular `ofBaseApp` lifecycle, one external OpenAI-compatible chat
endpoint, operation-scoped cancellation, and safe worker-thread shutdown.

The example uses `ofxImGui` for a compact editable chat surface, but intentionally
has no credential-store, installer, model, or local-runtime dependency. Generate its platform project with the
openFrameworks Project Generator, then configure it through the environment:

```powershell
$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:8080"
$env:OFXIC_MODEL = "your-model-id"       # optional for local servers
$env:OFXIC_API_KEY = "your-token"        # optional; never persisted here
```

Controls:

- edit the message and press Send;
- press Cancel request while an operation is running;
- press Clear conversation to reset chat history.

`ChatSession::send()` is blocking, so the example runs it outside
`update()`/`draw()`. The application owns the thread and joins it in `exit()`;
the addon itself does not create a hidden scheduler or own the model process.
