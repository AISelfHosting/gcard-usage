#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mongoose.h"
#include "index_html.h"

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
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        // --- Serve the embedded HTML at "/" ---
        if (mg_vcmp(&hm->uri, "/") == 0 || mg_vcmp(&hm->uri, "/index.html") == 0) {
            mg_http_reply(c, 200,
                "Content-Type: text/html\r\n"
                "Content-Length: %zu\r\n",
                index_html_len,
                index_html, index_html_len);
            return;
        }

        // --- Serve GPU stats as JSON at "/stats" ---
        if (mg_vcmp(&hm->uri, "/stats") == 0) {
            unsigned int gpu_util = 0;
            unsigned int vram_util = 0;

            get_gpu_stats(&gpu_util, &vram_util);

            mg_http_reply(c, 200,
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n",
                "{\"gpu_percent\": %u, \"vram_percent\": %u}",
                gpu_util, vram_util);
            return;
        }

        // --- 404 for everything else ---
        mg_http_reply(c, 404, "Content-Type: text/plain\r\n", "Not Found");
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
