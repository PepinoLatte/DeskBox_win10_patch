# DeskBox Round Corners（DeskBox Win10 圆角补丁）

> 让 [DeskBox](https://deskbox.fun)（桌面盒子整理工具）在 **Windows 10** 上也能拥有 Win11 风格的窗口圆角。
> 进程内融合方案：**无独立常驻进程**，不改 DeskBox 任何文件，DeskBox 官方更新后补丁依然有效。

---

## 这是什么

DeskBox 是 WinUI3 应用，自带「窗口圆角」设置，但该设置依赖的 `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)` API 是 **Windows 11 独有**的——在 Win10 上静默失效（返回 `E_INVALIDARG`），窗口永远是直角。

本补丁通过 Windows 传统的 `SetWindowRgn` 区域裁剪实现圆角，原理与 Win11 的 DWM 圆角不同但视觉效果接近：

| | Win11 DWM 圆角 | 本补丁 |
|---|---|---|
| 原理 | DWM 合成器原生支持 | 窗口区域裁剪（GDI Region） |
| 抗锯齿 | 完美 | 边缘有轻微锯齿（1px，正常使用不明显） |
| 圆角半径 | 系统固定 8px | 可配置（默认 24px，4~200px） |
| Win10 支持 | ✗ | ✓ |

## 特性

- **零常驻进程**：圆角逻辑以 DLL 形态寄生在 DeskBox 进程内部，DeskBox 退出即消失。任务管理器不会多出任何进程
- **更新免疫**：只识别「进程窗口 + 窗口类名 `WinUIDesktopWin32WindowClass`」，不依赖版本内存偏移，DeskBox 官方更新后无需重打补丁
- **实时跟随**：窗口新建、移动、缩放、显示器拓扑变化都会自动重新裁剪
- **可配置**：圆角半径、启用开关均由 `deskbox_round.ini` 控制
- **可逆**：卸载脚本恢复所有原始设置；或运行中改 `enabled=0` 即时恢复直角
- **范围广**：纯 Win32 API 实现（仅依赖 user32/gdi32/shlwapi），Win7~Win11 x64 理论全适用（主要面向 Win10）

## 快速开始

### 环境要求

- Windows 10 x64（1809 及以上理论上均可；在 Win10 Pro 22H2 19045 上开发测试）
- DeskBox（安装位置不限，本补丁自动从注册表探测）
- 不需要管理员权限

### 安装

1. 下载 Release 或自行编译，得到补丁目录（含 `DeskBoxRound.exe`、`deskbox_round.dll`、`deskbox_round.ini`、`install.cmd`、`uninstall.cmd`）
2. **把补丁目录放到 DeskBox 安装目录内**（推荐），例如：

   ```
   D:\Software_Data\DeskBox\RoundPatch\   ← 补丁放这里
   ├── DeskBoxRound.exe                   ← 启动器
   ├── deskbox_round.dll                  ← 圆角核心
   ├── deskbox_round.ini                  ← 配置
   ├── install.cmd                        ← 安装脚本
   └── uninstall.cmd                      ← 卸载脚本
   ```

3. 双击 `install.cmd`，一键完成：
   - 开机自启注册表项改指启动器（原值备份到 `original_run_key.txt`）
   - 桌面/开始菜单快捷方式改指启动器（图标保持 DeskBox 原图标）

### 使用

- 此后**从桌面图标或开机自启启动**的 DeskBox 都自动带圆角
- 注意：若直接双击 `DeskBox.exe` 本体，本次启动无补丁（重新从快捷方式启动即可）
- 调整圆角：编辑 `deskbox_round.ini` 的 `radius` 值 → 重启 DeskBox 生效

### 卸载

双击 `uninstall.cmd`，自动恢复：
- 开机自启注册表项 → 指回 DeskBox.exe 本体
- 桌面/开始菜单快捷方式 → 指回 DeskBox.exe 本体
- 之后可整体删除补丁目录；运行中的 DeskBox 重启后即恢复直角

## 工作原理

```
┌────────────────────────────────────────────────────┐
│  DeskBoxRound.exe（启动器，存活 <5 秒）              │
│  1. CREATE_SUSPENDED 挂起启动 DeskBox.exe           │
│  2. CreateRemoteThread → LoadLibraryW 注入 DLL      │
│  3. CreateRemoteThread → 远程调用 RoundPatch_Entry  │
│  4. ResumeThread 恢复 DeskBox 主线程                │
│  5. 启动器退出                                      │
└────────────────────────────────────────────────────┘
                    ↓ 注入
┌────────────────────────────────────────────────────┐
│  DeskBox.exe 进程内部                               │
│  ┌──────────────────────────────────────────────┐  │
│  │ deskbox_round.dll                            │  │
│  │ · SetWinEventHook 监听 CREATE/LOCATIONCHANGE │  │
│  │ · 常驻监听线程（GetMessage 消息泵）            │  │
│  │ · 对小组件窗口 SetWindowRgn 圆角裁剪           │  │
│  │ · DeskBox 退出 → DLL 随进程消亡               │  │
│  └──────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────┘
```

设计取舍详解：

1. **为什么不用 Win11 API**：`DWMWA_WINDOW_CORNER_PREFERENCE`（属性 33）在 Win10 的 dwmapi 中返回 `E_INVALIDARG`，硬编码无效
2. **为什么不用独立常驻进程**：注入后 DLL 在进程内用 `SetWinEventHook` 拿到窗口事件，比进程外监听更实时且零额外进程
3. **为什么 hook 要装在 DLL 自建的线程上**：`WINEVENT_OUTOFCONTEXT` 模式下回调投递到「安装 hook 的线程」的消息队列；注入用的远程线程跑完即退，hook 会随线程死亡失效，因此 Entry 里专门 `CreateThread` 一个跑 `GetMessage` 消息泵的常驻线程
4. **为什么不改 DeskBox 文件**：二进制补丁每次更新都要重打；运行时注入只认类名，版本免疫
5. **为什么不 hook DirectWrite/渲染做抗锯齿圆角**：复杂度和稳定性成本远超收益；region 裁剪的 1px 边缘锯齿在深色桌面背景上基本不可见

## 常见问题

**Q：杀毒软件报毒怎么办？**
启动器使用了进程注入（`CreateRemoteThread` + `LoadLibraryW`），这是杀软重点监控的行为模式，属于本类补丁的固有误报。请将补丁目录加入白名单。本补丁全部源码开源，可自行编译验证。

**Q：圆角边缘有锯齿 / 白边？**
`SetWindowRgn` 的固有特性，非 bug。Win11 的 DWM 圆角有合成器级抗锯齿而 region 裁剪没有。可通过降低半径（16px）减轻观感。

**Q：DeskBox 官方更新后还能用吗？**
能。补丁不依赖任何版本信息，只要 DeskBox 仍使用同一窗口类名 `WinUIDesktopWin32WindowClass`（该类名来自微软 WinUI3 框架本身，DeskBox 官方不控制），补丁即持续有效。

**Q：直接双击 DeskBox.exe 启动没有圆角？**
设计如此。补丁通过启动器注入，绕过启动器就没有注入环节。建议固定使用快捷方式/开机自启。

**Q：为什么 Win11 上不需要这个补丁？**
Win11 的 DeskBox 自带圆角设置直接生效（DWM API 可用）。本补丁在 Win11 上运行也无害（效果重复），但没必要安装。

**Q：支持 32 位 DeskBox 吗？**
不支持。当前 DeskBox 为 x64 原生程序（NativeAOT），补丁只编译了 x64 版本。

## 自行编译

需要 LLVM/Clang（任意较新版本，无需 Visual Studio）：

```bash
# 圆角核心 DLL
clang -O2 -shared -o deskbox_round.dll src/deskbox_round.c -luser32 -lgdi32 -lshlwapi

# 启动器
clang -O2 -municode -o DeskBoxRound.exe src/launcher.c -luser32 -lkernel32 -lshlwapi -Wl,/subsystem:console
```

## 已知限制

- 圆角边缘 1px 锯齿（见 FAQ）
- 必须通过启动器（快捷方式/自启）启动才生效
- 极小窗口的圆角半径自动缩小，可能与预期不同
- 杀软可能误报启动器（见 FAQ）

## 许可证

MIT

## 免责声明

本补丁为社区第三方工具，与 DeskBox 官方无关。请从官方渠道下载 DeskBox 本体：[deskbox.fun](https://deskbox.fun)。
