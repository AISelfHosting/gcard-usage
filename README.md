# gcard-usage

A lightweight C-based GPU monitor that serves a real-time web dashboard showing compute utilization and VRAM usage. Supports multi-GPU setups, live line charts (Chart.js), and works with both NVIDIA and AMD GPUs.

## Features

- **Multi-GPU support** — auto-detects all GPUs and renders a card per GPU
- **Live charts** — 60-second rolling line chart for compute and VRAM per GPU
- **Dual architecture** — compile for NVIDIA (NVML) or AMD (ROCm SMI)
- **Reverse-proxy friendly** — relative URLs on the frontend; optional `BASE_PATH` define on the backend
- **Zero-dependency frontend** — single HTML file embedded into the C binary (only external dependency is Chart.js via CDN)

## Building

### Prerequisites

- **NVIDIA**: NVIDIA drivers + NVML development headers (`libnvidia-ml-dev`)
- **AMD**: ROCm installed (typically at `/opt/rocm`)
- GCC, Make

### Compile

```bash
# For NVIDIA GPUs
make cuda

# For AMD GPUs
make rocm

# Debug build (with symbols, no optimization)
make cuda DEBUG=1

# Clean build artifacts
make clean
```

### Reverse-proxy base path

If your server sits behind a proxy that adds a URL prefix (e.g. `/gpu-monitor`), pass `BASE_PATH` as a Make variable:

```bash
make cuda BASE_PATH=/gpu-monitor
```

Both options can be combined:

```bash
make cuda BASE_PATH=/gpu-monitor DEBUG=1
```

## Running

```bash
./gcard_usage
```

The monitor starts on port **8000** by default:

- **Dashboard**: `http://localhost:8000/`
- **JSON API**: `http://localhost:8000/stats` — returns an array like:
  ```json
  [
    {"id": 0, "gpu_percent": 45, "vram_percent": 72},
    {"id": 1, "gpu_percent": 12, "vram_percent": 30}
  ]
  ```

## Project structure

| File | Description |
|---|---|
| `main.c` | C backend — GPU polling + HTTP server (mongoose) |
| `index.html` | Frontend dashboard (embedded at compile time) |
| `embed_html.sh` | Converts `index.html` → `index_html.h` for embedding |
| `Makefile` | Build system (cuda / rocm targets) |
| `mongoose.[ch]` | Lightweight HTTP server library |

