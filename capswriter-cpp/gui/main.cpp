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

#include <QApplication>
#include <QMessageLogContext>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../src/asr_engine.h"
#include "../src/config.h"
#include "../src/dictation.h"
#include "../src/hotkey_dispatch.h"
#include "../src/hotkey_listener.h"
#include "../src/log.h"
#include "../src/udp_io.h"

#include "main_window.h"
#include "tray_controller.h"

using namespace capswriter;

namespace {

// 跨线程事件(日志行/识别结果)先入此队列,主线程定时器取走刷新 UI。
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
  std::vector<std::string> DrainLogs() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(log_lines_);
  }
  std::vector<std::string> DrainResults() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(results_);
  }

 private:
  std::mutex mutex_;
  std::vector<std::string> log_lines_;
  std::vector<std::string> results_;
};

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

  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("CapsWriter-Offline"));
  app.setApplicationDisplayName(QStringLiteral("CapsWriter-Offline"));

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

  // ---- 窗口与托盘 ----
  MainWindow window;
  window.move(100, 100);
  if (!screenshot_path.isEmpty()) window.show();

  EventQueue queue;
  Logger::Instance().set_sink(
      [&queue](const std::string& line) { queue.PushLog(line); });

  // 主线程定时器:把队列中的日志/识别结果刷进窗口。
  QTimer flush_timer;
  QObject::connect(&flush_timer, &QTimer::timeout, &window, [&] {
    for (const std::string& line : queue.DrainLogs()) {
      window.AppendLog(QString::fromUtf8(line.c_str()));
    }
    for (const std::string& text : queue.DrainResults()) {
      window.AppendResult(QString::fromUtf8(text.c_str()));
    }
  });
  flush_timer.start(100);

  // ---- 后台加载核心运行时(模型约 3s,不阻塞托盘出现) ----
  auto runtime = std::make_unique<AppRuntime>();
  AppRuntime* rt = runtime.get();
  std::thread loader([&queue, rt, config_path, &window, screenshot_path]() {
    std::string err;
    if (!rt->Init(config_path, &err)) {
      LOG_ERROR("Init failed: %s", err.c_str());
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
    LOG_INFO("CapsWriter C++ (GUI) is ready. Hold the hotkey, speak, release.");

    if (!screenshot_path.isEmpty()) {
      QMetaObject::invokeMethod(qApp, [&window, screenshot_path]() {
        QTimer::singleShot(1500, [&window, screenshot_path]() {
          window.raise();
          window.activateWindow();
          window.show();
          QScreen* screen = QApplication::primaryScreen();
          if (screen) {
            screen->grabWindow(window.winId()).save(screenshot_path);
          }
          QApplication::quit();
        });
      });
    }
  });

  // 截图模式不需要托盘;正常模式托盘不可用则退出(与原程序一致)。
  if (screenshot_path.isEmpty()) {
    TrayController tray(&window);
    if (!tray.Initialize()) {
      loader.join();
      runtime->Stop();
      QMessageBox::critical(nullptr, QStringLiteral("CapsWriter-Offline"),
                            QStringLiteral("系统托盘不可用,无法驻留后台。"));
      return 1;
    }
    // 事件循环退出(托盘菜单"退出")后,安全收尾核心运行时。
    const int rc = app.exec();
    Q_UNUSED(rc);
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
