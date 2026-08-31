#pragma once

#include <windows.h>

#include <string>

namespace capswriter {

// 单实例互斥体名称。固定不变（跨版本一致），新版本靠它发现旧版本进程。
inline constexpr wchar_t kSingleInstanceMutexName[] =
    L"Local\\CapsWriter-Offline-SingleInstance";

// 应用版本号（与 version.h / version.rc 一致），从 exe 版本资源读出。
struct AppVersion {
  int major = 0;
  int minor = 0;
  int patch = 0;
  int build = 0;

  bool operator==(const AppVersion& o) const {
    return major == o.major && minor == o.minor && patch == o.patch &&
           build == o.build;
  }
  bool operator>(const AppVersion& o) const {
    if (major != o.major) return major > o.major;
    if (minor != o.minor) return minor > o.minor;
    if (patch != o.patch) return patch > o.patch;
    return build > o.build;
  }
  std::string ToString() const;
};

// 单实例守卫 + "新版本替代旧版本"。
//
// 两个前端 main() 的最开头这样用：
//
//   InstanceGuard guard;          // 必须存活到进程退出，保持互斥体句柄
//   std::string err;
//   if (!guard.Acquire(kSingleInstanceMutexName, &err)) {
//     // 已有实例在运行：
//     if (guard.ShouldReplaceOlder()) {
//       guard.ReplaceOlder(&err); // 本实例是更新版本：结束旧进程，继续启动
//     } else {
//       guard.ActivateExisting(); // 同版本/旧版本：唤出已有实例，本实例退出
//       return 0;
//     }
//   }
class InstanceGuard {
 public:
  // 检测并"认领"单实例互斥体。返回 true 表示没有其他实例，可直接继续；
  // 返回 false 表示已有实例在运行（mutex_existed_ = true）。
  // 注意：本函数只检测存在性、不抢占互斥体；若决定替代旧进程，结束旧进程
  // 后本实例直接继续运行即可（保持 mutex_ 句柄存活即可维持"已占用"状态）。
  bool Acquire(const wchar_t* mutex_name, std::string* err);

  // 已有实例运行时调用：判断本实例的版本是否比正在运行的实例更新。
  // 比较对象是正在运行的 capswriter.exe / capswriter-gui.exe 进程的
  // 文件版本；版本读取失败时保守返回 false（不替代）。
  bool ShouldReplaceOlder();

  // 结束正在运行的旧版本进程：先给其主窗口发 WM_CLOSE 请求优雅退出
  // （控制台版无窗口则直接跳过），超时后强杀。
  // 返回 false 时 err 给出原因（通常是权限不足，旧进程以管理员运行）。
  // 旧进程恰好已退出时视为成功。
  bool ReplaceOlder(std::string* err);

  // 通知已有实例显示主窗口：向该进程的所有顶层窗口投递自定义激活消息
  // （ActivateMessage()），由对方在自己的进程内把主窗口带到前台。
  // GUI 实例会安装 native event filter 响应此消息；控制台版不响应，
  // 因此本函数对控制台实例返回 false（找不到可投递的窗口）。
  bool ActivateExisting();

  // 显式释放互斥体句柄（CloseHandle）。本类无析构函数，正常退出交由 OS 回收；
  // 但"重启程序"时需要让新进程能认领互斥体，故由重启流程在退出前主动释放。
  // 幂等：句柄已释放/为空时静默返回。
  void Release();

  // 跨进程注册的"显示主窗口"消息 ID。两个进程各自调用
  // RegisterWindowMessageW 相同字符串会得到同一个系统级消息 ID，
  // 用于 ActivateExisting() 通知旧实例唤起自己的主窗口。
  static UINT ActivateMessage();

  // 本 exe 的文件版本（读取自身 exe 的版本资源）。
  static AppVersion CurrentVersion();

  // 从任意 exe 文件读取版本资源；失败返回 false。
  static bool ReadFileVersion(const std::wstring& exe_path, AppVersion* out);

 private:
  // 扫描进程表找到正在运行的实例，填充 old_pid_ / old_exe_path_。
  // exe 名不区分大小写，匹配 capswriter.exe 与 capswriter-gui.exe；
  // 找不到返回 false。
  bool FindRunningInstance();

  HANDLE mutex_ = nullptr;
  bool mutex_existed_ = false;
  DWORD old_pid_ = 0;
  std::wstring old_exe_path_;
};

}  // namespace capswriter
