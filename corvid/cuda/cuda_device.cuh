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
#include <stdexcept>
#include <utility>

#include <cuda_runtime.h>

#include "./cuda_status.cuh"

namespace corvid::cuda {

#pragma region cuda_device_attr

// Wrapper for `cudaDeviceAttr`.
// NOLINTNEXTLINE(performance-enum-size)
enum class cuda_device_attr : std::underlying_type_t<cudaDeviceAttr> {
  max_threads_per_block = cudaDevAttrMaxThreadsPerBlock,               // 1
  max_block_dim_x = cudaDevAttrMaxBlockDimX,                           // 2
  max_block_dim_y = cudaDevAttrMaxBlockDimY,                           // 3
  max_block_dim_z = cudaDevAttrMaxBlockDimZ,                           // 4
  max_grid_dim_x = cudaDevAttrMaxGridDimX,                             // 5
  max_grid_dim_y = cudaDevAttrMaxGridDimY,                             // 6
  max_grid_dim_z = cudaDevAttrMaxGridDimZ,                             // 7
  max_shared_memory_per_block = cudaDevAttrMaxSharedMemoryPerBlock,    // 8
  total_constant_memory = cudaDevAttrTotalConstantMemory,              // 9
  warp_size = cudaDevAttrWarpSize,                                     // 10
  max_pitch = cudaDevAttrMaxPitch,                                     // 11
  max_registers_per_block = cudaDevAttrMaxRegistersPerBlock,           // 12
  clock_rate = cudaDevAttrClockRate,                                   // 13
  texture_alignment = cudaDevAttrTextureAlignment,                     // 14
  gpu_overlap = cudaDevAttrGpuOverlap,                                 // 15
  multi_processor_count = cudaDevAttrMultiProcessorCount,              // 16
  kernel_exec_timeout = cudaDevAttrKernelExecTimeout,                  // 17
  integrated = cudaDevAttrIntegrated,                                  // 18
  can_map_host_memory = cudaDevAttrCanMapHostMemory,                   // 19
  compute_mode = cudaDevAttrComputeMode,                               // 20
  max_texture1d_width = cudaDevAttrMaxTexture1DWidth,                  // 21
  max_texture2d_width = cudaDevAttrMaxTexture2DWidth,                  // 22
  max_texture2d_height = cudaDevAttrMaxTexture2DHeight,                // 23
  max_texture3d_width = cudaDevAttrMaxTexture3DWidth,                  // 24
  max_texture3d_height = cudaDevAttrMaxTexture3DHeight,                // 25
  max_texture3d_depth = cudaDevAttrMaxTexture3DDepth,                  // 26
  max_texture2d_layered_width = cudaDevAttrMaxTexture2DLayeredWidth,   // 27
  max_texture2d_layered_height = cudaDevAttrMaxTexture2DLayeredHeight, // 28
  max_texture2d_layered_layers = cudaDevAttrMaxTexture2DLayeredLayers, // 29
  surface_alignment = cudaDevAttrSurfaceAlignment,                     // 30
  concurrent_kernels = cudaDevAttrConcurrentKernels,                   // 31
  ecc_enabled = cudaDevAttrEccEnabled,                                 // 32
  pci_bus_id = cudaDevAttrPciBusId,                                    // 33
  pci_device_id = cudaDevAttrPciDeviceId,                              // 34
  tcc_driver = cudaDevAttrTccDriver,                                   // 35
  memory_clock_rate = cudaDevAttrMemoryClockRate,                      // 36
  global_memory_bus_width = cudaDevAttrGlobalMemoryBusWidth,           // 37
  l2_cache_size = cudaDevAttrL2CacheSize,                              // 38
  max_threads_per_multi_processor =
      cudaDevAttrMaxThreadsPerMultiProcessor,                          // 39
  async_engine_count = cudaDevAttrAsyncEngineCount,                    // 40
  unified_addressing = cudaDevAttrUnifiedAddressing,                   // 41
  max_texture1d_layered_width = cudaDevAttrMaxTexture1DLayeredWidth,   // 42
  max_texture1d_layered_layers = cudaDevAttrMaxTexture1DLayeredLayers, // 43
  max_texture2d_gather_width = cudaDevAttrMaxTexture2DGatherWidth,     // 45
  max_texture2d_gather_height = cudaDevAttrMaxTexture2DGatherHeight,   // 46
  max_texture3d_width_alt = cudaDevAttrMaxTexture3DWidthAlt,           // 47
  max_texture3d_height_alt = cudaDevAttrMaxTexture3DHeightAlt,         // 48
  max_texture3d_depth_alt = cudaDevAttrMaxTexture3DDepthAlt,           // 49
  pci_domain_id = cudaDevAttrPciDomainId,                              // 50
  texture_pitch_alignment = cudaDevAttrTexturePitchAlignment,          // 51
  max_texture_cubemap_width = cudaDevAttrMaxTextureCubemapWidth,       // 52
  max_texture_cubemap_layered_width =
      cudaDevAttrMaxTextureCubemapLayeredWidth, // 53
  max_texture_cubemap_layered_layers =
      cudaDevAttrMaxTextureCubemapLayeredLayers,                       // 54
  max_surface1d_width = cudaDevAttrMaxSurface1DWidth,                  // 55
  max_surface2d_width = cudaDevAttrMaxSurface2DWidth,                  // 56
  max_surface2d_height = cudaDevAttrMaxSurface2DHeight,                // 57
  max_surface3d_width = cudaDevAttrMaxSurface3DWidth,                  // 58
  max_surface3d_height = cudaDevAttrMaxSurface3DHeight,                // 59
  max_surface3d_depth = cudaDevAttrMaxSurface3DDepth,                  // 60
  max_surface1d_layered_width = cudaDevAttrMaxSurface1DLayeredWidth,   // 61
  max_surface1d_layered_layers = cudaDevAttrMaxSurface1DLayeredLayers, // 62
  max_surface2d_layered_width = cudaDevAttrMaxSurface2DLayeredWidth,   // 63
  max_surface2d_layered_height = cudaDevAttrMaxSurface2DLayeredHeight, // 64
  max_surface2d_layered_layers = cudaDevAttrMaxSurface2DLayeredLayers, // 65
  max_surface_cubemap_width = cudaDevAttrMaxSurfaceCubemapWidth,       // 66
  max_surface_cubemap_layered_width =
      cudaDevAttrMaxSurfaceCubemapLayeredWidth, // 67
  max_surface_cubemap_layered_layers =
      cudaDevAttrMaxSurfaceCubemapLayeredLayers,                         // 68
  max_texture1d_linear_width = cudaDevAttrMaxTexture1DLinearWidth,       // 69
  max_texture2d_linear_width = cudaDevAttrMaxTexture2DLinearWidth,       // 70
  max_texture2d_linear_height = cudaDevAttrMaxTexture2DLinearHeight,     // 71
  max_texture2d_linear_pitch = cudaDevAttrMaxTexture2DLinearPitch,       // 72
  max_texture2d_mipmapped_width = cudaDevAttrMaxTexture2DMipmappedWidth, // 73
  max_texture2d_mipmapped_height =
      cudaDevAttrMaxTexture2DMipmappedHeight,                            // 74
  compute_capability_major = cudaDevAttrComputeCapabilityMajor,          // 75
  compute_capability_minor = cudaDevAttrComputeCapabilityMinor,          // 76
  max_texture1d_mipmapped_width = cudaDevAttrMaxTexture1DMipmappedWidth, // 77
  stream_priorities_supported = cudaDevAttrStreamPrioritiesSupported,    // 78
  global_l1_cache_supported = cudaDevAttrGlobalL1CacheSupported,         // 79
  local_l1_cache_supported = cudaDevAttrLocalL1CacheSupported,           // 80
  max_shared_memory_per_multiprocessor =
      cudaDevAttrMaxSharedMemoryPerMultiprocessor, // 81
  max_registers_per_multiprocessor =
      cudaDevAttrMaxRegistersPerMultiprocessor,                        // 82
  managed_memory = cudaDevAttrManagedMemory,                           // 83
  is_multi_gpu_board = cudaDevAttrIsMultiGpuBoard,                     // 84
  multi_gpu_board_group_id = cudaDevAttrMultiGpuBoardGroupID,          // 85
  host_native_atomic_supported = cudaDevAttrHostNativeAtomicSupported, // 86
  single_to_double_precision_perf_ratio =
      cudaDevAttrSingleToDoublePrecisionPerfRatio,                      // 87
  pageable_memory_access = cudaDevAttrPageableMemoryAccess,             // 88
  concurrent_managed_access = cudaDevAttrConcurrentManagedAccess,       // 89
  compute_preemption_supported = cudaDevAttrComputePreemptionSupported, // 90
  can_use_host_pointer_for_registered_mem =
      cudaDevAttrCanUseHostPointerForRegisteredMem,  // 91
  cooperative_launch = cudaDevAttrCooperativeLaunch, // 95
  max_shared_memory_per_block_optin =
      cudaDevAttrMaxSharedMemoryPerBlockOptin,                // 97
  can_flush_remote_writes = cudaDevAttrCanFlushRemoteWrites,  // 98
  host_register_supported = cudaDevAttrHostRegisterSupported, // 99
  pageable_memory_access_uses_host_page_tables =
      cudaDevAttrPageableMemoryAccessUsesHostPageTables, // 100
  direct_managed_mem_access_from_host =
      cudaDevAttrDirectManagedMemAccessFromHost,                         // 101
  max_blocks_per_multiprocessor = cudaDevAttrMaxBlocksPerMultiprocessor, // 106
  max_persisting_l2_cache_size = cudaDevAttrMaxPersistingL2CacheSize,    // 108
  max_access_policy_window_size = cudaDevAttrMaxAccessPolicyWindowSize,  // 109
  reserved_shared_memory_per_block =
      cudaDevAttrReservedSharedMemoryPerBlock,                       // 111
  sparse_cuda_array_supported = cudaDevAttrSparseCudaArraySupported, // 112
  host_register_read_only_supported =
      cudaDevAttrHostRegisterReadOnlySupported, // 113
  timeline_semaphore_interop_supported =
      cudaDevAttrTimelineSemaphoreInteropSupported,              // 114
  memory_pools_supported = cudaDevAttrMemoryPoolsSupported,      // 115
  gpu_direct_rdma_supported = cudaDevAttrGPUDirectRDMASupported, // 116
  gpu_direct_rdma_flush_writes_options =
      cudaDevAttrGPUDirectRDMAFlushWritesOptions, // 117
  gpu_direct_rdma_writes_ordering =
      cudaDevAttrGPUDirectRDMAWritesOrdering, // 118
  memory_pool_supported_handle_types =
      cudaDevAttrMemoryPoolSupportedHandleTypes, // 119
  cluster_launch = cudaDevAttrClusterLaunch,     // 120
  deferred_mapping_cuda_array_supported =
      cudaDevAttrDeferredMappingCudaArraySupported,      // 121
  ipc_event_support = cudaDevAttrIpcEventSupport,        // 125
  mem_sync_domain_count = cudaDevAttrMemSyncDomainCount, // 126
  numa_config = cudaDevAttrNumaConfig,                   // 130
  numa_id = cudaDevAttrNumaId,                           // 131
  mps_enabled = cudaDevAttrMpsEnabled,                   // 133
  host_numa_id = cudaDevAttrHostNumaId,                  // 134
  d3d12_cig_supported = cudaDevAttrD3D12CigSupported,    // 135
  vulkan_cig_supported = cudaDevAttrVulkanCigSupported,  // 138
  gpu_pci_device_id = cudaDevAttrGpuPciDeviceId,         // 139
  gpu_pci_subsystem_id = cudaDevAttrGpuPciSubsystemId,   // 140
  host_numa_memory_pools_supported =
      cudaDevAttrHostNumaMemoryPoolsSupported, // 142
  host_numa_multinode_ipc_supported =
      cudaDevAttrHostNumaMultinodeIpcSupported,                      // 143
  host_memory_pools_supported = cudaDevAttrHostMemoryPoolsSupported, // 144
  only_partial_host_native_atomic_supported =
      cudaDevAttrOnlyPartialHostNativeAtomicSupported,              // 147
  atomic_reduction_supported = cudaDevAttrAtomicReductionSupported, // 148
  cig_streams_supported = cudaDevAttrCigStreamsSupported,           // 151
};

// Register cuda_device_attr as a sparse sequence enum so it gets enum<->string
// conversion. The dense name list is indexed from value 1 (`min_value`); the
// reserved and unassigned slots in CUDA's numbering carry empty names, which
// print as their numeric value.
consteval auto corvid_enum_spec(cuda_device_attr*) {
  return corvid::enums::sequence::make_sequence_enum_spec<cuda_device_attr,
      "max_threads_per_block,max_block_dim_x,max_block_dim_y,max_block_dim_z,"
      "max_grid_dim_x,max_grid_dim_y,max_grid_dim_z,max_shared_memory_per_"
      "block,total_constant_memory,warp_size,max_pitch,max_registers_per_"
      "block,clock_rate,texture_alignment,gpu_overlap,multi_processor_count,"
      "kernel_exec_timeout,integrated,can_map_host_memory,compute_mode,max_"
      "texture1d_width,max_texture2d_width,max_texture2d_height,max_texture3d_"
      "width,max_texture3d_height,max_texture3d_depth,max_texture2d_layered_"
      "width,max_texture2d_layered_height,max_texture2d_layered_layers,"
      "surface_alignment,concurrent_kernels,ecc_enabled,pci_bus_id,pci_device_"
      "id,tcc_driver,memory_clock_rate,global_memory_bus_width,l2_cache_size,"
      "max_threads_per_multi_processor,async_engine_count,unified_addressing,"
      "max_texture1d_layered_width,max_texture1d_layered_layers,,max_"
      "texture2d_gather_width,max_texture2d_gather_height,max_texture3d_width_"
      "alt,max_texture3d_height_alt,max_texture3d_depth_alt,pci_domain_id,"
      "texture_pitch_alignment,max_texture_cubemap_width,max_texture_cubemap_"
      "layered_width,max_texture_cubemap_layered_layers,max_surface1d_width,"
      "max_surface2d_width,max_surface2d_height,max_surface3d_width,max_"
      "surface3d_height,max_surface3d_depth,max_surface1d_layered_width,max_"
      "surface1d_layered_layers,max_surface2d_layered_width,max_surface2d_"
      "layered_height,max_surface2d_layered_layers,max_surface_cubemap_width,"
      "max_surface_cubemap_layered_width,max_surface_cubemap_layered_layers,"
      "max_texture1d_linear_width,max_texture2d_linear_width,max_texture2d_"
      "linear_height,max_texture2d_linear_pitch,max_texture2d_mipmapped_width,"
      "max_texture2d_mipmapped_height,compute_capability_major,compute_"
      "capability_minor,max_texture1d_mipmapped_width,stream_priorities_"
      "supported,global_l1_cache_supported,local_l1_cache_supported,max_"
      "shared_memory_per_multiprocessor,max_registers_per_multiprocessor,"
      "managed_memory,is_multi_gpu_board,multi_gpu_board_group_id,host_native_"
      "atomic_supported,single_to_double_precision_perf_ratio,pageable_memory_"
      "access,concurrent_managed_access,compute_preemption_supported,can_use_h"
      "ost_pointer_for_registered_mem,,,,cooperative_launch,,max_shared_"
      "memory_per_block_optin,can_flush_remote_writes,host_register_supported,"
      "pageable_memory_access_uses_host_page_tables,direct_managed_mem_access_"
      "from_host,,,,,max_blocks_per_multiprocessor,,max_persisting_l2_cache_"
      "size,max_access_policy_window_size,,reserved_shared_memory_per_block,"
      "sparse_cuda_array_supported,host_register_read_only_supported,timeline_"
      "semaphore_interop_supported,memory_pools_supported,gpu_direct_rdma_"
      "supported,gpu_direct_rdma_flush_writes_options,gpu_direct_rdma_writes_"
      "ordering,memory_pool_supported_handle_types,cluster_launch,deferred_"
      "mapping_cuda_array_supported,,,,ipc_event_support,mem_sync_domain_"
      "count,,,,numa_config,numa_id,,mps_enabled,host_numa_id,d3d12_cig_"
      "supported,,,vulkan_cig_supported,gpu_pci_device_id,gpu_pci_subsystem_"
      "id,,host_numa_memory_pools_supported,host_numa_multinode_ipc_supported,"
      "host_memory_pools_supported,,,only_partial_host_native_atomic_"
      "supported,atomic_reduction_supported,,,cig_streams_supported",
      corvid::enums::wrapclip::none, cuda_device_attr{1}>();
}

#pragma endregion
#pragma region cuda_device

class cuda_device {
public:
#pragma region Construction

  explicit cuda_device(int device_id = get_current_device_id())
      : device_id(device_id) {}

#pragma endregion
#pragma region Accessors

  [[nodiscard]] int id() const { return device_id; }

#pragma endregion
#pragma region Operations

  [[nodiscard]] int get_attribute(cuda_device_attr attr) const {
    int value;
    cuda_last_status status{cudaDeviceGetAttribute(&value,
        static_cast<cudaDeviceAttr>(attr), device_id)};
    if (!status)
      throw std::runtime_error("Failed to get CUDA device attribute");
    return value;
  }

  static size_t get_device_count() {
    int count;
    cuda_last_status status{cudaGetDeviceCount(&count)};
    if (!status) throw std::runtime_error("Failed to get CUDA device count");
    return static_cast<size_t>(count);
  }

#pragma endregion
#pragma region Helpers
private:
  static int get_current_device_id() {
    int device_id;
    cuda_last_status status{cudaGetDevice(&device_id)};
    if (!status) throw std::runtime_error("Failed to get current CUDA device");
    return device_id;
  }

#pragma endregion
#pragma region Data members
private:
  int device_id{};

#pragma endregion
};

#pragma endregion
} // namespace corvid::cuda
