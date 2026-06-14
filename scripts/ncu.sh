#!/bin/bash

# Profile a CUDA sample's own kernels with Nsight Compute.
#
#   scripts/ncu.sh <sample> [ncu options...]
#
# <sample> names a binary built by cleanbuild.sh (tests/build/release_bin),
# with the usual shorthand: `matmul`, `notest_cuda_matmul`, and
# `notest_cuda_matmul.cu` all resolve to the same binary. Build first:
#
#   ./cleanbuild.sh notest_cuda_matmul.cu
#
# By default ncu profiles EVERY kernel launch, including library internals
# (e.g. cuBLAS sgemm) that you usually don't care about and that make full-set
# profiling slow. This script instead discovers the kernels embedded in the
# executable itself (yours; library kernels live in their own .so) via
# cuobjdump and filters to those. Pass your own --kernel-name/-k to override
# the filter, and any other ncu options (e.g. --set full, -o report, -c 1)
# after the sample name; they are forwarded as given.
#
# The console output is also written to tests/build/ncu-<stem>.log (mirroring
# how `cleanbuild.sh tidy` tees its output), so a long profile is scrollable
# after the fact.

set -e

root="$(cd "$(dirname "$0")/.." && pwd)"
binDir="$root/tests/build/release_bin"

arg="$1"
if [[ -z "$arg" ]]; then
  echo "usage: $0 <sample> [ncu options...]" >&2
  exit 2
fi
shift

stem="$(basename "${arg%.cu}")"
bin=""
for cand in "$arg" "$binDir/$stem" "$binDir/notest_cuda_$stem"; do
  if [[ -f "$cand" && -x "$cand" ]]; then
    bin="$cand"
    break
  fi
done
if [[ -z "$bin" ]]; then
  echo "$0: no binary found for '$arg' in $binDir" >&2
  echo "build it first: ./cleanbuild.sh $stem.cu" >&2
  exit 1
fi

# Filter to the binary's own kernels unless the caller supplied a filter.
filter=()
case " $* " in
*" --kernel-name"* | *" -k "*) ;;
*)
  kernels="$(cuobjdump --dump-elf-symbols "$bin" |
    grep STT_FUNC | grep STO_ENTRY | awk '{print $NF}' | sort -u |
    c++filt | sed 's/(.*//' | paste -sd'|')"
  if [[ -z "$kernels" ]]; then
    echo "$0: no kernels found in $bin (not a CUDA binary?)" >&2
    exit 1
  fi
  echo "Profiling kernels: ${kernels//|/, }"
  filter=(--kernel-name "regex:^(${kernels})$")
  ;;
esac

# Tee to a log alongside the build dir (gitignored), and preserve ncu's exit
# status rather than tee's. PIPESTATUS[0] is the ncu side of the pipe.
log="$root/tests/build/ncu-$(basename "$bin").log"
ncu "${filter[@]}" "$@" "$bin" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
echo "Full log: $log"
exit "$status"
