# 梦寻签到

学习通（超星）批量签到 Android App + 云服务器。自动检测活跃签到任务，一键批量签到，支持二维码扫描、GPS 定位、拍照签到，以及朋友帮签（分享链接）。

## 架构

```
手机 (Qt Android C++)                    云服务器 (FastAPI + SQLite)
┌──────────────────────┐              ┌──────────────────────┐
│ SignInTab            │──HTTP──────→│ /api/accounts/*      │
│   ChaoxingClient     │              │   (AES加密存储)       │
│   (直连学习通API)     │              │ /api/sign/decode-qr  │
│ AccountTab           │              │   (OpenCV QR解码)    │
│   ApiClient ─────────┘              │ /api/share/*         │
│                                      │   (朋友帮签)         │
│ ShareHelper (Java)   │              │ /api/update/*        │
│   (后台帮签服务)      │              │   (版本更新)         │
└──────────────────────┘              └──────────────────────┘
```

**关键设计**：学习通 API 调用走手机直连（服务器 IP 被学习通 CDN 封锁），服务器仅做加密存储、二维码解码和帮签协调。

## 功能

### 签到
- 自动检测：每 30 秒轮询，检测到新任务弹系统通知
- 一键签到：普通/位置/二维码/拍照/手势全类型
- 批量签到：从服务器拉取所有已存账号，逐个登录签到
- 二维码：`QGraphicsVideoItem` 实时预览 + 扫描框叠加层，每 600ms 自动拍照 → 服务端 OpenCV 解码
- GPS：自动请求位置权限，10 秒超时回退
- 拍照签到：相机模式拍照上传

### 账号管理
- 客户端 AES 加密后存储到服务器
- 设备 UUID 隔离：每台设备只能看到自己的账号
- 登录/退出/切换账号（自定义卡片弹窗）

### 朋友帮签
- 生成分享链接（按签到类型自动适配页面：二维码扫描/GPS/拍照/普通）
- 二维码页面：浏览器摄像头实时取景 → 云端 OpenCV 解码（`BarcodeDetector` + Cloud 双模）
- 后台帮签：`ShareHelper` 前台服务 + 实时学习通登录 + 新鲜 cookie 签到
- 分享链接 5 分钟自动过期

### 自动更新
- 启动检查更新 → 自定义卡片弹窗 → 内嵌进度条流式下载 → 权限检测 → 调起系统安装器
- 27MB APK 流式写入磁盘，避免 OOM

## 构建

### 环境要求

- Qt 6.11.1
- Android SDK + NDK 27
- JDK 17
- OpenSSL .so 文件（`libssl_3.so`, `libcrypto_3.so`）

### 客户端 (Qt Android)

```bash
export ANDROID_NDK_ROOT=/path/to/ndk/27.0.12077973
export QT_DIR=/path/to/Qt/6.11.1

mkdir -p client/build-qmake && cd client/build-qmake
$QT_DIR/mingw_64/bin/qmake6 \
  -qtconf $QT_DIR/android_arm64_v8a/bin/target_qt.conf \
  ../XXTSign.pro
$ANDROID_NDK_ROOT/prebuilt/linux-x86_64/bin/make -j4

# 打包 APK
cp libXXTSign_arm64-v8a.so ../build-android-qmake/android-build/libs/arm64-v8a/
./gradlew -p ../build-android-qmake/android-build assembleRelease

# 签名
apksigner sign --ks your.keystore --out mengxun-sign.apk <unsigned.apk>

# 安装
adb install -r mengxun-sign.apk
adb shell am start -n com.mengxun.sign/org.qtproject.qt.android.bindings.QtActivity
```

### 服务端 (Python FastAPI)

```bash
# 上传 & 重启
scp server/*.py user@your-server:/opt/xx-sign/
ssh user@your-server \
  "cd /opt/xx-sign && nohup python -m uvicorn main:app --host 0.0.0.0 --port 8001 &"
```

## 项目结构

```
├── client/                          # Qt Android App
│   ├── XXTSign.pro                  # qmake 项目文件
│   ├── main.cpp                     # 入口 + JNI 注册
│   ├── mainwindow.h/cpp             # 主窗口（导航、更新、省电）
│   ├── network/
│   │   ├── apiclient.h/cpp          # → 云服务器 HTTP
│   │   └── chaoxingclient.h/cpp     # → 学习通直连 HTTPS
│   ├── crypto/
│   │   └── cryptohelper.h/cpp       # AES 加解密 + inf_enc 签名
│   ├── ui/
│   │   ├── signintab.h/cpp          # 签到页（检测/签到/扫码/帮签）
│   │   └── accounttab.h/cpp         # 账号管理页
│   └── android/
│       ├── AndroidManifest.xml
│       ├── res/
│       └── src/com/mengxun/sign/
│           ├── ShareHelper.java     # 后台帮签前台服务
│           └── ShareReceiver.java   # 帮签广播接收器
│
└── server/                          # FastAPI 云服务器
    ├── main.py                      # 所有 API 端点
    ├── models.py                    # SQLAlchemy 模型
    ├── database.py                  # 数据库初始化 + 迁移
    ├── config.py                    # 配置常量
    └── update.json                  # 版本更新元数据
```

## API 端点

### 账号
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/accounts/add` | 存储加密账号（按 device_id 隔离） |
| GET | `/api/accounts/list` | 列出本设备所有账号 |
| DELETE | `/api/accounts/{id}` | 删除账号 |

### 签到
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/sign/decode-qr` | OpenCV 解码二维码图片 |
| POST | `/api/sign/log` | 记录签到结果 |

### 帮签
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/share/create` | App 创建帮签链接 |
| GET | `/api/share/page/{token}` | 浏览器帮签页面（按签到类型） |
| POST | `/api/share/submit/{token}` | 朋友提交数据（图片/GPS） |
| GET | `/api/share/pending/{token}` | App/ShareHelper 轮询待处理 |
| GET | `/api/share/sign-params/{token}` | 获取签到参数 + 凭据 |
| POST | `/api/share/result` | 上报签到结果 |
| GET | `/api/share/status/{token}` | 浏览器轮询状态 |

### 更新
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/update/check` | 返回最新版本信息 |
| GET | `/api/update/download` | 下载 APK |

## 许可

仅供学习交流使用。
