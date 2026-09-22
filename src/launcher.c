// DeskBoxRound.exe — A2 补丁套件启动器
//
// 职责（一次性，几秒内自退出）：
//   1. 以挂起主线程方式启动 DeskBox.exe
//   2. 向其注入同目录 deskbox_round.dll
//   3. 远程调用 DLL 的 RoundPatch_Entry 完成初始化
//   4. 恢复主线程，启动器退出——桌面不留任何常驻进程
//
// 用法：DeskBoxRound.exe [DeskBox.exe 完整路径]
//       缺省路径 = 启动器所在目录\..\DeskBox.exe（标准部署布局）
//                 或启动器所在目录\DeskBox.exe（平铺布局）

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <stdio.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shlwapi.lib")

static wchar_t g_dllPath[MAX_PATH] = {0};
static wchar_t g_exePath[MAX_PATH] = {0};

// 诊断日志（Release 时 DBG 宏为空即零开销）
#define DBG(...) do { fwprintf(stderr, __VA_ARGS__); fflush(stderr); } while (0)

static void FailBox(const wchar_t *msg)
{
    MessageBoxW(NULL, msg, L"DeskBox Round Patch", MB_ICONERROR);
    ExitProcess(1);
}

// 解析路径：DLL 与 exe 同目录查找；exe 支持命令行覆盖
static void ResolvePaths(const wchar_t *cmdline)
{
    GetModuleFileNameW(NULL, g_dllPath, MAX_PATH);
    wchar_t *slash = wcsrchr(g_dllPath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wchar_t baseDir[MAX_PATH];
    wcscpy_s(baseDir, MAX_PATH, g_dllPath);

    // DLL 恒在启动器同目录
    wcscat_s(g_dllPath, MAX_PATH, L"deskbox_round.dll");

    // exe 路径：命令行第 1 参 > 同目录 > 上级目录
    if (cmdline && cmdline[0]) {
        const wchar_t *p = cmdline;
        if (*p == L'"') { p++; wcscpy_s(g_exePath, MAX_PATH, p);
                          wchar_t *q = wcschr(g_exePath, L'"'); if (q) *q = 0; }
        else wcscpy_s(g_exePath, MAX_PATH, p);
        if (!PathFileExistsW(g_exePath)) FailBox(L"命令行指定的 DeskBox.exe 不存在");
        return;
    }
    wcscpy_s(g_exePath, MAX_PATH, baseDir);
    wcscat_s(g_exePath, MAX_PATH, L"DeskBox.exe");
    if (PathFileExistsW(g_exePath)) return;

    wcscpy_s(g_exePath, MAX_PATH, baseDir);             // 标准布局：..\DeskBox.exe
    PathRemoveBackslashW(g_exePath);                    // 尾部斜杠会让 RemoveFileSpec 失效
    PathRemoveFileSpecW(g_exePath);                     // 上级
    wcscat_s(g_exePath, MAX_PATH, L"\\DeskBox.exe");
    if (PathFileExistsW(g_exePath)) return;

    FailBox(L"未找到 DeskBox.exe：请把补丁放进 DeskBox 目录，或用命令行指定完整路径");
}

int wmain(int argc, wchar_t **argv)
{
    (void)argc;
    ResolvePaths(argc > 1 ? argv[1] : NULL);

    if (!PathFileExistsW(g_dllPath))
        FailBox(L"未找到 deskbox_round.dll：请确认补丁文件完整");

    // 1. 挂起启动 DeskBox
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    wchar_t workDir[MAX_PATH];
    wcscpy_s(workDir, MAX_PATH, g_exePath);
    PathRemoveFileSpecW(workDir);
    if (!CreateProcessW(g_exePath, NULL, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, workDir, &si, &pi))
        FailBox(L"启动 DeskBox.exe 失败（权限或路径问题）");
    DBG(L"[launcher] DeskBox 挂起启动 pid=%lu\n", pi.dwProcessId);

    // 2. 远程加载 DLL
    SIZE_T dllLen = (wcslen(g_dllPath) + 1) * sizeof(wchar_t);
    LPVOID remoteBuf = VirtualAllocEx(pi.hProcess, NULL, dllLen,
                                      MEM_COMMIT, PAGE_READWRITE);
    if (!remoteBuf || !WriteProcessMemory(pi.hProcess, remoteBuf, g_dllPath, dllLen, NULL)) {
        TerminateProcess(pi.hProcess, 1);
        FailBox(L"写入目标进程内存失败");
    }

    LPVOID loadLib = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                            "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0,
                                        (LPTHREAD_START_ROUTINE)loadLib,
                                        remoteBuf, 0, NULL);
    if (!hThread) {
        TerminateProcess(pi.hProcess, 1);
        FailBox(L"远程线程创建失败（可能被杀软拦截，请加白名单）");
    }
    WaitForSingleObject(hThread, 10000);
    DWORD loadTid = 0;
    GetExitCodeThread(hThread, &loadTid);   // LoadLibrary 返回基址低 32 位，仅判成败
    CloseHandle(hThread);
    VirtualFreeEx(pi.hProcess, remoteBuf, 0, MEM_RELEASE);
    DBG(L"[launcher] LoadLibrary 远程线程完成 exit=0x%08lX %s\n",
        loadTid, loadTid ? L"" : L"(失败!)");
    if (!loadTid) {
        ResumeThread(pi.hThread);
        TerminateProcess(pi.hProcess, 1);
        FailBox(L"DLL 加载失败");
    }

    // 2.5 远程调用 DLL 的 RoundPatch_Entry 完成初始化
    // 远程基址用 Toolhelp 快照获取——GetExitCodeThread 在 x64 上会把
    // 64 位基址截断成 32 位，不可用
    HMODULE hSelfDll = LoadLibraryW(g_dllPath);   // 本地加载只为拿导出函数偏移
    DBG(L"[launcher] 本地加载 hSelf=%p\n", (void*)hSelfDll);
    if (hSelfDll) {
        FARPROC entryLocal = GetProcAddress(hSelfDll, "RoundPatch_Entry");
        DBG(L"[launcher] 本地 Entry=%p\n", (void*)entryLocal);
        if (entryLocal) {
            HMODULE remoteBase = NULL;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pi.dwProcessId);
            DBG(L"[launcher] snap=%p\n", (void*)hSnap);
            if (hSnap != INVALID_HANDLE_VALUE) {
                MODULEENTRY32W me = { sizeof(me) };
                if (Module32FirstW(hSnap, &me)) {
                    do {
                        if (_wcsicmp(me.szModule, L"deskbox_round.dll") == 0) {
                            remoteBase = me.hModule;   // modBaseAddr，完整 64 位
                            DBG(L"[launcher] 远程基址=%p size=%lu\n",
                                (void*)me.hModule, me.modBaseSize);
                            break;
                        }
                    } while (Module32NextW(hSnap, &me));
                }
                CloseHandle(hSnap);
            }
            if (remoteBase) {
                uintptr_t delta = (uintptr_t)entryLocal - (uintptr_t)hSelfDll;
                LPVOID remoteEntry = (LPVOID)((uintptr_t)remoteBase + delta);
                DBG(L"[launcher] 远程 Entry=%p\n", remoteEntry);
                HANDLE hEntryThread = CreateRemoteThread(pi.hProcess, NULL, 0,
                                                         (LPTHREAD_START_ROUTINE)remoteEntry,
                                                         NULL, 0, NULL);
                DBG(L"[launcher] Entry 远程线程=%p\n", (void*)hEntryThread);
                if (hEntryThread) {
                    WaitForSingleObject(hEntryThread, 5000);
                    DWORD ec = 0;
                    GetExitCodeThread(hEntryThread, &ec);
                    DBG(L"[launcher] Entry 返回 %lu\n", ec);
                    CloseHandle(hEntryThread);
                }
            } else {
                DBG(L"[launcher] ❌ 快照中未找到 deskbox_round.dll\n");
            }
        } else {
            DBG(L"[launcher] ❌ GetProcAddress 未取到 RoundPatch_Entry err=%lu\n", GetLastError());
        }
        FreeLibrary(hSelfDll);
    }

    // 3. 恢复 DeskBox 主线程
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // 4. 启动器使命完成，退出
    DBG(L"[launcher] 完成\n");
    return 0;
}
