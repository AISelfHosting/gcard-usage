#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "mongoose.h"
#include "index_html.h"

// Enforce that one of the architectures is selected
#if !defined(USE_NVML) && !defined(USE_ROCM)
    #error "You must define either USE_NVML or USE_ROCM at compile time."
#endif

// --- GLOBALS FOR CACHING ---
#define MAX_GPUS 16
typedef struct {
    unsigned int gpu_percent;
    unsigned int vram_percent;
} gpu_stat_t;

gpu_stat_t cached_stats[MAX_GPUS];
unsigned int global_gpu_count = 0;

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

unsigned int get_gpu_count() {
    unsigned int count = 0;
    if (nvmlDeviceGetCount(&count) == NVML_SUCCESS) {
        return count;
    }
    return 0;
}

void get_gpu_stats(unsigned int index, unsigned int *gpu_util, unsigned int *vram_util) {
    nvmlDevice_t device;
    if (nvmlDeviceGetHandleByIndex(index, &device) == NVML_SUCCESS) {
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
    if (rsmi_init(0) != RSMI_STATUS_SUCCESS) {
        printf("Failed to initialize ROCm SMI. Are AMD drivers installed?\n");
    }
}

void gpu_shutdown() {
    rsmi_shut_down();
}

unsigned int get_gpu_count() {
    uint32_t count = 0;
    if (rsmi_num_monitor_devices(&count) == RSMI_STATUS_SUCCESS) {
        return count;
    }
    return 0;
}

void get_gpu_stats(unsigned int index, unsigned int *gpu_util, unsigned int *vram_util) {
    uint32_t busy_percent = 0;
    uint64_t mem_used = 0, mem_total = 0;

    if (rsmi_dev_busy_percent_get(index, &busy_percent) == RSMI_STATUS_SUCCESS) {
        *gpu_util = busy_percent;
    }
    if (rsmi_dev_memory_usage_get(index, RSMI_MEM_TYPE_VRAM, &mem_used) == RSMI_STATUS_SUCCESS &&
        rsmi_dev_memory_total_get(index, RSMI_MEM_TYPE_VRAM, &mem_total) == RSMI_STATUS_SUCCESS) {
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

#ifdef DEBUG
        printf("[DEBUG] HTTP %.*s %.*s\n",
               (int)hm->method.len, hm->method.ptr,
               (int)hm->uri.len, hm->uri.ptr);
#endif

        // --- Serve the embedded HTML at "/" ---
        if (mg_vcmp(&hm->uri, "/") == 0 || mg_vcmp(&hm->uri, "/index.html") == 0) {
#ifdef DEBUG
            printf("[DEBUG]   -> 200 (HTML)\n");
#endif
            {
                char headers[128];
                snprintf(headers, sizeof(headers),
                    "Content-Type: text/html\r\nContent-Length: %zu\r\n",
                    index_html_len);
                mg_http_reply(c, 200, headers, "%.*s", (int)index_html_len, index_html);
            }
            return;
        }

        // --- Serve GPU stats as JSON array at "/stats" ---
        if (mg_vcmp(&hm->uri, "/stats") == 0) {
            // Buffer to hold our dynamic JSON array
            char json_response[4096];
            strcpy(json_response, "[");

            for (unsigned int i = 0; i < global_gpu_count; i++) {
                // Note: We are pulling from the safe CACHE now, not querying the GPU directly!
                char gpu_json[128];
                snprintf(gpu_json, sizeof(gpu_json),
                         "{\"id\": %u, \"gpu_percent\": %u, \"vram_percent\": %u}%s",
                         i, cached_stats[i].gpu_percent, cached_stats[i].vram_percent,
                         (i == global_gpu_count - 1) ? "" : ","); // Add comma if not the last item

                // Append to main JSON string safely
                strncat(json_response, gpu_json, sizeof(json_response) - strlen(json_response) - 1);
            }

            strncat(json_response, "]", sizeof(json_response) - strlen(json_response) - 1);

#ifdef DEBUG
            printf("[DEBUG]   -> 200 %s\n", json_response);
#endif

            mg_http_reply(c, 200,
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n",
                "%s", json_response);
            return;
        }

        // --- 404 for everything else ---
#ifdef DEBUG
        printf("[DEBUG]   -> 404 Not Found\n");
#endif
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

    global_gpu_count = get_gpu_count();
    if (global_gpu_count > MAX_GPUS) global_gpu_count = MAX_GPUS;

    printf("Detected %u GPU(s).\n", global_gpu_count);

    // Wait 10 seconds before first GPU read to let other processes settle
    printf("Waiting 10 seconds before first GPU read...\n");
    sleep(10);

    time_t last_query = 0;

    // Infinite loop keeping the server alive
    for (;;) {
        time_t now = time(NULL);

        // Query GPUs once per second to prevent driver deadlocks
        if (now != last_query) {
#ifdef DEBUG
            printf("[GPU] Reading stats at %lu\n", (unsigned long)now);
#endif
            for (unsigned int i = 0; i < global_gpu_count; i++) {
                get_gpu_stats(i, &cached_stats[i].gpu_percent, &cached_stats[i].vram_percent);
            }
            last_query = now;
        }

        // Check for network requests (Yields CPU for 200ms)
        mg_mgr_poll(&mgr, 200);
    }

    mg_mgr_free(&mgr);
    gpu_shutdown();
    return 0;
}
