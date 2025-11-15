# Makefile for rummage - GPU Nostr Key Search

SRCDIR = src
OBJDIR = obj

# Detect platform
UNAME_S := $(shell uname -s)

# Common source files
COMMON_SRC = $(SRCDIR)/rummage.cpp \
             $(SRCDIR)/CPU/Point.cpp \
             $(SRCDIR)/CPU/Int.cpp \
             $(SRCDIR)/CPU/IntMod.cpp \
             $(SRCDIR)/CPU/SECP256K1.cpp

COMMON_OBJ = $(addprefix $(OBJDIR)/, \
             CPU/Point.o \
             CPU/Int.o \
             CPU/IntMod.o \
             CPU/SECP256K1.o \
             rummage.o)

# Platform-specific configuration
ifeq ($(UNAME_S),Darwin)
    # macOS - Metal backend
    GPU_BACKEND = metal
    GPU_OBJ = $(OBJDIR)/GPU/metal/MetalGPUMiner.o
    GPU_SRC = $(SRCDIR)/GPU/metal/MetalGPUMiner.mm
    CXX = clang++
    GMP_PATH = $(shell brew --prefix gmp 2>/dev/null || echo "/usr/local")
    # Use --sysroot to avoid /usr/local/include pollution
    SDK_PATH = $(shell xcrun --show-sdk-path)
    CXXFLAGS = -DUSE_METAL -O2 -I$(SRCDIR) -std=c++17 -Wno-write-strings -isysroot $(SDK_PATH) -I$(GMP_PATH)/include
    LFLAGS = -L$(GMP_PATH)/lib -lgmp -lpthread -framework Metal -framework Foundation

    # Metal shader compilation (to be implemented in Phase 2)
    METAL_SHADER = $(SRCDIR)/GPU/metal/MetalKernels.metal
    METAL_LIB = default.metallib
else
    # Linux - CUDA backend
    GPU_BACKEND = cuda
    GPU_OBJ = $(OBJDIR)/GPU/cuda/CudaGPUMiner.o
    GPU_SRC = $(SRCDIR)/GPU/cuda/CudaGPUMiner.cu
    CCAP = 86
    CUDA = /usr/local/cuda-11.8
    CXX = g++
    CXXCUDA = /usr/bin/g++
    CXXFLAGS = -DUSE_CUDA -DWITHGPU -m64 -mssse3 -Wno-write-strings -O2 -I$(SRCDIR) -I$(CUDA)/include
    LFLAGS = /usr/lib/x86_64-linux-gnu/libgmp.so.10 -lpthread -L$(CUDA)/lib64 -lcudart -lcurand
    NVCC = $(CUDA)/bin/nvcc
endif

# All object files
OBJET = $(COMMON_OBJ) $(GPU_OBJ)

#--------------------------------------------------------------------

all: info rummage

info:
	@echo "Building for platform: $(UNAME_S)"
	@echo "GPU Backend: $(GPU_BACKEND)"
	@echo ""

# CUDA compilation rule
ifeq ($(GPU_BACKEND),cuda)
$(OBJDIR)/GPU/cuda/CudaGPUMiner.o: $(SRCDIR)/GPU/cuda/CudaGPUMiner.cu
	@mkdir -p $(OBJDIR)/GPU/cuda
	$(NVCC) -allow-unsupported-compiler --compile --compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -O2 -I$(SRCDIR) -I$(CUDA)/include -gencode=arch=compute_$(CCAP),code=sm_$(CCAP) -o $@ -c $<
endif

# Metal compilation rule
ifeq ($(GPU_BACKEND),metal)
# Compile Metal shaders
$(METAL_LIB): $(METAL_SHADER)
	@echo "Compiling Metal shaders..."
	@mkdir -p $(dir $(METAL_LIB))
	xcrun -sdk macosx metal -c $(METAL_SHADER) -o MetalKernels.air
	xcrun -sdk macosx metallib MetalKernels.air -o $(METAL_LIB)
	@rm -f MetalKernels.air
	@echo "Metal shaders compiled successfully"

# Compile Objective-C++ implementation (depends on Metal library)
$(OBJDIR)/GPU/metal/MetalGPUMiner.o: $(SRCDIR)/GPU/metal/MetalGPUMiner.mm $(METAL_LIB)
	@mkdir -p $(OBJDIR)/GPU/metal
	$(CXX) $(CXXFLAGS) -o $@ -c $<
endif

# Common C++ compilation rules
$(OBJDIR)/%.o : $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

$(OBJDIR)/CPU/%.o : $(SRCDIR)/CPU/%.cpp
	@mkdir -p $(OBJDIR)/CPU
	$(CXX) $(CXXFLAGS) -o $@ -c $<

# Link
rummage: $(OBJET)
	@echo Making rummage...
	$(CXX) $(OBJET) $(LFLAGS) -o rummage

# Create directories
$(OBJET): | $(OBJDIR) $(OBJDIR)/GPU $(OBJDIR)/CPU

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/GPU: $(OBJDIR)
	mkdir -p $(OBJDIR)/GPU

$(OBJDIR)/CPU: $(OBJDIR)
	mkdir -p $(OBJDIR)/CPU

clean:
	@echo Cleaning...
	@rm -rf obj || true
	@rm -f rummage || true
	@rm -f $(METAL_LIB) MetalKernels.air || true

.PHONY: all clean info
