CC = gcc
CXX = g++
NVCC = nvcc

QT_MODULES = Qt6Widgets Qt6Gui Qt6Core Qt6Network
QT_CFLAGS := $(shell pkg-config --cflags $(QT_MODULES) 2>/dev/null)
QT_LIBS := $(shell pkg-config --libs $(QT_MODULES) 2>/dev/null)

ifeq ($(strip $(QT_LIBS)),)
QT_MODULES = Qt5Widgets Qt5Gui Qt5Core Qt5Network
QT_CFLAGS := $(shell pkg-config --cflags $(QT_MODULES) 2>/dev/null)
QT_LIBS := $(shell pkg-config --libs $(QT_MODULES) 2>/dev/null)
endif

ifeq ($(strip $(QT_LIBS)),)
$(error Qt development package not found. Please install Qt5 or Qt6 Widgets with pkg-config files.)
endif

# C / C++ 的編譯參數 (保留你原本的最佳化)
CFLAGS = -I. -O3 -march=native -ffast-math -Wall -Wextra -fopenmp -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -fPIC
CXXFLAGS = $(CFLAGS) -std=c++14 $(QT_CFLAGS)

# CUDA 編譯參數：改為跨世代 fatbin + PTX，避免綁定單一卡型號
NVCC_GENCODE = \
	-gencode arch=compute_52,code=sm_52 \
	-gencode arch=compute_61,code=sm_61 \
	-gencode arch=compute_70,code=sm_70 \
	-gencode arch=compute_75,code=sm_75 \
	-gencode arch=compute_80,code=sm_80 \
	-gencode arch=compute_86,code=sm_86 \
	-gencode arch=compute_89,code=sm_89 \
	-gencode arch=compute_90,code=sm_90 \
	-gencode arch=compute_90,code=compute_90
NVCCFLAGS = -O3 $(NVCC_GENCODE) -Xcompiler "-O3 -fopenmp -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -fPIC" -I.

# 所有需要編譯的物件檔
OBJS = main.o globals.o bch.o navbits.o channel.o bdssim.o rinex.o orbits.o coord.o path.o iono.o usrp_wrapper.o map_gui.o gui/geo_io.o gui/path_builder.o gui/signal_snapshot.o gui/control_layout.o gui/control_logic.o gui/control_paint.o gui/quad_panel_layout.o gui/osm_projection.o gui/map_render_utils.o

# 最終的執行檔 (加入 -lcudart 來連結 NVIDIA CUDA 核心函式庫)
LIBS = -lm -luhd -lboost_system -lcudart -pthread $(QT_LIBS)

all: bds-sim

# 針對 .cu 的特殊編譯規則 (交給 NVCC)
bdssim.o: bdssim.cu
	$(NVCC) $(NVCCFLAGS) -c bdssim.cu -o bdssim.o

# 針對 .c 的預設規則 (交給 GCC)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 針對 .cpp 的預設規則 (交給 G++)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 連結階段
bds-sim: $(OBJS)
	$(CXX) $(CXXFLAGS) -o bds-sim $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) bds-sim *.Identifier gui/*.Identifier