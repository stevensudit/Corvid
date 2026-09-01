#!/usr/bin/env python3
# reflow_comments.py - find and fix misflowed // comment paragraphs.
#
# clang-format (ReflowComments) only SPLITS over-long comment lines; it never
# re-joins short ones. So any edit that lengthens or shortens words mid-
# paragraph (a rename, a rewording) leaves ragged paragraphs behind: a line
# far short of the column limit followed by continuation text that belongs on
# it. This script detects those paragraphs and greedily refills them to 79
# columns, which clang-format then leaves stable.
#
# Detection: a paragraph is misflowed when some line other than its last is
# under 65 columns AND could absorb the next line's first word within 79.
# The 65-column slack matters: author-typed text that clang-format wrapped is
# already near-greedy, and a strict greedy test would flag nearly every
# committed paragraph in the repo (including the license header).
#
# What is NOT touched (each guard was earned by a bad refill in dry-run):
#  - the license header (first 17 lines of every file);
#  - fenced ``` blocks (verbatim quotes of compiler output etc.);
#  - paragraphs containing runs of multiple spaces (quoted examples where
#    whitespace is significant, e.g. dedent/fill examples in textwrap.h);
#  - indented continuations, bullets (-, *), numbered list items (1. );
#  - NOLINT and clang-format directive lines;
#  - TODO/FIXME lines (they start their own paragraph);
#  - heading lines ending with ':' (e.g. "Overview:") stand alone, and a
#    continuation line opening with a short "Label: " (FIFO:, Ascending:,
#    Predicate shape:) starts a new paragraph, so deliberate vertical label
#    lists stay vertical;
#  - paragraphs listed in SKIP below (path:firstline keys; these go stale as
#    line numbers shift, so prune entries once fixed or obsolete).
#
# Usage:
#   python scripts/reflow_comments.py [paths...]          # dry run (default)
#   python scripts/reflow_comments.py [paths...] --apply  # rewrite files
#
# Paths may be files or directories (searched recursively for .h/.cuh/.cpp/
# .cu); the default is corvid/. Dry-run prints each flagged paragraph as
# OLD/NEW lines for review. Run format_all and the test suite after --apply.

import glob
import os
import re
import sys
import textwrap

APPLY = "--apply" in sys.argv
ROOTS = [a for a in sys.argv[1:] if not a.startswith("--")] or ["corvid"]
EXTS = (".h", ".cuh", ".cpp", ".cu")
LICENSE_LINES = 17
LIMIT = 79
RAGGED_BELOW = 65

# Known-bad paragraphs, keyed by forward-slash path and first line number.
SKIP = {
    "corvid/lang/ast_pred.h:510",  # colon-less heading "Distribute OR over AND"
}

pat = re.compile(r"^(\s*)// (.*\S)\s*$")
item = re.compile(r"\d+\. ")
label = re.compile(r"\S[^:]{0,29}: ")


def breaks_para(c):
    return (c.startswith(("```", " ", "-", "* ", "clang-format", "TODO",
                          "FIXME")) or "NOLINT" in c or item.match(c))


def paragraphs(lines):
    """Yield (start, end, indent, content) for prose comment paragraphs."""
    in_fence = False
    i = LICENSE_LINES
    while i < len(lines):
        m = pat.match(lines[i])
        c = m.group(2) if m else None
        if c is not None and c.startswith("```"):
            in_fence = not in_fence
            i += 1
            continue
        if in_fence or m is None or breaks_para(c):
            i += 1
            continue
        indent = m.group(1)
        start = i
        content = [c]
        i += 1
        if not c.endswith(":"):  # a heading/introducer stands alone
            while i < len(lines):
                m2 = pat.match(lines[i])
                if not m2 or m2.group(1) != indent:
                    break
                c2 = m2.group(2)
                if breaks_para(c2) or label.match(c2):
                    break
                content.append(c2)
                i += 1
                if c2.endswith(":"):  # introducer ends the unit
                    break
        yield start, i, indent, content


def collect_files():
    files = []
    for root in ROOTS:
        if os.path.isfile(root):
            files.append(root)
        else:
            for ext in EXTS:
                files.extend(
                    glob.glob(os.path.join(root, "**", "*" + ext),
                              recursive=True))
    return sorted(set(files))


def main():
    total = skipped = 0
    for path in collect_files():
        lines = open(path, encoding="utf-8").read().splitlines()
        out = lines[:]
        hits = []
        for start, end, indent, content in reversed(list(paragraphs(lines))):
            room = len(indent) + 3  # the "// " prefix
            flagged = any(
                room + len(content[k]) < RAGGED_BELOW and
                room + len(content[k]) + 1 + len(content[k + 1].split()[0])
                <= LIMIT for k in range(len(content) - 1))
            if not flagged:
                continue
            key = f"{path.replace(os.sep, '/')}:{start + 1}"
            if any("  " in c for c in content) or key in SKIP:
                skipped += 1
                if not APPLY:
                    print(f"SKIPPED {key}")
                continue
            refilled = [
                f"{indent}// {l}" for l in textwrap.wrap(
                    " ".join(" ".join(content).split()),
                    width=LIMIT - room,
                    break_long_words=False,
                    break_on_hyphens=False)
            ]
            if refilled == out[start:end]:
                continue
            hits.append((start, end, out[start:end], refilled))
            out[start:end] = refilled
        if not hits:
            continue
        total += len(hits)
        for start, end, old, new in reversed(hits):
            print(f"=== {path}:{start + 1}-{end}")
            for l in old:
                print("OLD:", l)
            for l in new:
                print("NEW:", l)
            print()
        if APPLY:
            open(path, "w", encoding="utf-8",
                 newline="").write("\n".join(out) + "\n")
    print(f"{'applied' if APPLY else 'flagged'} {total} paragraphs, "
          f"skipped {skipped}")


if __name__ == "__main__":
    main()
