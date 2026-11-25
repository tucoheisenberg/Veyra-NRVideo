# 键鼠控制（PS5 手柄控制 · 采集卡）

更新：2026-11-26。基于源码 2.2.1。本记录区分「已做的检查」和「未验证范围」，后者需要在 Windows 实机构建后补测。

## 功能

采集卡负责画面和声音，现有的「PS5 手柄控制」用一个只传输入的 PS5 远程游玩会话把手柄状态发给主机。本功能把**键盘和鼠标转换成同一个虚拟 DualSense 状态**，与本机手柄（如有）合并后从同一通道发出。PS5 端无需任何改动或额外硬件。

- 采集卡对话框 →「PS5 手柄控制」→ 打开「键盘鼠标控制」→ 点击画面开始。
- 捕获期间鼠标隐藏并锁在窗口内；鼠标移动按速度换算成右摇杆幅度。
- 两套键位：**步行**和**驾驶**。默认 `` ` `` 键切换；也可在键位文件里写 `F = Triangle, Driving`，上车时一并切换。
- 驾驶键位下 A/D 逐渐打方向（`SteerRampMs`），W/S 是 R2/L2 油门刹车。
- `Ctrl+Alt+Shift+Z` 释放；切到别的窗口、PS5 会话断开、关闭该开关或停止手柄控制时也会自动释放并松开所有按键。
- 键位文件中未绑定的 F1–F12 仍由 Veyra 处理（F8 录制、F9 即时回放）。

键位文件：`<远程游玩配置目录>/kbm-mapping.ini`，与 `settings.ini` 同目录。首次使用或点「编辑键位文件」时写入默认内容（带中文注释）。修改后下次点击画面开始时生效。文件中无法识别的行会被忽略，并在提示和诊断日志中列出。

默认键位参照 GTA 系列的 PS 手柄布局。**GTA6 的实际按键没有核对**，请在游戏「设置 → 控制」里对照后修改键位文件。

## 鼠标 → 右摇杆

摇杆控制的是转动速度，所以按鼠标速度而不是位移来换算：

```
speed      = 最近 WindowMs 内的鼠标计数 / WindowMs           (计数/毫秒)
n          = min(1, (speed × 灵敏度 / FullSpeed) ^ Curve)
deflection = Deadzone + (1 − Deadzone) × n                    (有移动时)
```

- `Deadzone` 是反死区：游戏会忽略摇杆起始的一段行程，鼠标稍动就至少推到这里。
- 方向保持鼠标移动方向（径向计算，不按轴分别饱和）；`YRatio`、`InvertY` 调整纵向。
- 鼠标停下后，摇杆在一个 `WindowMs` 内回中（默认 12 ms）。
- 灵敏度由界面滑块调节（10–300%，保存为 `ps5KbmSensitivity`），其余参数在键位文件里改。

限制：游戏对摇杆另有加速、死区和瞄准辅助，用鼠标转摇杆的手感无法与原生键鼠游戏一致，只能靠这些参数尽量调近。

## 合并规则

按钮按位相或；每根摇杆取幅度较大的一方（键鼠或手柄）；扳机取较大值。手柄的触摸板与体感数据原样保留。未捕获时直接发送手柄状态，原有行为不变。

## 实现

| 文件 | 作用 |
|---|---|
| `include/veyra/remoteplay/KeyboardMouseMapper.h`、`src/remoteplay/KeyboardMouseMapper.cpp` | 纯逻辑（不依赖 Win32/Qt）：键名表、INI 解析、两套键位、鼠标换算、方向渐进、滚轮脉冲、合并 |
| `include/veyra/ui/KeyboardMouseCapture.h`、`src/ui/KeyboardMouseCapture.cpp` | Win32 捕获：参照 `MoonlightInputCapture`，在 Qt 之前的原生事件过滤器里处理，原始鼠标输入，`ClipCursor`，失去前台时释放，丢弃键盘自动重复 |
| `src/ui/bridge/RemotePlay.cpp` | 在 8 ms 手柄控制定时器里合并；新增 `ps5KbmCapture`、`ps5KbmOpenMapping`；`ps5ControlSet` 增加 `kbm`、`kbmSensitivity`；会话不在 connected 状态时释放 |
| `src/ui/QmlPlayerBridge_p.h`、`include/veyra/ui/QmlPlayerBridge.h` | 成员与接口声明 |
| `qml/Veyra/CaptureSourceDialog.qml` | 开关、灵敏度、键位行与「编辑键位文件」按钮 |
| `qml/Veyra/Main.qml` | 键鼠控制已开启、PS5 已连接且尚未捕获时，点击画面开始捕获 |
| `i18n/catalog.json` | 新增 18 条字符串，9 种语言 |
| `tests/remoteplay/KeyboardMouseMapperTests.cpp`、`CMakeLists.txt` | 新测试目标 `veyra_remoteplay_kbm_tests`；新源文件加入 `veyra_qml_bridge` |

切换键位时，正按着的键在松开前一律忽略：在步行键位按住 W 切到驾驶，不会立即变成全油门；切换键的自动重复也不会再切回去。

## 已做的检查（Linux，非 Windows 实机）

- `KeyboardMouseMapperTests`：Linux g++ 11 与 g++ 13 下均为 31/31 通过；同样的测试在 `-fsanitize=address,undefined` 下 31/31 通过，无报告。覆盖默认键位能否无警告解析、错误行告警与数值截断、斜向归一、慢走、反死区、饱和、停下后回中、线性段、灵敏度、键位切换与按住键抑制、自动重复、方向渐进与回正、滚轮分次按下、与手柄合并。
- mingw-w64 GCC 13 用 Linux 版 Qt 6 头文件做 `-fsyntax-only` 检查：`KeyboardMouseCapture.cpp` 与 `KeyboardMouseMapper.cpp` 在 `-Wall -Wextra` 下无错误、无警告。`RemotePlay.cpp`（定义 `VEYRA_ENABLE_REMOTEPLAY`）在 NGX 头文件打桩后，本文件与 `QmlPlayerBridge_p.h` 均无错误；剩余 6 个错误全部来自打桩的 NGX 头文件和 mingw 的 ddraw/strmif 头文件冲突。另外人为插入一处错误，确认编译器确实检查到了本文件主体。
- 两个 QML 文件可被 `qmlformat` 解析；`scripts/i18n/extract.py --check`：9 种语言缺失 0，占位符问题 0。

## 未验证（需 Windows 实机）

- MSVC 2022 + Qt 6.8.3 完整构建、moc，以及现有测试回归。
- 实机流程：采集卡 + PS5 手柄控制 + 键鼠捕获；与本机手柄同时使用；全屏；多显示器下的 `ClipCursor`；释放后光标恢复。
- GTA6 实际键位、鼠标瞄准手感和推荐参数；局域网/Wi-Fi 下的输入延迟。没有物理端到端延迟数据。
- 与 Moonlight 键鼠捕获的互斥（两者不会在同一来源同时出现，但未实测）。
