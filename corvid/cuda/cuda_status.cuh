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

#include <cstdint>
#include <stdexcept>

#include <cuda_runtime.h>

#include "../enums/bool_enums.h"
#include "../enums/sequence_enum.h"
#include "../enums/enum_conversion.h"

// CUDA status.
//
// Wraps CUDA error codes, whether returned by API calls or recorded by the
// runtime for asynchronous errors.

namespace corvid::cuda {
using corvid::enums::bool_enums::read_mode;

#pragma region cuda_status

// Enum to wrap `cudaError`.
// NOLINTNEXTLINE(performance-enum-size)
enum class cuda_status : std::underlying_type_t<cudaError_t> {
  success = cudaSuccess,                                               // 0
  invalid_value = cudaErrorInvalidValue,                               // 1
  memory_allocation = cudaErrorMemoryAllocation,                       // 2
  initialization_error = cudaErrorInitializationError,                 // 3
  cudart_unloading = cudaErrorCudartUnloading,                         // 4
  profiler_disabled = cudaErrorProfilerDisabled,                       // 5
  profiler_not_initialized = cudaErrorProfilerNotInitialized,          // 6
  profiler_already_started = cudaErrorProfilerAlreadyStarted,          // 7
  profiler_already_stopped = cudaErrorProfilerAlreadyStopped,          // 8
  invalid_configuration = cudaErrorInvalidConfiguration,               // 9
  version_translation = cudaErrorVersionTranslation,                   // 10
  invalid_pitch_value = cudaErrorInvalidPitchValue,                    // 12
  invalid_symbol = cudaErrorInvalidSymbol,                             // 13
  invalid_host_pointer = cudaErrorInvalidHostPointer,                  // 16
  invalid_device_pointer = cudaErrorInvalidDevicePointer,              // 17
  invalid_texture = cudaErrorInvalidTexture,                           // 18
  invalid_texture_binding = cudaErrorInvalidTextureBinding,            // 19
  invalid_channel_descriptor = cudaErrorInvalidChannelDescriptor,      // 20
  invalid_memcpy_direction = cudaErrorInvalidMemcpyDirection,          // 21
  address_of_constant = cudaErrorAddressOfConstant,                    // 22
  texture_fetch_failed = cudaErrorTextureFetchFailed,                  // 23
  texture_not_bound = cudaErrorTextureNotBound,                        // 24
  synchronization_error = cudaErrorSynchronizationError,               // 25
  invalid_filter_setting = cudaErrorInvalidFilterSetting,              // 26
  invalid_norm_setting = cudaErrorInvalidNormSetting,                  // 27
  mixed_device_execution = cudaErrorMixedDeviceExecution,              // 28
  not_yet_implemented = cudaErrorNotYetImplemented,                    // 31
  memory_value_too_large = cudaErrorMemoryValueTooLarge,               // 32
  stub_library = cudaErrorStubLibrary,                                 // 34
  insufficient_driver = cudaErrorInsufficientDriver,                   // 35
  call_requires_newer_driver = cudaErrorCallRequiresNewerDriver,       // 36
  invalid_surface = cudaErrorInvalidSurface,                           // 37
  duplicate_variable_name = cudaErrorDuplicateVariableName,            // 43
  duplicate_texture_name = cudaErrorDuplicateTextureName,              // 44
  duplicate_surface_name = cudaErrorDuplicateSurfaceName,              // 45
  devices_unavailable = cudaErrorDevicesUnavailable,                   // 46
  incompatible_driver_context = cudaErrorIncompatibleDriverContext,    // 49
  missing_configuration = cudaErrorMissingConfiguration,               // 52
  prior_launch_failure = cudaErrorPriorLaunchFailure,                  // 53
  launch_max_depth_exceeded = cudaErrorLaunchMaxDepthExceeded,         // 65
  launch_file_scoped_tex = cudaErrorLaunchFileScopedTex,               // 66
  launch_file_scoped_surf = cudaErrorLaunchFileScopedSurf,             // 67
  sync_depth_exceeded = cudaErrorSyncDepthExceeded,                    // 68
  launch_pending_count_exceeded = cudaErrorLaunchPendingCountExceeded, // 69
  invalid_device_function = cudaErrorInvalidDeviceFunction,            // 98
  no_device = cudaErrorNoDevice,                                       // 100
  invalid_device = cudaErrorInvalidDevice,                             // 101
  device_not_licensed = cudaErrorDeviceNotLicensed,                    // 102
  software_validity_not_established =
      cudaErrorSoftwareValidityNotEstablished,                           // 103
  startup_failure = cudaErrorStartupFailure,                             // 127
  invalid_kernel_image = cudaErrorInvalidKernelImage,                    // 200
  device_uninitialized = cudaErrorDeviceUninitialized,                   // 201
  map_buffer_object_failed = cudaErrorMapBufferObjectFailed,             // 205
  unmap_buffer_object_failed = cudaErrorUnmapBufferObjectFailed,         // 206
  array_is_mapped = cudaErrorArrayIsMapped,                              // 207
  already_mapped = cudaErrorAlreadyMapped,                               // 208
  no_kernel_image_for_device = cudaErrorNoKernelImageForDevice,          // 209
  already_acquired = cudaErrorAlreadyAcquired,                           // 210
  not_mapped = cudaErrorNotMapped,                                       // 211
  not_mapped_as_array = cudaErrorNotMappedAsArray,                       // 212
  not_mapped_as_pointer = cudaErrorNotMappedAsPointer,                   // 213
  ecc_uncorrectable = cudaErrorECCUncorrectable,                         // 214
  unsupported_limit = cudaErrorUnsupportedLimit,                         // 215
  device_already_in_use = cudaErrorDeviceAlreadyInUse,                   // 216
  peer_access_unsupported = cudaErrorPeerAccessUnsupported,              // 217
  invalid_ptx = cudaErrorInvalidPtx,                                     // 218
  invalid_graphics_context = cudaErrorInvalidGraphicsContext,            // 219
  nvlink_uncorrectable = cudaErrorNvlinkUncorrectable,                   // 220
  jit_compiler_not_found = cudaErrorJitCompilerNotFound,                 // 221
  unsupported_ptx_version = cudaErrorUnsupportedPtxVersion,              // 222
  jit_compilation_disabled = cudaErrorJitCompilationDisabled,            // 223
  unsupported_exec_affinity = cudaErrorUnsupportedExecAffinity,          // 224
  unsupported_dev_side_sync = cudaErrorUnsupportedDevSideSync,           // 225
  contained = cudaErrorContained,                                        // 226
  invalid_source = cudaErrorInvalidSource,                               // 300
  file_not_found = cudaErrorFileNotFound,                                // 301
  shared_object_symbol_not_found = cudaErrorSharedObjectSymbolNotFound,  // 302
  shared_object_init_failed = cudaErrorSharedObjectInitFailed,           // 303
  operating_system = cudaErrorOperatingSystem,                           // 304
  invalid_resource_handle = cudaErrorInvalidResourceHandle,              // 400
  illegal_state = cudaErrorIllegalState,                                 // 401
  lossy_query = cudaErrorLossyQuery,                                     // 402
  symbol_not_found = cudaErrorSymbolNotFound,                            // 500
  not_ready = cudaErrorNotReady,                                         // 600
  illegal_address = cudaErrorIllegalAddress,                             // 700
  launch_out_of_resources = cudaErrorLaunchOutOfResources,               // 701
  launch_timeout = cudaErrorLaunchTimeout,                               // 702
  launch_incompatible_texturing = cudaErrorLaunchIncompatibleTexturing,  // 703
  peer_access_already_enabled = cudaErrorPeerAccessAlreadyEnabled,       // 704
  peer_access_not_enabled = cudaErrorPeerAccessNotEnabled,               // 705
  set_on_active_process = cudaErrorSetOnActiveProcess,                   // 708
  context_is_destroyed = cudaErrorContextIsDestroyed,                    // 709
  assert = cudaErrorAssert,                                              // 710
  too_many_peers = cudaErrorTooManyPeers,                                // 711
  host_memory_already_registered = cudaErrorHostMemoryAlreadyRegistered, // 712
  host_memory_not_registered = cudaErrorHostMemoryNotRegistered,         // 713
  hardware_stack_error = cudaErrorHardwareStackError,                    // 714
  illegal_instruction = cudaErrorIllegalInstruction,                     // 715
  misaligned_address = cudaErrorMisalignedAddress,                       // 716
  invalid_address_space = cudaErrorInvalidAddressSpace,                  // 717
  invalid_pc = cudaErrorInvalidPc,                                       // 718
  launch_failure = cudaErrorLaunchFailure,                               // 719
  cooperative_launch_too_large = cudaErrorCooperativeLaunchTooLarge,     // 720
  tensor_memory_leak = cudaErrorTensorMemoryLeak,                        // 721
  not_permitted = cudaErrorNotPermitted,                                 // 800
  not_supported = cudaErrorNotSupported,                                 // 801
  system_not_ready = cudaErrorSystemNotReady,                            // 802
  system_driver_mismatch = cudaErrorSystemDriverMismatch,                // 803
  compat_not_supported_on_device = cudaErrorCompatNotSupportedOnDevice,  // 804
  mps_connection_failed = cudaErrorMpsConnectionFailed,                  // 805
  mps_rpc_failure = cudaErrorMpsRpcFailure,                              // 806
  mps_server_not_ready = cudaErrorMpsServerNotReady,                     // 807
  mps_max_clients_reached = cudaErrorMpsMaxClientsReached,               // 808
  mps_max_connections_reached = cudaErrorMpsMaxConnectionsReached,       // 809
  mps_client_terminated = cudaErrorMpsClientTerminated,                  // 810
  cdp_not_supported = cudaErrorCdpNotSupported,                          // 811
  cdp_version_mismatch = cudaErrorCdpVersionMismatch,                    // 812
  stream_capture_unsupported = cudaErrorStreamCaptureUnsupported,        // 900
  stream_capture_invalidated = cudaErrorStreamCaptureInvalidated,        // 901
  stream_capture_merge = cudaErrorStreamCaptureMerge,                    // 902
  stream_capture_unmatched = cudaErrorStreamCaptureUnmatched,            // 903
  stream_capture_unjoined = cudaErrorStreamCaptureUnjoined,              // 904
  stream_capture_isolation = cudaErrorStreamCaptureIsolation,            // 905
  stream_capture_implicit = cudaErrorStreamCaptureImplicit,              // 906
  captured_event = cudaErrorCapturedEvent,                               // 907
  stream_capture_wrong_thread = cudaErrorStreamCaptureWrongThread,       // 908
  timeout = cudaErrorTimeout,                                            // 909
  graph_exec_update_failure = cudaErrorGraphExecUpdateFailure,           // 910
  external_device = cudaErrorExternalDevice,                             // 911
  invalid_cluster_size = cudaErrorInvalidClusterSize,                    // 912
  function_not_loaded = cudaErrorFunctionNotLoaded,                      // 913
  invalid_resource_type = cudaErrorInvalidResourceType,                  // 914
  invalid_resource_configuration =
      cudaErrorInvalidResourceConfiguration,                // 915
  stream_detached = cudaErrorStreamDetached,                // 917
  graph_recapture_failure = cudaErrorGraphRecaptureFailure, // 918
  unknown = cudaErrorUnknown,                               // 999
  api_failure_base = cudaErrorApiFailureBase,               // 10000
};

// Register cuda_status as a sparse sequence enum so it gets enum<->string
// conversion. The names string is segmented: runs are separated by '|', each
// run starts with the numeric value of its first name and then names
// consecutive values. Gaps of one or two values stay inside a run as empty
// names; only the larger gaps between CUDA's error-code blocks start a new
// run.
consteval auto corvid_enum_spec(cuda_status*) {
  return corvid::enums::sequence::make_sequence_enum_spec<cuda_status,
      "0,success,invalid_value,memory_allocation,initialization_error,cudart_"
      "unloading,profiler_disabled,profiler_not_initialized,profiler_already_"
      "started,profiler_already_stopped,invalid_configuration,version_"
      "translation,,invalid_pitch_value,invalid_symbol,,,invalid_host_pointer,"
      "invalid_device_pointer,invalid_texture,invalid_texture_binding,invalid_"
      "channel_descriptor,invalid_memcpy_direction,address_of_constant,"
      "texture_fetch_failed,texture_not_bound,synchronization_error,invalid_"
      "filter_setting,invalid_norm_setting,mixed_device_execution,,,not_yet_"
      "implemented,memory_value_too_large,,stub_library,insufficient_driver,"
      "call_requires_newer_driver,invalid_surface|43,duplicate_variable_name,"
      "duplicate_texture_name,duplicate_surface_name,devices_unavailable,,,"
      "incompatible_driver_context,,missing_configuration,prior_launch_"
      "failure|65,launch_max_depth_exceeded,launch_file_scoped_tex,launch_"
      "file_scoped_surf,sync_depth_exceeded,launch_pending_count_exceeded|98,"
      "invalid_device_function,,no_device,invalid_device,device_not_licensed,"
      "software_validity_not_established|127,startup_failure|200,invalid_"
      "kernel_image,device_uninitialized|205,map_buffer_object_failed,unmap_"
      "buffer_object_failed,array_is_mapped,already_mapped,no_kernel_image_"
      "for_device,already_acquired,not_mapped,not_mapped_as_array,not_mapped_"
      "as_pointer,ecc_uncorrectable,unsupported_limit,device_already_in_use,"
      "peer_access_unsupported,invalid_ptx,invalid_graphics_context,nvlink_"
      "uncorrectable,jit_compiler_not_found,unsupported_ptx_version,jit_"
      "compilation_disabled,unsupported_exec_affinity,unsupported_dev_side_"
      "sync,contained|300,invalid_source,file_not_found,shared_object_symbol_"
      "not_found,shared_object_init_failed,operating_system|400,invalid_"
      "resource_handle,illegal_state,lossy_query|500,symbol_not_found|600,not_"
      "ready|700,illegal_address,launch_out_of_resources,launch_timeout,"
      "launch_incompatible_texturing,peer_access_already_enabled,peer_access_"
      "not_enabled,,,set_on_active_process,context_is_destroyed,assert,too_"
      "many_peers,host_memory_already_registered,host_memory_not_registered,"
      "hardware_stack_error,illegal_instruction,misaligned_address,invalid_"
      "address_space,invalid_pc,launch_failure,cooperative_launch_too_large,"
      "tensor_memory_leak|800,not_permitted,not_supported,system_not_ready,"
      "system_driver_mismatch,compat_not_supported_on_device,mps_connection_"
      "failed,mps_rpc_failure,mps_server_not_ready,mps_max_clients_reached,"
      "mps_max_connections_reached,mps_client_terminated,cdp_not_supported,"
      "cdp_version_mismatch|900,stream_capture_unsupported,stream_capture_"
      "invalidated,stream_capture_merge,stream_capture_unmatched,stream_"
      "capture_unjoined,stream_capture_isolation,stream_capture_implicit,"
      "captured_event,stream_capture_wrong_thread,timeout,graph_exec_update_"
      "failure,external_device,invalid_cluster_size,function_not_loaded,"
      "invalid_resource_type,invalid_resource_configuration,,stream_detached,"
      "graph_recapture_failure|999,unknown|10000,api_failure_base">();
}

#pragma endregion
#pragma region cuda_last_status

// CUDA status wrapper.
//
// Contains the last CUDA error status, which could be the return value of an
// API call, or the last error produced by any runtime call in the same host
// thread.
//
// When retrieving the thread-wide error status, it consumes it (unless
// `read_mode::peek` is used). However, execution errors are sticky: they
// corrupt the context, causing all subsequent CUDA calls to fail. Consuming
// these doesn't clear the condition, and every API call on the context will
// return the error. Synchronous launch errors are non-sticky.
class cuda_last_status {
public:
#pragma region Construction

  cuda_last_status() : cuda_last_status{read_mode::consume} {}
  explicit cuda_last_status(read_mode mode) : cuda_last_status{read(mode)} {}
  cuda_last_status(cudaError_t err) : value_{static_cast<cuda_status>(err)} {}

#pragma endregion
#pragma region Status

  [[nodiscard]] cuda_status value() const { return value_; }

  [[nodiscard]] bool ok() const noexcept {
    return value_ == cuda_status::success;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

#pragma endregion
#pragma region Errors

  [[nodiscard]] const char* message() const {
    return cudaGetErrorString(as_raw(value_));
  }

  // NOLINTNEXTLINE(modernize-use-nodiscard)
  bool or_throw() const {
    if (value_ != cuda_status::success)
      throw std::runtime_error{cudaGetErrorString(as_raw(value_))};
    return true;
  }
  bool operator*() const {
    or_throw();
    return true;
  }

#pragma endregion
#pragma region Helpers

  [[nodiscard]] static cudaError_t read(read_mode mode) {
    return mode == read_mode::peek
               ? cudaPeekAtLastError()
               : cudaGetLastError();
  }

  [[nodiscard]] static cudaError_t as_raw(cuda_status status) {
    return static_cast<cudaError_t>(status);
  }

#pragma endregion
#pragma region Data members
private:
  cuda_status value_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
