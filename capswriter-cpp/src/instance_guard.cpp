#include "instance_guard.h"

#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <vector>

#include "util.h"
#include "version.h"

#pragma comment(lib, "version.lib")

namespace capswriter {

namespace {

// 与本项目两个前端匹配的进程名（小写，不区分大小写比较）。
const wchar_t* kExeNames[] = {L"capswriter.exe", L"capswriter-gui.exe"};

// WM_CLOSE 后等待旧进程自行退出的宽限时间（毫秒）。
constexpr DWORD kGracefulExitTimeoutMs = 1500;
// 强杀后等待进程句柄信号的超时。
constexpr DWORD kForceExitTimeoutMs = 3000;

std::wstring LowerAscii(std::wstring s) {
  for (wchar_t& c : s) {
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
  }
  return s;
}

// 读取进程的完整 exe 路径。
bool GetProcessPath(DWORD pid, std::wstring* out) {
  HANDLE proc =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) return false;
  wchar_t buf[MAX_PATH];
  DWORD len = MAX_PATH;
  const bool ok = QueryFullProcessImageNameW(proc, 0, buf, &len) && len > 0;
  CloseHandle(proc);
  if (!ok) return false;
  *out = std::wstring(buf, len);
  return true;
}

// EnumWindows 回调上下文：按 pid 找窗口。
struct FindWindowCtx {
  DWORD pid = 0;
  HWND hwnd = nullptr;      // 找到的窗口（PostToAllByPid 记录最后一个）
  LONG best_area = -1;      // FindMainWindowByPid 用：当前最大窗口面积
  UINT msg = 0;             // PostToAllByPid 用：要投递的消息
};

// 找出 pid 的"主窗口"，用于给旧实例发 WM_CLOSE：
//  - 排除 WS_EX_TOOLWINDOW（HintToast 等辅助窗口）
//  - 排除 Qt 托盘消息窗口（类名含 "TrayIcon"，如 Qt6TrayIconMessageWindowClass）
//  - 剩余窗口中取面积最大者（最可能是主窗口）
// 注意：GUI 主窗口正常模式下从不 show()，可能没有原生句柄，找不到
// 也没关系——调用方（ReplaceOlder）有强杀兜底。
BOOL CALLBACK FindMainWindowByPid(HWND hwnd, LPARAM lparam) {
  auto* ctx = reinterpret_cast<FindWindowCtx*>(lparam);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != ctx->pid) return TRUE;

  const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (ex & WS_EX_TOOLWINDOW) return TRUE;

  wchar_t cls[128] = {};
  GetClassNameW(hwnd, cls, 128);
  if (wcsstr(cls, L"TrayIcon") != nullptr) return TRUE;

  RECT rc{};
  if (!GetWindowRect(hwnd, &rc)) return TRUE;
  const LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
  if (area > ctx->best_area) {
    ctx->best_area = area;
    ctx->hwnd = hwnd;
  }
  return TRUE;  // 继续枚举，取面积最大者
}

// 向 pid 的每个顶层窗口投递 ctx->msg（至少一个能收到即可）。
// 不能只发第一个窗口：不同窗口可能由不同子类处理，全发最稳。
BOOL CALLBACK PostToAllByPid(HWND hwnd, LPARAM lparam) {
  auto* ctx = reinterpret_cast<FindWindowCtx*>(lparam);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == ctx->pid) {
    PostMessageW(hwnd, ctx->msg, 0, 0);
    ctx->hwnd = hwnd;
  }
  return TRUE;
}

}  // namespace

std::string AppVersion::ToString() const {
  char buf[64];
  snprintf(buf, sizeof(buf), "%d.%d.%d.%d", major, minor, patch, build);
  return buf;
}

bool InstanceGuard::Acquire(const wchar_t* mutex_name, std::string* err) {
  mutex_ = CreateMutexW(nullptr, FALSE, mutex_name);
  if (!mutex_) {
    if (err) {
      *err = "CreateMutexW failed (error " + std::to_string(GetLastError()) +
             ")";
    }
    return false;
  }
  mutex_existed_ = (GetLastError() == ERROR_ALREADY_EXISTS);
  if (mutex_existed_ && err) {
    *err = "another instance is already running";
  }
  return !mutex_existed_;
}

bool InstanceGuard::FindRunningInstance() {
  old_pid_ = 0;
  old_exe_path_.clear();

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;

  PROCESSENTRY32W pe{sizeof(PROCESSENTRY32W)};
  bool found = false;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (pe.th32ProcessID == GetCurrentProcessId()) continue;
      const std::wstring name = LowerAscii(pe.szExeFile);
      bool match = false;
      for (const wchar_t* k : kExeNames) {
        if (name == k) {
          match = true;
          break;
        }
      }
      if (!match) continue;
      // 只认能取到完整路径的进程（避免刚退出的进程留下残影）。
      std::wstring path;
      if (!GetProcessPath(pe.th32ProcessID, &path)) continue;
      old_pid_ = pe.th32ProcessID;
      old_exe_path_ = path;
      found = true;
      break;
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return found;
}

bool InstanceGuard::ShouldReplaceOlder() {
  if (!FindRunningInstance()) return false;
  AppVersion old_ver;
  if (!ReadFileVersion(old_exe_path_, &old_ver)) return false;
  return CurrentVersion() > old_ver;
}

bool InstanceGuard::ReplaceOlder(std::string* err) {
  if (!FindRunningInstance()) {
    if (err) *err = "running instance no longer exists";
    return true;  // 旧进程已经退出，无需处理
  }

  HANDLE proc = OpenProcess(
      SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
      FALSE, old_pid_);
  if (!proc) {
    if (err) {
      *err = "cannot open the running instance (error " +
             std::to_string(GetLastError()) + ")";
    }
    return false;
  }

  // 先尝试优雅退出：给旧进程的主窗口发 WM_CLOSE。
  // （控制台版没有窗口；GUI 版关闭按钮默认隐藏到托盘，多半等不到退出，
  // 因此下面有强杀兜底。）
  FindWindowCtx ctx{old_pid_, nullptr, -1, 0};
  EnumWindows(FindMainWindowByPid, reinterpret_cast<LPARAM>(&ctx));
  if (ctx.hwnd) PostMessageW(ctx.hwnd, WM_CLOSE, 0, 0);

  const DWORD wait = WaitForSingleObject(proc, kGracefulExitTimeoutMs);
  if (wait == WAIT_OBJECT_0) {
    CloseHandle(proc);
    return true;
  }

  // 优雅退出超时，强杀。
  if (!TerminateProcess(proc, 0)) {
    const DWORD e = GetLastError();
    CloseHandle(proc);
    if (err) {
      *err = "failed to terminate the running instance (error " +
             std::to_string(e) +
             "); it may run with administrator privileges, please exit it "
             "manually first";
    }
    return false;
  }
  WaitForSingleObject(proc, kForceExitTimeoutMs);
  CloseHandle(proc);
  return true;
}

UINT InstanceGuard::ActivateMessage() {
  // 系统级注册：两个进程用相同字符串会得到相同 ID。
  static const UINT msg =
      RegisterWindowMessageW(L"CapsWriter-Offline.ActivateWindow");
  return msg;
}

bool InstanceGuard::ActivateExisting() {
  if (!FindRunningInstance()) return false;

  // 向已有实例的所有顶层窗口投递自定义激活消息，由对方在自己的进程内
  // 显示主窗口。不能直接 ShowWindow 对方的窗口——GUI 主窗口正常模式下
  // 从不 show()、往往还没有原生句柄，EnumWindows 只会命中 Qt 隐藏的
  // 托盘消息窗口（Qt6TrayIconMessageWindowClass），把它顶到前台就是
  // 一个全白、无标题栏、关不掉的"幽灵窗口"。
  FindWindowCtx ctx{old_pid_, nullptr, -1, ActivateMessage()};
  EnumWindows(PostToAllByPid, reinterpret_cast<LPARAM>(&ctx));
  return ctx.hwnd != nullptr;
}

AppVersion InstanceGuard::CurrentVersion() {
  wchar_t buf[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  AppVersion v;
  if (len == 0 || len >= MAX_PATH ||
      !ReadFileVersion(std::wstring(buf, len), &v)) {
    // 退路：用编译期版本宏（正常情况下不会走到）。
    v = {};
    v.major = CAPSWRITER_VERSION_MAJOR;
    v.minor = CAPSWRITER_VERSION_MINOR;
    v.patch = CAPSWRITER_VERSION_PATCH;
    v.build = CAPSWRITER_VERSION_BUILD;
  }
  return v;
}

bool InstanceGuard::ReadFileVersion(const std::wstring& exe_path,
                                    AppVersion* out) {
  if (!out) return false;
  DWORD unused = 0;
  const DWORD size = GetFileVersionInfoSizeW(exe_path.c_str(), &unused);
  if (size == 0) return false;
  std::vector<unsigned char> data(size);
  if (!GetFileVersionInfoW(exe_path.c_str(), 0, size, data.data())) {
    return false;
  }
  VS_FIXEDFILEINFO* ffi = nullptr;
  UINT ffi_len = 0;
  if (!VerQueryValueW(data.data(), L"\\",
                      reinterpret_cast<void**>(&ffi), &ffi_len) ||
      !ffi) {
    return false;
  }
  out->major = HIWORD(ffi->dwFileVersionMS);
  out->minor = LOWORD(ffi->dwFileVersionMS);
  out->patch = HIWORD(ffi->dwFileVersionLS);
  out->build = LOWORD(ffi->dwFileVersionLS);
  return true;
}

}  // namespace capswriter
