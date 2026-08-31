# example-documents

This focused openFrameworks example demonstrates the supported grounded-answer
path: an application explicitly loads a document, registers only
`search_documents`, and runs a bounded `ToolLoop` outside the frame thread.

It uses `ofxIC` plus `ofxImGui` for the focused document-chat surface. Generate its platform project with the
openFrameworks Project Generator and configure the endpoint through environment
variables:

```powershell
$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:8080"
$env:OFXIC_MODEL = "your-tool-capable-model"  # optional for local servers
$env:OFXIC_API_KEY = "your-token"             # optional; never persisted
$env:OFXIC_DOCUMENT_PATH = "C:\notes\project.md"
```

Without `OFXIC_DOCUMENT_PATH`, the example indexes a short built-in document so
the application lifecycle remains visible. A useful answer still requires a
tool-capable external model endpoint.

The model cannot select a file or execute arbitrary functions. Only text loaded
by the application enters `DocumentIndex`, and `ToolRegistry` exposes only the
document-search handler. The GUI exposes explicit cancellation and conversation
reset buttons; clearing chat history retains the explicit document index.
