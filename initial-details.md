Here is the complete setup. We will use C preprocessor macros (`#if defined(...)`) to conditionally compile either the NVIDIA (NVML) or AMD (ROCm SMI) backend. 

We will also use a `Makefile` that forces you to specify either `make cuda` or `make rocm`, and errors out if you just type `make`.

### 1. The C Code (`main.c`)
This file now contains the logic for both GPUs. It checks for `USE_NVML` or `USE_ROCM` at compile time and includes the appropriate libraries and API calls.

```c
#include <stdio.h>
#include <stdint.h>
#include "mongoose.h"

// Enforce that one of the architectures is selected
#if !defined(USE_NVML) && !defined(USE_ROCM)
    #error "You must define either USE_NVML or USE_ROCM at compile time."
#endif

// ---------------- NVIDIA / CUDA BACKEND ----------------
#if defined(USE_NVML)
#include <nvml.h>

void gpu_init() {
    if (nvmlInit() != NVML_SUCCESS) {
        printf("Failed to initialize NVML. Are NVIDIA drivers installed?\n");
    }
}

void gpu_shutdown() {
    nvmlShutdown();
}

void get_gpu_stats(unsigned int *gpu_util, unsigned int *vram_util) {
    nvmlDevice_t device;
    if (nvmlDeviceGetHandleByIndex(0, &device) == NVML_SUCCESS) {
        nvmlUtilization_t util;
        if (nvmlDeviceGetUtilizationRates(device, &util) == NVML_SUCCESS) {
            *gpu_util = util.gpu;
        }

        nvmlMemory_t mem;
        if (nvmlDeviceGetMemoryInfo(device, &mem) == NVML_SUCCESS) {
            *vram_util = (unsigned int)((mem.used * 100.0) / mem.total);
        }
    }
}

// ---------------- AMD / ROCm BACKEND ----------------
#elif defined(USE_ROCM)
#include <rocm_smi/rocm_smi.h>

void gpu_init() {
    // Pass 0 for initialization flags
    if (rsmi_init(0) != RSMI_STATUS_SUCCESS) {
        printf("Failed to initialize ROCm SMI. Are AMD drivers installed?\n");
    }
}

void gpu_shutdown() {
    rsmi_shut_down();
}

void get_gpu_stats(unsigned int *gpu_util, unsigned int *vram_util) {
    uint32_t device_index = 0; 
    uint32_t busy_percent = 0;
    uint64_t mem_used = 0, mem_total = 0;

    // Get GPU Compute Utilization
    if (rsmi_dev_busy_percent_get(device_index, &busy_percent) == RSMI_STATUS_SUCCESS) {
        *gpu_util = busy_percent;
    }

    // Get VRAM Usage and Total
    if (rsmi_dev_memory_usage_get(device_index, RSMI_MEM_TYPE_VRAM, &mem_used) == RSMI_STATUS_SUCCESS &&
        rsmi_dev_memory_total_get(device_index, RSMI_MEM_TYPE_VRAM, &mem_total) == RSMI_STATUS_SUCCESS) {
        if (mem_total > 0) {
            *vram_util = (unsigned int)((mem_used * 100.0) / mem_total);
        }
    }
}
#endif

// ---------------- HTTP SERVER LOGIC ----------------

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    if (ev == MG_EV_HTTP_MSG) {
        unsigned int gpu_util = 0;
        unsigned int vram_util = 0;

        // Fetch stats using whichever backend was compiled
        get_gpu_stats(&gpu_util, &vram_util);

        // Respond with JSON
        mg_http_reply(c, 200, 
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n", 
            "{\"gpu_percent\": %u, \"vram_percent\": %u}", 
            gpu_util, vram_util);
    }
}

int main() {
    // Initialize the selected GPU framework
    gpu_init();

    // Initialize the HTTP Server
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://0.0.0.0:8000", fn, NULL);
    
    #if defined(USE_NVML)
        printf("NVIDIA GPU Monitor API running at http://localhost:8000\n");
    #elif defined(USE_ROCM)
        printf("AMD ROCm GPU Monitor API running at http://localhost:8000\n");
    #endif

    // Infinite loop keeping the server alive
    for (;;) {
        mg_mgr_poll(&mgr, 1000); 
    }

    mg_mgr_free(&mgr);
    gpu_shutdown();
    return 0;
}
```

### 2. The `Makefile`
Place this `Makefile` in the exact same directory as your C source files. Make sure the indented lines under the targets are using **actual Tabs**, not spaces (this is a strict rule in Makefiles).

```makefile
# Compiler settings
CC = gcc
CFLAGS = -Wall -O2
SOURCES = main.c mongoose.c
OUT = gpu_monitor

# Make sure these targets aren't confused with actual files
.PHONY: default cuda rocm clean build

# Default target complains if the user just types "make"
default:
	@echo "==============================================="
	@echo "ERROR: You must specify a target GPU platform."
	@echo "Usage:"
	@echo "  make cuda    (Compiles for NVIDIA NVML)"
	@echo "  make rocm    (Compiles for AMD ROCm SMI)"
	@echo "==============================================="
	@exit 1

# Target for NVIDIA Cards
cuda: CFLAGS += -DUSE_NVML
cuda: LDFLAGS += -lnvidia-ml
cuda: build

# Target for AMD Cards
# Assumes ROCm is installed in the default /opt/rocm directory
rocm: CFLAGS += -DUSE_ROCM -I/opt/rocm/include
rocm: LDFLAGS += -L/opt/rocm/lib -lrocm_smi64
rocm: build

# Shared compilation step
build:
	$(CC) $(CFLAGS) -o $(OUT) $(SOURCES) $(LDFLAGS)
	@echo "Build successful! Run with: ./$(OUT)"

# Clean up
clean:
	rm -f $(OUT)
```

### How to use it:

1. Ensure `main.c`, `mongoose.c`, `mongoose.h` and the `Makefile` are all in the same folder.
2. If you type `make`, it will catch the error:
   ```text
   $ make
   ===============================================
   ERROR: You must specify a target GPU platform.
   Usage:
     make cuda    (Compiles for NVIDIA NVML)
     make rocm    (Compiles for AMD ROCm SMI)
   ===============================================
   make: *** [Makefile:11: default] Error 1
   ```
3. If you have an NVIDIA card, compile using:
   ```bash
   make cuda
   ./gpu_monitor
   ```
4. If you have an AMD card, compile using:
   ```bash
   make rocm
   ./gpu_monitor
   ```

*(Note: If you build for `rocm`, it assumes your AMD ROCm toolkit is installed in its standard Linux path `/opt/rocm`. If your OS uses a different path, you just need to adjust the `-I` and `-L` flags in the `Makefile`).*

The `index.html` JavaScript provided below will work flawlessly with this updated setup without needing any modifications!


### 2. The HTML & JavaScript (Frontend)
Now that your C program is running and emitting JSON on `http://localhost:8000`, you can create a simple `index.html` file. 

The JavaScript will query the API every 1000 milliseconds (1 second) and feed the numbers into standard HTML `<progress>` bars.

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Inference GPU Monitor</title>
    <style>
        body { font-family: Arial, sans-serif; padding: 20px; background: #1e1e1e; color: white; }
        .stat-box { margin-bottom: 20px; }
        .label { font-size: 18px; font-weight: bold; }
        .number { font-size: 24px; color: #4ade80; float: right; }
        progress { width: 100%; height: 25px; margin-top: 10px; }
        
        /* Styling the progress bars */
        progress::-webkit-progress-bar { background-color: #333; border-radius: 5px; }
        progress::-webkit-progress-value { background-color: #4ade80; border-radius: 5px; transition: width 0.3s; }
    </style>
</head>
<body>

    <h2>Local GPU Inference Status</h2>

    <div class="stat-box">
        <span class="label">Compute Utilization</span>
        <span class="number" id="gpu-num">0%</span>
        <progress id="gpu-bar" value="0" max="100"></progress>
    </div>

    <div class="stat-box">
        <span class="label">VRAM Usage</span>
        <span class="number" id="vram-num">0%</span>
        <progress id="vram-bar" value="0" max="100"></progress>
    </div>

    <script>
        async function fetchGpuStats() {
            try {
                // Query the HTTP API exposed by your C program
                const response = await fetch('http://localhost:8000');
                const data = await response.json();
                
                // Update text numbers
                document.getElementById('gpu-num').innerText = data.gpu_percent + '%';
                document.getElementById('vram-num').innerText = data.vram_percent + '%';

                // Update percentage bar graphs
                document.getElementById('gpu-bar').value = data.gpu_percent;
                document.getElementById('vram-bar').value = data.vram_percent;

            } catch (error) {
                console.error("Could not reach the C backend API", error);
            }
        }

        // Poll the C API every 1 second
        setInterval(fetchGpuStats, 1000);
        
        // Fetch immediately on load
        fetchGpuStats();
    </script>
</body>
</html>
```

### Summary of how this works:
1. **Inference Loads:** When you run your inference workload (e.g., Llama.cpp, TensorRT, PyTorch), the GPU core wakes up.
2. **C Polling:** The C program bypasses bulky runtimes and talks directly to the physical GPU driver via the C API (`nvml.h`). 
3. **JS Rendering:** Your browser handles all the visual rendering, grabbing the payload from the socket via standard HTTP `fetch()`, keeping your backend monitoring tool incredibly lean. 

*(Note: If you are doing inference on an **AMD** card instead of NVIDIA, the concept is identical, but you would compile against `#include <rocm_smi/rocm_smi.h>` instead of `nvml.h`.)*
