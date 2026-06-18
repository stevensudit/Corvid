// Unit test for corvid::cuda::cuda_device_attr (corvid/cuda/cuda_device.cuh):
// exercises the sparse sequence-enum registration. Walks the whole [1, 151]
// value range to confirm enum<->string round-trips for every value, checks
// that named values are pinned to the matching CUDA constants (with anchors
// straddling each gap so a systematic shift cannot hide), and that reserved or
// unassigned slots stringify to their number rather than a name.

#include <string>
#include <string_view>

#include <cuda_runtime.h>

#include "corvid/enums/enum_conversion.h"
#include "corvid/cuda/cuda_device.cuh"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::enums::sequence;
using corvid::cuda::cuda_device_attr;

namespace {

struct attr_case {
  cuda_device_attr value;
  int code;
  std::string_view name;
};

// Anchors the names to CUDA's own numbering. `code` is the CUDA constant
// itself, so each row guards against the enumerator drifting off its value.
// The pairs straddle every gap (the value just before and just after each
// reserved or unassigned run), since a whole-table shift would otherwise
// survive the self-consistent round-trip below.
constexpr attr_case attr_cases[] = {
    {cuda_device_attr::max_threads_per_block, cudaDevAttrMaxThreadsPerBlock,
        "max_threads_per_block"},
    {cuda_device_attr::max_texture1d_layered_layers,
        cudaDevAttrMaxTexture1DLayeredLayers, "max_texture1d_layered_layers"},
    {cuda_device_attr::max_texture2d_gather_width,
        cudaDevAttrMaxTexture2DGatherWidth, "max_texture2d_gather_width"},
    {cuda_device_attr::can_use_host_pointer_for_registered_mem,
        cudaDevAttrCanUseHostPointerForRegisteredMem,
        "can_use_host_pointer_for_registered_mem"},
    {cuda_device_attr::cooperative_launch, cudaDevAttrCooperativeLaunch,
        "cooperative_launch"},
    {cuda_device_attr::direct_managed_mem_access_from_host,
        cudaDevAttrDirectManagedMemAccessFromHost,
        "direct_managed_mem_access_from_host"},
    {cuda_device_attr::max_blocks_per_multiprocessor,
        cudaDevAttrMaxBlocksPerMultiprocessor,
        "max_blocks_per_multiprocessor"},
    {cuda_device_attr::max_access_policy_window_size,
        cudaDevAttrMaxAccessPolicyWindowSize, "max_access_policy_window_size"},
    {cuda_device_attr::reserved_shared_memory_per_block,
        cudaDevAttrReservedSharedMemoryPerBlock,
        "reserved_shared_memory_per_block"},
    {cuda_device_attr::deferred_mapping_cuda_array_supported,
        cudaDevAttrDeferredMappingCudaArraySupported,
        "deferred_mapping_cuda_array_supported"},
    {cuda_device_attr::ipc_event_support, cudaDevAttrIpcEventSupport,
        "ipc_event_support"},
    {cuda_device_attr::mem_sync_domain_count, cudaDevAttrMemSyncDomainCount,
        "mem_sync_domain_count"},
    {cuda_device_attr::numa_config, cudaDevAttrNumaConfig, "numa_config"},
    {cuda_device_attr::numa_id, cudaDevAttrNumaId, "numa_id"},
    {cuda_device_attr::mps_enabled, cudaDevAttrMpsEnabled, "mps_enabled"},
    {cuda_device_attr::d3d12_cig_supported, cudaDevAttrD3D12CigSupported,
        "d3d12_cig_supported"},
    {cuda_device_attr::vulkan_cig_supported, cudaDevAttrVulkanCigSupported,
        "vulkan_cig_supported"},
    {cuda_device_attr::gpu_pci_subsystem_id, cudaDevAttrGpuPciSubsystemId,
        "gpu_pci_subsystem_id"},
    {cuda_device_attr::host_numa_memory_pools_supported,
        cudaDevAttrHostNumaMemoryPoolsSupported,
        "host_numa_memory_pools_supported"},
    {cuda_device_attr::host_memory_pools_supported,
        cudaDevAttrHostMemoryPoolsSupported, "host_memory_pools_supported"},
    {cuda_device_attr::only_partial_host_native_atomic_supported,
        cudaDevAttrOnlyPartialHostNativeAtomicSupported,
        "only_partial_host_native_atomic_supported"},
    {cuda_device_attr::atomic_reduction_supported,
        cudaDevAttrAtomicReductionSupported, "atomic_reduction_supported"},
    {cuda_device_attr::cig_streams_supported, cudaDevAttrCigStreamsSupported,
        "cig_streams_supported"},
};

} // namespace

TEST_CASE("cuda_device_attr derived range", "[cuda][enums]") {
  static_assert(*min_value<cuda_device_attr>() == 1);
  static_assert(*max_value<cuda_device_attr>() == 151);
  static_assert(range_length<cuda_device_attr>() == 151);
}

TEST_CASE("cuda_device_attr anchors wrap the matching CUDA constants",
    "[cuda][enums]") {
  for (const auto& c : attr_cases) {
    CAPTURE(c.name);
    CHECK(*c.value == c.code);                // underlying value
    CHECK(enum_as_string(c.value) == c.name); // enum -> string
    cuda_device_attr parsed{};
    CHECK(convert_enum(parsed, c.name)); // string -> enum
    CHECK(parsed == c.value);
  }
}

TEST_CASE("cuda_device_attr comprehensive round-trip over the whole range",
    "[cuda][enums]") {
  // Every value in [1, 151] round-trips through its string form, whether that
  // form is a name or a number. Named values also resolve by name (names only,
  // no numeric text); unnamed slots stringify to their number and are never
  // matched as a name.
  size_t named_count = 0;
  for (int n = *min_value<cuda_device_attr>();
      n <= *max_value<cuda_device_attr>(); ++n)
  {
    CAPTURE(n);
    const auto value = static_cast<cuda_device_attr>(n);
    const auto name = enum_as_view(value);
    const auto str = enum_as_string(value);

    cuda_device_attr parsed{};
    REQUIRE(convert_enum(parsed, str));
    CHECK(parsed == value);

    if (!name.empty()) {
      ++named_count;
      CHECK(name == str); // canonical name is what stringifies
      CHECK(*enum_find_by_name<cuda_device_attr>(str) == value);
    } else {
      CHECK(str == std::to_string(n)); // unnamed prints its number
      CHECK(!enum_find_by_name<cuda_device_attr>(str)); // number is not a name
    }
  }
  // 126 named attributes; the remaining 25 slots in the range are reserved or
  // unassigned in CUDA's numbering.
  CHECK(named_count == 126);
}

TEST_CASE("cuda_device_attr gap and unknown handling", "[cuda][enums]") {
  // 44 is a valid-but-unnamed value (the lone gap after
  // max_texture1d_layered_layers); it stringifies to its number.
  CHECK(enum_as_string(cuda_device_attr{44}) == "44");
  // 92 sits in a reserved run; it too stringifies to its number.
  CHECK(enum_as_string(cuda_device_attr{92}) == "92");

  // An unknown name does not parse.
  cuda_device_attr unused{};
  CHECK_FALSE(convert_enum(unused, "not_a_real_attr"));
}
