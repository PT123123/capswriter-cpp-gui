#include "app_icons.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>

namespace capswriter {
namespace {

constexpr int kIconSize = 64;

QPixmap MakeKeycapPixmap(const QColor& background) {
  QPixmap pixmap(kIconSize, kIconSize);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  QPainterPath keycap;
  keycap.addRoundedRect(QRectF(2, 2, kIconSize - 4, kIconSize - 4), 14, 14);
  painter.fillPath(keycap, background);

  QFont font("Segoe UI", 28, QFont::Bold);
  painter.setFont(font);
  painter.setPen(Qt::white);
  painter.drawText(QRectF(0, 0, kIconSize, kIconSize), Qt::AlignCenter, "C");

  return pixmap;
}

}  // namespace

QIcon MakeTrayIcon() {
  return QIcon(MakeKeycapPixmap(QColor(47, 125, 246)));
}

}  // namespace capswriter
