#include "test_harness.h"
#include "../src/ofxIC.h"

#include <stdexcept>

namespace {
ofxIC::HttpResponse twoToolCalls() {
	ofxIC::HttpResponse response;
	response.started = true;
	response.status = 200;
	response.body = R"({"choices":[{"message":{"tool_calls":[{"id":"one","type":"function","function":{"name":"check","arguments":"{}"}},{"id":"two","type":"function","function":{"name":"check","arguments":"{}"}}]}}]})";
	return response;
}
} // namespace

OFXIC_TEST(tool_registry_is_an_explicit_allowlist) {
	ofxIC::DocumentIndex index;
	index.addText("guide.md", "Use a separate llama-server process boundary.");
	ofxIC::ToolRegistry tools;
	OFXIC_REQUIRE(tools.addDocumentSearch(index));
	OFXIC_REQUIRE(!tools.addDocumentSearch(index));

	ofxIC::ToolCall search{ "call-1", "search_documents", "{\"query\":\"process boundary\"}" };
	const auto found = tools.execute(search);
	OFXIC_REQUIRE(found);
	OFXIC_REQUIRE(found.citations == std::vector<std::string>{"[guide.md#chunk-1]"});
	OFXIC_REQUIRE(found.content.find("\"content_trust\":\"untrusted\"") != std::string::npos);
	OFXIC_REQUIRE(found.content.find("[guide.md#chunk-1]") != std::string::npos);

	ofxIC::ToolCall unknown{ "call-2", "read_file", "{\"path\":\"secret\"}" };
	const auto rejected = tools.execute(unknown);
	OFXIC_REQUIRE(!rejected);
	OFXIC_REQUIRE(rejected.error.find("not allowlisted") != std::string::npos);
}

OFXIC_TEST(tool_loop_searches_then_returns_grounded_answer) {
	int requests = 0;
	ofxIC::Endpoint endpoint("https://example.test/v1", [&](const ofxIC::HttpRequest & request) {
		++requests;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		if (requests == 1) {
			OFXIC_REQUIRE(request.body.find("\"tools\"") != std::string::npos);
			response.body = R"({"choices":[{"message":{"role":"assistant","tool_calls":[{"id":"call-1","type":"function","function":{"name":"search_documents","arguments":"{\"query\":\"process boundary\"}"}}]}}]})";
		} else {
			OFXIC_REQUIRE(request.body.find("\"tool_call_id\":\"call-1\"") != std::string::npos);
			OFXIC_REQUIRE(request.body.find("guide.md#chunk-1") != std::string::npos);
			response.body = R"({"choices":[{"message":{"role":"assistant","content":"The runtime stays separate [guide.md#chunk-1]."}}]})";
		}
		return response;
	});

	ofxIC::ChatSession chat(endpoint);
	chat.setSystemPrompt("Use search_documents and cite its results.");
	ofxIC::DocumentIndex index;
	index.addText("guide.md", "The native runtime stays behind a process boundary.");
	ofxIC::ToolRegistry tools;
	tools.addDocumentSearch(index);
	ofxIC::ToolLoop loop(chat, tools);

	std::vector<ofxIC::ToolLoopProgress> progress;
	const auto result = loop.run(
		"Why is the runtime separate?", 4, nullptr,
		[&](const ofxIC::ToolLoopProgress & item) { progress.push_back(item); });
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(result.modelRequests == 2);
	OFXIC_REQUIRE(result.steps.size() == 1);
	OFXIC_REQUIRE(result.text.find("[guide.md#chunk-1]") != std::string::npos);
	OFXIC_REQUIRE(chat.getMessages().size() == 4);
	OFXIC_REQUIRE(progress.size() == 3);
	OFXIC_REQUIRE(progress[0].stage == ofxIC::ToolLoopStage::RequestingModel);
	OFXIC_REQUIRE(progress[0].modelRequest == 1);
	OFXIC_REQUIRE(progress[1].stage == ofxIC::ToolLoopStage::ExecutingTool);
	OFXIC_REQUIRE(progress[1].toolName == "search_documents");
	OFXIC_REQUIRE(progress[2].stage == ofxIC::ToolLoopStage::RequestingModel);
	OFXIC_REQUIRE(progress[2].modelRequest == 2);
}

OFXIC_TEST(tool_loop_repairs_missing_citation_once_and_rolls_back_invalid_answers) {
	for (int scenario = 0; scenario < 5; ++scenario) {
		int requests = 0;
		ofxIC::Endpoint endpoint("https://example.test", [&](const ofxIC::HttpRequest & request) {
			++requests;
			ofxIC::HttpResponse response;
			response.started = true;
			response.status = 200;
			if (requests == 1)
				response.body = R"({"choices":[{"message":{"tool_calls":[{"id":"c1","type":"function","function":{"name":"search_documents","arguments":"{\"query\":\"boundary\"}"}}]}}]})";
			else if (requests == 2)
				response.body = R"({"choices":[{"message":{"content":"The runtime uses a process boundary."}}]})";
			else {
				OFXIC_REQUIRE(request.body.find("[guide.md#chunk-1]") != std::string::npos);
				OFXIC_REQUIRE(request.body.find("\"tools\"") == std::string::npos);
				if (scenario == 0)
					response.body = R"({"choices":[{"message":{"content":"The runtime is separate [guide.md#chunk-1]."}}]})";
				else if (scenario == 1)
					response.body = R"({"choices":[{"message":{"content":"Still no citation."}}]})";
				else if (scenario == 2)
					response.body = R"({"choices":[{"message":{"content":"Invented [other.md#chunk-1]."}}]})";
				else {
					response.failure = ofxIC::RequestFailure::Timeout;
					response.error = "correction timed out";
				}
			}
			return response;
		});
		ofxIC::ChatSession chat(endpoint);
		ofxIC::DocumentIndex index;
		index.addText("guide.md", "The runtime uses a process boundary.");
		ofxIC::ToolRegistry registry;
		registry.addDocumentSearch(index);
		ofxIC::ToolLoop loop(chat, registry);
		bool cancel = false;
		ofxIC::RequestControl control;
		control.shouldCancel = [&]() { return cancel; };
		const auto result = loop.run("Why separate?", 4, control,
			[&](const ofxIC::ToolLoopProgress & progress) {
				if (scenario == 4 && progress.modelRequest == 3) cancel = true;
			});
		OFXIC_REQUIRE(requests == (scenario == 4 ? 2 : 3));
		OFXIC_REQUIRE(result.modelRequests == (scenario == 4 ? 2 : 3));
		OFXIC_REQUIRE(static_cast<bool>(result) == (scenario == 0));
		if (scenario != 0) {
			OFXIC_REQUIRE(chat.getMessages().empty());
			OFXIC_REQUIRE(result.failure == (scenario == 4 ? ofxIC::RequestFailure::Cancelled :
				(scenario == 3 ? ofxIC::RequestFailure::Timeout : ofxIC::RequestFailure::Validation)));
		}
	}
}

OFXIC_TEST(tool_loop_can_be_cancelled_before_transport) {
	int transportCalls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++transportCalls;
		return ofxIC::HttpResponse{};
	});
	ofxIC::ChatSession chat(endpoint);
	ofxIC::DocumentIndex documents;
	documents.addText("source.md", "Grounded material");
	ofxIC::ToolRegistry tools;
	tools.addDocumentSearch(documents);
	ofxIC::ToolLoop loop(chat, tools);

	const auto result = loop.run("Question", 4, []() { return true; });

	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(result.modelRequests == 0);
	OFXIC_REQUIRE(transportCalls == 0);
	OFXIC_REQUIRE(chat.getMessages().empty());
}

OFXIC_TEST(document_tool_rejects_arguments_outside_its_declared_schema) {
	ofxIC::DocumentIndex index;
	index.addText("guide", "needle");
	ofxIC::ToolRegistry registry;
	registry.addDocumentSearch(index);
	const std::vector<std::string> invalid = {
		R"({"query":"needle")", R"({"query":"needle"} trailing)",
		R"({"nested":{"query":"needle"}})", R"([{"query":"needle"}])",
		R"({"query":"needle","query":"different"})",
		R"({"query":"needle","unexpected":true})", R"({"query":42})",
		R"({"query":""})", R"({"query":"\uD800"})", R"({"query":"\uDC00"})",
		R"({"query":"\uD800\u0041"})", "{\"query\":\"raw\nnewline\"}",
		"{\"query\":\"\xC0\xAF\"}", "{\"query\":\"\xED\xA0\x80\"}",
		"{\"query\":\"\xF4\x90\x80\x80\"}", "{\"query\":\"\x80\"}",
		"{\"query\":\"\xE6\xBC\"}", "\v{\"query\":\"needle\"}"
	};
	for (const auto & arguments : invalid) {
		const auto result = registry.execute({ "id", "search_documents", arguments });
		OFXIC_REQUIRE(!result);
		OFXIC_REQUIRE(!result.error.empty());
	}
}

OFXIC_TEST(document_tool_decodes_supplementary_unicode_and_escaped_keys) {
	const std::string symbol = "\xF0\x9F\x8C\x8D";
	ofxIC::DocumentIndex index;
	index.addText("world", "A world " + symbol);
	ofxIC::ToolRegistry registry;
	registry.addDocumentSearch(index);
	const auto result = registry.execute({ "id", "search_documents",
		" \t{ \"qu\\u0065ry\" : \"\\uD83C\\uDF0D\" }\r\n" });
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(result.content.find("[world#chunk-1]") != std::string::npos);
	OFXIC_REQUIRE(result.content.find(symbol) != std::string::npos);
	const auto raw = registry.execute({ "id", "search_documents", "{\"query\":\"" + symbol + "\"}" });
	OFXIC_REQUIRE(raw);
	OFXIC_REQUIRE(raw.content == result.content);
}

OFXIC_TEST(tool_loop_stops_between_tools_and_preserves_completed_step_evidence) {
	int requests = 0;
	int executions = 0;
	bool cancel = false;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++requests;
		return twoToolCalls();
	});
	ofxIC::ChatSession chat(endpoint);
	ofxIC::ToolRegistry registry;
	registry.add({ "check", "Check", "{}" }, [&](const std::string &) {
		++executions;
		cancel = true;
		return ofxIC::ToolExecutionResult{ true, "done", {} };
	});
	ofxIC::ToolLoop loop(chat, registry);
	const auto result = loop.run("Question", 4, [&]() { return cancel; });
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Cancelled);
	OFXIC_REQUIRE(executions == 1);
	OFXIC_REQUIRE(requests == 1);
	OFXIC_REQUIRE(result.steps.size() == 1);
	OFXIC_REQUIRE(chat.getMessages().empty());
}

OFXIC_TEST(tool_loop_checks_cancellation_after_progress_callback) {
	int executions = 0;
	bool cancel = false;
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		return twoToolCalls();
	});
	ofxIC::ChatSession chat(endpoint);
	ofxIC::ToolRegistry registry;
	registry.add({ "check", "Check", "{}" }, [&](const std::string &) {
		++executions;
		return ofxIC::ToolExecutionResult{ true, "done", {} };
	});
	ofxIC::ToolLoop loop(chat, registry);
	const auto result = loop.run("Question", 4, [&]() { return cancel; },
		[&](const ofxIC::ToolLoopProgress & progress) {
			if (progress.stage == ofxIC::ToolLoopStage::ExecutingTool) cancel = true;
		});
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(executions == 0);
	OFXIC_REQUIRE(chat.getMessages().empty());
}

OFXIC_TEST(tool_loop_rolls_back_history_when_handler_throws) {
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		return twoToolCalls();
	});
	ofxIC::ChatSession chat(endpoint);
	ofxIC::ToolRegistry registry;
	registry.add({ "check", "Check", "{}" }, [](const std::string &) -> ofxIC::ToolExecutionResult {
		throw std::runtime_error("handler exception");
	});
	ofxIC::ToolLoop loop(chat, registry);
	bool threw = false;
	try { loop.run("Question"); }
	catch (const std::runtime_error &) { threw = true; }
	OFXIC_REQUIRE(threw);
	OFXIC_REQUIRE(chat.getMessages().empty());
}

OFXIC_TEST(tool_loop_reports_limit_as_failure_without_executing_tools) {
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		return twoToolCalls();
	});
	ofxIC::ChatSession chat(endpoint);
	ofxIC::ToolRegistry registry;
	int executions = 0;
	registry.add({ "check", "Check", "{}" }, [&](const std::string &) {
		++executions;
		return ofxIC::ToolExecutionResult{ true, "done", {} };
	});
	ofxIC::ToolLoop loop(chat, registry);
	const auto result = loop.run("Question", 0);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.failure != ofxIC::RequestFailure::None);
	OFXIC_REQUIRE(executions == 0);
	OFXIC_REQUIRE(chat.getMessages().empty());
}

OFXIC_TEST(tool_loop_rolls_back_only_current_turn_on_execution_failure) {
	int requests = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		if (++requests > 1) return twoToolCalls();
		auto response = twoToolCalls();
		response.body = R"({"choices":[{"message":{"content":"Saved answer"}}]})";
		return response;
	});
	ofxIC::ChatSession chat(endpoint);
	OFXIC_REQUIRE(chat.send("Saved question"));
	ofxIC::ToolRegistry registry;
	registry.add({ "check", "Check", "{}" }, [](const std::string &) {
		return ofxIC::ToolExecutionResult{ false, {}, "tool rejected input" };
	});
	ofxIC::ToolLoop loop(chat, registry);
	const auto result = loop.run("Failed question");
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Validation);
	OFXIC_REQUIRE(result.steps.size() == 1);
	OFXIC_REQUIRE(result.error == "tool rejected input");
	OFXIC_REQUIRE(chat.getMessages().size() == 2);
	OFXIC_REQUIRE(chat.getMessages().back().content == "Saved answer");
}

OFXIC_TEST(tool_loop_rolls_back_history_when_progress_callback_throws) {
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		return twoToolCalls();
	});
	ofxIC::ChatSession chat(endpoint);
	ofxIC::ToolRegistry registry;
	int executions = 0;
	registry.add({ "check", "Check", "{}" }, [&](const std::string &) {
		++executions;
		return ofxIC::ToolExecutionResult{ true, "done", {} };
	});
	ofxIC::ToolLoop loop(chat, registry);
	bool threw = false;
	try {
		loop.run("Question", 4, ofxIC::RequestControl{}, [](const ofxIC::ToolLoopProgress & progress) {
			if (progress.stage == ofxIC::ToolLoopStage::ExecutingTool)
				throw std::runtime_error("progress exception");
		});
	} catch (const std::runtime_error &) { threw = true; }
	OFXIC_REQUIRE(threw);
	OFXIC_REQUIRE(executions == 0);
	OFXIC_REQUIRE(chat.getMessages().empty());
}

OFXIC_TEST(tool_loop_cancellation_at_model_progress_prevents_transport) {
	int requests = 0;
	bool cancel = false;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		++requests;
		return twoToolCalls();
	});
	ofxIC::ChatSession chat(endpoint);
	ofxIC::ToolRegistry registry;
	registry.add({ "check", "Check", "{}" }, [](const std::string &) {
		return ofxIC::ToolExecutionResult{ true, "done", {} };
	});
	ofxIC::ToolLoop loop(chat, registry);
	const auto result = loop.run("Question", 4, [&]() { return cancel; },
		[&](const ofxIC::ToolLoopProgress &) { cancel = true; });
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(requests == 0);
	OFXIC_REQUIRE(result.modelRequests == 0);
	OFXIC_REQUIRE(chat.getMessages().empty());
}
