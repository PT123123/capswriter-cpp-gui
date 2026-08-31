#include "main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>

namespace capswriter {
namespace {

// 与 tray_gui.py 一致的暗色主题样式表。
const char kAppStyleSheet[] = R"(
    QMainWindow, QWidget {
        background-color: #121212;
        color: #e0e0e0;
    }
    QTextEdit {
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
  log_view_ = MakeLogView();
  result_view_ = MakeLogView();
  tab_widget_->addTab(log_view_, QStringLiteral("日志"));
  tab_widget_->addTab(result_view_, QStringLiteral("识别结果"));
  content_layout->addWidget(tab_widget_);

  root->addWidget(content_widget);

  setStyleSheet(QString::fromUtf8(kAppStyleSheet));
}

QTextEdit* MainWindow::MakeLogView() {
  auto* view = new QTextEdit();
  view->setReadOnly(true);
  return view;
}

void MainWindow::AppendLog(const QString& line) {
  if (!log_view_) return;
  log_view_->append(line);
  log_view_->verticalScrollBar()->setValue(
      log_view_->verticalScrollBar()->maximum());
}

void MainWindow::AppendResult(const QString& text) {
  if (!result_view_) return;
  const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
  result_view_->append(QStringLiteral("[%1] %2").arg(ts, text));
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
