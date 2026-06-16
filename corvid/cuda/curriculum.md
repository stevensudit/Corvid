Here is a structured CUDA curriculum, ordered so that each layer depends on the previous one. It is written for your current setup: CUDA 13.3 under WSL Ubuntu, RTX 4090 / `sm_89`, and explicit CUDA host compiler selection.

## Track 0 — Environment and build model

Goal: make CUDA builds boring.

Topics:

```text
nvcc vs host compiler
CUDA Toolkit vs driver
.cu files
host compiler selection: -ccbin /usr/bin/g++-13 or g++-15
CMake CUDA language support
target architecture: sm_89 for RTX 4090
Debug vs Release
compute-sanitizer
```

Exercises:

```bash
nvcc -ccbin /usr/bin/g++-13 -std=c++20 -O2 -arch=sm_89 hello.cu -o hello
./hello
```

Then the same through CMake:

```cmake
project(cuda_test LANGUAGES CXX CUDA)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

add_executable(cuda_test hello.cu)
set_property(TARGET cuda_test PROPERTY CUDA_ARCHITECTURES 89)
```

Milestone: you can compile, run, and diagnose a trivial kernel from VSCode/WSL without thinking about PATH, driver mismatch, or GCC mismatch.

---

## Track 1 — CUDA’s execution model

Goal: understand what code runs where.

Core concepts:

```text
host code: runs on CPU
device code: runs on GPU
kernel: GPU entry point launched by host
thread: logical CUDA execution context
warp: 32 threads
block / CTA: group of threads scheduled onto one SM
grid: collection of blocks launched by one kernel
SM: hardware execution unit
```

NVIDIA’s Programming Guide summarizes the hierarchy as threads organized into blocks, and blocks organized into a grid. It also states that grids and blocks may be 1D, 2D, or 3D, and that `gridDim`, `blockDim`, and `blockIdx` expose this inside the kernel. ([NVIDIA Docs][1])

First mental model:

```cpp
kernel<<<num_blocks, threads_per_block>>>(args...);
```

The launch configuration creates:

```text
num_blocks blocks
threads_per_block threads per block
num_blocks * threads_per_block logical CUDA threads total
```

Exercise: write kernels that print:

```cpp
threadIdx.x
blockIdx.x
blockDim.x
gridDim.x
```

Milestone: given `kernel<<<4, 256>>>`, you can say exactly how many blocks, threads, and warps are involved.

---

## Track 2 — CUDA function qualifiers

Goal: understand `__global__`, `__device__`, and `__host__`.

### `__global__`

A `__global__` function is a **kernel entry point**. It is called from host code using the special launch syntax:

```cpp
__global__ void kernel(int* data) {
    // runs on GPU
}

int main() {
    kernel<<<blocks, threads>>>(data);
}
```

Important rules:

```text
__global__ functions are launched with <<<...>>>.
They execute on the device.
They are callable from host code.
They must return void.
The launch is asynchronous with respect to the host.
```

NVIDIA’s CUDA C++ language extension docs state that `__global__` functions require an execution configuration, must return `void`, and that calls to a `__global__` function are asynchronous: they return to the host thread before device execution completes. ([NVIDIA Docs][2])

### `__device__`

A `__device__` function runs on the GPU and is callable from other GPU code:

```cpp
__device__ int square(int x) {
    return x * x;
}

__global__ void kernel(int* out) {
    int i = threadIdx.x;
    out[i] = square(i);
}
```

The Programming Guide states that `__device__` indicates a function should be compiled for the GPU and callable from `__device__` or `__global__` functions. ([NVIDIA Docs][3])

### `__host__ __device__`

A function marked both ways is compiled for both CPU and GPU:

```cpp
__host__ __device__
int clamp_0_255(int x) {
    return x < 0 ? 0 : x > 255 ? 255 : x;
}
```

Use this for small pure functions that you want to share between CPU reference code and GPU kernels.

Exercises:

```text
1. Write a __global__ kernel that calls a __device__ helper.
2. Write a __host__ __device__ helper and call it from both CPU and GPU code.
3. Deliberately call a normal host-only function from a kernel and inspect the compiler error.
```

Milestone: you can predict whether a function is callable from host, device, or both.

---

## Track 3 — Built-in kernel variables

Goal: know the implicit variables available inside kernels.

Core built-ins:

```cpp
threadIdx  // dim3: thread index within the block
blockIdx   // dim3: block index within the grid
blockDim   // dim3: block dimensions
gridDim    // dim3: grid dimensions
warpSize   // usually 32
```

NVIDIA’s intro docs define `threadIdx`, `blockDim`, `blockIdx`, and `gridDim` as built-in variables available inside CUDA kernels. For example, `threadIdx` gives the thread’s index within its block, and `blockDim` gives the block dimensions specified by the launch configuration. ([NVIDIA Docs][4])

Canonical 1D global index:

```cpp
__global__ void kernel(float* out, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < n) {
        out[i] = 1.0f;
    }
}
```

Canonical launch size:

```cpp
int threads = 256;
int blocks = (n + threads - 1) / threads;

kernel<<<blocks, threads>>>(out, n);
```

Canonical 2D index:

```cpp
int x = blockIdx.x * blockDim.x + threadIdx.x;
int y = blockIdx.y * blockDim.y + threadIdx.y;
```

Exercises:

```text
1. Fill an array using one thread per element.
2. Fill a 2D image buffer using x/y indexing.
3. Handle non-multiple-of-block-size array lengths correctly.
```

Milestone: you can write correct 1D and 2D grid-stride indexing without looking it up.

---

## Track 4 — Memory model, first pass

Goal: know which memory exists and who can see it.

Memory spaces:

```text
registers:
  per-thread, fastest, compiler-managed

local memory:
  per-thread address space, physically backed by device memory, used for spills
  and some per-thread arrays

shared memory:
  per-block / CTA, explicitly declared with __shared__

global memory:
  device memory visible to all threads

constant memory:
  read-only from device code, written from host

texture/surface memory:
  specialized paths, mostly later
```

Block-level cooperation uses shared memory:

```cpp
__global__ void kernel(float* out) {
    __shared__ float scratch[256];

    int t = threadIdx.x;

    scratch[t] = static_cast<float>(t);

    __syncthreads();

    out[t] = scratch[255 - t];
}
```

NVIDIA describes threads within a block as cooperating by sharing data through shared memory and synchronizing with `__syncthreads()`. ([NVIDIA Docs][5])

Exercises:

```text
1. Reverse a 256-element array using shared memory.
2. Load a tile into shared memory, synchronize, then write it back.
3. Break the code by removing __syncthreads(), then restore it.
```

Milestone: you understand that `__shared__` is programmer-managed block-local memory, while L1 is hardware-managed cache.

---

## Track 5 — Synchronization and ordering

Goal: avoid invalid cross-thread assumptions.

Scopes:

```text
within a warp:
  __syncwarp()
  warp shuffle/vote intrinsics

within a block:
  __syncthreads()

within a grid:
  normally no in-kernel sync
  kernel boundary is the usual global synchronization point
  cooperative_groups grid sync only with special cooperative launches

between host and device:
  cudaDeviceSynchronize()
  cudaStreamSynchronize()
  events
```

NVIDIA’s async-barrier docs say that for full-block synchronization, `__syncthreads()` is the intended primitive, and for full-warp synchronization, `__syncwarp()` is the intended primitive. ([NVIDIA Docs][6])

Exercises:

```text
1. Use __syncthreads() correctly in a block reduction.
2. Use __syncwarp() in a warp-level reduction.
3. Demonstrate why blocks cannot safely synchronize with each other inside a normal kernel.
```

Milestone: you can identify whether an algorithm needs warp, block, grid, or host/device synchronization.

---

## Track 6 — Host/device memory management

Goal: move data deliberately.

Start with explicit allocation:

```cpp
float* d = nullptr;
cudaMalloc(&d, n * sizeof(float));
cudaMemcpy(d, h.data(), n * sizeof(float), cudaMemcpyHostToDevice);
kernel<<<blocks, threads>>>(d, n);
cudaMemcpy(h.data(), d, n * sizeof(float), cudaMemcpyDeviceToHost);
cudaFree(d);
```

Then learn:

```text
cudaMalloc / cudaFree
cudaMemcpy
cudaMemset
cudaGetLastError
cudaDeviceSynchronize
unified memory: cudaMallocManaged
pinned host memory
async copies
streams
```

NVIDIA’s Best Practices Guide organizes memory work around host/device transfers, pinned memory, asynchronous transfers, and overlapping transfers with computation. ([NVIDIA Docs][7])

Exercises:

```text
1. Vector add with cudaMalloc/cudaMemcpy.
2. Same with cudaMallocManaged.
3. Add error-checking wrappers.
4. Measure transfer time vs kernel time.
```

Milestone: you can explain where each pointer lives and whether it is legal to dereference on CPU, GPU, or both.

---

## Track 7 — Error handling and debugging

Goal: make CUDA failures visible immediately.

Error-checking macro:

```cpp
#define CUDA_CHECK(expr)                                                   \
    do {                                                                   \
        cudaError_t err = (expr);                                          \
        if (err != cudaSuccess) {                                          \
            std::fprintf(stderr, "%s failed: %s\n",                       \
                         #expr, cudaGetErrorString(err));                  \
            std::abort();                                                  \
        }                                                                  \
    } while (false)
```

Kernel launch checking:

```cpp
kernel<<<blocks, threads>>>(args...);
CUDA_CHECK(cudaGetLastError());
CUDA_CHECK(cudaDeviceSynchronize());
```

Tools:

```text
compute-sanitizer
cuda-gdb
Nsight Systems
Nsight Compute
```

Exercises:

```text
1. Introduce an out-of-bounds write and catch it with compute-sanitizer.
2. Introduce an illegal launch configuration.
3. Add file/line information to CUDA_CHECK.
```

Milestone: you never write a kernel launch without checking both launch failure and asynchronous execution failure.

---

## Track 8 — Performance basics

Goal: understand the first-order CUDA performance rules.

Topics:

```text
occupancy
warps and divergence
memory coalescing
shared-memory bank conflicts
arithmetic intensity
latency hiding
register pressure
block size selection
```

NVIDIA’s Best Practices Guide places coalesced global memory access, L2 behavior, and shared memory/bank conflicts in its device-memory performance section. ([NVIDIA Docs][7])

First rules:

```text
Prefer contiguous global memory access by neighboring threads.
Avoid divergent branches within a warp when practical.
Use shared memory to reuse data, not merely because it is "fast."
Measure before optimizing.
```

Exercises:

```text
1. Compare coalesced vs strided loads.
2. Compare naive matrix transpose vs tiled transpose.
3. Use Nsight Compute to inspect memory throughput and occupancy.
```

Milestone: you can explain why a kernel is memory-bound, compute-bound, or launch-overhead-bound.

---

## Track 9 — Common algorithm patterns

Goal: build a useful CUDA toolbox.

Patterns:

```text
map:
  one thread per element

stencil:
  neighbors around each element

reduction:
  many values to one or one per block

scan / prefix sum:
  cumulative operation

histogram:
  atomics, privatization, shared memory

sort / partition:
  usually use libraries first

gather/scatter:
  irregular access, harder to optimize
```

Exercises:

```text
1. Vector transform.
2. Dot product reduction.
3. Prefix sum, first block-local, then multi-kernel.
4. Histogram with global atomics, then shared-memory privatization.
```

Milestone: when facing a problem, you can classify it as map/reduce/stencil/scan/scatter/etc.

---

## Track 10 — Libraries before custom kernels

Goal: know when not to write CUDA kernels.

Libraries:

```text
Thrust
CUB
cuBLAS
cuFFT
cuDNN
cuSPARSE
CUDA Runtime API
CUDA Driver API
```

Use libraries for:

```text
sort
scan
reduce
BLAS
FFT
dense linear algebra
common primitives
```

Write custom kernels for:

```text
problem-specific fused operations
custom data layout
bespoke simulation logic
kernel fusion
irregular algorithms not well served by libraries
```

Milestone: you can decide whether to write a kernel or compose existing CUDA libraries.

---

## Track 11 — Streams and concurrency

Goal: overlap work.

Topics:

```text
default stream
explicit streams
cudaMemcpyAsync
pinned memory
events
overlap copy and compute
stream ordering
multi-buffering
```

Exercises:

```text
1. Use two streams to process two chunks independently.
2. Use events to measure elapsed GPU time.
3. Build a double-buffered host-to-device pipeline.
```

Milestone: you can distinguish CPU async launch behavior from actual GPU overlap.

---

## Track 12 — Advanced CUDA C++

Goal: write maintainable CUDA code without fighting the language.

Topics:

```text
templates in device code
lambdas
constexpr
__host__ __device__ utility functions
functors
RAII for device memory
separating .cpp and .cu code
compilation units
relocatable device code
```

Function-call rules become important here:

```cpp
__device__ int f(int);

__global__ void k() {
    f(123);       // OK: device function from kernel
}
```

But:

```cpp
int host_only(int);

__global__ void k() {
    host_only(123);   // not OK
}
```

Milestone: you can structure a CUDA project without putting everything into one `.cu` file.

---

## Track 13 — Architecture-specific knowledge

Goal: tune for actual hardware without overfitting too early.

For your RTX 4090, focus on:

```text
sm_89
warp size 32
block/shared-memory/register occupancy limits
L1/shared-memory behavior
L2 behavior
global memory bandwidth
tensor cores, later
```

Do not focus early on:

```text
thread-block clusters
distributed shared memory
Hopper-only or Blackwell-only mechanisms
```

NVIDIA’s compute capability table is the source for mapping GPUs to architectural feature levels; compute capability defines hardware features and supported instructions. ([NVIDIA Developer][8])

Milestone: you can read NVIDIA documentation and know whether a feature applies to your 4090.

---

## Track 14 — Cooperative groups and advanced synchronization

Goal: replace ad-hoc synchronization assumptions with explicit group abstractions.

Topics:

```text
cooperative_groups::thread_block
coalesced groups
tiled partition
grid sync with cooperative kernels
limitations of grid-wide sync
```

NVIDIA’s cooperative-groups docs show `cooperative_groups::sync()` as equivalent to `__syncthreads()` for a thread block, and also document grid synchronization for cooperative groups under restrictions. ([NVIDIA Docs][9])

Exercises:

```text
1. Rewrite a block reduction using cooperative_groups.
2. Use tiled_partition<32>.
3. Study cooperative launch restrictions; do not use casually.
```

Milestone: you understand why normal CUDA kernels usually synchronize globally by ending the kernel.

---

## Track 15 — Advanced memory movement

Goal: learn the mechanisms used by high-performance kernels.

Topics:

```text
asynchronous copy global -> shared
pipelines
double buffering
shared-memory tiling
cache hints
L2 persistence/access policy windows
```

NVIDIA’s Programming Guide describes asynchronous copy mechanisms and pipeline synchronization for staging work between global and shared memory. ([NVIDIA Docs][10])

Exercises:

```text
1. Tiled matrix multiply, naive shared-memory version.
2. Add double-buffering.
3. Compare global-load-only vs shared-tile versions.
```

Milestone: you can read optimized CUDA kernels without every line looking magical.

---

## Track 16 — Dynamic parallelism, clusters, and newer features

Goal: know what exists, not necessarily use it immediately.

Topics:

```text
dynamic parallelism: kernels launched from device code
thread-block clusters
distributed shared memory
cluster.sync()
architecture requirements
```

This should be late in the curriculum. For your current 4090, cluster/distributed shared memory is mostly conceptual unless you also target newer hardware.

Milestone: you can decide whether a newer CUDA feature applies to your target GPU and whether it is worth the complexity.

---

# Recommended lesson order

A practical sequence:

```text
1. Hello kernel, launch syntax, __global__
2. Built-in variables: threadIdx/blockIdx/blockDim/gridDim
3. Device functions: __device__, __host__ __device__
4. 1D vector add with cudaMalloc/cudaMemcpy
5. Error handling and compute-sanitizer
6. 2D indexing and image-like buffers
7. Shared memory and __syncthreads()
8. Warp basics and __syncwarp()
9. Reductions
10. Memory coalescing and profiling
11. Streams and async copies
12. CMake/VSCode project structure
13. Thrust/CUB/cuBLAS
14. Advanced shared-memory tiling
15. Cooperative groups
16. Architecture-specific tuning
```

For the first several lessons, every example should follow this skeleton:

```cpp
#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>

#define CUDA_CHECK(expr)                                                   \
    do {                                                                   \
        cudaError_t err = (expr);                                          \
        if (err != cudaSuccess) {                                          \
            std::fprintf(stderr, "%s failed: %s\n",                       \
                         #expr, cudaGetErrorString(err));                  \
            std::abort();                                                  \
        }                                                                  \
    } while (false)

__device__ int device_helper(int x) {
    return x * x;
}

__global__ void kernel(int* out, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < n) {
        out[i] = device_helper(i);
    }
}

int main() {
    constexpr int n = 1024;

    int* d_out = nullptr;
    CUDA_CHECK(cudaMalloc(&d_out, n * sizeof(int)));

    int threads = 256;
    int blocks = (n + threads - 1) / threads;

    kernel<<<blocks, threads>>>(d_out, n);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaFree(d_out));
}
```

This single example introduces:

```text
__global__
__device__
kernel launch syntax
threadIdx
blockIdx
blockDim
bounds checks
cudaMalloc
kernel error checking
cudaDeviceSynchronize
cudaFree
```

That is the right starting point. From there, the first real project should be a sequence of increasingly optimized vector/matrix kernels, not ML or graphics.

[1]: https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html?utm_source=chatgpt.com "2.2. Writing CUDA SIMT Kernels"
[2]: https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html?utm_source=chatgpt.com "5.4. C/C++ Language Extensions — CUDA Programming ..."
[3]: https://docs.nvidia.com/cuda/cuda-programming-guide/pdf/cuda-programming-guide.pdf?utm_source=chatgpt.com "cuda-programming-guide.pdf"
[4]: https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/intro-to-cuda-cpp.html?utm_source=chatgpt.com "2.1. Intro to CUDA C++ — CUDA Programming Guide"
[5]: https://docs.nvidia.com/cuda/archive/11.4.1/cuda-c-programming-guide/index.html?utm_source=chatgpt.com "Programming Guide :: CUDA Toolkit Documentation"
[6]: https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-barriers.html?utm_source=chatgpt.com "4.9. Asynchronous Barriers — CUDA Programming Guide"
[7]: https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/contents.html?utm_source=chatgpt.com "Contents — CUDA C++ Best Practices Guide 13.2 ..."
[8]: https://developer.nvidia.com/cuda/gpus?utm_source=chatgpt.com "CUDA GPU Compute Capability"
[9]: https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cooperative-groups.html?utm_source=chatgpt.com "4.4. Cooperative Groups — CUDA Programming Guide"
[10]: https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html?utm_source=chatgpt.com "4.11. Asynchronous Data Copies — CUDA Programming ..."
