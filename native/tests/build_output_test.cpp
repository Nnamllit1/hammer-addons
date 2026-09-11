#include "qt_compat.h"
#include "runtime.h"
#include "ui_bridge.h"
#include <QApplication>
#include <QDialog>
#include <QTextEdit>
#include <QMainWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <iostream>
// Public Qt metaobject identities matching the live, inspected widget pair.
class CQBuildMapDialog:public QDialog {
    Q_OBJECT
public:using QDialog::QDialog;};
class CQAutoScrollingTextEdit:public QTextEdit {
    Q_OBJECT
public:using QTextEdit::QTextEdit;};
extern "C" __declspec(dllimport) int __cdecl HA_StartUi(const HA_UiHost*);
namespace fs=std::filesystem;
static ha::Runtime* runtime;
struct Seen {uint64_t id,sequence;uint32_t flags;std::string phase,text,title;};
static std::vector<Seen> events;
static bool busy;
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static void pump(int ms=950){QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();QThread::msleep(2);}}
static size_t __cdecl read(void*,char* out,size_t capacity){const auto s=runtime->status_json();if(out && capacity>s.size())memcpy(out,s.c_str(),s.size()+1);return s.size()+1;}
static int __cdecl invoke(void*,uint64_t id,const char* tool,const char* phase,const char* control,const char* value,char* out,size_t capacity){return runtime->invoke(id,tool,phase,control,value,out,capacity);}
static int __cdecl build(void*,uint64_t id,const char* phase,const HA_BuildOutputV1* s){
    if(busy)return HA_BUSY;
    events.push_back({s->stream_id,s->sequence,s->flags,phase,s->text,s->title});
    return runtime->invoke(id,"hammer",phase,"","",nullptr,0,nullptr,s);
}
static std::string report(){
    for(const auto& v:QJsonDocument::fromJson(QByteArray::fromStdString(runtime->status_json())).object()["contributions"].toArray()){
        auto c=v.toObject();if(c["owner"]=="compile_report" && c["id"]=="report")
            for(const auto& x:c["controls"].toArray())if(x.toObject()["id"]=="report")return x.toObject()["value"].toString().toStdString();
    }return {};
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    try{
        check(argc==2,"pass checkout root");const fs::path root=QString::fromLocal8Bit(argv[1]).toStdWString();
        QTemporaryDir temp(QString::fromStdWString((root/"build/tests/build-output-XXXXXX").wstring()));check(temp.isValid(),"temp directory");
        fs::path package=temp.path().toStdWString();auto folder=package/"addons/compile_report";fs::create_directories(folder);
        fs::copy_file(fs::path(QCoreApplication::applicationDirPath().toStdWString())/"compile_report.dll",folder/"compile_report.dll");
        fs::copy_file(root/"addons/compile_report/addon.ini",folder/"addon.ini");
        ha::Runtime owned(package,package/"settings",false,"tools");runtime=&owned;check(owned.start().loaded==1,"real example loads");
        HA_InteractionV1 old{};old.size=offsetof(HA_InteractionV1,build);check(!HA_GetBuildOutput(&old),"old interaction is accepted without appended build field");
        uint64_t observer=0;
        for(const auto& v:QJsonDocument::fromJson(QByteArray::fromStdString(owned.status_json())).object()["contributions"].toArray())
            if(v.toObject()["id"]=="automatic")observer=static_cast<uint64_t>(v.toObject()["handle"].toDouble());
        check(observer && owned.invoke(observer,"hammer","build.output","","",nullptr,0)==HA_ERROR,"typed build state required");
        HA_BuildOutputV1 invalid{sizeof(invalid),0,1,1,1,"title","text"};
        check(owned.invoke(observer,"hammer","command","","",nullptr,0,nullptr,&invalid)==HA_ERROR,"wrong phase rejected");
        invalid.flags=8;check(owned.invoke(observer,"hammer","build.output","","",nullptr,0,nullptr,&invalid)==HA_ERROR,"unknown flags rejected");
        QMainWindow hammer;hammer.setWindowTitle("Hammer - fixture");hammer.show();
        const HA_UiHost host{sizeof(host),nullptr,read,invoke,nullptr,nullptr,nullptr,nullptr,build};
        check(HA_StartUi(&host),"UI starts");pump();
        QDialog unrelated(&hammer);auto* unrelatedText=new CQAutoScrollingTextEdit(&unrelated);unrelatedText->setPlainText("error unrelated");pump();
        check(events.empty(),"unrelated text control excluded");
        auto* dialog=new CQBuildMapDialog(&hammer);dialog->setWindowTitle("Build Map disposable.vmap");
        auto* text=new CQAutoScrollingTextEdit(dialog);text->setPlainText("Start build\nwarning fixture\nerror fixture");
        pump();check(events.size()==1 && report().find("2 diagnostic candidates")!=std::string::npos,"automatic capture reaches real report without file selection");
        const auto id=events.back().id;const auto sequence=events.back().sequence;
        pump();check(events.size()==1,"unchanged output not repeated");
        text->append(QString::fromUtf8("warning Gr\xc3\xb6\xc3\x9f"));pump(200);
        check(events.back().id==id && events.back().sequence>sequence && events.back().text.find("Gr\xc3\xb6\xc3\x9f")!=std::string::npos,"ordered UTF-8 updates");
        text->clear();pump(200);check(events.back().text.empty() && report().find("0 diagnostic candidates")!=std::string::npos,"clear replaces old findings");
        text->setPlainText(QString(40000,'x')+"\nwarning tail");pump(200);
        check(events.back().flags==HA_BUILD_TRUNCATED && events.back().text.size()<=131072 && report().find("earlier text was omitted")!=std::string::npos,"bounded tail and explicit truncation");
        busy=true;text->setPlainText("warning delayed");const auto count=events.size();pump(200);check(events.size()==count,"busy observer deferred");
        text->setPlainText("error newest");busy=false;pump(200);check(events.back().text=="error newest","busy retry uses newest snapshot");
        delete dialog;pump(200);check(events.back().phase=="build.closed" && events.back().id==id,"destruction publishes closed snapshot");
        dialog=new CQBuildMapDialog(&hammer);text=new CQAutoScrollingTextEdit(dialog);text->setPlainText("warning reopened");pump();
        check(events.back().phase=="build.output" && events.back().id!=id,"reopened dialog has fresh identity");
        delete dialog;pump(200);
        auto* ambiguous=new CQBuildMapDialog(&hammer);new CQAutoScrollingTextEdit(ambiguous);new CQAutoScrollingTextEdit(ambiguous);
        const auto before=events.size();pump();check(events.size()==before,"ambiguous output controls excluded");delete ambiguous;
        owned.shutdown();std::cout<<"Automatic build output integration tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#include "build_output_test.moc"
