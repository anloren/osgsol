# macOS normal-exit root cause

CHANGED_OWNER_FILE=applications/earth_explorer/earth_exit.h

## First repair and its limit

AddressSanitizer named an application owner. The minimal
`osgVerse_Test_OsgApplicationUsageExit` control returned zero, so the basic OSG 3.6.5
`ApplicationUsage` finalizer was not the failing owner. A staged bundle audit found no duplicate
Mach-O UUIDs and no non-canonical dylib install IDs, so packaging/runtime duplication was also
excluded.

## RED evidence

- The fixed Desktop app did not implement `EARTH_AUTOQUIT_FRAMES`; the 30-second watchdog ended
  it with status 142. This proved the packaged regression was capable of rejecting an app that did
  not take the requested graceful path.
- With the inert five-frame handler present, the pre-repair ASan bundle returned from
  `viewer.run()` and then aborted with status 134. The first invalid access was a heap-use-after-free
  on graphics thread T28 in `osgVerse::ImGuiManager::getContentHandler()` via
  `ImGuiRenderCallback`.
- ASan showed that main thread T0 freed `ImGuiManager` through the function-local `osg::ref_ptr`
  after `viewer.run()` returned. OSG's graphics thread was still drawing because it was not stopped
  until the later `osgViewer::Viewer` destructor.
- The previously observed `ApplicationUsage::~ApplicationUsage()` finalization stack was therefore
  a downstream symptom, not the first invalid owner.

## Repair

`applications/earth_explorer/earth_main.cpp` now owns a `ViewerThreadStopGuard`. It is constructed
immediately before the final `return viewer.run();`, so destruction of the guard stops OSG viewer
threads before any function-local callback owners or their raw dependencies are destroyed. Panel
Quit, window close, Cmd+Q, and `EARTH_AUTOQUIT_FRAMES` continue to set viewer done and return through
`viewer.run()`; there is no forced exit, signal suppression, or early-return bypass.

The inert `AutoQuitAfterFramesHandler` is registered only when `EARTH_AUTOQUIT_FRAMES` parses to a
positive value. On its requested FRAME it calls `setDone(true)` through the current view's
`ViewerBase` and does not consume the event.

That repair was necessary, but a current-binary manual session proved that it was not sufficient.
The earlier statement that the later `ApplicationUsage` report was stale was incorrect.

## Current real-session evidence

- Incident `113A04F6-6652-4D48-8243-26867F35AB43` is from the current Desktop executable UUID
  `45af404d-b4d8-3ae1-b29d-963110499a94`, launched at 18:40:36 and crashed during normal Quit at
  18:49:23 on 2026-07-18.
- The main thread reached `exit` and crashed while `ApplicationUsage` maps were being finalized.
- Eight other threads were still blocked in `std::condition_variable::wait`, all with the same
  return address in anonymous image index 28. The report no longer listed `osgdb_verse_tms.so` as
  a loaded image.
- `osgdb_tms` was the only Earth runtime owner matching both observations exactly: its
  `LayerLoadPool` created eight workers by default, detached them, leaked the singleton, and made
  every worker wait forever on one `std::condition_variable` inside the plugin.
- The previous offscreen exit test disabled or shortened the normal tile path, so it never
  exercised plugin unload with those eight persistent workers.

## Second repair and its limit

`LayerLoadPool` is now `osgVerse::TmsLayerLoadPool`, an owned function-local object inside the TMS
plugin. Its workers remain reusable during the session, but are joinable. At plugin destruction it
sets a stop predicate, wakes every idle worker, drains any already queued work, joins all worker
threads, and only then permits `dlclose` to unload their code. The repair does not use `_Exit`, kill
signals, crash suppression, macOS setting changes, or a packaging workaround.

That worker lifetime repair remains valid, but it was not the owner of the persistent normal-exit
crash. Treating the idle workers as the cause of `ApplicationUsage` map corruption was another
incorrect conclusion.

## Decisive real-session evidence

- Incident `72E054B1-E9BB-491C-AAF1-5EA19D2AFB35` is from Desktop executable UUID
  `a06b83f4-2d14-3ea0-b012-bc62b1f9b506`, launched at 20:52:37 and closed normally at 21:06:23
  on 2026-07-18.
- The TMS repair was active and the old eight waiting worker threads were absent. The process still
  crashed after `main` returned, inside `__cxa_finalize`.
- The first invalid owner is now unambiguous: `osg::ApplicationUsage::~ApplicationUsage()` enters
  `libsystem_malloc` with an invalid map node while destroying its first reverse-order member,
  `_commandLineOptionsDefaults`. Static disassembly identifies the failing root pointer load at
  object offset `0xc8`.
- OSG 3.6.5 implements `ApplicationUsage::instance()` as a function-local `osg::ref_ptr`; upstream
  master still has the same implementation. The singleton contains process-wide usage metadata and
  has no runtime need to be destroyed after `main`.

## Current repair

On macOS, `pinApplicationUsageForProcessLifetime()` takes exactly one additional OSG reference at
the first instruction of Earth `main`. Repeated calls are idempotent. Normal panel Quit, window
close, Cmd+Q, viewer thread shutdown, plugin shutdown, and return from `main` are unchanged. During
`__cxa_finalize`, OSG releases its own reference but the usage registry is not destructed; macOS
reclaims this process-lifetime metadata with the address space. This does not use `_Exit`, signals,
crash suppression, macOS settings, or a custom allocator.

The compile-only regression checks both the one-time reference increment and idempotence. It is
intentionally not executed from Codex after the user's foreground crash warning; final acceptance
belongs to the current Desktop package in a manual session.

## Historical automated evidence (insufficient for long-session acceptance)

- Focused owner-order test: `osgVerse_Test_ImGuiThreading` passed after failing on the absent
  stop-before-destruction boundary.
- OSG finalization control: `osgVerse_Test_OsgApplicationUsageExit` passed.
- Repaired ASan staged bundle: status 0, no AddressSanitizer finding, no timeout, and no new matching
  `osgSol_Earth*.ips` report.
- Repaired normal staged bundle: status 0, no timeout, and `comm -13` found no newly created matching
  crash report.
- Normal staging audit: 127 Mach-O files, 127 UUID records, zero duplicate UUIDs, and zero
  non-canonical dylib install IDs.
- Current owner regression: `osgVerse_Test_TmsLayerLoadPool` creates eight workers, executes the
  parallel task path, shuts the pool down twice safely, verifies zero remaining owned workers, and
  verifies that post-shutdown work falls back to the caller thread.
- Production-plugin unload regression: `osgVerse_Test_TileOverlay` loads the real
  `osgdb_verse_tms` plugin against a local fixture, observes its eight production workers, releases
  the created tile, calls `Registry::closeLibrary()`, and verifies that at least eight process
  threads disappear before the plugin code is unloaded. This path uses no external network.
- The focused exit, pool, and production-plugin tests pass in the Release GLCore build used by the
  macOS application (`build/science_64d_candidate`), rather than the accidentally reconfigured
  legacy-OpenGL build directory.
