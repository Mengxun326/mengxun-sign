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

**Critical design decision**: Chaoxing API calls go directly from the phone (via `ChaoxingClient`), NOT through the server. The cloud server IP (47.121.180.250) is blocked by Chaoxing's CDN. The server only handles account storage and QR image decoding.

## Build & Deploy

### Client (Qt Android)

Prerequisites: Qt 6.11.1 at `E:/Qt/6.11.1/`, Android SDK at `%LOCALAPPDATA%/Android/Sdk`, NDK 27, OpenSSL .so files in `client/android/libs/arm64-v8a/`.

```bash
# Build
export ANDROID_NDK_ROOT="$HOME/AppData/Local/Android/Sdk/ndk/27.0.12077973"
mkdir -p client/build-qmake && cd client/build-qmake
"E:/Qt/6.11.1/mingw_64/bin/qmake6" \
  -qtconf "E:/Qt/6.11.1/android_arm64_v8a/bin/target_qt.conf" \
  ../XXTSign.pro
"$ANDROID_NDK_ROOT/prebuilt/windows-x86_64/bin/make" -j4

# APK
cp libXXTSign_arm64-v8a.so ../build-android-qmake/android-build/libs/arm64-v8a/
cmd //c "E:\Code\XXT\client\build-android-qmake\android-build\gradlew.bat -p E:\Code\XXT\client\build-android-qmake\android-build assembleRelease --no-daemon"

# Sign & install
JAVA_HOME="C:/Program Files/Microsoft/jdk-17.0.7.7-hotspot"
"$JAVA_HOME/bin/java" -jar "$SDK/build-tools/36.0.0/lib/apksigner.jar" sign \
  --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias debug --out /tmp/app.apk <unsigned.apk>
adb install -r /tmp/app.apk
adb shell am start -n com.mengxun.sign/org.qtproject.qt.android.bindings.QtActivity
```

**Important**: After first install or when camera stops working, grant camera permission:
```bash
adb shell pm grant com.mengxun.sign android.permission.CAMERA
```

### Server (Python FastAPI)

Deployed on Alibaba Cloud Linux 3 at `47.121.180.250:8001`. Python via Miniconda at `/opt/miniconda/bin/python`.

```bash
# Upload & restart
scp -i <key> -P 32208 server/main.py root@47.121.180.250:/opt/xx-sign/
ssh -i <key> -p 32208 root@47.121.180.250 "kill \$(lsof -ti:8001) 2>/dev/null; sleep 1; cd /opt/xx-sign && nohup /opt/miniconda/bin/python -m uvicorn main:app --host 0.0.0.0 --port 8001 &"
```

SSH key: `C:\Users\Meng_\Downloads\47.121.180.250_id_ed25519`, port 32208.

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
