cmake_minimum_required(VERSION 3.18)
project(ofxIC_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ofxIC_tests
	test_main.cpp
	test_endpoint.cpp
	test_media_client.cpp
	test_acestep_music_client.cpp
	test_stability_audio_client.cpp
	test_transcription_client.cpp
	test_segmentation_client.cpp
	test_chat_session.cpp
	test_document_index.cpp
	test_example_settings.cpp
	test_example_credentials.cpp
	test_tool_loop.cpp
	test_public_headers.cpp
	../src/endpoint/ofxICEndpoint.cpp
	../src/media/ofxICMediaClient.cpp
	../src/media/ofxICMediaClientHuggingFaceFal.cpp
	../src/music/ofxICAceStepMusicClient.cpp
	../src/music/ofxICStabilityAudioClient.cpp
	../src/audio/ofxICTranscriptionClient.cpp
	../src/segmentation/ofxICSegmentationClient.cpp
	../src/chat/ofxICChatSession.cpp
	../src/documents/ofxICDocumentIndex.cpp
	../ofxICExample/src/ExampleSettings.cpp
	../ofxICExample/src/ExampleCredentialStore.cpp
	../src/tools/ofxICToolRegistry.cpp
	../src/tools/ofxICToolLoop.cpp)

add_executable(ofxIC_acestep_live_smoke
	live_acestep_smoke.cpp
	../src/endpoint/ofxICEndpoint.cpp
	../src/music/ofxICAceStepMusicClient.cpp)

target_include_directories(ofxIC_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_include_directories(ofxIC_acestep_live_smoke PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../src)

if(WIN32)
	target_link_libraries(ofxIC_tests PRIVATE Advapi32 Winhttp)
	target_link_libraries(ofxIC_acestep_live_smoke PRIVATE Winhttp)
endif()

if(MSVC)
	target_compile_options(ofxIC_tests PRIVATE /EHsc)
	target_compile_options(ofxIC_acestep_live_smoke PRIVATE /EHsc)
endif()

enable_testing()
add_test(NAME ofxIC_tests COMMAND ofxIC_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
	add_test(
		NAME ofxIC_web_snapshot_tests
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_web_snapshot.py)
	add_test(
		NAME ofxIC_sam_bridge_tests
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_sam_bridge.py)
endif()
