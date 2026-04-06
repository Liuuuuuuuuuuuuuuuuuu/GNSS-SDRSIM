CC = gcc
CXX = g++
NVCC ?= $(shell command -v nvcc 2>/dev/null || echo /usr/local/cuda/bin/nvcc)
CCACHE ?= $(shell command -v ccache 2>/dev/null)
.DEFAULT_GOAL := all
BIN_DIR ?= bin
OBJ_DIR ?= build
CUDA_OBJ_DIR ?= $(OBJ_DIR)/cuda
AUTO_PORTABLE ?= 0
JOBS ?= $(shell nproc)
PORTABLE_NAME ?= GNSS-SDRSIM
VERSION_MODE ?= manual
PORTABLE_VERSION ?= 1.0
DATE_VERSION_FORMAT ?= +%Y.%m.%d
AUTO_PATCH_VERSION ?= $(shell \
	latest_tag=$$(git tag --list 2>/dev/null | sed -n 's/^v\{0,1\}\([0-9]\+\.[0-9]\+\.[0-9]\+\)$$/\1/p' | sort -V | tail -n1); \
	if [ -z "$$latest_tag" ]; then \
		echo 0.0.1; \
	else \
		IFS=. read -r major minor patch <<EOF; \
$$latest_tag \
EOF \
		echo "$$major.$$minor.$$((patch + 1))"; \
	fi)
DATE_VERSION ?= $(shell date '$(DATE_VERSION_FORMAT)')

ifeq ($(VERSION_MODE),auto-patch)
PORTABLE_VERSION_EFFECTIVE := $(AUTO_PATCH_VERSION)
else ifeq ($(VERSION_MODE),date)
PORTABLE_VERSION_EFFECTIVE := $(DATE_VERSION)
else
PORTABLE_VERSION_EFFECTIVE := $(PORTABLE_VERSION)
endif

PORTABLE_RELEASE_DIR ?= dist/$(PORTABLE_NAME)-$(PORTABLE_VERSION_EFFECTIVE)-portable
PORTABLE_SELF_CHECK_SCRIPT ?= scripts/portable-self-check.sh

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

CUDA_PATH ?= $(if $(filter /usr/local/cuda/%,$(NVCC)),$(patsubst %/bin/nvcc,%,$(NVCC)),/usr/local/cuda)
LIBS = -L$(CUDA_PATH)/lib64 -lm -luhd -lboost_system -lcudart -pthread $(QT_LIBS)

include cuda/cuda_targets.mk

GENCODE_ALL = $(strip \
	$(if $(HAS_SM_61),$(GENCODE_PASCAL),) \
	$(if $(HAS_SM_75),$(GENCODE_TURING),) \
	$(if $(HAS_SM_86),$(GENCODE_AMPERE),) \
	$(if $(HAS_SM_89),$(GENCODE_ADA),) \
	$(GENCODE_BLACKWELL))
GENCODE_FAT = $(strip $(GENCODE_ALL) $(PTX_FALLBACK))
CUDA_MULTI_OBJ = $(CUDA_OBJ_DIR)/bdssim_multi.o
FAT_BIN = $(BIN_DIR)/bds-sim-fat
LAUNCHER_TEMPLATE = scripts/bds-sim-launcher.sh
REBUILD_SCRIPT = scripts/rebuild-local.sh
SUPPORT_CHECK_SCRIPT = scripts/check-gpu-series-support.sh
PORTABLE_BUILD_SCRIPT = scripts/build-portable.sh

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
ifneq ($(filter 1 true yes on,$(AUTO_PORTABLE)),)
all: portable
endif

release:
	@$(MAKE) -j$(JOBS) AUTO_PORTABLE=1 all
all-gpu-binaries: $(FAT_BIN) $(SUPPORTED_GPU_BINS)
check-gpu-series-support: $(SUPPORT_CHECK_SCRIPT)
	@bash $(SUPPORT_CHECK_SCRIPT)
portable: bds-sim $(PORTABLE_BUILD_SCRIPT)
	@PORTABLE_NAME='$(PORTABLE_NAME)' PORTABLE_VERSION='$(PORTABLE_VERSION_EFFECTIVE)' PORTABLE_RELEASE_DIR='$(PORTABLE_RELEASE_DIR)' bash $(PORTABLE_BUILD_SCRIPT)
portable-self-check: $(PORTABLE_SELF_CHECK_SCRIPT)
	@bash $(PORTABLE_SELF_CHECK_SCRIPT)
print-gencode-fat:
	@echo $(GENCODE_FAT)
print-portable-version:
	@echo $(PORTABLE_VERSION_EFFECTIVE)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CUDA_MULTI_OBJ): bdssim.cu
	@mkdir -p $(dir $@)
	@if [ -z "$(GENCODE_FAT)" ]; then echo "[make] no usable gencode (SASS/PTX) found in current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_FAT) -c $< -o $@

bds-sim: Makefile $(FAT_BIN) $(SUPPORTED_GPU_BINS) $(LAUNCHER_TEMPLATE) $(REBUILD_SCRIPT)
	@sed \
		-e 's|@BIN_DIR@|$(BIN_DIR)|g' \
		-e 's|@REBUILD_SCRIPT@|$(REBUILD_SCRIPT)|g' \
		$(LAUNCHER_TEMPLATE) > $@
	@chmod +x $@

$(SUPPORT_CHECK_SCRIPT):
	@chmod +x $(SUPPORT_CHECK_SCRIPT)


$(BIN_DIR):
	mkdir -p $@

$(FAT_BIN): $(COMMON_OBJS) $(CUDA_MULTI_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

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
	rm -f bds-sim bds-sim-fat bds-sim-pascal bds-sim-turing bds-sim-ampere bds-sim-ada bds-sim-blackwell bds-sim-modern

-include $(DEPS)