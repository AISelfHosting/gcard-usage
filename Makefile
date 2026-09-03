# Compiler settings
CC = gcc
CFLAGS = -Wall -O2
SOURCES = main.c mongoose.c
OUT = gcard_usage

# Optional: pass BASE_PATH for reverse-proxy setups (e.g. make rocm BASE_PATH=/gpu-monitor)
BASE_PATH ?=
ifeq ($(strip $(BASE_PATH)),)
  CFLAGS +=
else
  CFLAGS += -DBASE_PATH=\"$(BASE_PATH)\"
endif

# Optional: set DEBUG=1 for a debug build (e.g. make rocm DEBUG=1)
DEBUG ?= 0
ifeq ($(DEBUG),1)
  CFLAGS += -ggdb -Og -DDEBUG
endif

# Make sure these targets aren't confused with actual files
.PHONY: default cuda rocm clean build embed

# Default target complains if the user just types "make"
default:
	@echo "==============================================="
	@echo "ERROR: You must specify a target GPU platform."
	@echo "Usage:"
	@echo "  make cuda    (Compiles for NVIDIA NVML)"
	@echo "  make rocm    (Compiles for AMD ROCm SMI)"
	@echo "==============================================="
	@exit 1

# Embed index.html into index_html.h (runs before build)
embed:
	bash embed_html.sh

# Target for NVIDIA Cards
cuda: CFLAGS += -DUSE_NVML
cuda: LDFLAGS += -lnvidia-ml
cuda: embed build

# Target for AMD Cards
# Assumes ROCm is installed in the default /opt/rocm directory
rocm: CFLAGS += -DUSE_ROCM -I/opt/rocm/include
rocm: LDFLAGS += -L/opt/rocm/lib -lrocm_smi64
rocm: embed build

# Shared compilation step
build:
	$(CC) $(CFLAGS) -o $(OUT) $(SOURCES) $(LDFLAGS)
	@echo "Build successful! Run with: ./$(OUT)"

# Clean up
clean:
	rm -f $(OUT) index_html.h
