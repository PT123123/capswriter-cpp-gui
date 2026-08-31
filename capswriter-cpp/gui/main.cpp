// Qt 托盘前端:常驻系统托盘的 CapsWriter C++ 客户端。
// 与控制台前端 (src/main.cpp) 共用同一套核心代码 (capswriter_core):
// 模型加载与钩子安装复刻控制台版的启动序列,只是把 Sleep(INFINITE)
// 换成了 Qt 事件循环,并把日志与识别结果桥接到主窗口。
//
// 线程模型:
//  - 主线程:Qt 事件循环,窗口/托盘,以及一个 100ms 的定时器从线程
//    安全队列取日志行/识别结果刷新界面
//  - 加载线程:加载配置与模型(约 3s,不阻塞托盘出现),完成后回到
//    主线程装钩子(钩子线程由 HotkeyListener 自管,不占 Qt 循环)
//  - 识别线程:DictationController 的 worker(核心库自管)

#include <windows.h>
#include <shlobj.h>

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../src/asr_engine.h"
#include "../src/config.h"
#include "../src/dictation.h"
#include "../src/hotkey_dispatch.h"
#include "../src/hotkey_listener.h"
#include "../src/instance_guard.h"
#include "../src/log.h"
#include "../src/udp_io.h"

#include "app_icons.h"
#include "main_window.h"
#include "hint_toast.h"
#include "tray_controller.h"

using namespace capswriter;

namespace {

// --log-file 的大小上限。日志本身只写 stderr、没有轮转机制,托盘常驻时
// 会一直追加下去;超过上限就把旧文件挪成 .old(只留一代)再重新开写,
// 让单个进程的日志占用有恒定上界。
constexpr qint64 kMaxLogFileBytes = 8 * 1024 * 1024;  // 8 MB

// 开写前做一次轮转:超限则把现有日志改名为 <path>.old 并覆盖上一代。
void RotateLogFileIfNeeded(const QString& path) {
  QFileInfo info(path);
  if (!info.exists() || info.size() < kMaxLogFileBytes) return;

  const QString old_path = path + QStringLiteral(".old");
  QFile::remove(old_path);  // 只保留一代备份,不存在时静默失败。
  QFile::rename(path, old_path);
}

// 跨线程事件(日志行/识别结果/提示条事件)先入此队列,主线程定时器取走刷新 UI。
struct HintEvent {
  DictationEvent ev;
  OutputKind kind;
  std::string text;
};

class EventQueue {
 public:
  void PushLog(std::string line) {
    std::lock_guard<std::mutex> lock(mutex_);
    log_lines_.push_back(std::move(line));
  }
  void PushResult(std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.push_back(std::move(text));
  }
  void PushEvent(DictationEvent ev, OutputKind kind, std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back({ev, kind, std::move(text)});
  }
  std::vector<std::string> DrainLogs() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(log_lines_);
  }
  std::vector<std::string> DrainResults() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(results_);
  }
  std::vector<HintEvent> DrainEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(events_);
  }

 private:
  std::mutex mutex_;
  std::vector<std::string> log_lines_;
  std::vector<std::string> results_;
  std::vector<HintEvent> events_;
};

// 把核心事件翻译成提示条文案。只使用 emoji + 文字,不引入任何图片。
void ApplyHintEvent(HintToast* toast, const HintEvent& e) {
  switch (e.ev) {
    case DictationEvent::kRecordingStarted:
      // 长时长:保持到下一次事件(识别/取消)到来。
      toast->Show(QStringLiteral("🎤 正在听写…"), 60000);
      break;
    case DictationEvent::kRecordingEnded:
      toast->Hide();
      break;
    case DictationEvent::kTextOutput:
      switch (e.kind) {
        case OutputKind::kTyped:
          toast->Show(QStringLiteral("✍️ 已输入"), 2000);
          break;
        case OutputKind::kPasted:
          toast->Show(QStringLiteral("📋 已粘贴"), 2000);
          break;
        case OutputKind::kCopied:
          // 没有输入框时,文本写进了剪贴板。
          toast->Show(QStringLiteral("📋 已复制到剪贴板"), 2200);
          break;
      }
      break;
    case DictationEvent::kOutputFailed:
      toast->Show(QStringLiteral("⚠️ 输出失败"), 2000);
      break;
  }
}

// 核心运行时:配置、识别引擎、听写控制器、钩子、UDP 控制。
// 在加载线程里 Init,析构前必须 Stop(先停钩子/UDP,再停 worker)。
struct AppRuntime {
  Config config;
  std::unique_ptr<AsrEngine> engine;
  std::unique_ptr<DictationController> controller;
  std::unique_ptr<HotkeyDispatch> dispatch;
  std::unique_ptr<HotkeyListener> listener;
  std::unique_ptr<UdpController> udp;
  std::vector<HotkeyDef> hotkey_defs;
  // 加载线程置位、主线程读取:用于把托盘图标从"加载中"切回"待命"。
  std::atomic<bool> ready{false};
  std::atomic<bool> failed{false};

  bool Init(const std::string& config_path, std::string* err) {
    if (!config.LoadFromFile(config_path, err)) return false;
    Logger::Instance().set_level(config.log_level);
    LOG_INFO("Loaded config: %s", config_path.c_str());

    engine = std::make_unique<AsrEngine>();
    if (!engine->Init(config, err)) return false;

    for (const ShortcutConfig& sc : config.shortcuts) {
      if (!sc.enabled) continue;
      HotkeyDef def;
      if (!HotkeyListener::ParseKeyName(sc.key, &def)) {
        LOG_WARN("Unknown hotkey name ignored: %s", sc.key.c_str());
        continue;
      }
      def.suppress = sc.suppress;
      def.hold_mode = sc.hold_mode;
      hotkey_defs.push_back(def);
    }
    if (hotkey_defs.empty()) {
      *err = "no usable hotkeys configured";
      return false;
    }

    controller = std::make_unique<DictationController>(config, engine.get());
    dispatch = std::make_unique<HotkeyDispatch>(config, controller.get());

    listener = std::make_unique<HotkeyListener>();
    listener->SetHotkeys(hotkey_defs);
    listener->SetCallback([this](const HotkeyDef& def, bool is_down) {
      dispatch->Handle(def, is_down);
    });
    if (!listener->Start(err)) return false;

    if (config.udp_control) {
      udp = std::make_unique<UdpController>(
          config.udp_control_addr, config.udp_control_port,
          [this](bool start) {
            if (start) {
              controller->StartRecording();
            } else {
              controller->FinishRecording();
            }
          });
      if (!udp->Start()) udp.reset();
    }
    return true;
  }

  void Stop() {
    if (listener) listener->Stop();
    if (udp) udp->Stop();
    if (controller) controller->Stop();
  }
};

}  // namespace

// 响应其他实例发来的"显示主窗口"消息（见 InstanceGuard::ActivateExisting）。
// 跨进程不直接操作对方窗口（旧实例主窗口正常模式下没有原生句柄，直接
// ShowWindow 只会把 Qt 隐藏的托盘消息窗口顶到前台——全白幽灵窗口），
// 而是由对方在自己的进程内、自己的 Qt 事件循环里显示主窗口。
class ActivateFilter : public QAbstractNativeEventFilter {
 public:
  explicit ActivateFilter(std::function<void()> on_activate)
      : on_activate_(std::move(on_activate)) {}

  bool nativeEventFilter(const QByteArray& eventType, void* message,
                         qintptr* result) override {
    Q_UNUSED(eventType);
    Q_UNUSED(result);
    // 主线程消息循环中同步调用，直接操作窗口是线程安全的。
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == InstanceGuard::ActivateMessage()) {
      if (on_activate_) on_activate_();
      return true;
    }
    return false;
  }

 private:
  std::function<void()> on_activate_;
};

int main(int argc, char* argv[]) {
  // ---- 先扫一遍 --log-file:GUI 无控制台,可把日志落到文件 ----
  QString log_file;
  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--log-file") &&
        i + 1 < argc) {
      log_file = QString::fromLocal8Bit(argv[++i]);
    }
  }
  if (!log_file.isEmpty()) {
    // freopen 用 "w":每次启动截断重写;运行期只增不减,故开写前先轮转。
    RotateLogFileIfNeeded(log_file);
    FILE* f = nullptr;
    if (freopen_s(&f, log_file.toLocal8Bit().constData(), "w", stderr) == 0 &&
        f) {
      setvbuf(f, nullptr, _IONBF, 0);
    }
  } else if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    // 从终端启动时把日志附到父控制台(双击启动则无控制台,日志只进窗口)。
    FILE* unused = nullptr;
    freopen_s(&unused, "CONOUT$", "w", stderr);
  }

  // 任务栏图标:没有安装程序的便携软件默认会被 Windows 归进通用分组,
  // 显式声明 AppUserModelID 才能让任务栏按钮使用程序自己的图标。
  SetCurrentProcessExplicitAppUserModelID(L"CapsWriter.Offline.Cpp.1");

  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("CapsWriter-Offline"));
  app.setApplicationDisplayName(QStringLiteral("CapsWriter-Offline"));
  // 窗口/任务栏图标由 emoji 实时绘制,不需要 .ico 资源。
  app.setWindowIcon(MakeAppIcon());

  // 关闭主窗口不退出程序,托盘常驻。
  app.setQuitOnLastWindowClosed(false);

  // ---- 命令行 ----
  std::string config_path = ExeDirectory() + "/config.yaml";
  QString screenshot_path;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if ((arg == QStringLiteral("--config") || arg == QStringLiteral("-c")) &&
        i + 1 < argc) {
      config_path = QString::fromLocal8Bit(argv[++i]).toStdString();
    } else if (arg == QStringLiteral("--screenshot") && i + 1 < argc) {
      // --screenshot <path>:启动并加载完成后 1.5 秒截图保存并退出
      // (仅用于无人工参与的界面验证)。
      screenshot_path = QString::fromLocal8Bit(argv[++i]);
    }
  }

  // ---- 单实例 + 新版本替代旧版本 ----
  // guard 必须存活到进程退出，以保持互斥体句柄不被释放。
  InstanceGuard guard;
  std::string guard_err;
  if (!guard.Acquire(kSingleInstanceMutexName, &guard_err)) {
    LOG_INFO("Single-instance check: %s", guard_err.c_str());
    if (guard.ShouldReplaceOlder()) {
      // 本实例是更新版本：结束旧版本进程，由本实例接管。
      std::string kill_err;
      if (!guard.ReplaceOlder(&kill_err)) {
        LOG_ERROR("Replace older instance failed: %s", kill_err.c_str());
        QMessageBox::critical(
            nullptr, QStringLiteral("CapsWriter-Offline"),
            QStringLiteral("检测到旧版本正在运行，但无法自动替代：\n%1\n\n"
                           "请先手动退出旧版本（托盘菜单“退出”），再启动新版本。")
                .arg(QString::fromUtf8(kill_err.c_str())));
        return 1;
      }
      LOG_INFO("Old instance terminated; starting version %s.",
               InstanceGuard::CurrentVersion().ToString().c_str());
    } else {
      // 同版本或本实例更旧：唤起已有实例后退出，避免重复启动。
      LOG_INFO("Another instance is running; activating it and exiting.");
      if (!guard.ActivateExisting()) {
        QMessageBox::information(
            nullptr, QStringLiteral("CapsWriter-Offline"),
            QStringLiteral("CapsWriter 已在运行。\n请查看系统托盘图标；"
                           "若无法唤醒，请先结束旧进程再启动。"));
      }
      return 0;
    }
  }

  // ---- 窗口与托盘 ----
  MainWindow window;
  window.move(100, 100);
  if (!screenshot_path.isEmpty()) window.show();

  // 其他实例启动时通过自定义消息请求本实例显示主窗口。filter 必须存活
  // 到进程退出；native event filter 只在 Qt 事件循环中生效（截图模式不
  // 需要——它本来就会显示窗口）。
  ActivateFilter activate_filter([&window] {
    window.showNormal();
    window.raise();
    window.activateWindow();
  });
  if (screenshot_path.isEmpty()) {
    qApp->installNativeEventFilter(&activate_filter);
  }

  // 输入提示条:常驻整个会话,跟随光标/输入框显示。
  HintToast toast;

  auto runtime = std::make_unique<AppRuntime>();
  AppRuntime* rt = runtime.get();

  // 托盘控制器在下面才创建(截图模式没有托盘),刷新定时器通过裸指针
  // 引用它;指针在 TrayController 析构前一定被置空。
  TrayController* tray_ptr = nullptr;

  EventQueue queue;
  Logger::Instance().set_sink(
      [&queue](const std::string& line) { queue.PushLog(line); });

  // 主线程定时器:把队列中的日志/识别结果/提示条事件刷进窗口。
  QTimer flush_timer;
  QObject::connect(&flush_timer, &QTimer::timeout, &window, [&] {
    for (const std::string& line : queue.DrainLogs()) {
      window.AppendLog(QString::fromUtf8(line.c_str()));
    }
    for (const std::string& text : queue.DrainResults()) {
      window.AppendResult(QString::fromUtf8(text.c_str()));
    }
    // 托盘图标跟随运行时状态:加载完成切回待命,初始化失败显示出错。
    if (tray_ptr) {
      if (rt->failed.exchange(false)) {
        tray_ptr->SetState(TrayIconState::kError);
      } else if (rt->ready.exchange(false)) {
        tray_ptr->SetState(TrayIconState::kIdle);
      }
    }

    const std::vector<HintEvent> events = queue.DrainEvents();
    if (rt->config.show_hint) {
      for (const HintEvent& e : events) {
        ApplyHintEvent(&toast, e);
      }
    } else {
      // 关闭提示条时丢弃事件并隐藏(但状态更新仍然继续)。
      toast.Hide();
    }

    // 无论是否显示提示条,托盘图标都跟随录音状态。
    if (tray_ptr) {
      for (const HintEvent& e : events) {
        switch (e.ev) {
          case DictationEvent::kRecordingStarted:
            tray_ptr->SetState(TrayIconState::kRecording);
            break;
          case DictationEvent::kRecordingEnded:
            tray_ptr->SetState(TrayIconState::kIdle);
            break;
          default:
            break;
        }
      }
    }
  });
  flush_timer.start(100);

  // ---- 后台加载核心运行时(模型约 3s,不阻塞托盘出现) ----
  std::thread loader([&queue, rt, config_path, &window, &toast,
                      screenshot_path]() {
    std::string err;
    if (!rt->Init(config_path, &err)) {
      LOG_ERROR("Init failed: %s", err.c_str());
      rt->failed = true;
      if (screenshot_path.isEmpty()) {
        QMetaObject::invokeMethod(qApp, [&window, err] {
          QMessageBox::critical(&window, QStringLiteral("CapsWriter-Offline"),
                                QString::fromUtf8("初始化失败:\n%1")
                                    .arg(QString::fromLocal8Bit(err.c_str())));
          QApplication::quit();
        });
      } else {
        // 截图模式没有托盘,失败也必须退出,否则事件循环永远空转。
        QMetaObject::invokeMethod(qApp, [] { QApplication::quit(); });
      }
      return;
    }
    rt->controller->set_result_callback(
        [&queue](const std::string& text) { queue.PushResult(text); });
    rt->controller->set_event_callback(
        [&queue](DictationEvent ev, OutputKind kind, const std::string& text) {
          queue.PushEvent(ev, kind, text);
        });
    rt->ready = true;  // 主线程据此把托盘图标从"加载中"切回"待命"。
    LOG_INFO("CapsWriter C++ (GUI) is ready. Hold the hotkey, speak, release.");

    if (!screenshot_path.isEmpty()) {
      QMetaObject::invokeMethod(qApp, [&window, &toast, screenshot_path]() {
        // 截图模式:把提示条显示在窗口区域内,便于无人工验证渲染效果。
        QTimer::singleShot(800, [&window, &toast] {
          window.raise();
          window.activateWindow();
          window.show();
          // 钉在主窗口客户区中央(避开标签页和标题栏),不依赖 caret/cursor。
          // 窗口在 (100,100),客户区约 1024x660,中央 ~ (600, 430)。
          toast.SetAnchorOverride(QPoint(600, 430));
          toast.Show(QStringLiteral("🎤 正在听写…"), 5000);
          LOG_INFO("[screenshot] toast.Show('🎤 正在听写…', 5000) called");
        });
        QTimer::singleShot(3500, [&window, screenshot_path]() {
          QScreen* screen = QApplication::primaryScreen();
          if (screen) {
            // 抓整屏而不是 window.winId():后者只抓主窗口客户区,WS_EX_LAYERED
            // 的提示条在标准 BitBlt 屏幕中不可见。整屏抓取包含合成层,
            // 裁剪到主窗口矩形即可保留提示条+主窗口+任何 TOPMOST 覆盖物。
            QRect win_rect = window.frameGeometry();
            QPixmap full = screen->grabWindow(0);
            if (!full.isNull() && !win_rect.isEmpty()) {
              full.copy(win_rect).save(screenshot_path);
            } else {
              full.save(screenshot_path);  // 退化:保存整屏
            }
            LOG_INFO("[screenshot] grabWindow + save done (full=%dx%d, win=%dx%d@%d,%d)",
                     full.width(), full.height(), win_rect.width(),
                     win_rect.height(), win_rect.x(), win_rect.y());
          } else {
            LOG_ERROR("[screenshot] primaryScreen is null");
          }
          QApplication::quit();
        });
      });
    }
  });

  // 截图模式不需要托盘;正常模式托盘不可用则退出(与原程序一致)。
  if (screenshot_path.isEmpty()) {
    TrayController tray(&window, &rt->config, config_path);
    // 模型加载约 3s,托盘先显示"加载中"状态,就绪后定时器切回"待命"。
    tray.SetState(TrayIconState::kBusy);
    if (!tray.Initialize()) {
      loader.join();
      runtime->Stop();
      QMessageBox::critical(nullptr, QStringLiteral("CapsWriter-Offline"),
                            QStringLiteral("系统托盘不可用,无法驻留后台。"));
      return 1;
    }
    tray_ptr = &tray;

    // 事件循环退出(托盘菜单"退出")后,安全收尾核心运行时。
    const int rc = app.exec();
    Q_UNUSED(rc);
    tray_ptr = nullptr;
    loader.join();
    runtime->Stop();
    return 0;
  }

  const int rc = app.exec();
  Q_UNUSED(rc);
  loader.join();
  runtime->Stop();
  return 0;
}
