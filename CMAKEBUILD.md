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
- `line_height(unsigned)` overrides line pitch per editor; zero uses the
  original font metrics.
- `text_y_offset(int)` aligns text baselines without changing the scroll range.
- `keep_scrollbars_at_border()` keeps a settings field's scrollbars aligned
  with its outer border when custom padding is used.

Default settings preserve upstream text editor behavior. Consumers must rebuild
Nana and their application together after these header changes.

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
