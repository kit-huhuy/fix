# DecryptEngine Split Build

This workspace now has two clear layers:

- `DecryptEngineSafe.hpp` - the real safe implementation. It decodes coordinate
  data from buffers that the caller already owns. It does not use a driver.
- `DecryptEngine.hpp` - the public facade. Existing code can keep including this
  file; by default it only includes the safe backend.
- `DecryptEngineDriverBackend.hpp` - a placeholder driver backend interface. It
  compiles when explicitly included, but it does not load, extract, run, or talk
  to the kernel driver.
- `DecryptEngineSelfTest.cpp` - userspace checks for the safe API.
- `Makefile` - convenience targets for checking and running the safe self-test.
- `paradise_api.h`, `libparadise_api.a`, `driver_ko_601.zip` - preserved assets,
  not used by this safe build.

## Commands

```sh
make check-header
make run-selftest
```

Expected self-test result:

```text
DecryptEngine mode: safe-local-buffer
Kernel backend supported: no
[PASS] self_test
[PASS] result_api
[PASS] offset_api
[PASS] layout_api
[PASS] bounds_reject
[PASS] batch_api
[PASS] safe_build
[PASS] kernel_backend_disabled
DecryptEngine self-test: ok
```

## Default Safe Usage

```cpp
#include "DecryptEngine.hpp"

float raw[3] = {10.0f, 20.0f, 30.0f};

const auto result = DecryptEngine::get().decode_local(raw, sizeof(raw));
if (result) {
    DecryptedCoordinate pos = result.coordinate;
}
```

## Driver Backend Include Check

The placeholder backend is opt-in at compile time:

```sh
g++ -std=c++17 -DDECRYPT_ENGINE_INCLUDE_DRIVER_BACKEND your_file.cpp
```

Example:

```cpp
#define DECRYPT_ENGINE_INCLUDE_DRIVER_BACKEND
#include "DecryptEngine.hpp"

DecryptDriverBackend backend;
DecryptDriverBackendConfig config{};
config.device_path = "/dev/your_driver_device";

if (!backend.connect(config)) {
    const char* error = backend.last_error();
}
```

`DecryptDriverBackend::available()` currently returns `false`. To make it real,
replace the placeholder methods in `DecryptEngineDriverBackend.hpp` with code
that connects to a driver you have already started and authorized outside this
build.

## Android Safe Run Script

Use this script to build the userspace self-test for Android and run it through
`adb` when a device is connected:

```sh
./android_run_safe.sh
```

Defaults:

- `ANDROID_ABI=arm64-v8a`
- `ANDROID_API=24`
- `ANDROID_REMOTE_BIN=/data/local/tmp/DecryptEngineSelfTest`

Examples:

```sh
ANDROID_API=29 ./android_run_safe.sh
ANDROID_ABI=armeabi-v7a ANDROID_API=23 ./android_run_safe.sh
```

The script does not load, extract, or run a kernel driver. It only builds and
runs the userspace safe self-test binary.

## Auto Runtime Mode

`DecryptEngineRuntime.hpp` adds a userspace auto-select layer:

1. Try the driver backend first.
2. If the driver backend connects, `runtime.get(address, out)` uses driver mode.
3. If the driver backend is not available, safe local-buffer APIs still work.

Build and run on Android:

```sh
./android_run_auto_mode.sh
```

Pass driver settings with environment variables:

```sh
DRIVER_DEVICE=/dev/decrypt_engine \
HOOK_LITERAL=0x0 \
COORDINATE_ADDRESS=0x0 \
./android_run_auto_mode.sh
```

`android_run_auto_mode.sh` does not run `6.1.ko.sh`, extract
`driver_ko_601.zip`, or load a kernel driver. Run your driver manually first,
then use this script to run the userspace binary.

To make auto mode actually switch to driver mode, implement the real connection
and coordinate-read logic in `DecryptEngineDriverBackend.hpp`:

```cpp
static constexpr bool available() { return true; }
bool connect(const DecryptDriverBackendConfig& config);
bool ready() const;
bool get(uint64_t address, DecryptedCoordinate& out);
void disconnect();
```

Until that backend is implemented, auto mode will report driver connect failure
and continue proving that safe local-buffer decode still works.
