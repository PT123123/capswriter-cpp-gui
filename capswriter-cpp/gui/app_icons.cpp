#include "app_icons.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>

namespace capswriter {
namespace {

// Windows 10/11 自带的彩色 emoji 字体,不引入任何外部资源。
constexpr char kEmojiFont[] = "Segoe UI Emoji";

constexpr uint32_t kMic = 0x1F3A4;        // 🎤
constexpr uint32_t kRecord = 0x1F534;     // 🔴
constexpr uint32_t kHourglass = 0x23F3;   // ⏳
constexpr uint32_t kWarning = 0x26A0;     // ⚠️

// 绘制带圆角背景的 emoji 徽章,返回多尺寸 QIcon。
QIcon RenderEmojiBadge(uint32_t codepoint,
                       const QColor& bg,
                       const QColor& border,
                       const QList<int>& sizes) {
  QIcon icon;

  for (int size : sizes) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal radius = size * 0.22;
    const qreal margin = size * 0.08;
    const qreal x = margin;
    const qreal y = margin;
    const qreal w = size - margin * 2.0;
    const qreal h = size - margin * 2.0;

    QPainterPath path;
    path.addRoundedRect(QRectF(x, y, w, h), radius, radius);
    painter.fillPath(path, bg);

    if (border.isValid()) {
      QPen pen(border);
      pen.setWidthF(qMax(1.0, size / 32.0));
      painter.setPen(pen);
      painter.drawPath(path);
    }

    // char32_t 重载才是非弃用的 QString::fromUcs4。
    char32_t cp = static_cast<char32_t>(codepoint);
    QString emoji = QString::fromUcs4(&cp, 1);

    // 计算 emoji 大小:占画布约 55% 高度,不同 emoji 视觉重心不同,用比例微调。
    qreal emoji_size = size * 0.55;
    QFont font(kEmojiFont);
    font.setPixelSize(static_cast<int>(emoji_size));
    painter.setFont(font);

    // 在整幅画中居中渲染 emoji。Windows 的 Segoe UI Emoji 在 Qt 6 的
    // DirectWrite 后端下会保持彩色,因此这里得到的是彩色图标。
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, emoji);

    icon.addPixmap(pixmap);
  }

  return icon;
}

}  // namespace

QIcon MakeAppIcon() {
  // 窗口/任务栏图标：深蓝圆角键帽 + 🎤。
  return RenderEmojiBadge(
      kMic, QColor(45, 110, 158), QColor(80, 150, 205),
      {16, 24, 32, 48, 64, 128});
}

QIcon MakeTrayIcon(TrayIconState state) {
  switch (state) {
    case TrayIconState::kRecording:
      // 录音中：深红背景 + 大红圆,警示且醒目。
      return RenderEmojiBadge(
          kRecord, QColor(150, 45, 45), QColor(210, 80, 80),
          {16, 24, 32, 48, 64});
    case TrayIconState::kBusy:
      // 加载中：琥珀背景 + 沙漏。
      return RenderEmojiBadge(
          kHourglass, QColor(180, 135, 25), QColor(230, 180, 60),
          {16, 24, 32, 48, 64});
    case TrayIconState::kError:
      // 错误：红底 + 警告三角。
      return RenderEmojiBadge(
          kWarning, QColor(160, 50, 50), QColor(230, 100, 80),
          {16, 24, 32, 48, 64});
    case TrayIconState::kIdle:
    default:
      // 待命：与原实现保持一致的蓝色键帽 + 🎤。
      return RenderEmojiBadge(
          kMic, QColor(47, 125, 246), QColor(90, 170, 255),
          {16, 24, 32, 48, 64});
  }
}

QIcon MakeEmojiIcon(uint32_t emoji_codepoint,
                    int size,
                    const QColor& background,
                    const QColor& border) {
  return RenderEmojiBadge(emoji_codepoint, background, border,
                          {size, size * 3 / 2, size * 2});
}

}  // namespace capswriter
