#include "qt_compat.h"
#include "runtime.h"
#include "ui_bridge.h"
#include <QApplication>
#include <QMainWindow>
#include <QAction>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#include <atomic>
#include <thread>
extern "C" __declspec(dllimport) int __cdecl HA_StartUi(const HA_UiHost*);
namespace fs=std::filesystem;
static ha::Runtime* runtime;
static uint64_t observedWindow;
static std::atomic<bool> gate;
static std::atomic<unsigned> entered, exited;
static void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static void pump(int ms=150) {
    QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();QThread::msleep(2);}
}
static void until(const auto& predicate,const char* message) {
    QElapsedTimer t;t.start();while(!predicate() && t.elapsed()<5000)pump(20);check(predicate(),message);
}
static size_t __cdecl read(void*,char* output,size_t capacity) {
    const auto value=runtime->status_json();if(output && capacity>value.size())memcpy(output,value.c_str(),value.size()+1);return value.size()+1;
}
static int __cdecl invoke(void*,uint64_t id,const char* tool,const char* phase,const char* control,const char* value,char* out,size_t size) {
    return runtime->invoke(id,tool,phase,control,value,out,size);
}
static void __cdecl tick(void*) {runtime->pump_jobs();}
static void __cdecl window(void*,uint64_t id,int open) {observedWindow=id;runtime->editor_window(id,open!=0);}
static uint64_t handle(const char* id) {
    for(const auto& v:QJsonDocument::fromJson(QByteArray::fromStdString(runtime->status_json())).object()["contributions"].toArray()) {
        const auto c=v.toObject();if(c["owner"]=="compile_report" && c["id"]==id)return static_cast<uint64_t>(c["handle"].toDouble());
    }throw std::runtime_error("missing compile report contribution");
}
static void HA_CALL noop(const HA_JobEventV1*) {}
static int HA_CALL blocked(const HA_JobContextV1*,const char*,char*,size_t) {
    ++entered;while(!gate)Sleep(1);++exited;return 1;
}
static void queue_limits() {
    {
        ha::Jobs queue;
        HA_JobV1 request{sizeof(request),"owned input",blocked,noop,0};
        uint64_t first=0;
        for(int i=0;i<4;++i){auto id=queue.submit("a",request);check(id!=0,"four jobs allowed");if(!i)first=id;}
        check(!queue.submit("a",request),"per-addon job bound");
        check(!queue.cancel("b",first),"foreign cancellation rejected");
        for(const char* owner:{"b","c","d"})for(int i=0;i<4;++i)check(queue.submit(owner,request)!=0,"global job capacity");
        check(!queue.submit("e",request),"global job bound");
        for(int i=0;i<64;++i)check(queue.post("a",noop,"copied",0),"bounded post queue accepts capacity");
        check(!queue.post("a",noop,"overflow",0),"post queue backpressure");
        check(!queue.post("a",noop,"unknown window",1234),"unknown window rejected");
        bool rejected=false;
        try {queue.post("a",noop,std::string(4097,'x').c_str(),0);}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"oversized post rejected");
        // Ensure every worker is running before destroying its queue.
        until([]{return entered.load()==16;},"workers entered");
    }
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    try {
        check(argc==2 || argc==3,"pass checkout root and optional original tier0.dll");
        // Isolate queue capacity from the runtime; workers are released after queue destruction.
        queue_limits();
        gate=true;until([]{return exited.load()==16;},"detached workers retain their own storage after destruction");
        const fs::path root=QString::fromLocal8Bit(argv[1]).toStdWString();
        fs::create_directories(root/"build/tests");
        QTemporaryDir temp(QString::fromStdWString((root/"build/tests/jobs-XXXXXX").wstring()));check(temp.isValid(),"temp directory");
        const fs::path package=temp.path().toStdWString();
        const fs::path bin=QCoreApplication::applicationDirPath().toStdWString();
        for(const auto* name:{"compile_report","tool_console","jobs_probe"}) {
            const auto folder=package/"addons"/name;fs::create_directories(folder);
            fs::copy_file(bin/(std::string(name)+".dll"),folder/(std::string(name)+".dll"));
            if(std::string(name)!="jobs_probe")fs::copy_file(root/"addons"/name/"addon.ini",folder/"addon.ini");
            else std::ofstream(folder/"addon.ini")<<"[addon]\nformat=1\nid=jobs_probe\nversion=0.1.0\nabi=1\nentry=jobs_probe.dll\nenabled=true\n";
        }
        ha::Runtime owned(package,package/"settings",false,"tools");runtime=&owned;
        HMODULE tier0=nullptr;
        if(argc==3) {
            const auto path=QString::fromLocal8Bit(argv[2]).toStdWString();
            tier0=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            check(tier0 && runtime->attach_tool_logs(tier0),"original engine logger attachment");
        }
        check(runtime->start().loaded==3,"real jobs/logging add-ons load");
        HA_ExtensionsV1 oldTable{};oldTable.size=offsetof(HA_ExtensionsV1,jobs);oldTable.version=1;
        HA_HostV1 oldHost{};oldHost.size=sizeof(oldHost);oldHost.abi_version=1;oldHost.extensions=&oldTable;
        check(HA_GetExtensions(&oldHost) && !HA_GetJobs(&oldHost),"old extension prefix remains supported");
        auto module=GetModuleHandleW(L"jobs_probe.dll");
        auto reset=reinterpret_cast<void(HA_CALL*)()>(GetProcAddress(module,"ProbeReset"));
        auto calls=reinterpret_cast<unsigned(HA_CALL*)()>(GetProcAddress(module,"ProbeCalls"));
        auto terminal=reinterpret_cast<unsigned(HA_CALL*)()>(GetProcAddress(module,"ProbeTerminal"));
        auto wrong=reinterpret_cast<int(HA_CALL*)()>(GetProcAddress(module,"ProbeWrongThread"));
        auto last=reinterpret_cast<const char*(HA_CALL*)()>(GetProcAddress(module,"ProbeLast"));
        auto submit=reinterpret_cast<uint64_t(HA_CALL*)(const char*,uint64_t)>(GetProcAddress(module,"ProbeSubmit"));
        auto post=reinterpret_cast<int(HA_CALL*)(const char*,uint64_t)>(GetProcAddress(module,"ProbePost"));
        auto cancel=reinterpret_cast<int(HA_CALL*)(uint64_t)>(GetProcAddress(module,"ProbeCancel"));
        check(reset && calls && terminal && wrong && last && submit && post && cancel,"probe exports");
        auto* browser=new QMainWindow;browser->setWindowTitle("Asset Browser");browser->show();
        const HA_UiHost bridge{sizeof(bridge),nullptr,read,invoke,nullptr,nullptr,tick,window};
        check(HA_StartUi(&bridge),"jobs bridge starts");pump(200);check(observedWindow!=0,"window registered without observer add-on");
        reset();char copied[]="copied message";check(post(copied,0),"post accepted");copied[0]='X';
        check(calls()==0,"post is deferred");
        std::thread wrongThread([]{runtime->pump_jobs();});wrongThread.join();check(calls()==0,"wrong thread cannot drain");
        until([&]{return calls()==1;},"posted callback runs");check(std::string(last())=="copied message" && !wrong(),"owned payload and GUI thread");
        reset();check(post("repost",0),"repost accepted");runtime->pump_jobs();check(calls()==1,"nested posts wait for another pump");
        runtime->pump_jobs();check(calls()==2,"next pump delivers nested post");
        reset();check(submit("worker result",0)!=0,"worker starts");
        until([&]{return terminal()!=0;},"worker terminal callback");check(terminal()==HA_JOB_SUCCEEDED && std::string(last())=="worker result" && !wrong(),"worker result on GUI thread");
        reset();const auto waiting=submit("wait",0);check(waiting && cancel(waiting),"cancel request accepted");
        until([&]{return terminal()!=0;},"cooperative cancellation finishes");check(terminal()==HA_JOB_CANCELLED,"cancelled state");
        reset();check(submit("throw_worker",0)!=0,"throwing worker submitted");until([&]{return terminal()!=0;},"worker exception reported");
        check(terminal()==HA_JOB_FAILED,"worker exception contained");
        reset();check(post("closed window",observedWindow) && submit("wait",observedWindow),"window-scoped work accepted");
        const auto oldWindow=observedWindow;delete browser;pump(150);
        check(calls()==0 && !post("stale",oldWindow) && !submit("stale",oldWindow),"window destruction cancels and suppresses callbacks");
        browser=new QMainWindow;browser->setWindowTitle("Asset Browser");browser->show();pump(850);
        auto* consoleAction=browser->findChild<QAction*>("HA.Action.tool_console.console");check(consoleAction,"live log action");
        consoleAction->trigger();pump(30);
        auto* consoleDock=browser->findChild<QDockWidget*>("HA.Panel.tool_console.console");
        auto* consoleView=consoleDock->findChild<QPlainTextEdit*>();check(consoleView,"live log panel");
        if(tier0) {
            auto find=reinterpret_cast<int(__cdecl*)(const char*)>(GetProcAddress(tier0,"LoggingSystem_FindChannel"));
            auto writeLog=reinterpret_cast<int(__cdecl*)(int,int,const char*)>(GetProcAddress(tier0,"LoggingSystem_LogDirect"));
            const int channel=find("General");check(channel>=0 && writeLog,"engine exports");
            writeLog(channel,HA_LOG_MESSAGE,"Filtered informational fixture\n");
            std::thread writer([&]{writeLog(channel,HA_LOG_WARNING,"Live warning fixture\n");});writer.join();
            until([&]{return consoleView->toPlainText().contains("Live warning fixture");},"engine output reaches real add-on panel");
            check(!consoleView->toPlainText().contains("Filtered informational fixture"),"severity filter");
            uint64_t consoleHandle=0;
            for(const auto& value:QJsonDocument::fromJson(QByteArray::fromStdString(runtime->status_json())).object()["contributions"].toArray()) {
                auto c=value.toObject();if(c["owner"]=="tool_console")consoleHandle=static_cast<uint64_t>(c["handle"].toDouble());
            }
            check(runtime->invoke(consoleHandle,"asset_browser","panel.click","pause","",nullptr,0)==HA_HANDLED,"pause subscription");
            writeLog(channel,HA_LOG_WARNING,"Paused warning fixture\n");pump(1000);
            check(!consoleView->toPlainText().contains("Paused warning fixture"),"unsubscribed callback suppressed");
            check(runtime->invoke(consoleHandle,"asset_browser","panel.click","pause","",nullptr,0)==HA_HANDLED,"resume subscription");
        } else {
            bool unavailable=false;
            for(auto* label:consoleDock->findChildren<QLabel*>())unavailable|=label->text().contains("unavailable");
            check(unavailable,"unavailable provider explained to user");
        }
        auto* action=browser->findChild<QAction*>("HA.Action.compile_report.report");check(action,"report action");action->trigger();pump(50);
        auto* dock=browser->findChild<QDockWidget*>("HA.Panel.compile_report.report");check(dock,"report panel");
        auto* view=dock->findChild<QPlainTextEdit*>();check(view && view->isReadOnly(),"report is read-only");
        const auto log=package/"test.log";
        std::ofstream(log)<<"Starting build\nFailed loading resource materials/example.vmat_c\nWarning: example fixture\nDone\n";
        char response[1024]{};const auto path=log.u8string();
        check(runtime->invoke(handle("inspect"),"asset_browser","import","",reinterpret_cast<const char*>(path.c_str()),response,sizeof(response))==HA_HANDLED,"scan selected log");
        until([&]{return view->toPlainText().contains("2 diagnostic candidates");},"real worker updates panel");
        check(view->toPlainText().contains("Line 2: Failed loading resource"),"diagnostic line preserved");
        std::ofstream(log,std::ios::binary|std::ios::trunc)<<std::string("a\0b",3);
        check(runtime->invoke(handle("inspect"),"asset_browser","import","",reinterpret_cast<const char*>(path.c_str()),response,sizeof(response))==HA_HANDLED,"binary input starts");
        until([&]{return view->toPlainText().contains("Binary or UTF-16");},"binary input rejected visibly");
        reset();check(post("throw_callback",0),"throwing callback queued");pump(150);
        check(runtime->status_json().find("Queued editor callback threw")!=std::string::npos && !post("after failure",0),"callback failure disables owner");
        runtime->shutdown();const auto previous=calls();pump(100);check(calls()==previous,"no callbacks after shutdown");
        delete browser;
        std::cout<<"Jobs integration passed: limits, ownership, copied messages, worker lifetime, cancellation, GUI dispatch, reentrancy, window closure, exceptions, real log inspection and binary rejection.\n";
        return 0;
    } catch(const std::exception& error){gate=true;std::cerr<<error.what()<<'\n';return 1;}
}
