#include "main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QTabWidget>
#include <QTime>
#include <QVBoxLayout>

#include "app_icons.h"

namespace capswriter {
namespace {

// 日志/识别结果视图各自保留的最大行数。程序托盘常驻,超出后由
// QPlainTextEdit 自动丢弃最旧的段落,内存占用因此有恒定上界。
// 一次听写约产生 3~5 行日志 + 1 行结果,2000 行约合数百次听写的历史。
constexpr int kMaxViewBlocks = 2000;

// 与 tray_gui.py 一致的暗色主题样式表。
const char kAppStyleSheet[] = R"(
    QMainWindow, QWidget {
        background-color: #121212;
        color: #e0e0e0;
    }
    QTextEdit, QPlainTextEdit {
        background-color: #1e1e1e;
        color: #e0e0e0;
        border: 1px solid #333;
    }
    QTabWidget::pane {
        border: 1px solid #333;
        background-color: #1e1e1e;
    }
    QTabWidget::tab-bar {
        left: 5px;
    }
    QTabBar::tab {
        background-color: #1e1e1e;
        color: #e0e0e0;
        padding: 8px 12px;
        margin-right: 2px;
        border: 1px solid #333;
        border-bottom: none;
        border-top-left-radius: 4px;
        border-top-right-radius: 4px;
    }
    QTabBar::tab:selected {
        background-color: #2d2d2d;
        border-bottom: none;
    }
    QTabBar::tab:!selected {
        margin-top: 2px;
    }
)";

const char kTitleBarStyleSheet[] = R"(
    QWidget#titleBar {
        background-color: #1e1e1e;
    }
    QLabel#titleLabel {
        color: #e0e0e0;
        font-size: 12px;
    }
    QPushButton {
        background-color: transparent;
        border: none;
        color: #e0e0e0;
        padding: 4px 8px;
        font-family: "Segoe MDL2 Assets";
        font-size: 10px;
    }
    QPushButton:hover {
        background-color: #404040;
    }
    QPushButton#closeButton:hover {
        background-color: #c42b1c;
    }
)";

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
  setWindowTitle(QStringLiteral("CapsWriter-Offline"));
  setMinimumSize(800, 600);
  resize(960, 640);

  // 无边框窗口。
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ---- 自定义标题栏(32px)----
  auto* title_bar = new QWidget(this);
  title_bar->setObjectName(QStringLiteral("titleBar"));
  title_bar->setFixedHeight(32);
  title_bar->setStyleSheet(QString::fromUtf8(kTitleBarStyleSheet));

  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(8, 0, 0, 0);
  title_layout->setSpacing(0);

  // 窗口/任务栏图标:emoji 实时绘制,不使用任何外部 .ico 资源。
  setWindowIcon(MakeAppIcon());

  // 标题栏左侧的徽标:与窗口/任务栏图标同一张 emoji 徽章。
  // 这里不用裸 emoji 字符——🎤 在 Segoe UI Emoji 里是深色字形,
  // 放在 #1e1e1e 的标题栏上几乎看不见,徽章的蓝底能把它衬出来。
  auto* logo_label = new QLabel(title_bar);
  logo_label->setObjectName(QStringLiteral("logoLabel"));
  logo_label->setPixmap(MakeAppIcon().pixmap(16, 16));
  logo_label->setStyleSheet(
      QStringLiteral("QLabel#logoLabel { margin-right: 6px; }"));
  title_layout->addWidget(logo_label);

  title_label_ = new QLabel(QStringLiteral("CapsWriter-Offline"), title_bar);
  title_label_->setObjectName(QStringLiteral("titleLabel"));
  title_layout->addWidget(title_label_);
  title_layout->addStretch();

  // Segoe MDL2 Assets 图标字符:最小化 / 最大化 / 关闭。
  min_button_ = new QPushButton(QString::fromUtf8("\uE921"), title_bar);
  max_button_ = new QPushButton(QString::fromUtf8("\uE922"), title_bar);
  close_button_ = new QPushButton(QString::fromUtf8("\uE8BB"), title_bar);
  close_button_->setObjectName(QStringLiteral("closeButton"));

  connect(min_button_, &QPushButton::clicked, this, &MainWindow::showMinimized);
  connect(max_button_, &QPushButton::clicked, this, &MainWindow::ToggleMaximize);
  connect(close_button_, &QPushButton::clicked, this, &MainWindow::hide);

  title_layout->addWidget(min_button_);
  title_layout->addWidget(max_button_);
  title_layout->addWidget(close_button_);

  root->addWidget(title_bar);

  // ---- 内容容器(1px 边框包裹)----
  auto* content_widget = new QWidget(this);
  content_widget->setStyleSheet(
      QStringLiteral("QWidget { background-color: #121212;"
                     " border: 1px solid #333; border-top: none; }"));
  auto* content_layout = new QVBoxLayout(content_widget);
  content_layout->setContentsMargins(1, 0, 1, 1);
  content_layout->setSpacing(0);

  // ---- 标签页:日志 / 识别结果 ----
  // C++ 客户端没有独立的服务端进程(识别在进程内完成),因此与
  // 原版 tray_gui 的"服务端日志/客户端日志"不同,这里改为程序
  // 日志与识别历史两个视图。
  tab_widget_ = new QTabWidget(content_widget);
  tab_widget_->setIconSize(QSize(16, 16));
  log_view_ = MakeLogView();
  result_view_ = MakeLogView();
  // 标签页图标:📄 日志 / 💬 识别结果,同样由 emoji 实时绘制。
  tab_widget_->addTab(log_view_,
                      MakeEmojiIcon(0x1F4C4, 16, QColor(45, 110, 158)),
                      QStringLiteral("日志"));
  tab_widget_->addTab(result_view_,
                      MakeEmojiIcon(0x1F4AC, 16, QColor(38, 140, 110)),
                      QStringLiteral("识别结果"));
  content_layout->addWidget(tab_widget_);

  root->addWidget(content_widget);

  setStyleSheet(QString::fromUtf8(kAppStyleSheet));
}

QPlainTextEdit* MainWindow::MakeLogView() {
  auto* view = new QPlainTextEdit();
  view->setReadOnly(true);
  // 上限保护:超出后 Qt 自动丢弃最旧段落,常驻运行也不会无限膨胀。
  view->setMaximumBlockCount(kMaxViewBlocks);
  // 长行不换行,避免单行文本触发昂贵的横向布局计算。
  view->setLineWrapMode(QPlainTextEdit::NoWrap);
  return view;
}

void MainWindow::AppendLog(const QString& line) {
  if (!log_view_) return;
  // appendPlainText 不做富文本解析,识别结果里的 '<' '&' 不会被当成标记。
  log_view_->appendPlainText(line);
  log_view_->verticalScrollBar()->setValue(
      log_view_->verticalScrollBar()->maximum());
}

void MainWindow::AppendResult(const QString& text) {
  if (!result_view_) return;
  const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
  result_view_->appendPlainText(QStringLiteral("[%1] %2").arg(ts, text));
  result_view_->verticalScrollBar()->setValue(
      result_view_->verticalScrollBar()->maximum());
}

void MainWindow::RequestExit() {
  exit_requested_ = true;
  close();
}

void MainWindow::ToggleMaximize() {
  if (isMaximized()) {
    showNormal();
  } else {
    showMaximized();
  }
}

// 关闭事件:只有托盘菜单"退出"才真正关闭;其余一律收进托盘。
void MainWindow::closeEvent(QCloseEvent* event) {
  if (exit_requested_) {
    event->accept();
  } else {
    event->ignore();
    hide();
  }
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & Qt::LeftButton) && !drag_offset_.isNull()) {
    move(event->globalPosition().toPoint() - drag_offset_);
    event->accept();
  }
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    drag_offset_ = QPoint();
    event->accept();
  }
}

}  // namespace capswriter
