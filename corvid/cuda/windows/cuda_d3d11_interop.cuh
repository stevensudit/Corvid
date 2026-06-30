// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

// d3d11.h pulls windows.h's min/max macros; keep them out.
#include <type_traits>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>

#include <utility>

#include <cuda_runtime.h>

#include "../../enums/bitmask_enum.h"
#include "../cuda_status.cuh"
#include "./hr_status.h"
#include "./com_ptr.h"

#include <cuda_d3d11_interop.h>

// CUDA-D3D11 interop: let a CUDA kernel write a Direct3D 11 texture in place,
// so the frame never crosses PCIe.

namespace corvid::cuda {

using namespace win32;

#pragma region cuda_interop_adapter

// Select the GPU that CUDA and D3D11 can share, make it CUDA's current device,
// and return its DXGI adapter so a D3D11 device built on it can interop.
//
// CUDA-D3D11 interop requires the D3D11 device and the CUDA context to live on
// one physical GPU. The default D3D adapter is the wrong one on a
// hybrid-graphics (Optimus) laptop: the display is driven by an integrated GPU
// that has no CUDA device, so a texture created there cannot be registered
// with CUDA. Walk the DXGI adapters and pick the first that CUDA recognizes
// (via `cudaD3D11GetDevice`), then make it current.
//
// Returns a null handle when no adapter hosts a CUDA device (no NVIDIA GPU or
// driver). The caller then builds on the default adapter, which surfaces the
// underlying CUDA error at registration rather than here.
[[nodiscard]] inline com_ptr<IDXGIAdapter> cuda_interop_adapter() {
  com_ptr<IDXGIFactory1> factory;
  hr_status{CreateDXGIFactory1(IID_PPV_ARGS(factory.put()))}.or_throw();

  com_ptr<IDXGIAdapter> found;
  int cuda_device = 0;
  for (UINT ndx = 0;; ++ndx) {
    com_ptr<IDXGIAdapter> adapter;
    if (factory->EnumAdapters(ndx, adapter.put()) == DXGI_ERROR_NOT_FOUND)
      break;
    if (cudaD3D11GetDevice(&cuda_device, adapter.get()) == cudaSuccess) {
      found = std::move(adapter);
      break;
    }
  }

  // Probing a non-CUDA adapter (the iGPU) records a thread-wide error; consume
  // it so a later status read can't mistake it for its own call's failure.
  [[maybe_unused]] const cuda_last_status cleared{read_mode::consume};

  if (found) cuda_last_status{cudaSetDevice(cuda_device)}.or_throw();
  return found;
}

#pragma endregion
#pragma region cuda_graphics_register_flags

// Bitmask wrapper for `cudaGraphicsRegisterFlags`, the registration flags for
// `cudaGraphicsD3D11RegisterResource`.
//
// NOLINTNEXTLINE(performance-enum-size)
enum class cuda_graphics_register_flags : std::underlying_type_t<
    cudaGraphicsRegisterFlags> {
  none = cudaGraphicsRegisterFlagsNone,
  read_only = cudaGraphicsRegisterFlagsReadOnly,
  write_discard = cudaGraphicsRegisterFlagsWriteDiscard,
  surface_load_store = cudaGraphicsRegisterFlagsSurfaceLoadStore,
  texture_gather = cudaGraphicsRegisterFlagsTextureGather,
};
consteval auto corvid_enum_spec(cuda_graphics_register_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<
      cuda_graphics_register_flags,
      "texture_gather, surface_load_store, write_discard, read_only">();
}

#pragma endregion
#pragma region cuda_d3d11_resource

// RAII registration of a D3D11 resource for CUDA access, by default for
// surface load/store so a kernel can `surf2Dwrite` it.
//
// Reach its storage by mapping it per frame with a `cuda_d3d11_mapping`.
class cuda_d3d11_resource {
public:
#pragma region Construction

  cuda_d3d11_resource() = default;

  explicit cuda_d3d11_resource(ID3D11Resource* resource,
      cuda_graphics_register_flags flags =
          cuda_graphics_register_flags::surface_load_store) {
    cuda_last_status{
        cudaGraphicsD3D11RegisterResource(&resource_, resource, *flags)}
        .or_throw();
  }

  cuda_d3d11_resource(const cuda_d3d11_resource&) = delete;
  cuda_d3d11_resource& operator=(const cuda_d3d11_resource&) = delete;

  cuda_d3d11_resource(cuda_d3d11_resource&& other) noexcept
      : resource_{std::exchange(other.resource_, nullptr)} {}
  cuda_d3d11_resource& operator=(cuda_d3d11_resource&& other) noexcept {
    if (this != &other) {
      unregister();
      resource_ = std::exchange(other.resource_, nullptr);
    }
    return *this;
  }
  ~cuda_d3d11_resource() { unregister(); }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] cudaGraphicsResource* get() const noexcept {
    return resource_;
  }
  [[nodiscard]] operator cudaGraphicsResource*() const noexcept {
    return resource_;
  }

#pragma endregion
#pragma region Helpers
private:
  void unregister() {
    if (resource_) cudaGraphicsUnregisterResource(resource_);
  }

#pragma endregion
#pragma region Data members
private:
  cudaGraphicsResource* resource_{};

#pragma endregion
};

#pragma endregion
#pragma region cuda_d3d11_mapping

// Scoped map of a `cuda_d3d11_resource` for CUDA access.
//
// The unmap on scope exit also synchronizes CUDA work against the resource's
// subsequent D3D use. Create one per frame on the stack. The `cudaArray` it
// yields is borrowed, valid only within this scope.
class cuda_d3d11_mapping {
public:
#pragma region Construction

  explicit cuda_d3d11_mapping(const cuda_d3d11_resource& resource)
      : resource_{resource} {
    cuda_last_status{cudaGraphicsMapResources(1, &resource_)}.or_throw();
  }

  cuda_d3d11_mapping(const cuda_d3d11_mapping&) = delete;
  cuda_d3d11_mapping& operator=(const cuda_d3d11_mapping&) = delete;
  cuda_d3d11_mapping(cuda_d3d11_mapping&&) = delete;
  cuda_d3d11_mapping& operator=(cuda_d3d11_mapping&&) = delete;

  ~cuda_d3d11_mapping() { cudaGraphicsUnmapResources(1, &resource_); }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] operator bool() const noexcept { return resource_; }
  [[nodiscard]] bool operator!() const noexcept { return !resource_; }

  // The mapped resource's array (sub-resource 0, mip 0), to build a surface or
  // texture object. Borrowed: valid only while this mapping is alive.
  [[nodiscard]] cudaArray_t array() const {
    cudaArray_t array{};
    cuda_last_status{
        cudaGraphicsSubResourceGetMappedArray(&array, resource_, 0, 0)}
        .or_throw();
    return array;
  }

#pragma endregion
#pragma region Data members
private:
  cudaGraphicsResource* resource_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
