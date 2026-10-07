# CMakeBuild Nana extensions

This branch is based on the official Nana `develop-1.8` snapshot
`efa10557660b6454fc70cbb7f5a64796b1feb882` used by CMakeBuild.
Nana retains its Boost Software License and its upstream version number.

The text editor extensions are the complete library changes previously applied
by the consumer's `cmake/nana.cmake`:

- `restore_content_origin(point)` restores the viewport after appending or
  pruning a read-only journal, without moving the caret or selection. Both
  scrollbar values are synchronized with the clamped viewport, so subsequent
  wheel, arrow and thumb input continues from the restored position.
- `content_coordinates(upoint)` and `content_anchor()` preserve the visible
  text anchor when the consumer removes an old prefix.
- `scroll_space(unsigned)` scales scrollbar width per editor.
- `scroll_corner_color(color)` colors the scrollbar intersection without
  changing text metrics, padding or viewport. Its default is Nana's original
  `button_face`; an unchanged color is a no-op and updates respect batching.
- `line_height(unsigned)` overrides line pitch per editor; zero uses the
  original font metrics.
- `text_y_offset(int)` aligns text baselines without changing the scroll range.
- `keep_scrollbars_at_border()` keeps a settings field's scrollbars aligned
  with its outer border when custom padding is used.

Default settings preserve upstream text editor behavior. Consumers must rebuild
Nana and their application together after these header changes.

When both scrollbars are visible, their lengths end at their shared corner even
when asymmetric padding offsets them independently. Single-scrollbar placement,
text bounds and scroll ranges retain their existing behavior.

## Batched widget updates

`api::batch_updates(window, action)` keeps changes to widget buffers inside the
window's root pending until the synchronous action finishes. A journal can
append or prune text and restore its viewport, caret and selection before any
intermediate scrollbar or text position is presented. Ordinary `refresh_window`
and `update_window` calls retain their buffer updates; Nana presents the final
accumulated result when the owning batch finishes. Nested batches leave an
already active outer batch and its pending requests intact.

Call it on the owning GUI thread. The action must not pump events, enter a
modal loop, or force an immediate update with `update_window(window, true)`.
`refresh_window_tree` also composes the tree immediately; use it only after
all changes are complete, or outside a batch.
Empty actions and invalid windows are ignored. If the action throws, the batch
restores normal updates, flushes and clears pending requests when the root still
exists, then rethrows the original exception. A flush failure also clears pending
requests and propagates unless an original action exception takes priority.
Each root has a separate batch depth and flush guard. Synchronous native events
cannot drain an action that has not finished yet. Batches started by drawing callbacks join
that flush; they cannot recursively drain or clear its queue. Current, completed
and pending requests remain visible to the usual ancestor deduplication. Each
requester is processed at most once per flush, so mutually refreshing transparent
widgets cannot keep the drain running indefinitely. Newly requested independent
widgets are drained before returning. Iterators never survive a paint callback.
Nested native event guards preserve the outer lazy-update state and queue.
The queue and flush guard are cleared on success and on exceptions; a later
independent batch can refresh the same widgets again. As with Nana's original
deduplication, this is not a fixed-point solver for arbitrary drawing callbacks
that repeatedly change already painted widgets.

## Menu measurements

Popup menus cache their measured client size across hover and keyboard redraws.
Changing text, items, renderer, font, DPI, borders or size limits invalidates
the cache. Placement and monitor work-area checks still run on every refresh,
so the optimization does not pin a menu to stale screen coordinates.

## UTF-8 captions

`is_utf8` accepts all Unicode scalar values through U+10FFFF, including
four-byte emoji, while rejecting truncated sequences, invalid continuation
bytes, overlong encodings, surrogate values and values above the Unicode limit.
Empty views and embedded NUL bytes are handled by their explicit length.
The UTF-8/UTF-16 converters retain supplementary characters and the text after
them, including the U+10000 offset when decoding little-endian surrogate pairs.
This keeps narrow caption measurements consistent with their complete wide
text. The converters' legacy malformed-input policy is otherwise unchanged.

## Repeated font assignments

`api::typeface(window, font)` skips font notifications and repainting when the
widget already uses the same resolved font. Nana's font cache returns the same
font identity for equivalent family, size, style and pixel-height requests, so
recreating an equivalent font during layout also preserves cached text metrics.
An actual font change retains the original notification and refresh behavior,
including recalculation of text-editor line widths and scrollbar ranges.
`graphics::typeface(font)` likewise avoids selecting and measuring an unchanged
resolved font during repeated drawing. It still records the font shadow for
empty graphics, and an empty font retains the existing realized font as before.

## Unwrapped caret coordinates

The editor computes the vertical content coordinate of an unwrapped caret
directly from its logical row. Each unwrapped logical row is one visual line,
so this avoids walking every preceding row on each caret reset or anchor query.
Wrapped text keeps the original visual-line summation. Horizontal shaping,
selection, viewport restoration and maximum-line width semantics are unchanged.

## Per-monitor DPI integration

CMakeBuild uses custom physical-pixel layouts, so it owns DPI changes for its
panel and settings windows. Its platform module consumes `WM_DPICHANGED` before
Nana's default handler changes the window and font buffers. It applies the
message's DPI before resizing, updates the HWND and Nana layout synchronously,
then saves logical dimensions. Active dragging is rebased to the new window
rectangle. This also avoids applying desktop coordinates through Nana's
owner-relative `form::move` API.

The consumer checks repeated native DPI messages for 100/125/150/200 percent,
HWND/Nana size agreement, fonts, focus, selection and available physical
monitors. Offscreen visual previews are also compared with its FLTK interface.
The focus/hover Nimbus effect is disabled explicitly on its read-only journal.

## Windows capture cancellation protection

Releasing a widget's capture only calls `ReleaseCapture` when its native window
still owns Windows capture. A consumer can therefore clear Nana's capture state
after `WM_CAPTURECHANGED` without releasing capture acquired by another window.

## Incremental unwrapped text insertion

`text_editor::put(std::wstring)` reuses the line sections already updated by
selection removal and insertion. Previously it immediately measured every
stored line a second time, making a journal with repeated append progressively
more expensive. Unwrapped insertion now recalculates the content/scroll range
without repeating that full document measurement. Wrapped insertion retains
the full pass because scrollbar visibility can change the available line width.
Caption replacement, undo/redo, font and DPI changes, loading, and wrap-mode
changes retain their existing complete recalculations. The public API and
horizontal range semantics are unchanged.

## Deferred GUI cleanup

`at_safe_place` captures the window's owning thread when the action is queued.
Pending actions no longer retain a `basic_window*` which a nested modal loop
may delete before dispatch. Destroy handlers can therefore finish cleanup even
after the original window has been collected.

Each thread's queue is drained in FIFO order, removing one action before it is
called. No table iterators survive a callback, and remaining actions stay
available to nested event loops. Actions queued by a callback are also drained
before returning. The Windows modal loop dispatches these actions before
collecting deleted windows, matching the other event loop branches.

## Batching regression checks

The Windows workflow builds this checkout with MSVC and C++23, then runs the
standalone scenarios in `tests/batch_updates`. They exercise actual offscreen
widgets, bounded mutual refresh, same-root and cross-root nesting, exceptions,
native event batching and a subsequent independent update. Run locally with:

```powershell
cmake -S tests/batch_updates -B build/batch-updates -G "Visual Studio 17 2022" -A x64
cmake --build build/batch-updates --config Release --parallel 2
ctest --test-dir build/batch-updates -C Release --output-on-failure
```

`.gitattributes` keeps text files in LF and Visual Studio project/solution files
and BAT scripts in CRLF in working copies. Git stores normalized text as LF.
