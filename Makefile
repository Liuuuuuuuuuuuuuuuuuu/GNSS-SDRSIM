CC = gcc
CXX = g++
NVCC ?= /usr/local/cuda-12.8/bin/nvcc
CCACHE ?= $(shell command -v ccache 2>/dev/null)
.DEFAULT_GOAL := all
BIN_DIR ?= bin
OBJ_DIR ?= build
CUDA_OBJ_DIR ?= $(OBJ_DIR)/cuda

ifneq ($(strip $(CCACHE)),)
CC := $(CCACHE) gcc
CXX := $(CCACHE) g++
endif

# 自動偵測 Qt 參數
QT_MODULES = Qt6Widgets Qt6Gui Qt6Core Qt6Network
QT_CFLAGS := $(shell pkg-config --cflags $(QT_MODULES) 2>/dev/null)
QT_LIBS := $(shell pkg-config --libs $(QT_MODULES) 2>/dev/null)

ifeq ($(strip $(QT_LIBS)),)
QT_MODULES = Qt5Widgets Qt5Gui Qt5Core Qt5Network
QT_CFLAGS := $(shell pkg-config --cflags $(QT_MODULES) 2>/dev/null)
QT_LIBS := $(shell pkg-config --libs $(QT_MODULES) 2>/dev/null)
endif

GUI_SRC_DIRS := $(shell find gui -type d)
GUI_CPP_SRCS := $(shell find gui -type f -name '*.cpp')
COMMON_SRCS = main.c globals.c bch.c navbits.c channel.c rinex.c orbits.c coord.c path.c iono.c \
			 usrp_wrapper.cpp main_gui.cpp cuda/cuda_runtime_info.c $(GUI_CPP_SRCS)
COMMON_OBJS = $(addprefix $(OBJ_DIR)/,$(COMMON_SRCS))
COMMON_OBJS := $(COMMON_OBJS:.c=.o)
COMMON_OBJS := $(COMMON_OBJS:.cpp=.o)
DEPS := $(COMMON_OBJS:.o=.d)

CFLAGS = -I. $(addprefix -I,$(GUI_SRC_DIRS)) -O3 -march=native -ffast-math -Wall -Wextra -fopenmp -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -fPIC -MMD -MP
CXXFLAGS = $(CFLAGS) -std=c++14 $(QT_CFLAGS)
NVCCFLAGS_BASE = -O3 --use_fast_math -Xptxas -O3 -Xcompiler "-O3 -fopenmp -fPIC" -I.

CUDA_PATH ?= $(patsubst %/bin/nvcc,%,$(NVCC))
LIBS = -L$(CUDA_PATH)/lib64 -lm -luhd -lboost_system -lcudart -pthread $(QT_LIBS)

include cuda/cuda_targets.mk

GENCODE_ALL = $(strip \
	$(if $(HAS_SM_61),$(GENCODE_PASCAL),) \
	$(if $(HAS_SM_75),$(GENCODE_TURING),) \
	$(if $(HAS_SM_86),$(GENCODE_AMPERE),) \
	$(if $(HAS_SM_89),$(GENCODE_ADA),) \
	$(GENCODE_BLACKWELL))
CUDA_MULTI_OBJ = $(CUDA_OBJ_DIR)/bdssim_multi.o

SUPPORTED_GPU_BIN_NAMES =
ifneq ($(HAS_SM_61),)
SUPPORTED_GPU_BIN_NAMES += bds-sim-pascal
endif
ifneq ($(HAS_SM_75),)
SUPPORTED_GPU_BIN_NAMES += bds-sim-turing
endif
ifneq ($(HAS_SM_86),)
SUPPORTED_GPU_BIN_NAMES += bds-sim-ampere
endif
ifneq ($(HAS_SM_89),)
SUPPORTED_GPU_BIN_NAMES += bds-sim-ada
endif
ifneq ($(strip $(GENCODE_BLACKWELL)),)
SUPPORTED_GPU_BIN_NAMES += bds-sim-blackwell
endif
ifneq ($(strip $(GENCODE_MODERN)),)
SUPPORTED_GPU_BIN_NAMES += bds-sim-modern
endif

SUPPORTED_GPU_BINS = $(addprefix $(BIN_DIR)/,$(SUPPORTED_GPU_BIN_NAMES))

all: bds-sim
all-gpu-binaries: $(SUPPORTED_GPU_BINS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CUDA_MULTI_OBJ): bdssim.cu
	@mkdir -p $(dir $@)
	@if [ -z "$(GENCODE_ALL)" ]; then echo "[make] no supported GPU arch found in current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_ALL) -c $< -o $@

bds-sim: Makefile $(SUPPORTED_GPU_BINS)
	@printf '%s\n' \
	'#!/usr/bin/env bash' \
	'set -eu' \
	'' \
	'ROOT_DIR="$$(cd "$$(dirname "$${BASH_SOURCE:-$$0}")" && pwd)"' \
	'' \
	'pick_from_cc() {' \
	'  cc="$$1"' \
	'  case "$$cc" in' \
	'    6.1*) echo "bds-sim-pascal" ;;' \
	'    7.5*) echo "bds-sim-turing" ;;' \
	'    8.6*|8.7*|8.8*) echo "bds-sim-ampere" ;;' \
	'    8.9*) echo "bds-sim-ada" ;;' \
	'    12.*|11.*|10.*) echo "bds-sim-blackwell" ;;' \
	'    *) echo "bds-sim-modern" ;;' \
	'  esac' \
	'}' \
	'' \
	'gpu_cc=""' \
	'if command -v nvidia-smi >/dev/null 2>&1; then' \
	'  gpu_cc=$$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n 1 | tr -d "[:space:]" || true)' \
	'fi' \
	'' \
	'selected=""' \
	'if [ -n "$$gpu_cc" ]; then' \
	'  selected=$$(pick_from_cc "$$gpu_cc")' \
	'else' \
	'  gpu_name=""' \
	'  if command -v nvidia-smi >/dev/null 2>&1; then' \
	'    gpu_name=$$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -n 1 || true)' \
	'  fi' \
	'  case "$$gpu_name" in' \
	'    *"GTX 10"*|*"P10"*) selected="bds-sim-pascal" ;;' \
	'    *"RTX 20"*|*"TITAN RTX"*) selected="bds-sim-turing" ;;' \
	'    *"RTX 30"*) selected="bds-sim-ampere" ;;' \
	'    *"RTX 40"*) selected="bds-sim-ada" ;;' \
	'    *"RTX 50"*) selected="bds-sim-blackwell" ;;' \
	'    *) selected="bds-sim-modern" ;;' \
	'  esac' \
	'fi' \
	'' \
	'if [ "$$selected" = "bds-sim-pascal" ] && [ ! -x "$$ROOT_DIR/$(BIN_DIR)/$$selected" ]; then' \
	'  echo "[launcher] Pascal GPU detected, but $$selected was not built." >&2' \
	'  echo "[launcher] Rebuild with CUDA 12.x NVCC that supports sm_61:" >&2' \
	'  echo "[launcher]   NVCC=/usr/local/cuda-12.4/bin/nvcc make clean && NVCC=/usr/local/cuda-12.4/bin/nvcc make" >&2' \
	'  exit 2' \
	'fi' \
	'' \
	'for bin in "$$selected" bds-sim-modern bds-sim-ada bds-sim-ampere bds-sim-turing bds-sim-blackwell; do' \
	'  if [ -x "$$ROOT_DIR/$(BIN_DIR)/$$bin" ]; then' \
	'    echo "[launcher] Using $$bin (compute_cap=$${gpu_cc:-unknown})" >&2' \
	'    exec "$$ROOT_DIR/$(BIN_DIR)/$$bin" "$$@"' \
	'  fi' \
	'done' \
	'' \
	'echo "[launcher] No runnable bds-sim binary found. Please run: make" >&2' \
	'exit 2' > $@
	@chmod +x $@


$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/bds-sim-pascal: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/bds-sim-turing: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/bds-sim-ampere: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/bds-sim-ada: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/bds-sim-blackwell: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/bds-sim-modern: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	rm -f bds-sim bds-sim-pascal bds-sim-turing bds-sim-ampere bds-sim-ada bds-sim-blackwell bds-sim-modern

-include $(DEPS)