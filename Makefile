CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude
BUILD := build
APP := $(BUILD)/campus_market.exe
TEST := $(BUILD)/campus_tests.exe
SRC := src/model.cpp src/store.cpp src/user_service.cpp src/item_service.cpp \
       src/search_service.cpp src/reservation_service.cpp src/report_service.cpp \
       src/log_stats_service.cpp src/cli.cpp src/main.cpp
TEST_SRC := $(filter-out src/cli.cpp src/main.cpp,$(SRC)) tests/tests.cpp

.PHONY: all app test run clean

all: app test

app: $(APP)

$(APP): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(APP)

test: $(TEST)

$(TEST): $(TEST_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $(TEST)

run: $(APP)
	$(APP) --data data

$(BUILD):
	mkdir $(BUILD)

clean:
	rm -rf $(BUILD)
