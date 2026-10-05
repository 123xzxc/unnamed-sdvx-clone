# iPadOS / iOS 移植说明

本目录包含把 unnamed-sdvx-clone 移植到 iPadOS 所需的全部代码、构建脚本和文档。
桌面版本（Windows / Linux / macOS）的源码仍然可以照常编译，所有移动端改动都通过
`USC_IOS` 宏隔离。

> **重要**：iOS 二进制只能在 macOS + Xcode + iOS SDK 上编译。本目录中的代码是在
> Windows 上编写并静态检查的，完整的 arm64 真机构建由
> [`.github/workflows/ios.yml`](../.github/workflows/ios.yml)（GitHub Actions 的
> macOS 运行器）负责，目前已经可以稳定产出未签名的 `usc-game-unsigned.ipa`，
> 见第 7 节。桌面版源码不受任何影响。

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
| 左上角开关 | 无（仅切换覆盖层可见性） | 隐藏/显示整个虚拟控制器，隐藏时不拦截触摸 |
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
 [HIDE]            (START)            [BACK]

  (KNOB)                                (KNOB)

        A     B     C       D

      [ FX-L ]          [ FX-R ]
```

* 左上角 `HIDE`：隐藏整个虚拟控制器（画面完全无遮挡），此时只有左上角那个按钮还
  会响应触摸，其余触摸全部转发给游戏界面；再按一次（标签变成 `SHOW`）恢复。
* 旋钮：按 PHAC（RP2040 旋钮控制器固件）里 EC11 编码器的那套做法重写过。
  手指绕着旋钮转圈，**只取绕旋钮中心的角度变化**，每转 15°（24 个咔哒/圈）算一个「咔哒」；
  拇指在玻璃上打滑没有角度变化，所以激光不会左右乱蹦——早先的版本把手指的横向和纵向位移
  直接相加，那点抖动自然全被算了进去。
  每个咔哒产生的鼠标像素数按 `6 / pow(200 / Mouse_Sensitivity, 1.2)` 反算（和
  `Input::CalculateRealMouseSens` 一致），手指转一圈 = 激光转 2 圈，而且不管灵敏度滑块在哪
  手感都一样。像素不是一次性发出去的：每帧只发剩余量的一半（PHAC 的 `SMOOTHING_FACTOR` 在
  1ms 主循环里用的是 `1/6`，这里的帧间隔大约 17ms，所以除数相应调小），余量累积到下一帧，
  一个咔哒大约 50ms 走完，既不瞬移也不拖沓；反向转动时会先清掉没发完的余量，
  急转不会把激光甩回头。旋钮里的指针会显示转过的角度。
* 按钮：支持多点触控与滑动切换（手指滑到另一个按钮会松开旧的、按下新的）。
* 空白区域：转发为鼠标点击，用于操作设置界面。
* 手柄：MFi / 蓝牙手柄走 SDL joystick 通道，可以在设置里把输入设备改成
  `Controller`，此时屏幕控制器仍然可用。

## 5. 已知限制

* 横竖屏可以自由旋转：`Info.plist` 声明了四个方向，`iOSPlatform::Init` 也不再锁定
  `Portrait`。渲染用的投影、相机和舞台都会按 `g_aspectRatio` 自动切换横/竖版本，
  所以 `ForcePortrait` 默认关闭（它只会把 9:16 的舞台放进黑边里，横屏时反而不合适）。
  想回到「竖屏 + 居中舞台」的老样子，可以在设置里打开 `ForcePortrait`。
* 屏幕控制器的位置全部是分辨率的百分比，尺寸取自屏幕的短边，横竖屏各有自己的锚点，
  两种方向都能用。
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
* `Main/src/GameConfig.cpp`：iOS 默认输入设备（旋钮=鼠标）、`ForcePortrait` 默认关闭
* `Main/src/Main.cpp`：iOS 平台初始化
* `Graphics/src/Shader.cpp`：把桌面 GLSL 降级成 GLSL ES 3.00（`#version 300 es`、
  `#extension` / `gl_PerVertex` / `layout(...)` 处理、显式顶点属性 location、highp）
* `Graphics/src/RenderQueue.cpp`：文本绘制补上 `mapSize`（见下面的字体说明）
* `Main/nuklear/nuklear_sdl_gles2.h`：nuklear 片元着色器改用 highp
* `bin/skins/Default/shaders/*.vs/.fs`：为不支持 `texelFetch` 的目标加 `EMBEDDED` 分支

### 字体渲染的两个坑（设置界面文字错乱）

设置界面左侧页签用的是引擎自己的 `TextRes` 路径（不是 nuklear），它把字形在
图集里的**绝对像素坐标**当成纹理坐标传给着色器。GLES 上没有 `texelFetch` 时
`font.fs` 用 `fsTex / vec2(mapSize)` 归一化，所以：

1. `mapSize` 这个 uniform 以前只在 `RenderQueue::DrawScissored(text)` 里绑定，而那个
   重载从来没被调用过，于是它一直是 GL 默认值 `(0,0)`，每次文字绘制都在除以零。
   现在 `RenderQueue::Draw(text)` 也会绑定图集尺寸。
2. 片元着色器里的插值精度必须是 `highp`：图集宽达 8192 时 mediump（fp16）在一个字形
   四边形内只剩几位有效数字，采样会落到隔壁格子。iOS 的 GLSL ES 3.00 前缀和
   nuklear 的着色器都已经改成 `highp`。

另外 `platform/ios/stubs/cpr/cpr.h` 为 `cpr::AsyncResponse` 补齐了 `wait_for()`，
因为 `ScoreScreen` 会先轮询再取结果；没有它的话 `USC_IOS_HTTP=OFF` 的构建无法编译。

---

## 7. GitHub Actions 构建与自签安装

推送到 `develop`（或在 Actions 页面手动触发 `workflow_dispatch`）会运行
`.github/workflows/ios.yml`：macOS 运行器上用 vcpkg 的 `arm64-ios` triplet 构建
依赖（有缓存），再用 Xcode 生成器编译，全程关闭签名，最后把 `.app` 打成 IPA。一次
完整运行大约 7 分钟。

产物在运行页面的 Artifacts 里，名字是 `usc-game-ios-unsigned-ipa`，解包后有两份文件：

| 文件 | 用途 |
| --- | --- |
| `usc-game-unsigned.ipa` | `Payload/usc-game.app` 的 zip 包，未签名，直接喂给自签工具 |
| `usc-game-app-unsigned.tar.gz` | 原始的 `.app`，iOS App Signer / Sideloadly 也接受 |

Bundle ID 为 `me.drewol.usc`，最低系统要求 iOS 15.0，只编 arm64（真机）。

### 自签侧载

IPA 里没有任何证书和描述文件，必须在本机重新签名后才能安装：

* **AltStore / Sideloadly**：把 IPA 拖进去，用你的 Apple ID 签名安装即可；免费账号
  签出来的 App 有效期 7 天，到期需要重签。
* **iOS App Signer + Xcode**：用 `.app`（tar 包解出来，或从 IPA 里解出 `Payload/`），
  选好证书与描述文件重签，再从 Xcode 的 Devices 窗口安装。
* **TrollStore**：支持的 iOS 版本上可以直接安装未签名 IPA，不需要证书。

首次运行要在 iPad 上信任证书：设置 → 通用 → VPN 与设备管理 → 开发者 App → 信任。
如果自签时报 bundle ID 冲突，把 `me.drewol.usc` 换成自己的（例如 `com.you.usc`）后
重新跑一次 CI，或者直接在 Xcode 里改。

### 这个构建里踩过的坑

* iOS 上的 vcpkg 只提供 `libSDL2.a`，没有 `libSDL2main.a`，所以 `main()` 和
  `SDL_main()` 都由 `Main/src/Main.cpp` 自己提供（等价于 SDL 的
  `src/main/uikit/SDL_uikit_main.c`）。
* 不要在 iOS 上定义 `SDL_MAIN_AVAILABLE`：它会把每个包含 SDL.h 的翻译单元里的
  `main()` 改名成 `SDL_main()`，于是链接时就没有 `_main` 了。
* Xcode 生成器只把 `add_executable()` 执行时已经存在的源文件写进
  `usc-game.LinkFileList`，所以 iOS 平台层的源文件必须在创建目标时一起列出
  （见 `Main/CMakeLists.txt` 里的 `USC_IOS_PLATFORM_SOURCES`）。
* `cpr` / `libcurl` 是 iOS 依赖里最脆的一环。默认用 `-DUSC_IOS_HTTP=OFF` 走 API
  兼容的 stub；需要 Internet Ranking / 皮肤下载时，用 `workflow_dispatch` 勾上
  `http_enabled`。
* `Path::CreateDirRecursive()` 以前会把绝对路径开头的前导 `/` 丢掉，把整条路径当
  相对路径逐级 `mkdir`。游戏目录是沙盒 Documents 下的绝对路径，于是在只读的 .app
  里 `mkdir("private")` 直接失败，目录建不出来，首次启动一个资源文件都不会复制，
  加载第一个材质时就会报 `Could not load shaders skins/Default/shaders/font.vs`。
  这个 helper 现在会保留前导分隔符（它是唯一调用者，只影响 iOS）。

 * **画面只占屏幕左下角的四分之一，并且虚拟按键和手指对不上**。根因是窗口带了
   `SDL_WINDOW_ALLOW_HIGHDPI`：开着它的时候，"窗口尺寸（点）""绘制缓冲尺寸（像素）"
   "SDL 触摸事件的归一化坐标"是三个不同的单位，而引擎把它们当成一个用。只要有一次
   报告的单位不是点，`g_resolution` 就会变成面板的一半，于是 viewport 只填左下角、
   触摸映射也跟着错位。现在 iOS 不再请求 high DPI，三者统一用点，iOS 自己把帧缓冲
   放大到面板（画质由引擎自己的渲染分辨率设置控制）。同时窗口初始化后立刻显式
   `glViewport`，不再依赖 SDL 的隐式设置和后续的 resize 事件纠正。
 * **BT-C/BT-D/FX-R 跑到面板外面去了**。旧的单行布局把 C 摆在 D 右边，而底衬圆角矩形
   只画在 `x < 0.5w`，所以右手那三个按钮都悬在面板外。现在按控制台的分手布局排：
   左手 FX-L/A/B/C，右手 FX-R/D，底衬也分成左右两块，和按钮坐标一致。
 * **返回上一级之后点不动其他选项**。手指从按钮滑到空白处时，旧代码会按下鼠标左键，
   但只在抬指时才松开：如果手指是在空白处抬起的，左键就一直按着，之后菜单里的点击
   全被当成拖拽，于是"点哪个都没反应"。现在从控件上开始的触摸整个生命周期都算控件
   触摸，滑出去只是松开按键，不会产生鼠标事件。
 * **Get Songs 一直显示 LOADING...**。默认构建带着 HTTP，请求失败时 `status` 是 0，
   而 lua 只在 `status ~= 200` 时 `error()`，界面就永远停在加载中。现在会明确显示失败
   原因和请求的 URL，并停止无意义的重复请求。只有显式用 `-DUSC_IOS_HTTP=OFF` 构建时
   才会提示"这个构建没有 HTTP 支持"。
 * **Get Songs 点进去是全空白的**。失败回调会同时把 `loading` 置回 `false` 并设置
   `loadingFailed`，而 `render_loading()` 的第一行是 `if not loading then return end`，
   于是失败提示永远画不出来，屏幕上什么都没有。现在画提示的判断在 `loading` 门控之前。
 * **HTTP 现在是 iOS 构建的默认值**。`Get Songs` 是游戏内置的谱面浏览器，没有
   HTTP 就只是一块空白，所以 CI 默认带着 `cpr`/`libcurl` 构建；要回到
   `-DUSC_IOS_HTTP=OFF` 的 stub 构建，把 `workflow_dispatch` 的 `http_enabled`
   改成 `false`（或者本地 `-DUSC_IOS_HTTP=OFF`）即可。
 * **字体图集的残留数据会把 UI 画花**（标题碎成白色块、BACK 按钮里出现放大的
   `US...` 图像）。`Graphics/src/Image.cpp` 的 `Allocate()` 用 `new Colori[]` 分配
   图集却从不初始化，而 `SpriteMap` 只写入真正用到的字形区域，剩下的堆内存就直接
   进了 GPU 纹理。Windows 上堆页恰好是清零的所以看不出来。`Allocate()`/`ReSize()`
   现在会 `memset` 清零。
 * **Xbox / MFi 手柄认不出来**。`SDL_Init()` 以前没有请求
   `SDL_INIT_GAMECONTROLLER`，而且所有设备都走 `SDL_JoystickOpen()`，原始 joystick
   的按键编号和默认键位假设的 Xbox 布局对不上，于是按什么都没反应。现在有映射的设备
   走 `SDL_GameControllerOpen()`（其余回退到原始设备），并且只把 `SDL_CONTROLLER*`
   事件喂给这类设备，热插拔时还会让设置页面重建设备列表。

## 诊断日志

设备上跑的构建没法挂调试器，所以每一次排查都靠"日志" `usc-ios.log`：它在可写游戏目录里
（`文稿/unnamed-sdvx-clone/usc-ios.log`），可以直接在 iOS「文件」App 或 Finder 的文件共享里
打开、导出。

* 引擎自带的 `Logger` 把 `log_<module>.txt` 写在可执行文件旁边，而那个路径是 `Logger` 构造函数
  （静态初始化阶段）决定的，那时 `Path::gameDir` 还没算出来。iOS 上它落在只读的 `.app` 里，
  每次写入都失败，等于什么都没有记录。
* `platform/ios/src/iOSLog.cpp` 是 iOS 专用的第二条日志通道：追加写入、超过 4 MB 轮转、
  每行 `fsync`，所以进程被杀掉或崩溃也不会丢掉最关键的几行。
* `platform/ios/src/iOSPlatform.mm` 在 `Init()` 里把它挂到 `Logger::SetSink()` 上，
  无需改动任何 `Log()` / `Logf()` 调用点。
* `Main/src/Main.cpp` 注册 `SIGSEGV`/`SIGBUS`/`SIGFPE`/`SIGILL`，用 `write(2)` 把信号名写进
  日志；iOS 入口也包了 `try/catch`，异常不再默默消失在 SDL 的 UIKit 代理里。
* `Main/src/SkinHttp.cpp` 记录每个 HTTP 请求的 URL、状态码和 libcurl 错误，
  所以"Get Songs 是空的"现在是一行日志而不是需要手动转述的弹窗。

遇到问题时，把 `usc-ios.log` 一起发出来即可。
