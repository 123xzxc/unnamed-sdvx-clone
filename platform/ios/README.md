# iPadOS / iOS 移植说明

本目录包含把 unnamed-sdvx-clone 移植到 iPadOS 所需的全部代码、构建脚本和文档。
桌面版本（Windows / Linux / macOS）的源码仍然可以照常编译，所有移动端改动都通过
`USC_IOS` 宏隔离。

> **重要**：iOS 二进制只能在 macOS + Xcode + iOS SDK 上编译。本目录中的代码是在
> Windows 上编写和静态检查的，**尚未在真机/模拟器上编译验证过**。第一次构建时请
> 预期需要少量修正（主要是第三方依赖与签名相关的细节）。

---

## 1. 总体思路

这个项目原本是面向桌面 OpenGL 3.3 Core + 键盘/鼠标的。移植的关键决策如下。

### 1.1 渲染：复用已有的 `EMBEDDED`（OpenGL ES）路径

仓库里已经存在一条面向嵌入式设备（树莓派）的渲染路径：`-DEMBEDDED`。它使用
OpenGL ES 2.0 / GLSL ES 1.00，不使用 geometry shader、program pipeline 和
`glGetTexImage`，而这些恰好都是 iOS 的 OpenGL ES 不支持的特性。

因此 iOS 构建 = `EMBEDDED` + `USC_IOS`：

| 位置 | 桌面路径 | iOS 路径 |
| --- | --- | --- |
| `Graphics/include/Graphics/GL.hpp` | `<GL/glew.h>` | `<OpenGLES/ES2/gl.h>`（新增分支） |
| `Graphics/src/OpenGL.cpp` | Core 3.3 + GLEW | ES 2.0，无需 GLEW（复用 `EMBEDDED` 分支） |
| `Graphics/src/Shader.cpp` | 多阶段 pipeline | 单程序，`#version 100` 前缀（复用 `EMBEDDED`） |
| `bin/skins/Default/shaders/*` | GLSL 330 | 皮肤里已有的 `#ifdef EMBEDDED` 分支 |
| nanovg | `nvgCreateGL3` | `nvgCreateGLES2`（复用 `EMBEDDED`） |
| nuklear | `nuklear_sdl_gl3.h` | `nuklear_sdl_gles2.h`（复用 `EMBEDDED`） |

关于 OpenGL ES 在 iOS 上的前途：Apple 从 iOS 12 起弃用 OpenGL ES，但至今仍然可用。
如果将来某个 iOS 版本真正移除了它，正确的替代方案是在 SDL 与游戏之间接入
**ANGLE**（把 GLES 转译为 Metal），游戏代码基本无需改动——这也是 Chromium、Flutter
等项目的做法。这一点在「后续工作」里有说明。

### 1.2 输入：把触屏模拟成游戏本来就支持的输入设备

iPadOS 上没有键盘，所以新增了一个屏幕控制器 `iOSTouchControls`，但它**不修改游戏
逻辑**，而是把触屏转译成引擎已经支持的输入：

| 屏幕控件 | 注入方式 | 使用的引擎机制 |
| --- | --- | --- |
| 左/右旋钮 | `Window::InjectMouseMotion()` | `InputDevice::Mouse` 激光输入（本来就是模拟旋钮的） |
| BT-A/B/C/D | `Window::InjectKey()` | 玩家当前配置的键位（`Key_BT0`…`Key_BT3`） |
| FX-L / FX-R | `Window::InjectKey()` | `Key_FX0` / `Key_FX1` |
| START / BACK | `Window::InjectKey()` | `Key_BTS` / `Key_Back` |
| 空白区域 | `Window::InjectMousePosition/Button()` | 给 nuklear 界面用的鼠标点击 |

这样做的结果：录像回放、判定、分数上传、校准界面看到的都是「正常」输入，不需要为
触屏写第二套判定。

为了让拖动playfield不会误触发激光，iOS 上关闭了 SDL 的触摸→鼠标自动模拟
（`SDL_HINT_TOUCH_MOUSE_EVENTS=0`），菜单点击由上面的「空白区域」转发提供。

### 1.3 音频：直接使用 SDL 后端

`Audio/src/Unix/AudioOutput_SDL.cpp` 通过 `SDL_OpenAudioDevice` 输出，iOS 上 SDL 会
使用 CoreAudio 后端，因此不需要新的音频实现。

### 1.4 文件系统：一切都放进 Documents

iOS 的 `.app` 包是只读的，而游戏会往游戏目录里写 `config.ini`、`maps.db`、截图等。
所以：

* `Path::gameDir` 指向 `Documents/unnamed-sdvx-clone/`；
* 首次启动时，把包里的 `skins/ fonts/ audio/` 复制进去（`iOSPlatform::
  InstallGameDataIfNeeded`，只复制缺失的文件，不会覆盖用户改动）；
* `Info.plist` 打开 `UIFileSharingEnabled` 和 `LSSupportsOpeningDocumentsInPlace`，
  于是可以直接用「文件」App / Finder / AirDrop 把谱面和皮肤拖进
  `Documents/unnamed-sdvx-clone/songs/`。

### 1.5 被裁剪的功能

| 功能 | 处理 | 原因 |
| --- | --- | --- |
| Discord Rich Presence | 空实现 stub（`stubs/discord_rpc_stub.cpp`） | 桌面 IPC，iOS 不可用 |
| 自动更新 / 崩溃上报（breakpad） | iOS 不编译 | 依赖 Windows 二进制和 `updater.exe` |
| 灯光插件 `.dll` | iOS 不加载 | iOS 不允许 dlopen 非随包代码 |
| Internet Ranking / 皮肤下载 | 默认开启并走 vcpkg 的 `cpr`；可用 `-DUSC_IOS_HTTP=OFF` 关闭 | 需要 libcurl；关闭时用 API 兼容 stub |

---

## 2. 前置条件

* macOS，安装 Xcode（含 iOS SDK）与命令行工具
* CMake ≥ 3.21（iOS 支持 + `$<TARGET_BUNDLE_DIR>`）
* [vcpkg](https://github.com/microsoft/vcpkg)（用于构建 iOS 版依赖）
* 一个 Apple ID（免费账号即可在真机上跑 7 天，付费账号用于长期安装/上架）

## 3. 构建

```sh
export VCPKG_ROOT=$HOME/vcpkg
export USC_IOS_TEAM_ID=ABCDE12345        # 可选：真机签名用
export USC_IOS_BUNDLE_ID=me.example.usc  # 可选

cd platform/ios/scripts
./build.sh deps        # 只为 iOS 构建一次依赖（较慢，几分钟起）
./build.sh all         # 生成 Xcode 工程并编译（默认 arm64 真机）

# 模拟器（Apple Silicon）：
./build.sh all --simulator

# 打开 Xcode 工程（可以在这里配置签名、真机运行）
./build.sh open
```

产物是 `usc-game.app`（脚本最后会打印它的绝对路径；由于顶层 CMakeLists 把输出目录设成
仓库的 `bin/`，通常是 `bin/usc-game.app`）。

安装到已连接的 iPad：

```sh
xcrun devicectl device install app --device <UDID> \
    "$(find build-ios/project bin -maxdepth 4 -name 'usc-game.app' -print -quit)"
```

首次在真机上运行时需要在 iPad 上信任开发者证书：
设置 → 通用 → VPN 与设备管理 → 开发者 App → 信任。

### 依赖构建失败时

`cpr` / `libcurl` 是 iOS 上最容易出问题的一环。如果 `vcpkg install` 在 `cpr` 上失败，
直接关掉 HTTP 功能即可完成其余部分的构建（Internet Ranking 和皮肤下载会显示为
不可用）：

```sh
./build.sh deps   # 先把 platform/ios/vcpkg.json 里的 "cpr" 删掉
USC_IOS_HTTP=OFF ./build.sh all
```

## 4. 操作方式

进入游戏后屏幕底部会出现虚拟控制器：

```
 [START]                                              [BACK]

   (KNOB)                                                  (KNOB)
  FX-L  A    B    C          D                        FX-R
```

* 旋钮：在旋钮区域内按任意方向拖动（水平或垂直都可），等价于街机旋钮的无限旋转。
  灵敏度沿用游戏里的 `Mouse_Sensitivity` 设置。
* 按钮：支持多点触控与滑动切换（手指滑到另一个按钮会松开旧的、按下新的）。
* 空白区域：转发为鼠标点击，用于操作设置界面。
* 手柄：MFi / 蓝牙手柄走 SDL joystick 通道，可以在设置里把输入设备改成
  `Controller`，此时屏幕控制器仍然可用。

## 5. 已知限制

* 竖屏皮肤（`ForcePortrait`）需要设备旋转到竖屏；当前 `Info.plist` 只声明了横屏，
  想用竖屏皮肤需要调用 `iOSPlatform::SetLandscapeOnly(false)` 并放开 plist 里的
  方向声明。
* 屏幕控制器的布局是横屏专用的，尚未为竖屏重新排布。
* 一半透明度的控件在明亮背景的皮肤上可能不够清晰。
* 未做 Metal/ANGLE 后端；如果 Apple 移除 OpenGL ES，需要接入 ANGLE。
* 进入后台 / 失去焦点时会调用 `iOSTouchControls::ReleaseAll()` 松开所有触屏按键，
  避免回到前台后按键卡住；但没有做完整的暂停/恢复（音乐仍会继续播放，是否静音由
  `MuteUnfocused` 设置决定）。
* 未提供 App 图标（Asset Catalog）和 `LaunchScreen` 资源，目前用
  `UILaunchScreen` + 背景色占位。

## 6. 代码改动清单

新增：

* `platform/ios/src/iOSPlatform.h/.mm` — 沙盒路径、数据安装、方向、屏幕常亮
* `platform/ios/src/iOSTouchControls.hpp/.cpp` — 屏幕控制器（含 `ReleaseAll()`，
  失去焦点时松开所有触屏按键）
* `platform/ios/stubs/` — discord-rpc 与 cpr 的替身
* `platform/ios/CMakeLists.txt`、`Info.plist`、`vcpkg.json`、`scripts/build.sh`

修改（均可用 `USC_IOS` 检索）：

* `CMakeLists.txt`：iOS 检测、默认部署目标、跳过桌面专有依赖
* `third_party/CMakeLists.txt`：cpr / discord-rpc / GLEW 的 iOS 分支
* `Graphics/CMakeLists.txt`、`Main/CMakeLists.txt`：链接与 bundle 设置
* `Graphics/include/Graphics/GL.hpp`：OpenGL ES 头文件
* `Graphics/src/OpenGL.cpp`：ES 2.0 下的 GL 调试回调
* `Graphics/include/Graphics/Window.hpp`、`Graphics/src/Window.cpp`：事件注入 API、
  触摸事件委托、事件分发重构（`HandleEvent`）
* `Main/include/Application.hpp`、`Main/src/Application.cpp`：游戏目录、窗口尺寸、
  屏幕控制器生命周期、关闭自动更新与灯光插件
* `Main/src/GameConfig.cpp`：iOS 默认输入设备（旋钮=鼠标）
* `Main/src/Main.cpp`：iOS 平台初始化

另外 `platform/ios/stubs/cpr/cpr.h` 为 `cpr::AsyncResponse` 补齐了 `wait_for()`，
因为 `ScoreScreen` 会先轮询再取结果；没有它的话 `USC_IOS_HTTP=OFF` 的构建无法编译。
