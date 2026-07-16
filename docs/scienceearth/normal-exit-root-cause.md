# macOS normal-exit root cause

CHANGED_OWNER_FILE=applications/earth_explorer/earth_main.cpp

## Selected evidence branch

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

## GREEN evidence

- Focused owner-order test: `osgVerse_Test_ImGuiThreading` passed after failing on the absent
  stop-before-destruction boundary.
- OSG finalization control: `osgVerse_Test_OsgApplicationUsageExit` passed.
- Repaired ASan staged bundle: status 0, no AddressSanitizer finding, no timeout, and no new matching
  `osgSol_Earth*.ips` report.
- Repaired normal staged bundle: status 0, no timeout, and `comm -13` found no newly created matching
  crash report.
- Normal staging audit: 127 Mach-O files, 127 UUID records, zero duplicate UUIDs, and zero
  non-canonical dylib install IDs.
