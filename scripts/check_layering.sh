#!/usr/bin/env bash
# check_layering.sh: enforce the core/utils band layering described in
# corvid/deps.md.
#
# For every library header under corvid/, each local `#include "..."` is
# resolved to a target header, both source and target are mapped to a band by
# their folder path, and the edge is checked against the allow-list below. The
# check is deliberately crude: it inspects direct edges only (sufficient by
# transitivity, since the in-band property is transitive) and treats the apex
# bands (ecs, proto, lang, sim) as permitted to depend on anything lower.
#
# Subsystem umbrella headers ("../enums.h", "../strings.h", "../containers.h",
# etc.) map to the band `umbrella` and are rejected from any non-apex band,
# which keeps the de-umbrella work from regressing. The `corvid/meta.h` and
# `corvid/infra.h` umbrellas are the exception: they aggregate the foundation,
# so they map to `meta`/`infra` and stay cheap to depend on.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORVID="$ROOT/corvid"

# Map a corvid-relative path to its band. Order matters: specific before
# general.
band_of() {
  case "$1" in
  meta.h | meta/*) echo meta ;;
  infra.h | infra/*) echo infra ;;
  strings/core/*) echo strings/core ;;
  strings/utils/*) echo strings/utils ;;
  containers/core/*) echo containers/core ;;
  containers/utils/*) echo containers/utils ;;
  enums/*) echo enums ;;
  filesys/*) echo filesys ;;
  concurrency/*) echo concurrency ;;
  ecs/*) echo ecs ;;
  proto/*) echo proto ;;
  lang/*) echo lang ;;
  sim/*) echo sim ;;
  controllers.h | controllers/*) echo controllers ;;
  *.h) echo umbrella ;; # any other top-level corvid/<sub>.h
  *) echo external ;;
  esac
}

# Return 0 if a source band may depend on a destination band.
allowed() {
  local src="$1" dst="$2"
  # A band may always include its own siblings.
  [ "$src" = "$dst" ] && return 0
  # Apex bands may depend on anything lower (including umbrellas).
  case "$src" in ecs | proto | lang | sim) return 0 ;; esac
  # meta is the universal foundation.
  [ "$dst" = meta ] && return 0
  case "$src=>$dst" in
  infra'=>'meta) return 0 ;;
  strings/core'=>'meta) return 0 ;;
  containers/core'=>'infra) return 0 ;;
  enums'=>'strings/core | enums'=>'containers/core) return 0 ;;
  strings/utils'=>'strings/core | strings/utils'=>'enums) return 0 ;;
  filesys'=>'strings/core | filesys'=>'strings/utils | filesys'=>'enums) return 0 ;;
  concurrency'=>'infra | concurrency'=>'filesys) return 0 ;;
  containers/utils'=>'infra | containers/utils'=>'strings/core | \
    containers/utils'=>'strings/utils | containers/utils'=>'enums | \
    containers/utils'=>'containers/core | containers/utils'=>'concurrency)
    return 0
    ;;
  esac
  return 1
}

violations="$(mktemp)"
trap 'rm -f "$violations"' EXIT

while IFS= read -r -d '' file; do
  rel="${file#"$CORVID"/}"
  src_band="$(band_of "$rel")"
  # Top-level umbrellas are consumer aggregators, not layered headers.
  [ "$src_band" = umbrella ] && continue
  [ "$src_band" = external ] && continue
  dir="$(dirname "$file")"
  while IFS= read -r inc; do
    [ -z "$inc" ] && continue
    target="$(realpath -m "$dir/$inc")"
    case "$target" in
    "$CORVID"/*) ;;
    *) continue ;; # outside corvid (3rd-party / unexpected)
    esac
    trel="${target#"$CORVID"/}"
    dst_band="$(band_of "$trel")"
    [ "$dst_band" = external ] && continue
    if ! allowed "$src_band" "$dst_band"; then
      printf '  %s (%s)\n    -> %s (%s)\n' \
        "$rel" "$src_band" "$inc" "$dst_band" >>"$violations"
    fi
  done < <(grep -oE '#[[:space:]]*include[[:space:]]*"[^"]+"' "$file" |
    sed -E 's/.*"([^"]+)".*/\1/')
done < <(find "$CORVID" -name '*.h' -print0)

if [ -s "$violations" ]; then
  n="$(($(grep -c '^  [^ ]' "$violations")))"
  echo "Layering check FAILED: $n disallowed include edge(s):"
  cat "$violations"
  echo
  echo "See corvid/deps.md for the band allow-list."
  exit 1
fi

echo "Layering check passed."
