#include "qt_compat.h"
#include <windows.h>
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QTimer>
#include <QDockWidget>
#include <QTreeWidget>
#include <QFile>
#include <QStringList>
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    const auto args=app.arguments();
    const auto original=QCoreApplication::applicationDirPath()+"/assetbrowser.dll";
    if(!LoadLibraryW(reinterpret_cast<const wchar_t*>(original.utf16()))) return 9;
    QMainWindow window;window.setWindowTitle("Asset Browser");
    window.menuBar()->addMenu("File");window.menuBar()->addMenu("Help")->addAction("About");
    window.show();
    QTimer timer;int attempts=0;
    QObject::connect(&timer,&QTimer::timeout,[&] {
        if(auto* dock=window.findChild<QDockWidget*>("HammerAddonsDock")) {
            auto* rows=dock->findChild<QTreeWidget*>("HammerAddonsList");
            if(rows && (rows->topLevelItemCount()==2 || rows->topLevelItemCount()==3) && rows->topLevelItem(0)->text(2)=="Loaded" && rows->topLevelItem(rows->topLevelItemCount()-1)->text(2)=="Failed") { timer.stop(); QTimer::singleShot(1500,&app,[&app]{app.exit(0);}); }
        }
        if(++attempts>120) app.exit(10);
    });
    timer.start(250);
    return app.exec();
}
