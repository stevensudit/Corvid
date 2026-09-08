# Containers roadmap

Forward work for `corvid/containers`. Rulings and finished items belong in
the headers and the git log, not here.

## Deferred

Ideas raised 2026-09-08 during the LLM quest's stage 3, parked so they do not
hold up the forward pass. Each carries the assessment made when it was
raised, so the tradeoffs do not have to be rediscovered.

### `enum_span`: a `Stride` parameter

Add `size_t Stride`, defaulting to 1, or `std::dynamic_extent` for a stride
fixed at construction. When the stride is not 1, logical element `i` lives at
underlying index `i * Stride`, `size()` is the underlying size divided by the
stride (rounded up), and `first`, `last`, and `subspan` work in logical
elements. This is what a column view of a row-major matrix needs.

Assessment: a stride of 1 keeps the class a pure pass-through, but any other
stride ends contiguity, so `data()` stops meaning "the elements", iteration
has to go through `std::views::stride`, and there is no flat span of the
logical elements. That is a second set of invariants under one name.

Direction settled 2026-09-08: `strided_span` is its own class, and
`enum_span` is parameterized on the span it wraps, `std::span` or
`strided_span`. That separates the two concerns, striding and enum-as-index,
so each can be used alone, while the pairing gives the column view. A strided
span is contiguous with skips, so it is still fairly called a span and can
offer everything a span offers, subspans included. A runtime stride is set at
construction only, since changing it under a live view changes what every
index means.

### `matrix_view`: static extent and stride

Specialize on `matrix_extent Extent` and `size_t Stride`, both defaulting to
dynamic, with each of the three counts independently static or dynamic. When
dynamic, today's behavior persists.

Assessment: the payoff is rows that come back as fixed-size spans, so loops
over a 768-wide feature row have a compile-time trip count, and a packed
stride that folds into the indexing. The sequence length stays dynamic, so
the useful combination is static columns and stride with dynamic rows, and
that is the agreed starting point: making all three independent is where the
machinery grows (conditional storage for the dynamic counts, subview return
types per combination, static-to-dynamic conversions). Worth doing when the
CPU reference is measured to need it. The device pass takes its constants
from kernel template parameters, not from the view.

### `matrix_view`: subscript operators for spans and subviews

Expose `row_as_span` as `operator[](row_ndx)`, add `col_as_span` on top of a
strided `enum_span` and expose it as `operator[](col_ndx)`, and expose the
two `subview` overloads as `operator[](coord, coord)` and
`operator[](coord, matrix_extent)`.

The case for it: the operator's one job is to select part of the matrix.
Row and column together select a cell, a row alone selects all of its
columns, a column alone all of its rows, and two coordinates select the
rectangle they cut out. The coordinate-plus-extent form is harder to justify
on those terms and may stay named only.

Agreed shape: every operator is a thin wrapper over a named method that has
the fuller interface, so `operator[](row_ndx)` calls `row_as_span` with its
column defaults, and supporting `operator[](col_ndx)` means adding
`col_as_span`, which waits on the strided span. Open concern to weigh at the
time: `m[a, b]` is then an element or a rectangle depending on the argument
types, which a cold read cannot tell apart.
