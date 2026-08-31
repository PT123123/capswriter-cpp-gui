#pragma once

#include <QIcon>
#include <QString>

namespace capswriter {

// 托盘图标当前所处状态。主循环根据 DictationEvent 切换。
enum class TrayIconState {
  kIdle = 0,      // 待命：🎤 蓝色键帽
  kRecording,     // 录音中：🔴 红色背景
  kBusy,          // 加载/忙碌：⏳ 琥珀色
  kError,         // 错误：⚠️ 红色警告
};

// 程序主窗口 / 任务栏图标：🎤 圆角键帽徽章。
QIcon MakeAppIcon();

// 系统托盘图标，可随状态变化。
QIcon MakeTrayIcon(TrayIconState state = TrayIconState::kIdle);

// 通用 emoji 徽章：在 size x size 画布上画一个圆角背景，中央渲染一个 emoji。
// emoji 为单个 Unicode 码点（如 0x1F3A4）,fontFamily 优先使用 "Segoe UI Emoji"。
QIcon MakeEmojiIcon(uint32_t emoji_codepoint,
                    int size = 16,
                    const QColor& background = QColor(45, 110, 158),
                    const QColor& border = QColor());

}  // namespace capswriter
