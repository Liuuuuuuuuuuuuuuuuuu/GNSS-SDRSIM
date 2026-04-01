CC = gcc
CXX = g++
NVCC = nvcc

# 自動偵測 Qt 參數
QT_MODULES = Qt6Widgets Qt6Gui Qt6Core Qt6Network
QT_CFLAGS := $(shell pkg-config --cflags $(QT_MODULES) 2>/dev/null)
QT_LIBS := $(shell pkg-config --libs $(QT_MODULES) 2>/dev/null)

ifeq ($(strip $(QT_LIBS)),)
QT_MODULES = Qt5Widgets Qt5Gui Qt5Core Qt5Network
QT_CFLAGS := $(shell pkg-config --cflags $(QT_MODULES) 2>/dev/null)
QT_LIBS := $(shell pkg-config --libs $(QT_MODULES) 2>/dev/null)
endif

# 統一編譯參數，這是確保 path.c 和 main.c 能對接的關鍵
CFLAGS = -I. -O3 -march=native -ffast-math -Wall -Wextra -fopenmp -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -fPIC
CXXFLAGS = $(CFLAGS) -std=c++14 $(QT_CFLAGS)

# 保留對 20, 30, 40 系列的支援，並讓 50 系列穩定相容執行 40 系列代碼 (sm_89)
# 移除 sm_120 以避開 WSL2 JIT 編譯器崩潰 Bug
NVCC_GENCODE = \
  -gencode arch=compute_75,code=sm_75 \
  -gencode arch=compute_86,code=sm_86 \
  -gencode arch=compute_89,code=sm_89 \
  -gencode arch=compute_89,code=compute_89

NVCCFLAGS = -O3 $(NVCC_GENCODE) -Xcompiler "-O3 -fopenmp -fPIC" -I.

CUDA_PATH ?= /usr/local/cuda

# 嚴格定義物件檔清單，確保 path.o (移動邏輯) 緊跟在 main.o 之後
OBJS = main.o globals.o bch.o navbits.o channel.o bdssim.o rinex.o orbits.o coord.o path.o iono.o usrp_wrapper.o map_gui.o \
       gui/geo_io.o gui/path_builder.o gui/signal_snapshot.o gui/control_layout.o gui/control_logic.o \
       gui/control_paint.o gui/quad_panel_layout.o gui/osm_projection.o gui/map_render_utils.o gui/dji_nfz.o

LIBS = -L$(CUDA_PATH)/lib64 -lm -luhd -lboost_system -lcudart -pthread $(QT_LIBS)

all: bds-sim

# 核心：bdssim 的編譯
bdssim.o: bdssim.cu
	$(NVCC) $(NVCCFLAGS) -c bdssim.cu -o bdssim.o

# 關鍵：子目錄 gui/ 的編譯規則，確保與主程式使用相同的 CXXFLAGS
gui/%.o: gui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 根目錄的編譯規則
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 最終連結
bds-sim: $(OBJS)
	$(CXX) $(CXXFLAGS) -o bds-sim $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) bds-sim