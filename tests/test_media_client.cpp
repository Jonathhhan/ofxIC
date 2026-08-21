#include "test_harness.h"
#include "../src/ofxIC.h"

#include <string>

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

OFXIC_TEST(media_client_submits_native_image_job) {
	ofxIC::HttpRequest captured;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest & request) {
		captured = request;
		ofxIC::HttpResponse response;
		response.started = true;
		response.status = 202;
		response.body = R"({"id":"job-image","kind":"img_gen","status":"queued","poll_url":"/sdcpp/v1/jobs/job-image"})";
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Image;
	request.prompt = "A paper sculpture";
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
	ofxIC::HttpRequest submitted;
	ofxIC::HttpRequest polled;
	ofxIC::Endpoint endpoint("http://localhost:1234", [&](const ofxIC::HttpRequest & request) {
		ofxIC::HttpResponse response;
		response.started = true;
		if (calls++ == 0) {
			submitted = request;
			response.status = 202;
			response.body = R"({"id":"job-video","kind":"vid_gen","status":"queued","poll_url":"/sdcpp/v1/jobs/job-video"})";
		} else {
			polled = request;
			response.status = 200;
			response.body = R"({"id":"job-video","kind":"vid_gen","status":"completed","result":{"output_format":"webm","mime_type":"video/webm","fps":12,"frame_count":29,"b64_json":"dmlkZW8="},"error":null})";
		}
		return response;
	});
	ofxIC::MediaClient media(endpoint);
	ofxIC::MediaJobRequest request;
	request.kind = ofxIC::MediaKind::Video;
	request.prompt = "A paper sculpture turning slowly";
	request.videoFrames = 29;
	request.fps = 12;

	const auto submittedJob = media.submit(request);
	const auto completedJob = media.poll(submittedJob);
	OFXIC_REQUIRE(submittedJob);
	OFXIC_REQUIRE(completedJob);
	OFXIC_REQUIRE(submitted.url == "http://localhost:1234/sdcpp/v1/vid_gen");
	OFXIC_REQUIRE(submitted.body.find("\"video_frames\":29") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("\"fps\":12") != std::string::npos);
	OFXIC_REQUIRE(submitted.body.find("batch_count") == std::string::npos);
	OFXIC_REQUIRE(polled.url == "http://localhost:1234/sdcpp/v1/jobs/job-video");
	OFXIC_REQUIRE(completedJob.kind == ofxIC::MediaKind::Video);
	OFXIC_REQUIRE(completedJob.state == ofxIC::MediaJobState::Completed);
	OFXIC_REQUIRE(completedJob.mimeType == "video/webm");
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
	OFXIC_REQUIRE(captured.method == ofxIC::HttpMethod::Post);
	OFXIC_REQUIRE(captured.url == "http://localhost:1234/sdcpp/v1/jobs/job-cancel/cancel");
}
