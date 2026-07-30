# check_layering.ps1: enforce the core/utils band layering described in
# corvid/deps.md. Windows PowerShell counterpart of check_layering.sh.
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
# which keeps the de-umbrella work from regressing. The `corvid/meta.h`,
# `corvid/infra.h`, and `corvid/math.h` umbrellas are the exception: they
# aggregate the foundation, so they map to `meta`/`infra`/`math` and stay cheap
# to depend on.
#
# The band map and allow-list below mirror check_layering.sh clause for clause;
# a change to one belongs in the other (and in corvid/deps.md).

$ErrorActionPreference = 'Stop'

$corvid = [IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) 'corvid'))

# Map a corvid-relative path to its band. Order matters: specific before
# general.
function Get-Band([string] $rel) {
  switch -Wildcard ($rel) {
    'meta.h' { return 'meta' }
    'meta/*' { return 'meta' }
    'infra.h' { return 'infra' }
    'infra/*' { return 'infra' }
    'math.h' { return 'math' }
    'math/*' { return 'math' }
    'strings/*' { return 'strings' }
    'containers/core/*' { return 'containers/core' }
    'containers/utils/*' { return 'containers/utils' }
    'enums/*' { return 'enums' }
    'filesys/*' { return 'filesys' }
    'concurrency/*' { return 'concurrency' }
    'ecs/*' { return 'ecs' }
    'proto/*' { return 'proto' }
    'lang/*' { return 'lang' }
    'sim/*' { return 'sim' }
    'controllers.h' { return 'controllers' }
    'controllers/*' { return 'controllers' }
    '*.h' { return 'umbrella' } # any other top-level corvid/<sub>.h
    default { return 'external' }
  }
}

# Return true if a source band may depend on a destination band.
function Test-BandEdge([string] $src, [string] $dst) {
  # A band may always include its own siblings.
  if ($src -eq $dst) { return $true }
  # controllers is a standalone leaf (std only): no cross-band edges, not
  # even the otherwise-universal meta. Reject before the meta shortcut.
  if ($src -eq 'controllers') { return $false }
  # meta is the bottom of the graph: std and its own siblings only. Rejecting
  # it as a source is what keeps the two universally-dependable destinations
  # below from admitting a meta <-> math cycle.
  if ($src -eq 'meta') { return $false }
  # Apex bands may depend on anything lower (including umbrellas).
  if ($src -in 'ecs', 'proto', 'lang', 'sim') { return $true }
  # meta is the universal foundation.
  if ($dst -eq 'meta') { return $true }
  # math is a std-only foundation too, universally dependable like meta.
  if ($dst -eq 'math') { return $true }
  return "$src=>$dst" -in @(
    'infra=>meta'
    'strings=>meta'
    'containers/core=>infra'
    'enums=>strings', 'enums=>containers/core'
    'filesys=>strings', 'filesys=>enums'
    'concurrency=>infra', 'concurrency=>filesys'
    'containers/utils=>infra', 'containers/utils=>strings'
    'containers/utils=>enums', 'containers/utils=>containers/core'
    'containers/utils=>concurrency'
  )
}

$violations = [Collections.Generic.List[string]]::new()

foreach ($file in Get-ChildItem -Path $corvid -Recurse -File -Filter '*.h') {
  $rel = $file.FullName.Substring($corvid.Length + 1) -replace '\\', '/'
  $srcBand = Get-Band $rel
  # Top-level umbrellas are consumer aggregators, not layered headers.
  if ($srcBand -eq 'umbrella' -or $srcBand -eq 'external') { continue }
  $text = Get-Content $file.FullName -Raw
  foreach ($m in [regex]::Matches($text, '#[ \t]*include[ \t]*"([^"]+)"')) {
    $inc = $m.Groups[1].Value
    $target = [IO.Path]::GetFullPath((Join-Path $file.DirectoryName $inc))
    # Outside corvid (3rd-party / unexpected).
    if (-not $target.StartsWith("$corvid\", [StringComparison]::OrdinalIgnoreCase)) { continue }
    $trel = $target.Substring($corvid.Length + 1) -replace '\\', '/'
    $dstBand = Get-Band $trel
    if ($dstBand -eq 'external') { continue }
    if (-not (Test-BandEdge $srcBand $dstBand)) {
      $violations.Add("  $rel ($srcBand)`n    -> $inc ($dstBand)")
    }
  }
}

if ($violations.Count) {
  Write-Host "Layering check FAILED: $($violations.Count) disallowed include edge(s):"
  $violations | ForEach-Object { Write-Host $_ }
  Write-Host ''
  Write-Host 'See corvid/deps.md for the band allow-list.'
  exit 1
}

Write-Host 'Layering check passed.'
exit 0
