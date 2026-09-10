#include "qt_compat.h"
#include "runtime.h"
#include "ui_bridge.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
extern "C" __declspec(dllimport) int __cdecl HA_StartUi(const HA_UiHost*);
namespace fs=std::filesystem;
static ha::Runtime* runtime;
static int callbacks=0;
static std::string last_phase,last_value,last_response;
static void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
static void pump(int ms=850) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<ms) { QApplication::processEvents(); QThread::msleep(5); }
}
static size_t __cdecl read(void*,char* out,size_t capacity) {
    const auto text=runtime->status_json();
    if(text.empty()) return 0;
    if(out && capacity>text.size()) std::memcpy(out,text.c_str(),text.size()+1);
    return text.size()+1;
}
static int __cdecl invoke(void*,uint64_t id,const char* tool,const char* phase,const char* control,const char* value,char* out,size_t size) {
    check(QThread::currentThread()==qApp->thread(),"callback must be on GUI thread");
    ++callbacks; last_phase=phase; last_value=value;
    const auto result=runtime->invoke(id,tool,phase,control,value,out,size);
    last_response=out;
    return result;
}
static void __cdecl report(void*,uint64_t,const char*,const char*) {}
static QAction* action(QMainWindow& w,const char* name) {
    auto* result=w.findChild<QAction*>(name);
    check(result!=nullptr,name);
    return result;
}
static uint64_t find_handle(const char* owner,const char* id) {
    auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(runtime->status_json())).object();
    for(const auto& entry:doc["contributions"].toArray()) {
        auto c=entry.toObject();
        if(c["owner"]==owner && c["id"]==id) return static_cast<uint64_t>(c["handle"].toDouble());
    }
    throw std::runtime_error("missing registration");
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        check(argc==2,"pass checkout root");
        fs::path root=fs::path(std::u8string(reinterpret_cast<const char8_t*>(argv[1])));
        fs::create_directories(root/"build/tests");
        QTemporaryDir temp(QString::fromStdString((root/"build/tests/extensions-XXXXXX").string()));
        check(temp.isValid(),"temporary directory");
        fs::path work=fs::path(temp.path().toStdWString());
        const auto package=work/"hammer-addons";
        fs::create_directories(package);
        fs::create_directories(package/"addons");
        for(const auto* name:{"commands","panel_settings","menu_hooks","note_import"})
            fs::copy(root/"dist/examples/addons"/name,package/"addons"/name,fs::copy_options::recursive);
        const auto probe=package/"addons/extension_probe";
        fs::create_directories(probe);
        fs::copy_file(fs::path(QCoreApplication::applicationDirPath().toStdWString())/"extension_probe.dll",probe/"extension_probe.dll");
        std::ofstream(probe/"addon.ini")<<"[addon]\nformat=1\nid=extension_probe\nversion=0.1.0\nabi=1\nentry=extension_probe.dll\nenabled=true\n";
        ha::Runtime owned(package,work/"settings"); runtime=&owned;
        auto summary=runtime->start();
        check(summary.loaded==5 && summary.rejected==0,"real examples load");
        // Legacy hosts have no appended extension pointer; helper must not read it.
        HA_HostV1 legacy{}; legacy.size=static_cast<uint32_t>(offsetof(HA_HostV1,extensions));
        check(!HA_GetExtensions(&legacy),"old host bounds");
        char buffer[4096]{};
        check(runtime->invoke(find_handle("commands","greet"),"hammer","command","","",buffer,sizeof(buffer))==HA_ERROR,"tool scope");
        check(runtime->invoke(find_handle("commands","greet"),"asset_browser","import","","",buffer,sizeof(buffer))==HA_ERROR,"phase scope");
        auto probe_dll=GetModuleHandleW(L"extension_probe.dll");
        auto entered=reinterpret_cast<int(*)()>(GetProcAddress(probe_dll,"ProbeEntered"));
        auto release=reinterpret_cast<void(*)()>(GetProcAddress(probe_dll,"ProbeRelease"));
        auto late=reinterpret_cast<int(*)()>(GetProcAddress(probe_dll,"ProbeLateRegistration"));
        check(entered && release && late && !late(),"registration only during load");
        const auto probe_handle=find_handle("extension_probe","probe");
        std::thread worker([&] { char out[8]{}; runtime->invoke(probe_handle,"asset_browser","command","","block",out,sizeof(out)); });
        while(!entered()) QThread::msleep(1);
        check(runtime->status_json().empty(),"busy snapshot skips");
        check(runtime->invoke(probe_handle,"asset_browser","command","","",buffer,sizeof(buffer))==HA_BUSY,"busy callback skips");
        runtime->event("test","must not wait for blocked callback");
        release(); worker.join();

        QMainWindow browser; browser.setWindowTitle("Asset Browser"); browser.resize(900,650);
        auto* file=browser.menuBar()->addMenu("&File"); file->addAction("Open...");
        auto* help=browser.menuBar()->addMenu("&Help");
        auto* nativeAbout=help->addAction("&About...");
        nativeAbout->setObjectName("NativeAbout");
        nativeAbout->setShortcut(QKeySequence("Ctrl+Alt+A"));
        int nativeCalls=0;
        QObject::connect(nativeAbout,&QAction::triggered,&browser,[&] { ++nativeCalls; });
        browser.menuBar()->addMenu("Duplicate");
        auto* duplicate=browser.menuBar()->addMenu("Duplicate");
        browser.show();
        QMainWindow model; model.setWindowTitle("ModelDoc :: fixture"); model.resize(850,600);
        auto* modelFile=model.menuBar()->addMenu("File");
        auto* nativeImport=modelFile->addAction("Import...");
        nativeImport->setObjectName("NativeImport");
        int importCalls=0;
        QObject::connect(nativeImport,&QAction::triggered,&model,[&] { ++importCalls; });
        model.show();
        const HA_UiHost bridge{sizeof(HA_UiHost),nullptr,read,invoke,report};
        check(HA_StartUi(&bridge),"UI startup");
        pump();

        check(!browser.findChild<QAction*>("HA.Action.extension_probe.waiting"),"missing target waits");
        check(!browser.findChild<QAction*>("HA.Action.extension_probe.ambiguous"),"ambiguous target waits");
        auto* extensionRows=browser.findChild<QTreeWidget*>("HammerAddonsExtensions");
        bool waitingShown=false;
        for (int i=0;i<extensionRows->topLevelItemCount();++i)
            waitingShown |= extensionRows->topLevelItem(i)->text(3).startsWith("Waiting:");
        check(waitingShown,"binding diagnostics visible");
        auto* lateMenu=browser.menuBar()->addMenu("Late"); delete duplicate; pump();
        action(browser,"HA.Action.extension_probe.waiting");
        action(browser,"HA.Action.extension_probe.ambiguous");
        delete lateMenu; pump(); browser.menuBar()->addMenu("Late"); pump();
        action(browser,"HA.Action.extension_probe.waiting");
        action(browser,"HA.Action.commands.greet")->trigger();
        check(last_response.find("asset_browser")!=std::string::npos,"command response");
        action(browser,"HA.Action.panel_settings.preferences")->trigger();
        auto* dock=browser.findChild<QDockWidget*>("HA.Panel.panel_settings.preferences");
        check(dock && dock->isVisible(),"panel opens");
        auto* name=dock->findChild<QLineEdit*>("HA.Control.name");
        name->setText("Ada");
        QMetaObject::invokeMethod(name,"editingFinished",Qt::DirectConnection);
        auto* enabled=dock->findChild<QCheckBox*>("HA.Control.enabled"); enabled->setChecked(true);
        dock->findChild<QComboBox*>("HA.Control.mode")->setCurrentIndex(1);
        dock->findChild<QPushButton*>("HA.Control.greet")->click();
        check(last_response.find("Ada")!=std::string::npos,"saved setting read");
        ha::Settings second(work/"settings");
        check(second.get("panel_settings","name",buffer,sizeof(buffer))==4 && std::string(buffer)=="Ada","settings persist across store instances");
        check(second.get("commands","name",buffer,sizeof(buffer))==1 && !*buffer,"settings isolated");
        check(!second.set("panel_settings","../escape","x"),"setting traversal rejected");
        char sentinel='x';
        check(second.get("panel_settings","name",&sentinel,1)==4 && sentinel=='x',"setting buffer contract");
        auto* wrapper=action(browser,"HA.Hook.NativeAbout");
        check(!help->actions().contains(nativeAbout) && nativeAbout->shortcuts().isEmpty(),"native action wrapped");
        wrapper->trigger(); check(nativeCalls==1 && last_phase=="hook.after","before/native/after");
        action(browser,"HA.Action.menu_hooks.options")->trigger();
        browser.findChild<QDockWidget*>("HA.Panel.menu_hooks.options")->findChild<QCheckBox*>("HA.Control.handle")->setChecked(true);
        wrapper->trigger(); check(nativeCalls==1 && last_phase=="hook.before","handled cancels native action");
        nativeAbout->setEnabled(false); check(!wrapper->isEnabled(),"native enable state mirrored");
        nativeAbout->setEnabled(true);
        nativeAbout->setShortcut(QKeySequence("Ctrl+Alt+B"));
        check(nativeAbout->shortcuts().isEmpty() && wrapper->shortcut()==QKeySequence("Ctrl+Alt+B"),"updated shortcut stays wrapped");
        const auto note=work/"sample.hanote";
        std::ofstream(note)<<"HAMMER_ADDONS_NOTE\nImported by a real callback\n";
        const auto wrong=work/"wrong.txt"; std::ofstream(wrong)<<"wrong";
        const auto importer=find_handle("note_import","note");
        check(runtime->invoke(importer,"asset_browser","import","",wrong.string().c_str(),buffer,sizeof(buffer))==HA_ERROR,"extension validated");
        action(browser,"HA.Action.note_import.note")->trigger();
        pump(100);
        auto* picker=browser.findChild<QFileDialog*>("HA.ImportPicker");
        check(picker && picker->selectedNameFilter().contains("*.hanote"),"picker advertises custom extension");
        picker->setDirectory(QString::fromStdWString(work.wstring()));
        picker->findChild<QLineEdit*>("fileNameEdit")->setText("sample.hanote");
        pump(400);
        QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection); pump(200);
        check(last_phase=="import" && last_response.find("Imported by a real callback")!=std::string::npos,"file reaches handler");
        const auto beforeCancel=callbacks;
        action(browser,"HA.Action.note_import.note")->trigger(); pump(100);
        browser.findChild<QFileDialog*>("HA.ImportPicker")->reject(); pump(100);
        check(callbacks==beforeCancel,"cancel does not invoke importer");
        action(model,"HA.Hook.NativeImport")->trigger(); pump(100);
        auto* routes=model.findChild<QDialog*>("HA.ImportRoutes");
        check(routes && importCalls==0,"native import call extended");
        routes->findChild<QPushButton*>("HA.BuiltinImporter")->click(); pump(100);
        check(importCalls==1,"original importer retained");
        action(model,"HA.Hook.NativeImport")->trigger(); pump(100);
        routes=model.findChild<QDialog*>("HA.ImportRoutes");
        auto* custom=routes->findChild<QPushButton*>("HA.Route."+QString::number(find_handle("note_import","model_note")));
        check(custom!=nullptr,"registered import route");
        custom->click(); pump(100);
        picker=model.findChild<QFileDialog*>("HA.ImportPicker");
        picker->setDirectory(QString::fromStdWString(work.wstring()));
        picker->findChild<QLineEdit*>("fileNameEdit")->setText("sample.hanote");
        pump(400);
        QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection); pump(200);
        check(last_phase=="import" && importCalls==1,"custom import route handles file without native call");

        // Callback failure removes only the failed add-on's contributions.
        action(browser,"HA.Action.extension_probe.probe")->trigger(); pump();
        check(!browser.findChild<QAction*>("HA.Action.extension_probe.probe"),"failed callback action removed");
        check(browser.findChild<QAction*>("HA.Action.commands.greet")!=nullptr,"other add-ons retained");
        const int before=callbacks; pump();
        check(callbacks==before,"refresh does not invoke callbacks");
        runtime->shutdown(); pump();
        check(help->actions().contains(nativeAbout),"native menu restored at shutdown");
        check(nativeAbout->shortcut()==QKeySequence("Ctrl+Alt+B"),"native shortcut restored");
        check(!browser.findChild<QDockWidget*>("HA.Panel.panel_settings.preferences"),"panels cleaned up");
        std::cout<<"Extension integration passed: real example DLLs, GUI callbacks, scoped registration/settings, menu continuation/cancellation, importer routes, busy callbacks and failure cleanup.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
