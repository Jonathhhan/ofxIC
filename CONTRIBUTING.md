# Contributing

Keep changes on the smallest vertical path: OpenAI-compatible chat, explicit
document search, and one allowlisted tool call. Local and hosted endpoints are
transport configurations, not separate backends.

Before proposing a public API, show where the canonical example uses it. Before
adding an abstraction, identify two real consumers. Before adding a backend,
identify the concrete native-runtime or installation boundary it solves.

Run the focused tests before handoff. Do not commit models, generated projects,
binaries, downloaded runtimes, caches, or local inference output.
