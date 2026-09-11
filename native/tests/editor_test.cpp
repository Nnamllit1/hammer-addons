#include "qt_compat.h"
#include "runtime_fixture.h"
#include "ui_bridge.h"
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <iostream>
#include <vector>
extern "C" __declspec(dllimport) int __cdecl HA_StartUi(const HA_UiHost*);
namespace fs=std::filesystem;
static ha::Runtime* runtime;
struct Seen {std::string phase,tool,session,title,path;uint64_t id,sequence;uint32_t flags;};
static std::vector<Seen> events;
static void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static void pump(int ms=950) {
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<ms){QApplication::processEvents();QThread::msleep(5);}
}
static size_t __cdecl read(void*,char* out,size_t capacity) {
    const auto text=runtime->status_json();
    if(text.empty())return 0;
    if(out && capacity>text.size())std::memcpy(out,text.c_str(),text.size()+1);
    return text.size()+1;
}
static int __cdecl invoke(void*,uint64_t id,const char* tool,const char* phase,const char* control,const char* value,char* out,size_t size) {
    return runtime->invoke(id,tool,phase,control,value,out,size);
}
static int __cdecl observe(void*,uint64_t id,const char* tool,const char* phase,const HA_EditorStateV1* state) {
    events.push_back({phase,tool,state->session_id,state->title,state->document_path,state->window_id,state->sequence,state->flags});
    return runtime->invoke(id,tool,phase,"","",nullptr,0,state);
}
static uint64_t handle(const char* owner,const char* id) {
    for(const auto& item:QJsonDocument::fromJson(QByteArray::fromStdString(runtime->status_json())).object()["contributions"].toArray()) {
        auto c=item.toObject();
        if(c["owner"]==owner && c["id"]==id)return static_cast<uint64_t>(c["handle"].toDouble());
    }
    throw std::runtime_error("missing contribution");
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    try {
        check(argc==2,"pass checkout root");
        const fs::path root=fs::path(QString::fromLocal8Bit(argv[1]).toStdWString());
        fs::create_directories(root/"build/tests");
        QTemporaryDir temp(QString::fromStdWString((root/"build/tests/editor-XXXXXX").wstring()));
        check(temp.isValid(),"temp directory");
        const fs::path package=fs::path(temp.path().toStdWString())/"package";
        for(const auto* name:{"editor_watch","live_status","extension_probe"}) {
            const auto folder=package/"addons"/name;fs::create_directories(folder);
            fs::copy_file(fs::path(QCoreApplication::applicationDirPath().toStdWString())/(std::string(name)+".dll"),folder/(std::string(name)+".dll"));
            if(std::string(name)!="extension_probe")fs::copy_file(root/"addons"/name/"addon.ini",folder/"addon.ini");
            else std::ofstream(folder/"addon.ini")<<"[addon]\nformat=1\nid=extension_probe\nversion=0.1.0\nabi=1\nentry=extension_probe.dll\nenabled=true\n";
        }
        RuntimeFixture owned(package,package/"settings",false,"tools");runtime=&owned;
        check(owned.start().loaded==3,"new examples load");
        HA_InteractionV1 oldEvent{};oldEvent.size=offsetof(HA_InteractionV1,editor);
        check(!HA_GetEditorState(&oldEvent),"old interaction bounds");
        HA_ExtensionsV1 oldTable{};oldTable.size=offsetof(HA_ExtensionsV1,set_panel_text);oldTable.version=1;
        HA_HostV1 oldHost{};oldHost.size=sizeof(oldHost);oldHost.abi_version=1;oldHost.extensions=&oldTable;
        check(HA_GetExtensions(&oldHost)==&oldTable && !HA_SetPanelText(&oldHost,1,"x","x"),"old extension prefix accepted");
        auto probe=reinterpret_cast<int(HA_CALL*)(uint64_t,const char*,const char*)>(GetProcAddress(GetModuleHandleW(L"extension_probe.dll"),"ProbePanelText"));
        check(probe && !probe(handle("editor_watch","history"),"events","foreign write"),"panel ownership");
        check(runtime->invoke(handle("editor_watch","watch"),"hammer","editor.changed","","",nullptr,0)==HA_ERROR,"observer requires typed state");
        QMainWindow browser;browser.setWindowTitle("Asset Browser");browser.resize(900,600);browser.show();
        auto* hammer=new QMainWindow;hammer->setWindowTitle("Hammer - fixture [*]");hammer->resize(900,600);
        hammer->setWindowFilePath(QString::fromUtf8("C:/fixtures/map_\xc3\xa4.vmap"));hammer->show();
        const HA_UiHost bridge{sizeof(HA_UiHost),nullptr,read,invoke,nullptr,observe};
        check(HA_StartUi(&bridge),"UI startup");pump();
        Seen original{};
        for(const auto& e:events)if(e.tool=="hammer" && e.phase=="editor.opened")original=e;
        check(original.id && !original.session.empty() && original.sequence==1,"window identity");
        check(original.path=="C:/fixtures/map_\xc3\xa4.vmap" && (original.flags&HA_EDITOR_HAS_DOCUMENT_PATH),"reported path UTF-8");
        bool distinct=false,unknown=false;
        for(const auto& e:events)if(e.tool=="asset_browser") {
            distinct|=e.id!=original.id && e.session==original.session;
            unknown|=e.path.empty() && !(e.flags&HA_EDITOR_HAS_DOCUMENT_PATH);
        }
        check(distinct && unknown,"separate windows and unknown document path");
        pump();const auto unchanged=events.size();pump();
        check(events.size()==unchanged,"no duplicate observations on unchanged refresh");
        hammer->setWindowModified(true);hammer->setWindowTitle("Hammer - changed [*]");pump();
        check(events.back().id==original.id && events.back().sequence>original.sequence &&
            (events.back().flags&HA_EDITOR_WINDOW_MODIFIED),"ordered metadata changes");
        hammer->findChild<QAction*>("HA.Action.editor_watch.history")->trigger();pump();
        auto* history=hammer->findChild<QPlainTextEdit*>("HA.Control.events");
        check(history && history->isReadOnly() && history->toPlainText().contains("editor.changed"),"live read-only observation panel");
        hammer->findChild<QAction*>("HA.Action.live_status.counter")->trigger();
        auto* counter=hammer->findChild<QDockWidget*>("HA.Panel.live_status.counter");
        counter->findChild<QPushButton*>("HA.Control.increment")->click();pump();
        check(counter->findChild<QLabel*>("HA.Control.count")->text()=="Session counter: 1","live label refresh");
        delete hammer;pump();
        bool closed=false;
        for(const auto& e:events)closed|=e.id==original.id && e.phase=="editor.closed" && e.path==original.path;
        check(closed,"destruction with cached metadata");
        runtime->shutdown();pump();
        std::cout<<"Editor infrastructure passed: ABI prefixes, ownership, session/window identity, metadata events, unavailable paths, live panels and destruction.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
