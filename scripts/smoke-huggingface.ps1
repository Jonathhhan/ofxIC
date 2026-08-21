$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($env:OFXIC_API_KEY)) {
    throw "OFXIC_API_KEY is missing. Set it to a Hugging Face token with Inference Providers permission."
}
if ([string]::IsNullOrWhiteSpace($env:OFXIC_MODEL)) {
    throw "OFXIC_MODEL is missing. Set it to a tool-capable Hugging Face model."
}

$baseUrl = $env:OFXIC_ENDPOINT_URL
if ([string]::IsNullOrWhiteSpace($baseUrl)) {
    $baseUrl = "https://router.huggingface.co/v1"
}
$baseUrl = $baseUrl.TrimEnd("/")
if ($baseUrl.EndsWith("/v1")) {
    $chatUrl = $baseUrl + "/chat/completions"
} else {
    $chatUrl = $baseUrl + "/v1/chat/completions"
}

$headers = @{
    Authorization = "Bearer " + $env:OFXIC_API_KEY
}
$systemMessage = @{
    role = "system"
    content = "You are running a protocol smoke test. You must call search_documents before answering. After its result, answer using the returned citation token exactly."
}
$userMessage = @{
    role = "user"
    content = "Why does ofxIC keep the model runtime outside the addon?"
}
$tool = @{
    type = "function"
    function = @{
        name = "search_documents"
        description = "Search explicitly loaded documents and return source citations."
        parameters = @{
            type = "object"
            properties = @{
                query = @{ type = "string" }
            }
            required = @("query")
            additionalProperties = $false
        }
    }
}

function Invoke-ChatCompletion {
    param(
        [Parameter(Mandatory = $true)] $Messages,
        [Parameter(Mandatory = $true)] $Tools
    )
    $request = @{
        model = $env:OFXIC_MODEL
        messages = $Messages
        tools = $Tools
        tool_choice = "auto"
        temperature = 0
        max_tokens = 256
        stream = $false
    }
    $json = $request | ConvertTo-Json -Depth 12 -Compress
    Invoke-RestMethod -Method Post -Uri $chatUrl -Headers $headers -ContentType "application/json" -Body $json
}

Write-Host "Calling Hugging Face model '$($env:OFXIC_MODEL)'..."
$first = Invoke-ChatCompletion -Messages @($systemMessage, $userMessage) -Tools @($tool)
$assistant = $first.choices[0].message
$calls = @($assistant.tool_calls)
if ($calls.Count -ne 1) {
    throw "Expected exactly one tool call, received $($calls.Count). Choose a model/provider with function-calling support."
}
$call = $calls[0]
if ($call.function.name -ne "search_documents") {
    throw "Model requested unexpected tool '$($call.function.name)'."
}

$searchResult = @{
    query = "model runtime process boundary"
    hits = @(
        @{
            citation = "[smoke.md#chunk-1]"
            source = "smoke.md"
            text = "ofxIC keeps ggml, CUDA, and the model runtime outside the addon behind an HTTP process boundary."
        }
    )
} | ConvertTo-Json -Depth 6 -Compress

$assistantMessage = @{
    role = "assistant"
    content = $assistant.content
    tool_calls = $assistant.tool_calls
}
$toolMessage = @{
    role = "tool"
    tool_call_id = $call.id
    content = $searchResult
}
$second = Invoke-ChatCompletion `
    -Messages @($systemMessage, $userMessage, $assistantMessage, $toolMessage) `
    -Tools @($tool)
$answer = [string]$second.choices[0].message.content
if ([string]::IsNullOrWhiteSpace($answer)) {
    throw "The second completion returned no answer text."
}
if (-not $answer.Contains("smoke.md#chunk-1")) {
	throw "The answer omitted the required source identifier smoke.md#chunk-1. Answer was: $answer"
}

Write-Host "Hugging Face tool smoke passed."
Write-Host "Tool: $($call.function.name)"
Write-Host "Answer: $answer"
