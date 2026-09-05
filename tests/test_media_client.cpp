#include "test_harness.h"
#include "../src/ofxIC.h"

#include <string>

namespace {
ofxIC::HttpResponse interruptedMediaResponse(ofxIC::RequestFailure failure, int status,
	const std::string & body) {
	ofxIC::HttpResponse response;
	response.started = true;
	response.status = status;
	response.body = body;
	response.failure = failure;
	response.cancelled = failure == ofxIC::RequestFailure::Cancelled;
	response.error = "transport interrupted";
	return response;
}
} // namespace

OFXIC_TEST(media_client_preserves_transport_failure_even_after_success_headers) {
	for (auto failure : { ofxIC::RequestFailure::Cancelled, ofxIC::RequestFailure::Timeout,
		ofxIC::RequestFailure::Transport, ofxIC::RequestFailure::InvalidResponse }) {
		for (int status : { 0, 200 }) {
			ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
				return interruptedMediaResponse(failure, status,
					R"({"supported_modes":["img_gen"],"id":"one","status":"completed","b64_json":"aW1hZ2U="})");
			});
			ofxIC::MediaClient media(endpoint);
			const auto verify = [&](const auto & result) {
				OFXIC_REQUIRE(!result);
				OFXIC_REQUIRE(result.failure == failure);
				OFXIC_REQUIRE(result.cancelled == (failure == ofxIC::RequestFailure::Cancelled));
				OFXIC_REQUIRE(result.error == "transport interrupted");
			};
			verify(media.inspectCapabilities());
			ofxIC::ImageRequest image;
			image.prompt = "image";
			verify(media.generateImage(image));
			verify(media.poll("one"));
			ofxIC::MediaJob job;
			job.id = "one";
			verify(media.cancel(job));
		}
	}
}

OFXIC_TEST(media_client_classifies_missing_http_status_as_transport_failure) {
	ofxIC::Endpoint endpoint("http://example.test", [](const ofxIC::HttpRequest &) {
		return interruptedMediaResponse(ofxIC::RequestFailure::None, 0, "");
	});
	ofxIC::MediaClient media(endpoint);
	OFXIC_REQUIRE(media.poll("one").failure == ofxIC::RequestFailure::Transport);
}

OFXIC_TEST(media_client_preserves_failure_during_native_submit_after_capabilities) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://example.test", [&](const ofxIC::HttpRequest &) {
		if (++calls == 1) {
			auto response = interruptedMediaResponse(ofxIC::RequestFailure::None, 200,
				R"({"supported_modes":["img_gen"]})");
			response.error.clear();
			return response;
		}
		return interruptedMediaResponse(ofxIC::RequestFailure::Timeout, 0, "");
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.prompt = "image";
	const auto result = media.submit(request);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Timeout);
	OFXIC_REQUIRE(calls == 2);
}

OFXIC_TEST(media_client_stops_fal_pipeline_on_transport_failure_at_every_stage) {
	for (auto failure : { ofxIC::RequestFailure::Cancelled, ofxIC::RequestFailure::Timeout,
		ofxIC::RequestFailure::Transport, ofxIC::RequestFailure::InvalidResponse }) {
		for (int faultAt = 1; faultAt <= 5; ++faultAt) {
			int calls = 0;
			ofxIC::Endpoint endpoint("https://router.huggingface.co", [&](const ofxIC::HttpRequest &) {
				const std::string bodies[] = {
					R"({"inferenceProviderMapping":{"fal-ai":{"providerId":"fal-ai/wan/test","task":"text-to-video"}}})",
					R"({"request_id":"one","status":"IN_QUEUE","response_url":"https://queue.fal.run/fal-ai/wan/test/requests/one"})",
					R"({"status":"COMPLETED"})",
					R"({"video":{"url":"https://cdn.example/video.mp4"}})", "video bytes"
				};
				++calls;
				OFXIC_REQUIRE(calls <= 5);
				auto response = interruptedMediaResponse(calls == faultAt ? failure : ofxIC::RequestFailure::None,
					200, bodies[calls - 1]);
				if (calls != faultAt) response.error.clear();
				return response;
			});
			ofxIC::MediaClient media(endpoint);
			ofxIC::MediaJobRequest request;
			request.kind = ofxIC::MediaKind::Video;
			request.model = "test/video";
			request.prompt = "video";
			auto result = media.submitHuggingFaceFal(request);
			if (result) result = media.poll(result);
			OFXIC_REQUIRE(!result);
			OFXIC_REQUIRE(result.failure == failure);
			OFXIC_REQUIRE(result.error == "transport interrupted");
			OFXIC_REQUIRE(calls == faultAt);
		}
	}
}

OFXIC_TEST(media_client_generates_openai_compatible_images) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:1234/v1", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"created":7,"output_format":"png","data":[{"b64_json":"aW1hZ2U="}]})";
		return response;
	});
	endpoint.setBearerToken("media-token");
	ofxIC::MediaClient media(endpoint);
	ofxIC::ImageRequest request;
	request.prompt = "A small red cube";
	request.model = "gpt-image-test";
	request.width = 640;
	request.height = 480;

	const auto result = media.generateImage(request);
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(captured.url == "http://localhost:1234/v1/images/generations");
	OFXIC_REQUIRE(captured.body.find("\"model\":\"gpt-image-test\"") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("\"size\":\"640x480\"") != std::string::npos);
	OFXIC_REQUIRE(captured.headers.size() == 1);
	OFXIC_REQUIRE(result.outputFormat == "png");
	OFXIC_REQUIRE(result.imagesBase64.size() == 1);
	OFXIC_REQUIRE(result.imagesBase64[0] == "aW1hZ2U=");
}

OFXIC_TEST(media_client_accepts_image_urls) {
	ofxIC::Endpoint endpoint("https://media.example/v1", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"data":[{"url":"https://cdn.example/image.png"}]})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::ImageRequest request;
	request.prompt = "A blue sphere";
	const auto result = media.generateImage(request);
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(result.urls.size() == 1);
	OFXIC_REQUIRE(result.urls[0] == "https://cdn.example/image.png");
}

OFXIC_TEST(media_client_surfaces_openai_image_error_detail) {
	ofxIC::Endpoint endpoint("https://api.openai.com/v1", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 400;
		response.body = R"({"error":{"message":"Invalid value for size: 512x512"}})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::ImageRequest request;
	request.prompt = "test";
	request.model = "gpt-image-2";
	request.width = 512;
	request.height = 512;

	const auto result = media.generateImage(request);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.httpStatus == 400);
	OFXIC_REQUIRE(result.error ==
		"image endpoint returned HTTP 400: Invalid value for size: 512x512");
}

OFXIC_TEST(media_client_submits_native_image_job) {
	int calls = 0;
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest & request) {
		ofxIC::HttpResponse response;
		response.started = true;
		if (calls++ == 0) {
			response.status = 200;
			response.body = R"({"model":{"name":"sd_turbo.safetensors"},"supported_modes":["img_gen"],"output_formats_by_mode":{"img_gen":["png"]}})";
		} else {
			captured = request;
			response.status = 202;
			response.body = R"({"id":"job-image","kind":"img_gen","status":"queued","poll_url":"/sdcpp/v1/jobs/job-image"})";
		}
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Image;
	request.prompt = "A paper sculpture";
	request.model = "G:/Models/sd_turbo.safetensors";
	request.imageCount = 2;

	const auto job = media.submit(request);
	OFXIC_REQUIRE(job);
	OFXIC_REQUIRE(job.state == ofxIC::MediaJobState::Queued);
	OFXIC_REQUIRE(job.id == "job-image");
	OFXIC_REQUIRE(captured.url == "http://localhost:1234/sdcpp/v1/img_gen");
	OFXIC_REQUIRE(captured.body.find("\"batch_count\":2") != std::string::npos);
	OFXIC_REQUIRE(captured.body.find("\"output_format\":\"png\"") != std::string::npos);
}

OFXIC_TEST(media_client_submits_and_polls_native_video_job) {
	int calls = 0;
	ofxIC::HttpRequest inspected;
	ofxIC::HttpRequest submitted;
	ofxIC::HttpRequest polled;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest & request) {
		ofxIC::HttpResponse response;
		response.started = true;
		if (calls++ == 0) {
			inspected = request;
			response.status = 200;
			response.body = R"({"model":{"name":"wan-video.gguf"},"supported_modes":["vid_gen"],"output_formats_by_mode":{"vid_gen":["avi","webm"]}})";
		} else if (calls == 2) {
			submitted = request;
			response.status = 202;
			response.body = R"({"id":"job-video","kind":"vid_gen","status":"queued","poll_url":"/sdcpp/v1/jobs/job-video"})";
		} else {
			polled = request;
			response.status = 200;
			response.body = R"({"id":"job-video","kind":"vid_gen","status":"completed","result":{"output_format":"avi","mime_type":"video/x-msvideo","fps":12,"frame_count":29,"b64_json":"dmlkZW8="},"error":null})";
		}
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Video;
	request.prompt = "A paper sculpture turning slowly";
	request.videoFrames = 29;
	request.fps = 12;
	request.seed = 123;
	request.steps = 24;
	request.guidance = 4.5f;
	request.sampleMethod = "euler";
	request.scheduler = "simple";

	const auto submittedJob = media.submit(request);
	const auto completedJob = media.poll(submittedJob);
	OFXIC_REQUIRE(submittedJob);
	OFXIC_REQUIRE(completedJob);
	OFXIC_REQUIRE(inspected.url == "http://localhost:1234/sdcpp/v1/capabilities");
	OFXIC_REQUIRE(inspected.headers.empty());
	OFXIC_REQUIRE(submitted.url == "http://localhost:1234/sdcpp/v1/vid_gen");
	OFXIC_REQUIRE(submitted.body.find("\"video_frames\":29") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"fps\":12") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"seed\":123") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"sample_steps\":24") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"txt_cfg\":4.5") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"sample_method\":\"euler\"") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"scheduler\":\"simple\"") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"output_format\":\"avi\"") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("batch_count") == std::string::npos);
	OFXIC_REQUIRE(polled.url == "http://localhost:1234/sdcpp/v1/jobs/job-video");
	OFXIC_REQUIRE(completedJob.kind == ofxIC::MediaKind::Video);
	OFXIC_REQUIRE(completedJob.state == ofxIC::MediaJobState::Completed);
	OFXIC_REQUIRE(completedJob.mimeType == "video/x-msvideo");
	OFXIC_REQUIRE(completedJob.frameCount == 29);
	OFXIC_REQUIRE(completedJob.payloadsBase64.size() == 1);
}

OFXIC_TEST(media_client_reports_failed_jobs) {
	ofxIC::MediaJob queued;
	queued.success = true;
	queued.kind = ofxIC::MediaKind::Video;
	queued.id = "job-failed";
	ofxIC::Endpoint endpoint("http://localhost:1234", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"id":"job-failed","kind":"vid_gen","status":"failed","result":null,"error":{"code":"generation_failed","message":"no frames"}})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	const auto failed = media.poll(queued);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.state == ofxIC::MediaJobState::Failed);
	OFXIC_REQUIRE(failed.error == "no frames");
}

OFXIC_TEST(media_client_cancels_native_jobs) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"id":"job-cancel","kind":"vid_gen","status":"cancelled"})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJob job;
	job.kind = ofxIC::MediaKind::Video;
	job.id = "job-cancel";

	const auto cancelled = media.cancel(job);
	OFXIC_REQUIRE(cancelled);
	OFXIC_REQUIRE(cancelled.state == ofxIC::MediaJobState::Cancelled);
	OFXIC_REQUIRE(!cancelled.cancelled);
	OFXIC_REQUIRE(cancelled.failure == ofxIC::RequestFailure::None);
	OFXIC_REQUIRE(captured.method == ofxIC::HttpMethod::Post);
	OFXIC_REQUIRE(captured.url == "http://localhost:1234/sdcpp/v1/jobs/job-cancel/cancel");
}

OFXIC_TEST(media_client_generates_hugging_face_images_through_fal) {
	std::vector<ofxIC::HttpRequest> captured;
	ofxIC::Endpoint endpoint("https://router.huggingface.co/v1", [&](const ofxIC::HttpRequest & request) {
		captured.push_back(request);
		ofxIC::HttpResponse response;
		response.started = true;
		if (captured.size() == 1) {
			response.status = 200;
			response.body = R"({"inferenceProviderMapping":{"fal-ai":{"status":"live","providerId":"fal-ai/flux/dev","task":"text-to-image"}}})";
		} else if (captured.size() == 2) {
			response.status = 200;
			response.body = R"({"images":[{"url":"https://cdn.example/generated.png"}]})";
		} else {
			response.status = 200;
			response.body = std::string("\x89PNG\0payload", 12);
		}
		return response;
	});
	endpoint.setBearerToken("hf_test_token");
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Image;
	request.prompt = "A paper fox";
	request.model = "black-forest-labs/FLUX.1-dev";
	request.width = 640;
	request.height = 480;

	const auto result = media.submitHuggingFaceFal(request);
	OFXIC_REQUIRE(result);
	OFXIC_REQUIRE(result.protocol == ofxIC::MediaProtocol::HuggingFaceFal);
	OFXIC_REQUIRE(result.state == ofxIC::MediaJobState::Completed);
	OFXIC_REQUIRE(result.payloadBytes.size() == 1);
	OFXIC_REQUIRE(result.outputFormat == "png");
	OFXIC_REQUIRE(captured.size() == 3);
	OFXIC_REQUIRE(captured[0].url.find("https://huggingface.co/api/models/black-forest-labs/FLUX.1-dev") == 0);
	OFXIC_REQUIRE(captured[0].headers.empty());
	OFXIC_REQUIRE(captured[1].url == "https://router.huggingface.co/fal-ai/fal-ai/flux/dev");
	OFXIC_REQUIRE(captured[1].body.find("\"image_size\":{\"width\":640,\"height\":480}") != std::string::npos);
	OFXIC_REQUIRE(captured[1].headers.size() == 1);
	OFXIC_REQUIRE(captured[2].url == "https://cdn.example/generated.png");
	OFXIC_REQUIRE(captured[2].headers.empty());
	OFXIC_REQUIRE(captured[2].accept == "*/*");
}

OFXIC_TEST(media_client_submits_and_polls_hugging_face_video) {
	std::vector<ofxIC::HttpRequest> captured;
	ofxIC::Endpoint endpoint("https://router.huggingface.co/v1", [&](const ofxIC::HttpRequest & request) {
		captured.push_back(request);
		ofxIC::HttpResponse response;
		response.started = true;
		switch (captured.size()) {
		case 1:
			response.status = 200;
			response.body = R"({"inferenceProviderMapping":{"fal-ai":{"status":"live","providerId":"fal-ai/wan/v2.2-5b/text-to-video","task":"text-to-video"}}})";
			break;
		case 2:
			response.status = 200;
			response.body = R"({"request_id":"queue-7","status":"IN_QUEUE","response_url":"https://queue.fal.run/fal-ai/wan/v2.2-5b/text-to-video/requests/queue-7"})";
			break;
		case 3:
			response.status = 200;
			response.body = R"({"status":"COMPLETED"})";
			break;
		case 4:
			response.status = 200;
			response.body = R"({"video":{"url":"https://cdn.example/generated.mp4"}})";
			break;
		default:
			response.status = 200;
			response.body = std::string("video\0bytes", 11);
			break;
		}
		return response;
	});
	endpoint.setBearerToken("hf_test_token");
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Video;
	request.prompt = "A paper fox turns around";
	request.model = "Wan-AI/Wan2.2-TI2V-5B";
	request.videoFrames = 25;

	const auto queued = media.submitHuggingFaceFal(request);
	OFXIC_REQUIRE(queued);
	OFXIC_REQUIRE(queued.state == ofxIC::MediaJobState::Queued);
	OFXIC_REQUIRE(queued.id == "queue-7");
	OFXIC_REQUIRE(captured[1].url == "https://router.huggingface.co/fal-ai/fal-ai/wan/v2.2-5b/text-to-video?_subdomain=queue");
	OFXIC_REQUIRE(captured[1].body.find("\"num_frames\":25") != std::string::npos);

	const auto completed = media.poll(queued);
	OFXIC_REQUIRE(completed);
	OFXIC_REQUIRE(completed.state == ofxIC::MediaJobState::Completed);
	OFXIC_REQUIRE(completed.outputFormat == "mp4");
	OFXIC_REQUIRE(completed.payloadBytes.size() == 1);
	OFXIC_REQUIRE(captured.size() == 5);
	OFXIC_REQUIRE(captured[2].url.find("/status?_subdomain=queue") != std::string::npos);
	OFXIC_REQUIRE(captured[3].url.find("/requests/queue-7?_subdomain=queue") != std::string::npos);
	OFXIC_REQUIRE(captured[4].headers.empty());
}

OFXIC_TEST(media_client_explains_hugging_face_payment_required) {
	int calls = 0;
	ofxIC::Endpoint endpoint("https://router.huggingface.co/v1", [&](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		if (calls++ == 0) {
			response.status = 200;
			response.body = R"({"inferenceProviderMapping":{"fal-ai":{"status":"live","providerId":"fal-ai/wan/v2.2-5b/text-to-video","task":"text-to-video"}}})";
		} else {
			response.status = 402;
		}
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Video;
	request.prompt = "A paper fox turns around";
	request.model = "Wan-AI/Wan2.2-TI2V-5B";

	const auto failed = media.submitHuggingFaceFal(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.httpStatus == 402);
	OFXIC_REQUIRE(failed.error.find("inference credits or pay-as-you-go billing are required") != std::string::npos);
}

OFXIC_TEST(media_client_inspects_native_runtime_capabilities) {
	ofxIC::Endpoint endpoint("http://localhost:1234", [](const ofxIC::HttpRequest & request) {
		OFXIC_REQUIRE(request.url == "http://localhost:1234/sdcpp/v1/capabilities");
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"current_mode":"vid_gen","defaults":{"width":512,"height":384,"video_frames":33,"fps":16,"output_format":"avi"},"limits":{"min_width":64,"max_width":2048,"min_height":64,"max_height":1024},"model":{"name":"Wan2.1.gguf"},"output_formats_by_mode":{"vid_gen":["avi","webm"]},"samplers":["euler","dpm++2m"],"schedulers":["simple","karras"],"supported_modes":["vid_gen"]})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	const auto capabilities = media.inspectCapabilities();
	OFXIC_REQUIRE(capabilities);
	OFXIC_REQUIRE(capabilities.model == "Wan2.1.gguf");
	OFXIC_REQUIRE(capabilities.currentMode == "vid_gen");
	OFXIC_REQUIRE(capabilities.supports(ofxIC::MediaKind::Video));
	OFXIC_REQUIRE(!capabilities.supports(ofxIC::MediaKind::Image));
	OFXIC_REQUIRE(capabilities.videoOutputFormats.size() == 2);
	OFXIC_REQUIRE(capabilities.minWidth == 64);
	OFXIC_REQUIRE(capabilities.maxWidth == 2048);
	OFXIC_REQUIRE(capabilities.minHeight == 64);
	OFXIC_REQUIRE(capabilities.maxHeight == 1024);
	OFXIC_REQUIRE(capabilities.defaultWidth == 512);
	OFXIC_REQUIRE(capabilities.defaultHeight == 384);
	OFXIC_REQUIRE(capabilities.defaultVideoFrames == 33);
	OFXIC_REQUIRE(capabilities.defaultFps == 16);
	OFXIC_REQUIRE(capabilities.defaultOutputFormat == "avi");
	OFXIC_REQUIRE(capabilities.samplers.size() == 2);
	OFXIC_REQUIRE(capabilities.schedulers.size() == 2);
}

OFXIC_TEST(media_client_rejects_incomplete_capability_success) {
	ofxIC::Endpoint endpoint("http://localhost:1234", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"model":{"name":"unknown"}})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	const auto capabilities = media.inspectCapabilities();
	OFXIC_REQUIRE(!capabilities);
	OFXIC_REQUIRE(capabilities.failure == ofxIC::RequestFailure::InvalidResponse);
	OFXIC_REQUIRE(capabilities.error.find("supported_modes") != std::string::npos);
}

OFXIC_TEST(media_client_rejects_dimensions_outside_loaded_context_limits) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest &) {
		++calls;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"limits":{"min_width":64,"max_width":1024,"min_height":64,"max_height":1024},"model":{"name":"sd_turbo.safetensors"},"supported_modes":["img_gen"]})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Image;
	request.prompt = "Too wide";
	request.model = "sd_turbo.safetensors";
	request.width = 2048;
	request.height = 512;

	const auto failed = media.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("2048x512") != std::string::npos);
	OFXIC_REQUIRE(failed.error.find("outside the loaded context limits") != std::string::npos);
	OFXIC_REQUIRE(calls == 1);
}

OFXIC_TEST(media_client_rejects_unsupported_capability_choice_before_submission) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest &) {
		++calls;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"model":{"name":"sd_turbo.safetensors"},"supported_modes":["img_gen"],"output_formats_by_mode":{"img_gen":["png"]},"samplers":["euler"],"schedulers":["simple"]})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Image;
	request.prompt = "Unsupported sampler";
	request.model = "sd_turbo.safetensors";
	request.sampleMethod = "not-a-sampler";

	const auto failed = media.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error == "loaded context does not support sampler not-a-sampler");
	OFXIC_REQUIRE(calls == 1);
}

OFXIC_TEST(media_client_rejects_image_when_another_context_is_loaded) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest &) {
		++calls;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"model":{"name":"Wan2.1-T2V.gguf"},"supported_modes":["vid_gen"]})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Image;
	request.prompt = "A paper sculpture";
	request.model = "G:/Models/sd_turbo.safetensors";

	const auto failed = media.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("loaded Wan2.1-T2V.gguf") != std::string::npos);
	OFXIC_REQUIRE(failed.error.find("selected context is sd_turbo.safetensors") != std::string::npos);
	OFXIC_REQUIRE(calls == 1);
}

OFXIC_TEST(media_client_rejects_video_when_loaded_model_is_image_only) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest &) {
		++calls;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"model":{"name":"sd_turbo.safetensors"},"supported_modes":["img_gen"],"output_formats_by_mode":{"img_gen":["png"]}})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Video;
	request.prompt = "This requires a video model";
	const auto failed = media.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error.find("sd_turbo.safetensors") != std::string::npos);
	OFXIC_REQUIRE(failed.error.find("does not support video") != std::string::npos);
	OFXIC_REQUIRE(calls == 1);
}

OFXIC_TEST(media_client_surfaces_native_submission_error_detail) {
	int calls = 0;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		if (calls++ == 0) {
			response.status = 200;
			response.body = R"({"model":{"name":"wan.gguf"},"supported_modes":["vid_gen"],"output_formats_by_mode":{"vid_gen":["avi"]}})";
		} else {
			response.status = 400;
			response.body = R"({"error":"invalid video dimensions"})";
		}
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Video;
	request.prompt = "invalid request";
	const auto failed = media.submit(request);
	OFXIC_REQUIRE(!failed);
	OFXIC_REQUIRE(failed.error == "media endpoint returned HTTP 400: invalid video dimensions");
}

OFXIC_TEST(media_client_forwards_control_and_classifies_local_cancellation) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = false;
		response.cancelled = true;
		response.failure = ofxIC::RequestFailure::Cancelled;
		response.error = "request cancelled";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.prompt = "cancel this image";
	ofxIC::RequestControl control;
	control.timeoutSeconds = 7;
	control.shouldCancel = [] { return true; };

	const auto result = media.submit(request, control);
	OFXIC_REQUIRE(!result);
	OFXIC_REQUIRE(result.cancelled);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Cancelled);
	OFXIC_REQUIRE(captured.timeoutSeconds == 7);
	OFXIC_REQUIRE(captured.shouldCancel && captured.shouldCancel());
}

OFXIC_TEST(media_client_keeps_remote_job_cancellation_distinct) {
	ofxIC::Endpoint endpoint("http://localhost:1234", [](const ofxIC::HttpRequest &) {
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"id":"job-remote","kind":"vid_gen","status":"cancelled"})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJob job;
	job.kind = ofxIC::MediaKind::Video;
	job.id = "job-remote";

	const auto result = media.poll(job);
	OFXIC_REQUIRE(result.state == ofxIC::MediaJobState::Cancelled);
	OFXIC_REQUIRE(!result.cancelled);
	OFXIC_REQUIRE(result.failure == ofxIC::RequestFailure::Provider);
}
