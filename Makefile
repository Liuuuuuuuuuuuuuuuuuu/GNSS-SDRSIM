CC = gcc
CXX = g++
NVCC ?= $(shell command -v nvcc 2>/dev/null || echo /usr/local/cuda/bin/nvcc)
CCACHE ?= $(shell command -v ccache 2>/dev/null)
.DEFAULT_GOAL := all
BIN_DIR ?= bin
OBJ_DIR ?= build
CUDA_OBJ_DIR ?= $(OBJ_DIR)/cuda
JOBS ?= $(shell nproc)

# Default to all logical CPUs unless user already passed -j/--jobs.
ifeq ($(filter -j% --jobs=%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif

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
INCLUDE_DIRS := $(shell find include -type d 2>/dev/null)
GUI_CPP_SRCS := $(shell find gui -type f -name '*.cpp')
COMMON_SRCS = src/apps/cli/main.c src/core/globals.c src/core/bch.c src/nav/navbits.c src/core/channel.c src/nav/rinex.c src/nav/rinex_catalog.c src/nav/orbits.c src/geo/coord.c src/geo/path.c src/nav/iono.c src/runtime/gnss_rx.c \
			 src/runtime/usrp_wrapper.cpp src/runtime/rid_rx.cpp src/runtime/wifi_rid_rx.c src/runtime/ble_rid_rx.c src/apps/gui/main_gui.cpp cuda/cuda_runtime_info.c $(GUI_CPP_SRCS)
COMMON_OBJS = $(addprefix $(OBJ_DIR)/,$(COMMON_SRCS))
COMMON_OBJS := $(COMMON_OBJS:.c=.o)
COMMON_OBJS := $(COMMON_OBJS:.cpp=.o)
DEPS := $(COMMON_OBJS:.o=.d)

CFLAGS = -I. $(addprefix -I,$(INCLUDE_DIRS)) $(addprefix -I,$(GUI_SRC_DIRS)) -O3 -march=native -ffast-math -Wall -Wextra -fopenmp -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -fPIC -ffunction-sections -fdata-sections -MMD -MP
CXXFLAGS = $(CFLAGS) -std=c++14 $(QT_CFLAGS)
NVCCFLAGS_BASE = -O3 --use_fast_math -Xptxas -O3 -Xcompiler "-O3 -fopenmp -fPIC" -I. $(addprefix -I,$(INCLUDE_DIRS))

CUDA_PATH ?= $(patsubst %/bin/nvcc,%,$(NVCC))
CUDA_LIB_CANDIDATES := \
	$(CUDA_PATH)/targets/x86_64-linux/lib64 \
	$(CUDA_PATH)/targets/x86_64-linux/lib \
	$(CUDA_PATH)/lib64 \
	/usr/lib/x86_64-linux-gnu
CUDA_LIB_DIR ?= $(firstword $(foreach d,$(CUDA_LIB_CANDIDATES),$(if $(wildcard $(d)/libcudart.so*),$(d),)))
ifeq ($(strip $(CUDA_LIB_DIR)),)
CUDA_LIB_DIR := /usr/lib/x86_64-linux-gnu
endif
LIBS = -L$(CUDA_LIB_DIR) -Wl,-rpath,$(CUDA_LIB_DIR) -Wl,--gc-sections -lm -luhd -lboost_system -lcudart -pthread $(QT_LIBS)

include cuda/cuda_targets.mk

GENCODE_ALL = $(strip \
	$(if $(HAS_SM_61),$(GENCODE_PASCAL),) \
	$(if $(HAS_SM_75),$(GENCODE_TURING),) \
	$(if $(HAS_SM_86),$(GENCODE_AMPERE),) \
	$(if $(HAS_SM_89),$(GENCODE_ADA),) \
	$(GENCODE_BLACKWELL))
GENCODE_FAT = $(strip $(GENCODE_ALL) $(PTX_FALLBACK))
CUDA_MULTI_OBJ = $(CUDA_OBJ_DIR)/bdssim_multi.o
FAT_BIN = $(BIN_DIR)/gnss-sim-fat
WIFI_RID_BRIDGE = $(BIN_DIR)/wifi-rid-bridge
WIFI_RID_TSHARK_BRIDGE = $(BIN_DIR)/wifi-rid-tshark-bridge
BLE_RID_BRIDGE  = $(BIN_DIR)/ble-rid-bridge
GNSS_SPLASH_BIN = $(BIN_DIR)/gnss-loading-splash
LAUNCHER_TEMPLATE = scripts/launcher/gnss-sim-launcher.sh
REBUILD_SCRIPT = scripts/build/rebuild-local.sh
SUPPORT_CHECK_SCRIPT = scripts/build/check-gpu-series-support.sh
GNSS_LAUNCHER = gnss-sim

.PHONY: all release rebuild all-gpu-binaries check-gpu-series-support print-gencode-fat clean \
	cuda-smoke cuda-doctor cuda-heal package-deb package-appimage
	run-wifi-auto install-desktop-icon

SUPPORTED_GPU_BIN_NAMES =
ifneq ($(HAS_SM_61),)
SUPPORTED_GPU_BIN_NAMES += gnss-sim-pascal
endif
ifneq ($(HAS_SM_75),)
SUPPORTED_GPU_BIN_NAMES += gnss-sim-turing
endif
ifneq ($(HAS_SM_86),)
SUPPORTED_GPU_BIN_NAMES += gnss-sim-ampere
endif
ifneq ($(HAS_SM_89),)
SUPPORTED_GPU_BIN_NAMES += gnss-sim-ada
endif
ifneq ($(strip $(GENCODE_BLACKWELL)),)
SUPPORTED_GPU_BIN_NAMES += gnss-sim-blackwell
endif
ifneq ($(strip $(GENCODE_MODERN)),)
SUPPORTED_GPU_BIN_NAMES += gnss-sim-modern
endif

SUPPORTED_GPU_BINS = $(addprefix $(BIN_DIR)/,$(SUPPORTED_GPU_BIN_NAMES))

# ── Dependency pre-check ────────────────────────────────────────────────────
.PHONY: check-deps
check-deps:
	@ok=1; \
	\
	echo "[check-deps] compiler ..."; \
	if ! command -v g++ >/dev/null 2>&1; then \
		echo "  MISSING: g++ (install: sudo apt install build-essential)"; ok=0; \
	fi; \
	\
	echo "[check-deps] NVCC ..."; \
	if [ ! -x "$(NVCC)" ]; then \
		echo "  MISSING: nvcc  (install CUDA Toolkit: https://developer.nvidia.com/cuda-downloads)"; ok=0; \
	fi; \
	\
	echo "[check-deps] pkg-config ..."; \
	if ! command -v pkg-config >/dev/null 2>&1; then \
		echo "  MISSING: pkg-config (install: sudo apt install pkg-config)"; ok=0; \
	fi; \
	\
	echo "[check-deps] Qt (Widgets/Network) ..."; \
	if [ -z "$(QT_LIBS)" ]; then \
		echo "  MISSING: Qt dev headers  (install: sudo apt install qtbase5-dev qtbase5-dev-tools)"; ok=0; \
	fi; \
	\
	echo "[check-deps] libuhd ..."; \
	if ! pkg-config --exists uhd 2>/dev/null && ! ldconfig -p 2>/dev/null | grep -q libuhd; then \
		echo "  MISSING: libuhd  (install: sudo apt install libuhd-dev)"; ok=0; \
	fi; \
	\
	echo "[check-deps] libboost_system ..."; \
	if ! ldconfig -p 2>/dev/null | grep -q libboost_system; then \
		echo "  MISSING: libboost_system  (install: sudo apt install libboost-system-dev)"; ok=0; \
	fi; \
	\
	echo "[check-deps] libcudart ..."; \
	if [ -z "$(CUDA_LIB_DIR)" ] || [ ! -d "$(CUDA_LIB_DIR)" ]; then \
		echo "  MISSING: libcudart  (check CUDA Toolkit installation)"; ok=0; \
	fi; \
	\
	if [ "$$ok" -eq 0 ]; then \
		echo ""; \
		echo "[check-deps] *** One or more dependencies are missing. Fix the above, then re-run make. ***"; \
		exit 1; \
	fi; \
	echo "[check-deps] All required dependencies found."

all: check-deps $(GNSS_SPLASH_BIN) $(GNSS_LAUNCHER) $(WIFI_RID_BRIDGE) $(WIFI_RID_TSHARK_BRIDGE) $(BLE_RID_BRIDGE)

release:
	@$(MAKE) -j$(JOBS) all

rebuild:
	@$(MAKE) clean
	@$(MAKE) -j$(JOBS) all
all-gpu-binaries: $(FAT_BIN) $(SUPPORTED_GPU_BINS)
check-gpu-series-support: $(SUPPORT_CHECK_SCRIPT)
	@bash $(SUPPORT_CHECK_SCRIPT)
print-gencode-fat:
	@echo $(GENCODE_FAT)

cuda-smoke:
	@set -e; \
	SMOKE_SRC=/tmp/bds_cuda_smoke_min.cu; \
	SMOKE_BIN=/tmp/bds_cuda_smoke_min; \
	printf '%s\n' \
	'#include <cstdio>' \
	'#include <cuda_runtime.h>' \
	'int main(){' \
	'  cudaError_t st = cudaFree(0);' \
	'  if(st!=cudaSuccess){' \
	'    std::printf("cudaFree(0) fail: %s\\n", cudaGetErrorString(st));' \
	'    return 2;' \
	'  }' \
	'  void* p=nullptr;' \
	'  st = cudaMalloc(&p, 1<<20);' \
	'  if(st!=cudaSuccess){' \
	'    std::printf("cudaMalloc fail: %s\\n", cudaGetErrorString(st));' \
	'    return 3;' \
	'  }' \
	'  cudaFree(p);' \
	'  std::puts("ok");' \
	'  return 0;' \
	'}' > $$SMOKE_SRC; \
	"$(NVCC)" $$SMOKE_SRC -Xlinker -rpath -Xlinker "$(CUDA_LIB_DIR)" -L"$(CUDA_LIB_DIR)" -lcudart -o $$SMOKE_BIN; \
	$$SMOKE_BIN

cuda-doctor:
	@set -e; \
	echo "[doctor] NVCC: $(NVCC)"; \
	"$(NVCC)" --version | tail -n 3 || true; \
	echo "[doctor] CUDA_LIB_DIR: $(CUDA_LIB_DIR)"; \
	if command -v nvidia-smi >/dev/null 2>&1; then \
		nvidia-smi --query-gpu=name,driver_version,compute_cap --format=csv,noheader; \
	else \
		echo "[doctor][warn] nvidia-smi not found"; \
	fi; \
	if $(MAKE) --no-print-directory cuda-smoke >/tmp/bds_cuda_doctor_smoke.log 2>&1; then \
		echo "[doctor] CUDA smoke: PASS"; \
		cat /tmp/bds_cuda_doctor_smoke.log; \
		exit 0; \
	fi; \
	echo "[doctor][error] CUDA smoke: FAIL"; \
	cat /tmp/bds_cuda_doctor_smoke.log; \
	echo "[doctor] lsmod (nvidia):"; \
	lsmod | grep -E '^nvidia_uvm|^nvidia ' || true; \
	echo "[doctor] /dev/nvidia*:"; \
	ls -l /dev/nvidia* 2>/dev/null || true; \
	echo "[doctor][next] run: make cuda-heal"; \
	exit 2

cuda-heal:
	@set -e; \
	echo "[heal] Installing required package: nvidia-modprobe"; \
	sudo apt update; \
	sudo apt install -y nvidia-modprobe; \
	echo "[heal] Recreating NVIDIA UVM device nodes"; \
	sudo nvidia-modprobe -u -c=0; \
	echo "[heal] Reloading nvidia_uvm kernel module"; \
	if ! sudo modprobe -r nvidia_uvm; then \
		echo "[heal][error] Failed to unload nvidia_uvm (module may be in use)."; \
		echo "[heal][hint] Close CUDA apps, then rerun: make cuda-heal"; \
		exit 3; \
	fi; \
	sudo modprobe nvidia_uvm; \
	echo "[heal] Verifying by running doctor..."; \
	$(MAKE) --no-print-directory cuda-doctor

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CUDA_MULTI_OBJ): src/compute/cuda/bdssim.cu
	@mkdir -p $(dir $@)
	@if [ -z "$(GENCODE_FAT)" ]; then echo "[make] no usable gencode (SASS/PTX) found in current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_FAT) -c $< -o $@

$(GNSS_LAUNCHER): Makefile $(FAT_BIN) $(SUPPORTED_GPU_BINS) $(LAUNCHER_TEMPLATE) $(REBUILD_SCRIPT)
	@sed \
		-e 's|@BIN_DIR@|$(BIN_DIR)|g' \
		-e 's|@REBUILD_SCRIPT@|$(REBUILD_SCRIPT)|g' \
		$(LAUNCHER_TEMPLATE) > $(GNSS_LAUNCHER)
	@chmod +x $(GNSS_LAUNCHER)

$(SUPPORT_CHECK_SCRIPT):
	@chmod +x $(SUPPORT_CHECK_SCRIPT)


$(BIN_DIR):
	mkdir -p $@

$(FAT_BIN): $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/gnss-sim-pascal: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/gnss-sim-turing: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/gnss-sim-ampere: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/gnss-sim-ada: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/gnss-sim-blackwell: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/gnss-sim-modern: $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(WIFI_RID_BRIDGE): src/bridges/wifi/wifi_rid_bridge.cpp | $(BIN_DIR)
	$(CXX) -O2 -Wall -Wextra -std=c++14 -o $@ $<

$(WIFI_RID_TSHARK_BRIDGE): src/bridges/wifi/wifi_rid_tshark_bridge.cpp | $(BIN_DIR)
	$(CXX) -O2 -Wall -Wextra -std=c++14 -o $@ $<

$(BLE_RID_BRIDGE): src/bridges/ble/ble_rid_bridge.cpp | $(BIN_DIR)
	$(CXX) -O2 -Wall -Wextra -std=c++14 -o $@ $< -lbluetooth

$(GNSS_SPLASH_BIN): src/apps/tools/gnss_loading_splash.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(QT_LIBS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	rm -f gnss-sim gnss-sim-fat gnss-sim-pascal gnss-sim-turing gnss-sim-ampere gnss-sim-ada gnss-sim-blackwell gnss-sim-modern
	rm -f gnss-loading-splash

run-wifi-auto: $(GNSS_LAUNCHER)
	@bash scripts/wifi/run_gnss_sim_wifi_auto.sh

package-deb: all
	@bash scripts/package/build-deb.sh

package-appimage: all
	@bash scripts/package/build-appimage.sh

install-desktop-icon: all
	@bash scripts/install/install-desktop-icon.sh

-include $(DEPS)