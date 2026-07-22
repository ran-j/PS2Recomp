# Android runner

## Requirements

- Android Studio (recommended) or a local Gradle 8.7+ / JDK 17 install
- Android SDK 34 + NDK (installed automatically by Android Studio on first sync)
- CMake 3.22.1 from the SDK (Android Studio installs it on demand)

## Building

Option A — Android Studio: open the `android/` folder and run the `app` configuration.
Option B — command line (no wrapper is committed; generate it once):

```sh
cd android
gradle wrapper --gradle-version 8.9
./gradlew assembleRelease
```

APK output: `android/app/build/outputs/apk/release/app-release.apk`.

## Running a game

Since there is no argv on Android, the guest ELF path comes from
`PS2X_DEFAULT_BOOT_ELF`, set via the `ps2xBootElf` Gradle property
(`android/gradle.properties`, or `-Pps2xBootElf=...` on the command line).


```sh
adb install app/build/outputs/apk/release/app-release.apk
adb shell mkdir -p /storage/emulated/0/Android/data/com.ps2x.runner/files
adb push "path/to/game/." /storage/emulated/0/Android/data/com.ps2x.runner/files/
```

Logs: raylib output goes to logcat (`adb logcat -s raylib`); runtime `std::cout`/`cerr`
output is not redirected to logcat yet.
