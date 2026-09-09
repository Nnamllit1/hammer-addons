#include "qt_compat.h"
#include <windows.h>
#include <QApplication>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QMainWindow>
#include <QDockWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QAction>
#include <QStatusBar>
#include <filesystem>
#include <string>
#include <iostream>
int main(int argc,char** argv) {
    if(argc<2)return 2;
    const auto mode=QString::fromLocal8Bit(argv[1]).toStdWString();
    if(mode==L"cancel")return 0;
    if((mode==L"picker-ui" || mode==L"picker-cancel")) {
        QApplication app(argc,argv);
        QMainWindow window;window.setWindowTitle("Workshop Tools");
        window.resize(700,450);
        auto* content=new QWidget(&window);
        auto* layout=new QVBoxLayout(content);
        auto* launch=new QPushButton("Launch Tools",content);
        layout->addWidget(launch);
        window.setCentralWidget(content);
        window.show();
        QTimer timer;int attempts=0;
        QObject::connect(&timer,&QTimer::timeout,[&]{
            if(auto* button=window.findChild<QPushButton*>("HammerAddonsPickerButton")) {
                if(window.centralWidget()!=content || button->width()<button->sizeHint().width() ||
                   button->height()<button->sizeHint().height() || !button->isVisible() ||
                   button->mapTo(&window,QPoint(0,0)).y()<content->geometry().bottom() ||
                   !window.statusBar()->isAncestorOf(button)) {
                    std::cerr<<"Picker button geometry: "<<button->width()<<"x"<<button->height()
                        <<" hint "<<button->sizeHint().width()<<"x"<<button->sizeHint().height()
                        <<" y "<<button->mapTo(&window,QPoint(0,0)).y()<<" content bottom "<<content->geometry().bottom()
                        <<" parent "<<button->parentWidget()->metaObject()->className()<<" visible "<<button->isVisible()<<"\n";
                    app.exit(13);return;
                }
                button->click();
                auto* manager=window.findChild<QMainWindow*>();
                auto* rows=manager ? manager->findChild<QTreeWidget*>("HammerAddonsList") : nullptr;
                auto* notes=manager ? manager->findChild<QDockWidget*>("HA.Panel.picker_notes.preferences") : nullptr;
                if(notes && rows && rows->topLevelItemCount()==1 && rows->topLevelItem(0)->text(0)=="picker_notes" &&
                   rows->topLevelItem(0)->text(2)=="Loaded") {
                    auto* action=manager->findChild<QAction*>("HA.Action.picker_notes.preferences");
                    auto* reminder=notes->findChild<QLineEdit*>("HA.Control.name");
                    auto* read=notes->findChild<QPushButton*>("HA.Control.greet");
                    if(!action || !reminder || !read) {app.exit(10);return;}
                    action->trigger();
                    if(!notes->isVisible()) {app.exit(11);return;}
                    reminder->setText("Fixture reminder");
                    QMetaObject::invokeMethod(reminder,"editingFinished",Qt::DirectConnection);
                    read->click();
                    if(manager->statusBar()->currentMessage()!="Reminder: Fixture reminder") {app.exit(12);return;}
                    timer.stop();QTimer::singleShot(1500,&app,[&app]{app.exit(0);});
                }
            }
            if(++attempts>120)app.exit(9);
        });
        timer.start(100);
        if(app.exec()!=0)return 9;
        if(mode==L"picker-cancel")return 0;
    }
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(nullptr,path,32768))return 3;
    const auto executable=std::filesystem::path(path).parent_path()/L"cs2.exe";
    std::wstring command=L"\""+executable.wstring()+L"\" -tools -addon fixture_project";
    if(mode!=L"picker-reject")command+=L" -insecure";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,
        executable.parent_path().c_str(),&startup,&child))return 4;
    CloseHandle(child.hThread);
    WaitForSingleObject(child.hProcess,30000);
    CloseHandle(child.hProcess);
    return 0;
}
