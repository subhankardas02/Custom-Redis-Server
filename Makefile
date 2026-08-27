CXX = g++
CXXFLAGS = -O3 -Wall -std=c++17 -I./src

SRC_DIR = src
OBJ_DIR = obj

SERVER_OBJS = $(OBJ_DIR)/server.o $(OBJ_DIR)/hashtable.o $(OBJ_DIR)/heap.o
CLIENT_OBJS = $(OBJ_DIR)/client.o
BENCH_OBJS = $(OBJ_DIR)/benchmark.o

all: server client benchmark

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

server: $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

client: $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

benchmark: $(BENCH_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(OBJ_DIR) server client benchmark
