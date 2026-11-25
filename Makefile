CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -static -Iinclude
BUILD := build
APP := $(BUILD)/campus_market.exe
TEST := $(BUILD)/campus_tests.exe
WEB := $(BUILD)/campus_market_web.exe
CORE := src/model.cpp src/store.cpp src/user_service.cpp src/item_service.cpp \
        src/search_service.cpp src/reservation_service.cpp src/report_service.cpp \
        src/log_stats_service.cpp
SRC := $(CORE) src/cli.cpp src/main.cpp
TEST_SRC := $(CORE) tests/tests.cpp
WEB_SRC := $(CORE) src/web/web_server.cpp src/web/main_web.cpp

.PHONY: all app test web run clean

all: app test web

app: $(APP)

$(APP): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(APP)

test: $(TEST)

$(TEST): $(TEST_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $(TEST)

web: $(WEB)

$(WEB): $(WEB_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $(WEB_SRC) -o $(WEB) -lws2_32 -lshell32

run: $(APP)
	$(APP) --data data

$(BUILD):
	mkdir $(BUILD)

clean:
	rm -rf $(BUILD)
