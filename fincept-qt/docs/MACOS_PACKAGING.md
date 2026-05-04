# macOS Packaging & Runtime Notes

This document captures the requirements for producing a standalone, runnable
`FinceptTerminal.app` on macOS, plus the failure modes you'll see if any step is
skipped. It was written after a 2026-05-05 incident where a build-only bundle
both failed to surface its window and produced a misleading "Network error"
during sign-up.

## TL;DR

- Configure with `-DDEPLOY_QT=ON`. The default OFF produces an unbundled,
  unrunnable `.app`.
- macdeployqt does **not** copy non-Qt dylibs. Homebrew `openssl@3` libraries
  must be bundled separately, otherwise HTTPS calls silently fail.
- Any time `install_name_tool` rewrites a binary or dylib, the existing ad-hoc
  signature is invalidated and macOS will SIGKILL the process. Always re-sign.
- The HTTP client must **not** set `Accept-Encoding` manually. See
  `src/network/http/HttpClient.cpp::build_request`.

## Required Bundle Layout

A complete bundle looks like:

```
FinceptTerminal.app/
├── Contents/
│   ├── _CodeSignature/
│   ├── Frameworks/         ← Qt frameworks + libssl.3.dylib + libcrypto.3.dylib + libavcodec/...
│   ├── Info.plist
│   ├── MacOS/
│   │   ├── FinceptTerminal
│   │   ├── resources/      ← Python requirements, etc.
│   │   ├── scripts/        ← bundled Python data adapters
│   │   └── yt-dlp
│   ├── PkgInfo             ← "APPL????"
│   ├── PlugIns/            ← Qt platform/tls/sqldrivers/imageformats plugins
│   └── Resources/
│       ├── fincept.icns
│       └── qt.conf
```

If `Frameworks/` or `PlugIns/` are missing, macdeployqt was never run.

## Required Info.plist Keys

The build template only writes a minimal plist. macOS will not promote the
process to a foreground GUI app without these:

| Key | Value | Why |
| --- | --- | --- |
| `CFBundleExecutable` | `FinceptTerminal` | Standard |
| `CFBundleIdentifier` | `com.fincept.terminal` | LaunchServices key |
| `CFBundlePackageType` | `APPL` | Standard |
| `CFBundleShortVersionString` | `4.0.3` (or current) | Display |
| `NSPrincipalClass` | `NSApplication` | **Required** for foreground GUI activation |
| `NSHighResolutionCapable` | `true` | Retina rendering |
| `LSMinimumSystemVersion` | `11.0` | Gatekeeper compatibility |
| `CFBundleIconFile` | `fincept.icns` | Resources/ icon |

Without `NSPrincipalClass=NSApplication`, the binary launches and creates its
window, but `open(1)` exits 0 silently and the window never reaches the user's
Space.

## Failure Modes & Their Fingerprints

### 1. `open` silently exits, `pgrep` finds nothing
- **Cause:** Code signature invalidated by macdeployqt and/or
  `install_name_tool`, AMFI sends `SIGKILL`.
- **Evidence:** `~/Library/Logs/DiagnosticReports/FinceptTerminal-*.ips` with
  `termination.namespace == "CODESIGNING"` and
  `exception.signal == "SIGKILL (Code Signature Invalid)"`.
- **Fix:** Recursive `codesign --force --sign - --timestamp=none` over every
  `.dylib`, `.framework`, executable, then `codesign --force --sign - --deep`
  on the bundle.

### 2. Direct binary runs, but window never appears
- **Cause:** Missing `NSPrincipalClass`. macOS keeps the process headless.
- **Evidence:** `osascript` reports the window exists at expected coords with
  `AXMinimized=false`, yet `frontmost` is false even after explicit activation.
- **Fix:** Add the Info.plist keys above and `lsregister -f -R <app>`.

### 3. HTTPS calls return "Network error" with no log entry
- **Cause A:** `libssl.3.dylib` / `libcrypto.3.dylib` not bundled, so the
  OpenSSL TLS plugin can't actually use system OpenSSL via @executable_path.
  Network requests fail with `status==0`.
- **Cause B:** Manual `Accept-Encoding: gzip` header on the request disables
  Qt's automatic gzip decompression. The compressed body fails JSON parsing
  in `HttpClient::handle_reply` and falls into a silent error path.
- **Evidence (A):** `lsof -p <pid> | grep -i ssl` doesn't show any
  `libssl.3.dylib` from `Contents/Frameworks/`.
- **Evidence (B):** No `LOG_WARN("HTTP", ...)` for the failing endpoint, but
  curl with the same headers returns `200 + content-encoding: gzip` and the
  body starts with bytes `1f 8b 08 00`.
- **Fix (A):** Copy from `/opt/homebrew/opt/openssl@3/lib/`, rewrite
  install_names to `@executable_path/../Frameworks/...`, fix the libssl→libcrypto
  cross-link with `install_name_tool -change`, then re-sign.
- **Fix (B):** Remove the manual `req.setRawHeader("Accept-Encoding", ...)` from
  `src/network/http/HttpClient.cpp::build_request`.

## Manual Recovery Recipe

When you discover a broken bundle (CI artifact, partial build, etc.) and need
to make it runnable without a full rebuild:

```bash
APP=fincept-qt/build/macos-release/FinceptTerminal.app
QT_BIN=$HOME/Projects/FinceptTerminal/.qt/6.8.3/macos/bin

# 1. Bundle Qt frameworks + plugins
"$QT_BIN/macdeployqt" "$APP" -verbose=1

# 2. Patch Info.plist
plutil -replace NSPrincipalClass    -string NSApplication "$APP/Contents/Info.plist"
plutil -replace NSHighResolutionCapable -bool true        "$APP/Contents/Info.plist"
plutil -replace LSMinimumSystemVersion  -string 11.0      "$APP/Contents/Info.plist"

# 3. Bundle OpenSSL
cp -L /opt/homebrew/opt/openssl@3/lib/libssl.3.dylib    "$APP/Contents/Frameworks/"
cp -L /opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib "$APP/Contents/Frameworks/"
chmod u+w "$APP/Contents/Frameworks/"libssl.3.dylib "$APP/Contents/Frameworks/"libcrypto.3.dylib
install_name_tool -id "@executable_path/../Frameworks/libssl.3.dylib"    "$APP/Contents/Frameworks/libssl.3.dylib"
install_name_tool -id "@executable_path/../Frameworks/libcrypto.3.dylib" "$APP/Contents/Frameworks/libcrypto.3.dylib"
CRYPTO_OLD=$(otool -L "$APP/Contents/Frameworks/libssl.3.dylib" | awk '/libcrypto/ && !/@executable_path/ {print $1; exit}')
[ -n "$CRYPTO_OLD" ] && install_name_tool -change "$CRYPTO_OLD" "@executable_path/../Frameworks/libcrypto.3.dylib" "$APP/Contents/Frameworks/libssl.3.dylib"

# 4. PkgInfo + icon (optional but expected)
printf "APPL????" > "$APP/Contents/PkgInfo"

# 5. Re-sign everything (deepest first)
find "$APP" -type f \( -name "*.dylib" -o -name "*.so" \) \
    -exec codesign --force --sign - --timestamp=none {} \;
find "$APP/Contents/Frameworks" -type d -name "*.framework" \
    -exec codesign --force --sign - --timestamp=none --deep {} \;
codesign --force --sign - --timestamp=none "$APP/Contents/MacOS/FinceptTerminal"
codesign --force --sign - --deep --timestamp=none "$APP"
codesign --verify --deep --strict "$APP"

# 6. Refresh LaunchServices and run
/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister -f -R "$APP"
open "$APP"
```

## Diagnostics Cheat Sheet

| Question | Command |
| --- | --- |
| Is the bundle complete? | `find "$APP" -maxdepth 3 -type d` |
| Is the binary linked against bundled OpenSSL? | `otool -L "$APP/Contents/MacOS/FinceptTerminal" \| grep -i ssl` |
| Is the running process loading bundle dylibs? | `lsof -p $(pgrep -f FinceptTerminal) \| grep -i ssl` |
| Are the TLS plugins loadable? | run with `QT_DEBUG_PLUGINS=1`; look for `crypto/ssl loaded library` lines |
| Why did the bundle SIGKILL? | parse newest `.ips` for `termination.namespace` |
| Did the HTTP layer see anything? | `tail -n 200 ~/Library/Application\ Support/com.fincept.terminal/logs/fincept.log` |

## See Also

- `src/network/http/HttpClient.cpp` — Accept-Encoding rule (do not set manually)
- `CMakeLists.txt` § macOS Qt deployment — POST_BUILD macdeployqt + OpenSSL bundling
- `~/Library/Logs/DiagnosticReports/` — crash logs
- `~/Library/Application Support/com.fincept.terminal/` — runtime data, logs, sentinels
