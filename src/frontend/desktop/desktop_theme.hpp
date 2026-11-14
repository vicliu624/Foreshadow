#pragma once
#include <QApplication>
namespace signalscope::desktop {
inline void applyDesktopTheme(QApplication& app) {
    app.setStyle("Fusion");
    app.setStyleSheet("QDockWidget::title{background:#1b2c43;padding:5px;font-weight:600;} QMainWindow::separator{background:#203047;width:5px;height:5px;} QMainWindow::separator:hover{background:#5bdcc6;} QWidget{background:#142033;color:#dce7f5;font-size:13px;} QPushButton{background:#29415b;padding:6px;border:1px solid #49647f;border-radius:4px;} QPushButton:disabled{color:#77869a;} QLineEdit,QAbstractSpinBox,QComboBox,QTextEdit,QTableWidget{background:#0e1929;} QHeaderView::section{background:#22364d;padding:5px;} QTabBar::tab{padding:7px;background:#22364d;} QTabBar::tab:selected{background:#355775;}");
}
}
