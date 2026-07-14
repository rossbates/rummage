# Makefile for rummage - GPU Nostr Key Search

SRCDIR = src

SRC = $(SRCDIR)/rummage.cpp \
      $(SRCDIR)/CPU/Point.cpp \
      $(SRCDIR)/CPU/Int.cpp \
      $(SRCDIR)/CPU/IntMod.cpp \
      $(SRCDIR)/CPU/SECP256K1.cpp

OBJDIR = obj

OBJET = $(addprefix $(OBJDIR)/, \
		GPU/GPURummage.o \
		CPU/Point.o \
		CPU/Int.o \
		CPU/IntMod.o \
		CPU/SECP256K1.o \
        rummage.o \
)

CCAP     ?= 86
CUDA     ?= /usr/local/cuda
CXX       = g++
CXXCUDA   = /usr/bin/g++

# GPU Performance Configuration
# Adjust these based on your GPU (see docs/PERFORMANCE.md)
NOSTR_BLOCKS_PER_GRID   = 512
NOSTR_THREADS_PER_BLOCK = 256
KEYS_PER_THREAD_BATCH   = 64

CXXFLAGS  = -DWITHGPU -m64 -mssse3 -Wno-write-strings -O2 -I$(SRCDIR) -I$(CUDA)/include \
            -DNOSTR_BLOCKS_PER_GRID=$(NOSTR_BLOCKS_PER_GRID) \
            -DNOSTR_THREADS_PER_BLOCK=$(NOSTR_THREADS_PER_BLOCK) \
            -DKEYS_PER_THREAD_BATCH=$(KEYS_PER_THREAD_BATCH)
LFLAGS    = /usr/lib/x86_64-linux-gnu/libgmp.so.10 -lpthread -L$(CUDA)/lib64 -lcudart -lcurand
NVCC      = $(CUDA)/bin/nvcc

#--------------------------------------------------------------------

all: rummage

$(OBJDIR)/GPU/GPURummage.o: $(SRCDIR)/GPU/GPURummage.cu
	$(NVCC) -allow-unsupported-compiler --compile --compiler-options -fPIC -Xcompiler -U_GNU_SOURCE -ccbin $(CXXCUDA) -m64 -O2 -I$(SRCDIR) -I$(CUDA)/include \
	-DNOSTR_BLOCKS_PER_GRID=$(NOSTR_BLOCKS_PER_GRID) \
	-DNOSTR_THREADS_PER_BLOCK=$(NOSTR_THREADS_PER_BLOCK) \
	-DKEYS_PER_THREAD_BATCH=$(KEYS_PER_THREAD_BATCH) \
	-gencode=arch=compute_$(CCAP),code=sm_$(CCAP) -o $(OBJDIR)/GPU/GPURummage.o -c $(SRCDIR)/GPU/GPURummage.cu

$(OBJDIR)/%.o : $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

$(OBJDIR)/CPU/%.o : $(SRCDIR)/CPU/%.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

rummage: $(OBJET)
	@echo Making rummage...
	$(CXX) $(OBJET) $(LFLAGS) -o rummage

$(OBJET): | $(OBJDIR) $(OBJDIR)/GPU $(OBJDIR)/CPU

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/GPU: $(OBJDIR)
	cd $(OBJDIR) &&	mkdir -p GPU

$(OBJDIR)/CPU: $(OBJDIR)
	cd $(OBJDIR) &&	mkdir -p CPU

clean:
	@echo Cleaning...
	@rm -rf obj || true
	@rm -f rummage || true

.PHONY: all clean
