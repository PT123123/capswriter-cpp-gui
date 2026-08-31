#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "asr_engine.h"
#include "config.h"
#include "dictation.h"
#include "hotkey_dispatch.h"
#include "hotkey_listener.h"
#include "log.h"
#include "udp_io.h"

using namespace capswriter;

namespace {

HotkeyListener* g_listener = nullptr;
DictationController* g_controller = nullptr;
UdpController* g_udp = nullptr;

void Shutdown() {
  if (g_listener) g_listener->Stop();
  if (g_udp) g_udp->Stop();
  if (g_controller) g_controller->Stop();
}

BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
      type == CTRL_CLOSE_EVENT) {
    Shutdown();
  }
  return FALSE;
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);

  std::string config_path = ExeDirectory() + "/config.yaml";
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
    }
  }

  Config config;
  std::string err;
  if (!config.LoadFromFile(config_path, &err)) {
    LOG_ERROR("%s", err.c_str());
    LOG_ERROR("Usage: capswriter.exe [--config path/to/config.yaml]");
    return 1;
  }
  Logger::Instance().set_level(config.log_level);
  LOG_INFO("Loaded config: %s", config_path.c_str());

  AsrEngine engine;
  if (!engine.Init(config, &err)) {
    LOG_ERROR("ASR init failed: %s", err.c_str());
    return 1;
  }

  std::vector<HotkeyDef> defs;
  for (const ShortcutConfig& sc : config.shortcuts) {
    if (!sc.enabled) continue;
    HotkeyDef def;
    if (!HotkeyListener::ParseKeyName(sc.key, &def)) {
      LOG_WARN("Unknown hotkey name ignored: %s", sc.key.c_str());
      continue;
    }
    def.suppress = sc.suppress;
    def.hold_mode = sc.hold_mode;
    defs.push_back(def);
  }
  if (defs.empty()) {
    LOG_ERROR("No usable hotkeys configured");
    return 1;
  }

  DictationController controller(config, &engine);
  g_controller = &controller;

  // Press/release state machine shared with the Qt frontend.
  HotkeyDispatch dispatch(config, &controller);

  HotkeyListener listener;
  g_listener = &listener;
  listener.SetHotkeys(defs);
  listener.SetCallback([&dispatch](const HotkeyDef& def, bool is_down) {
    dispatch.Handle(def, is_down);
  });

  if (!listener.Start(&err)) {
    LOG_ERROR("Hotkey listener failed: %s", err.c_str());
    return 1;
  }

  // UDP control (START/STOP) mirrors UDPController of the Python client.
  std::unique_ptr<UdpController> udp;
  if (config.udp_control) {
    udp = std::make_unique<UdpController>(
        config.udp_control_addr, config.udp_control_port,
        [&controller](bool start) {
          if (start) {
            controller.StartRecording();
          } else {
            controller.FinishRecording();
          }
        });
    if (udp->Start()) g_udp = udp.get();
  }

  LOG_INFO("CapsWriter C++ is ready. Hold the hotkey, speak, release.");
  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

  // The listener owns its own thread and message loop; keep the process
  // alive here.
  Sleep(INFINITE);

  Shutdown();
  return 0;
}
