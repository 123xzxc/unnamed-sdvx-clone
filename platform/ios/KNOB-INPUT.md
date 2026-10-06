# iOS 虚拟旋钮输入（左右滑动）：完整更改文档

> 相关代码：`platform/ios/src/iOSTouchControls.cpp`
> 参考实现：PHAC（RP2040 旋钮控制器固件）的 `main.c` 与 `modules/encoder/ec11.c`

## 1. 这套东西要解决什么

iPadOS 上没有实体旋钮也没有键盘，所以屏幕控制器把触屏手势翻译成引擎**本来就支持**的
鼠标相对位移，游戏逻辑一行都不用改：

| 屏幕控件 | 注入调用 | 游戏内映射 |
| --- | --- | --- |
| 左旋钮 | `Window::InjectMouseMotion(dx, 0)` | `Mouse_Laser0Axis = 0`（鼠标 X 轴） |
| 右旋钮 | `Window::InjectMouseMotion(0, dy)` | `Mouse_Laser1Axis = 1`（鼠标 Y 轴） |

游戏侧在 `Main/src/Input.cpp` 的 `Input::Update()` 里把位移换算成激光输入：

```cpp
m_laserStates[i] = m_mouseSensitivity * m_mousePos[m_mouseAxisMapping[i]];
m_mousePos[m_mouseAxisMapping[i]] = 0;   // 读完就清零，所以注入的是"增量"
```

其中

```cpp
double Input::CalculateRealMouseSens(double sensSetting)
{
    double ppr = EstimatePprFromSens(abs(sensSetting));   // pow(200 / (sens * 0.1), 1.2)
    return sensSign * 6.0 / ppr;
}
```

也就是「1 个鼠标像素 = `6 / ppr` 弧度激光」。屏幕控制器反过来用这个公式，把"想转多少
角度"折算成"要注入多少像素"，所以**游戏内灵敏度滑块不会改变触屏旋钮的手感**（它只影响
真实鼠标 / 触控板）。

## 2. 三个版本，以及为什么最后是左右滑动

### 2.1 第一版：横纵位移直接相加（会乱蹦）

```cpp
const float travel = delta.x + delta.y;   // 旧代码
```

手指按在玻璃上时，拇指会以手腕 / 手掌为轴轻微转动，产生几十像素的纵向抖动。它被直接加进了
转动量里，于是激光在左右两边来回蹦。

### 2.2 第二版：绕旋钮中心转圈（不抖了，但不符直觉）

改成只取「手指绕旋钮圆心的角度变化」，量化成 24 咔哒/圈。抖动问题解决了，但它要求玩家
**绕着圆心画圈**；大多数人会下意识地在旋钮上左右划，结果几乎没有反应。

### 2.3 第三版（当前）：左右滑动 + 只看横向位移

- 手势：手指落在旋钮上，**左右滑动**。
- 只取横向位移 `pos.x - dragOrigin.x`，纵向分量完全丢弃 → 抖动无法进入激光。
- 位移按「屏幕短边的 3%」量化成一个咔哒（detent），每个咔哒让激光转 22.5°。
- 每个咔哒产生的鼠标像素**分帧平滑输出**，并沿用 PHAC 的「反向清队列」。

## 3. 实现细节

### 3.1 命中与手势

`HitTest()` 用 `knobRadius * kKnobGrabScale`（1.35 倍）作为抓取半径。手指落下后，
`Target::dragOrigin` 记住落点；**之后手指滑出旋钮、滑到屏幕任何位置，手势都继续有效**，
因为转动量是相对落点算的，不依赖手指是否还在圆里。

### 3.2 量化成咔哒

```cpp
const float travel = pos.x - target.dragOrigin.x;          // 只看水平
const float detentTravel = kKnobDetentFraction * unit;     // unit = min(w, h)
const int total = (int)(travel / detentTravel);            // 截断，正负对称
```

`total` 是「这次手势累计应该产生了几个咔哒」，把它和上一次的值比较，只把差值 `steps`
排进队列。关键点：

- 用**落点相对位移**而不是逐帧增量累加，所以抖动不会累积漂移；
- 手指在同一个咔哒区间里来回微动时 `total` 不变，不会产生任何输出；
- 手指往回滑会得到负的 `steps`，激光转回去，符合"相对滑动"的直觉。

### 3.3 每个咔哒折算成鼠标像素

```cpp
const float radiansPerPixel = MouseRadiansPerPixel();      // 6 / ppr
pixels = kKnobRadiansPerDetent / radiansPerPixel;
```

### 3.4 分帧平滑（PHAC 的 SMOOTHING_FACTOR）

PHAC 固件里一次咔哒被切成 `SMOOTHING_FACTOR = 6` 个插值事件，在 1 ms 的主循环里分摊
发出，余量带到下一帧。这里帧间隔约 17 ms，所以把除数调小成 `kKnobSmoothing = 2`：

```cpp
step = round(|pending| / kKnobSmoothing);   // 每帧发出剩余量的一半
step = clamp(step, 1, |pending|);           // 保证最后一两个像素也一定发出去
pending -= step;
InjectMouseMotion(±step, 0);
```

效果：一个咔哒大约 50 ms 走完，激光是「滑」过去而不是瞬移；因为总量守恒，反复转动不会
累积偏差。

### 3.5 反向清队列

```cpp
if(knobPendingDir[index] != 0 && knobPendingDir[index] != direction)
    knobPending[index] = 0.0f;     // 方向变了，丢掉旧方向的余量
```

这是 PHAC 在 `ec11.c` 的 `ec11_update()` 里「检测到方向变化就清空事件队列」的等价物：
快速左右来回划时，不会出现「旧方向还欠着几个像素、新手势又反着推」的甩头现象。

## 4. 参数速查

| 常量 | 当前值 | 含义 | 想改手感时 |
| --- | --- | --- | --- |
| `kKnobDetentFraction` | `0.030` | 一个咔哒需要的水平位移 = 屏幕短边 × 3% | 调大更钝更稳；调小更灵敏 |
| `kKnobRadiansPerDetent` | `kPi / 8`（22.5°） | 一个咔哒让激光转多少 | 调大转得更快 |
| `kKnobSmoothing` | `2.0` | 每帧发出剩余量的 1/N | `1.0` = 一帧发完（最跟手）；调大更绵 |
| `kKnobGrabScale` | `1.35` | 旋钮抓取区相对半径的放大倍数 | 调大更好按 |

## 5. 已知取舍

- **单次手势的行程受屏幕边界限制**：左旋钮在屏幕左侧，向左滑的余量比向右小；滑到头
  松开手指再滑一次即可。
- **斜着滑只算水平分量**：这是刻意的，纵向位移正是过去抖动之源。
- **触屏旋钮不受游戏内鼠标灵敏度影响**：注入的像素数已经按公式反算过，见第 1 节。

## 6. 界面提示

旋钮里画了左右两个箭头（chevron）提示「左右滑动」，中心的指针仍然显示激光转过的累计角度
（每咔哒 22.5°）。
