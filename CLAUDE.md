# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

梦寻签到 — an Android app (Qt/C++) + cloud server (Python/FastAPI) for batch Chaoxing (学习通) sign-in. The app detects active sign-in tasks and signs in all stored accounts with one tap. QR code sign-ins are handled via camera auto-scan.

## Architecture

```
Phone (Qt Android)                    Cloud Server (FastAPI)
┌──────────────────────┐              ┌──────────────────────┐
│ SignInTab            │────HTTP────→│ /api/accounts/*      │
│   ChaoxingClient     │              │   (encrypted storage) │
│   (direct HTTPS to   │              │ /api/sign/decode-qr  │
│    chaoxing.com)     │              │   (OpenCV QR decode)  │
│ AccountTab           │              └──────────────────────┘
│   ApiClient ─────────┘
└──────────────────────┘
```

**Critical design decision**: Chaoxing API calls go directly from the phone (via `ChaoxingClient`), NOT through the server. Cloud server IPs are blocked by Chaoxing's CDN. The server only handles account storage and QR image decoding.

## Build & Deploy

### Client (Qt Android)

Prerequisites: Qt 6.11.1, Android SDK + NDK 27, JDK 17, OpenSSL .so files in `client/android/libs/arm64-v8a/`.

```bash
export ANDROID_NDK_ROOT=/path/to/ndk/27.0.12077973
export QT_DIR=/path/to/Qt/6.11.1

mkdir -p client/build-qmake && cd client/build-qmake
$QT_DIR/mingw_64/bin/qmake6 \
  -qtconf $QT_DIR/android_arm64_v8a/bin/target_qt.conf \
  ../XXTSign.pro
$ANDROID_NDK_ROOT/prebuilt/linux-x86_64/bin/make -j4

# APK
cp libXXTSign_arm64-v8a.so ../build-android-qmake/android-build/libs/arm64-v8a/
./gradlew -p ../build-android-qmake/android-build assembleRelease

# Sign & install
apksigner sign --ks your.keystore --out /tmp/app.apk <unsigned.apk>
adb install -r /tmp/app.apk
adb shell am start -n com.mengxun.sign/org.qtproject.qt.android.bindings.QtActivity
adb shell pm grant com.mengxun.sign android.permission.CAMERA
```

### Server (Python FastAPI)

```bash
pip install -r server/requirements.txt
cd server && uvicorn main:app --host 0.0.0.0 --port 8001
```

## Key Files

### Server (`server/`)
- `main.py` — all API endpoints (FastAPI), no separate router files
- `chaoxing_api.py` — Chaoxing login, course listing, task detection, sign-in execution
- `encryption.py` — AES-256 credential encryption, Chaoxing inf_enc MD5 signing
- `models.py` — SQLAlchemy `ChaoxingAccount` model (SQLite)
- `config.py` — server config, Chaoxing API constants

### Client (`client/`)
- `XXTSign.pro` — qmake project file (add new .cpp/.h here)
- `network/chaoxingclient.h/cpp` — **Direct Chaoxing HTTPS from phone** — login, get courses, detect tasks, execute sign-in. Uses `QNetworkCookieJar` for automatic cookie management.
- `network/apiclient.h/cpp` — HTTP to our cloud server (account CRUD, QR decode)
- `ui/signintab.h/cpp` — Main sign-in page: check tasks button, batch sign-in button, QR scan with camera auto-capture every 600ms
- `ui/accounttab.h/cpp` — Account management: verify locally via ChaoxingClient, then store to server via addAccountDirect
- `mainwindow.h/cpp` — Bottom nav (签到/账号), header, server settings dialog
- `android/` — AndroidManifest.xml, icons, network security config

## Key Design Decisions

1. **Chaoxing API runs on the phone, not the server** — server IP is CDN-blocked. `ChaoxingClient` handles all Chaoxing HTTPS directly.

2. **Two-phase account add** — AccountTab first verifies credentials locally via `ChaoxingClient::login()`, then stores to server via `/api/accounts/add-direct` (no server-side re-verification).

3. **Signals for task aggregation** — `ChaoxingClient::checkAllTasks()` uses `tasksReceived` internally for per-course results, then emits `allTasksReady` once with the aggregated list. UI connects to `allTasksReady` only.

4. **Camera fullscreen mode** — When scanning QR, title/buttons/log are hidden, camera fills the screen. On stop/decode, visibility is restored.

5. **OpenSSL required** — Android lacks system OpenSSL. `libssl_3.so` and `libcrypto_3.so` from KDAB/android_openssl must be in `android-build/libs/arm64-v8a/` and `client/android/libs/arm64-v8a/`.

6. **Cookie handling** — Qt's `QNetworkCookieJar` auto-manages cookies across Chaoxing requests. No manual Cookie header manipulation needed.

## Common Pitfalls

- After USB disconnect, `adb reverse tcp:8001 tcp:8001` is lost. Re-run it for local testing, or use the cloud server URL.
- Never use `follow_redirects=True` with Chaoxing — the CDN redirect-loops cloud IPs. Phone requests avoid this.
- Build directory (`build-qmake`) can have stale moc files after header changes. `rm -rf *` and re-run qmake+make when headers change.
- The AndroidManifest in `android-build/` is copied by androiddeployqt from `client/android/`. Update the source, not the copy.
- `android:label` and `android:icon` in the build manifest may need manual fixes after androiddeployqt overwrites them.
