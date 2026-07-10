# Wave 0 Optional Runtime Support — 2026-07-10

| Runtime | Configure evidence | Wave 0 state | Findings |
|---|---|---|---|
| ONNX Runtime | include and lib paths are `NOTFOUND` | Explicitly disabled and unsupported in this macOS artifact | C03, C04 remain open for a dependency-enabled job |
| Bullet | include and lib paths are `NOTFOUND` | Explicitly disabled; `PhysicsEngine.h` is not installed | C10 remains open for a dependency-enabled job |

Reproduction flags:

```bash
-DONNXRUNTIME_FEATURE_ENABLED=OFF -DBULLET_FEATURE_ENABLED=OFF
```

Reproduce the unsupported artifact surface with a fresh install prefix:

```bash
cmake -S . -B build/optional_probe \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/optional_sdk" \
  -DOSG_ROOT="$PWD/build/sdk_core" \
  -DVERSE_BUILD_EXAMPLES=OFF \
  -DONNXRUNTIME_FEATURE_ENABLED=OFF \
  -DBULLET_FEATURE_ENABLED=OFF
cmake --build build/optional_probe --target install -j2
test ! -e build/optional_sdk/include/osgVerse/ai/OnnxRuntimeEngine.h
test ! -e build/optional_sdk/include/osgVerse/animation/PhysicsEngine.h
```

Wave 0 does not claim that C03, C04, or C10 is fixed. Those findings require a separate
dependency-enabled build and runtime test environment.
