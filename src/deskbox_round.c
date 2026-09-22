// deskbox_round.dll — DeskBox 桌面盒子 Win10 圆角补丁（进程内融合版）
//
// 原理：被启动器注入 DeskBox.exe 进程后，在进程内用 SetWinEventHook 监听
// 窗口创建/位置/尺寸变化，对所有 DeskBox 桌面小组件窗口（类名
// WinUIDesktopWin32WindowClass 且无边框）应用 SetWindowRgn 圆角裁剪。
// DLL 生命周期与 DeskBox 进程一致——桌面上不产生任何常驻进程。
//
// Win10 通用性：仅依赖 user32/gdi32/kernel32/shlwapi，Win7~Win11 x64 全适用。
// 版本免疫：只认「自身进程窗口 + 窗口类名」，不依赖任何版本偏移量，
//          DeskBox 更新后无需重打补丁。
//
// 导出：
//   RoundPatch_Entry      注入入口（启动器远程调用）
//   RoundPatch_Reload     运行中重读 ini 配置
//   Uninstall_RoundPatch  运行中卸载（恢复直角并自我卸载）

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdarg.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shlwapi.lib")

// ---------- 配置 ----------
#define TARGET_CLASS       L"WinUIDesktopWin32WindowClass"
#define CONFIG_FILE_NAME   L"deskbox_round.ini"
#define DEFAULT_RADIUS     24          // 圆角半径（像素），24px ≈ Win11 风格

static HMODULE  g_self        = NULL;   // 自身模块句柄
static HWINEVENTHOOK g_hookCreate = NULL;
static HWINEVENTHOOK g_hookLocate = NULL;
static DWORD    g_radius      = DEFAULT_RADIUS;
static BOOL     g_enabled     = TRUE;
static wchar_t  g_configPath[MAX_PATH] = {0};
static DWORD    g_monitorTid  = 0;      // 监听线程 ID

// ---------- 配置读取 ----------
// ini 格式（放 DLL 同目录，值只允许数字）：
//   [round]
//   enabled=1        ; 1=启用圆角 0=临时关闭（不用卸载）
//   radius=24        ; 圆角半径像素，4~200
static void LoadConfig(void)
{
    GetModuleFileNameW(g_self, g_configPath, MAX_PATH);
    wchar_t *slash = wcsrchr(g_configPath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(g_configPath, MAX_PATH, CONFIG_FILE_NAME);

    // GetPrivateProfileInt 自带默认值回退，文件不存在也安全
    g_radius  = GetPrivateProfileIntW(L"round", L"radius",  DEFAULT_RADIUS, g_configPath);
    g_enabled = GetPrivateProfileIntW(L"round", L"enabled", 1, g_configPath) != 0;

    // 半径合法性钳制
    if (g_radius < 4)   g_radius = 4;
    if (g_radius > 200) g_radius = 200;
}

// ---------- 圆角裁剪 ----------
// 半径以像素绝对值生效；高 DPI 下窗口尺寸已随系统放大，观感一致
static void ApplyRoundCorners(HWND hwnd)
{
    if (!g_enabled || !hwnd || !IsWindow(hwnd)) return;

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    // 半径不得超过短边一半，否则 region 退化成胶囊形
    int r = (int)g_radius;
    int half = (w < h ? w : h) / 2;
    if (r > half) r = half;

    // CreateRoundRectRgn 的最后两参是圆角椭圆直径（2*r），
    // +1 修正右/下边界像素不归属 region 的 GDI 历史行为
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, r * 2, r * 2);
    if (!rgn) return;

    // 返回值忽略：窗口已销毁时 SetWindowRgn 失败属正常竞态
    SetWindowRgn(hwnd, rgn, TRUE);
    // 成功后 region 所有权归系统，无需 DeleteObject
}

// 判断是否为目标窗口：类名匹配 + 可见 + 顶层 + 无标题栏。
// 类名是跨版本稳定标识；无标题栏校验排除 DeskBox 主设置窗等常规窗口。
// 小组件窗实测 style=0x14080000（WS_VISIBLE|WS_CLIPSIBLINGS，无边框无标题）
static BOOL IsTargetWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) return FALSE;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return FALSE;

    wchar_t cls[64] = {0};
    if (!GetClassNameW(hwnd, cls, 64)) return FALSE;
    if (wcscmp(cls, TARGET_CLASS) != 0) return FALSE;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CAPTION) == WS_CAPTION) return FALSE;
    return TRUE;
}

// 对本进程所有目标窗口做一次全量补裁（初始化/重载配置时用）
static void ApplyAllCurrentWindows(void)
{
    DWORD selfPid = GetCurrentProcessId();
    HWND hwnd = NULL;
    do {
        hwnd = FindWindowExW(NULL, hwnd, TARGET_CLASS, NULL);
        if (hwnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == selfPid && IsTargetWindow(hwnd)) {
                ApplyRoundCorners(hwnd);
            }
        }
    } while (hwnd);
}

// ---------- WinEventHook 回调 ----------
// EVENT_OBJECT_CREATE：新窗口出现（小组件新建/重挂载）
// EVENT_OBJECT_LOCATIONCHANGE：窗口移动/缩放/DPI 变化（region 需重算）
static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event,
                                  HWND hwnd, LONG idObject, LONG idChild,
                                  DWORD idThread, DWORD dwmsEventTime)
{
    (void)hook; (void)idThread; (void)dwmsEventTime;

    // 只关心窗口本体（OBJID_WINDOW=0，idChild=0）
    if (idObject != OBJID_WINDOW || idChild != 0) return;
    if (!hwnd || !IsTargetWindow(hwnd)) return;

    switch (event) {
    case EVENT_OBJECT_CREATE:
    case EVENT_OBJECT_LOCATIONCHANGE:
        ApplyRoundCorners(hwnd);
        break;
    default:
        break;
    }
}

// ---------- 常驻监听线程 ----------
// WINEVENT_OUTOFCONTEXT 的回调投递到「安装 hook 的线程」的消息队列。
// 远程 Entry 线程跑完即退——hook 会随线程死亡而失效，
// 因此 hook 必须安装在一个长期存活、跑着消息泵的线程上。
// 本线程 GetMessage 阻塞常驻；DeskBox 进程退出时随之消亡。
static DWORD WINAPI MonitorThread(LPVOID lpParam)
{
    (void)lpParam;

    g_hookCreate = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
                                   NULL, WinEventProc,
                                   GetCurrentProcessId(), 0,
                                   WINEVENT_OUTOFCONTEXT);
    g_hookLocate = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                   NULL, WinEventProc,
                                   GetCurrentProcessId(), 0,
                                   WINEVENT_OUTOFCONTEXT);

    // 补裁 hook 安装前已存在的存量窗口
    ApplyAllCurrentWindows();

    // 消息泵：WinEvent 回调在此线程投递执行
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// 清理 hook 并恢复所有窗口直角（供 Reload 关闭开关 / Uninstall 共用）
static void Teardown(void)
{
    if (g_monitorTid) PostThreadMessageW(g_monitorTid, WM_QUIT, 0, 0);
    if (g_hookCreate) { UnhookWinEvent(g_hookCreate); g_hookCreate = NULL; }
    if (g_hookLocate) { UnhookWinEvent(g_hookLocate); g_hookLocate = NULL; }

    DWORD selfPid = GetCurrentProcessId();
    HWND hwnd = NULL;
    do {
        hwnd = FindWindowExW(NULL, hwnd, TARGET_CLASS, NULL);
        if (hwnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == selfPid) SetWindowRgn(hwnd, NULL, TRUE);
        }
    } while (hwnd);
}

// ---------- 导出：注入入口 ----------
// 启动器通过 CreateRemoteThread 调用
__declspec(dllexport)
DWORD WINAPI RoundPatch_Entry(LPVOID lpParam)
{
    (void)lpParam;

    // 防重复初始化（理论上不会发生，防御性保留）
    if (g_monitorTid) return 0;

    LoadConfig();

    HANDLE h = CreateThread(NULL, 0, MonitorThread, NULL, 0, &g_monitorTid);
    if (h) CloseHandle(h);
    return 0;
}

// ---------- 导出：运行中重载配置 ----------
// 修改 ini 后由外部触发；enabled=0 时会先恢复直角再停监听，
// 重新 enabled=1 需重启 DeskBox（或由启动器重新注入）——简单可靠
__declspec(dllexport)
void WINAPI RoundPatch_Reload(void)
{
    LoadConfig();
    if (g_enabled) {
        ApplyAllCurrentWindows();
    } else {
        Teardown();
    }
}

// ---------- 导出：运行中卸载 ----------
// 恢复直角 + 停监听 + 自我卸载（FreeLibraryAndExitThread 必须最后调用）
__declspec(dllexport)
void WINAPI Uninstall_RoundPatch(void)
{
    Teardown();
    FreeLibraryAndExitThread(g_self, 0);
}

// ---------- DllMain ----------
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_self = hinst;
        DisableThreadLibraryCalls(hinst);
        // loader lock 纪律：DllMain 内不做初始化，由启动器调 RoundPatch_Entry
        break;
    case DLL_PROCESS_DETACH:
        // 进程退出路径：hook 随进程消亡，无需清理
        break;
    }
    return TRUE;
}
