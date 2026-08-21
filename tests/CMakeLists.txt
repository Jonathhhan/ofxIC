cmake_minimum_required(VERSION 3.18)
project(ofxIC_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ofxIC_tests
	test_main.cpp
	test_endpoint.cpp
	test_chat_session.cpp
	test_document_index.cpp
	test_tool_loop.cpp
	test_public_headers.cpp
	../src/endpoint/ofxICEndpoint.cpp
	../src/chat/ofxICChatSession.cpp
	../src/documents/ofxICDocumentIndex.cpp
	../src/tools/ofxICToolRegistry.cpp
	../src/tools/ofxICToolLoop.cpp)

target_include_directories(ofxIC_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../src)

enable_testing()
add_test(NAME ofxIC_tests COMMAND ofxIC_tests)
