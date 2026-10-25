# Logging and sensor reliability

On PS5, llama.cpp's asynchronous log writer prints to stdout and the configured
log file. If either sink stalls, the 512-entry queue can fill and block a server
thread trying to log. `/health` can still answer while `/slots`, `/metrics`, or an
inference request waits. A healthy `/health` alone is not a readiness test.

`patches/0006-log-never-blocks.patch` backports the PS5-only queue policy from
[cobanov/PS5LM v0.9.1](https://github.com/cobanov/PS5LM/blob/88063d23500e7ea90d6a83a7156e556dc2d62b7a/patches/0008-log-never-blocks.patch)
(by @bilawalriaz). Text and JSON producers drop new entries when the queue is full;
the writer reports the number dropped once it catches up. A PS5-only follow-up
also waits for capacity before enqueueing the shutdown marker: wrapping a full
queue can otherwise make it appear empty before the writer wakes, deadlocking
flush/shutdown. Other platforms retain
their blocking behavior. Logs may be incomplete under backpressure. Explicit
flush/shutdown can still wait for a stalled sink; this change protects producers,
not every logging operation.

The app also bundles ps5-exporter v0.3.1, pinned by SHA-256 in `build-app.sh`.
This updates sensor reporting; it does not change the fan policy.

## Validation

After applying the patches to the pinned llama.cpp submodule, run:

```sh
python3 scripts/test-log-backpressure.py
```

This host-side regression test blocks a real pipe used as stdout/stderr and exercises
text and JSON logging under both the PS5 queue policy and the native policy.
It repeats the patched lifecycle 50 times per format and simulates a delayed
writer to exercise a full queue at shutdown. Negative controls reproduce both
producer blocking and the shutdown hang without the stop-slot reservation.
It does not emulate the console or replace a complete application build and hardware test.
The same logger test can also be compiled as a PS5 payload:

```sh
PS5_PAYLOAD_SDK=/absolute/path/to/ps5-payload-sdk bash scripts/build-log-test-ps5.sh
```

The build script never launches it. When explicitly run through elfldr, it tests
text and JSON logging and writes `/data/PS5LM/log-backpressure-test.jsonl`.
It refuses to overwrite an existing result; archive that file before another run.
A 20-second watchdog exits the test payload if the logger fails to recover.
The payload neither loads a model nor changes the running application or fan.
The PR was checked with official payload SDK v0.45 and LLVM 23.1.4.
On-console text and JSON cases completed with exit code 0; Plus remained healthy.
The payload repeats each case 25 times to cover queue shutdown and restart.
This validates the logger component on PS5, not a rebuilt graphical application.

The `Log reliability` GitHub Actions workflow repeats the host test on Linux and
macOS and cross-compiles the PS5 test using a hash-verified SDK.

For console validation, use one client, check `/slots` is idle before starting,
and record request success, generation throughput, `/health`, `/slots`, and
independent sensor telemetry. Compare identical prompts over time; different
prompt lengths naturally change throughput. Include streaming, tool calls,
and contexts near the configured capacity, with room for output tokens.

An external investigation of the published v0.8.4 build observed 86 successful
requests and no inference failures across about 53 minutes with logging disabled
and a 75 C automatic fan target, including a 30-minute mixed-prompt run (48
requests, mean 53.70 generated tokens/s). This is evidence for that combined
workaround, not hardware validation of this source patch or proof of a thermal
or logging root cause. Transient monitoring connection errors remained.

For existing releases, `--log-disable` in `/data/PS5LM/app-args.txt` can suppress
llama logging, but that file overrides the entire planner argument list, and
model selection can remove it. Preserve a backup and recheck after model changes.
A source build containing the queue patch does not depend on this override.
Fan targets are console preferences: the project does not impose a target.

## Fresh application builds

The original ps5-homebrew-ui repository is unavailable. The submodule URL uses
[Phononenom's mirror](https://github.com/Phononenom/ps5-homebrew-ui) at the original
pinned commit; no UI source revision changes. The original OpenGL SDK download
URL is also unavailable. Place the
[ps5-opengl SDK 1.0.0 archive](https://github.com/MyLuxy/overdrive-deps/releases/tag/ps5-opengl-v1.0.0)
in `third_party/ps5-homebrew-ui/.deps/ps5-opengl/`, named
`ps5-opengl-sdk-1.0.0.tar.gz`, then run:

```sh
export PS5_OPENGL_SDK="$(bash third_party/ps5-homebrew-ui/tools/fetch-opengl-sdk.sh)"
```

The pinned UI script verifies the archive SHA-256
`f93643c04c843d56143b00951df1f8042ea7706ae9f19e4e9abf158e1ead77c5`
and the extracted SDK manifest against their original digests. Use the normal
native build instructions in README after setting up those dependencies.
`build-app.sh` creates its output directory before redirecting CMake's configure
log, so it also works when `build/` does not yet exist.
