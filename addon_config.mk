meta:
	ADDON_NAME = ofxIC
	ADDON_DESCRIPTION = Inference connector for local and hosted model endpoints in openFrameworks
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "ai,inference,endpoints,llm,chat,tools,openai"
	ADDON_URL = https://github.com/Jonathhhan/ofxIC

common:
	ADDON_INCLUDES = src
	ADDON_SOURCES = src/endpoint/ofxICEndpoint.cpp
	ADDON_SOURCES += src/chat/ofxICChatSession.cpp
	ADDON_SOURCES += src/documents/ofxICDocumentIndex.cpp
	ADDON_SOURCES += src/tools/ofxICToolRegistry.cpp
	ADDON_SOURCES += src/tools/ofxICToolLoop.cpp
