#include "qt_compat.h"
#include "ui_build_output.h"
#include "hammer_build.h"
#include <QMainWindow>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextCursor>
#include "ui_pointer.h"
#include <QTimer>
#include <QJsonObject>
#include <QMap>
#include <list>

namespace {
struct Source {
    ha::UiPointer<QWidget> dialog;
    ha::UiPointer<QTextDocument> document;
    uint64_t id=0, sequence=0;
    int revision=-1;
    uint32_t flags=0;
    QByteArray title, text;
    QMap<quint64,quint64> delivered;
    bool closed=false;
};
class Observer final:public QObject {
    QMainWindow* window_;
    HA_UiHost host_;
    uint64_t windowId_;
    QJsonArray observers_;
    std::list<Source> sources_;
    void capture(Source& source) {
        auto* document=source.document.data();
        if(!document || !source.dialog) {
            if(!source.closed){source.closed=true;++source.sequence;}
            return;
        }
        const auto title=source.dialog->windowTitle().left(1024).toUtf8();
        if(source.sequence && source.revision==document->revision() && source.title==title)return;
        source.title=title;source.revision=document->revision();++source.sequence;
        // Select a bounded tail without allocating the full compiler transcript.
        const int end=document->characterCount()-1;
        int start=qMax(0,end-32768);
        if(start && document->characterAt(start).isLowSurrogate())++start;
        source.flags=start ? HA_BUILD_TRUNCATED : 0;
        QTextCursor cursor(document);cursor.setPosition(start);
        cursor.setPosition(end,QTextCursor::KeepAnchor);
        auto text=cursor.selectedText();text.replace(QChar::ParagraphSeparator,QChar(10));
        text.replace(QChar::LineSeparator,QChar(10));
        source.text=text.toUtf8();
    }
    void tick() {
        for(auto it=sources_.begin();it!=sources_.end();) {
            auto& source=*it;capture(source);bool busy=false;
            const HA_BuildOutputV1 state{sizeof(state),source.flags,windowId_,source.id,source.sequence,
                source.title.constData(),source.text.constData()};
            for(const auto& value:observers_) {
                const auto key=static_cast<quint64>(value.toObject()["handle"].toDouble());
                if(source.delivered.value(key)==source.sequence)continue;
                const int result=host_.invoke_build(host_.context,key,source.closed?"build.closed":"build.output",&state);
                if(result==HA_BUSY)busy=true;
                else source.delivered[key]=source.sequence;
            }
            if(source.closed && !busy)it=sources_.erase(it);else ++it;
        }
    }
public:
    Observer(QMainWindow* window,const HA_UiHost& host,uint64_t id):QObject(window),window_(window),host_(host),windowId_(id) {
        auto* timer=new QTimer(this);
        connect(timer,&QTimer::timeout,this,[this]{tick();});timer->start(100);
    }
    void refresh(const QJsonArray& definitions) {
        observers_={};
        for(const auto& value:definitions)if(value.toObject()["kind"].toInt()==HA_BUILD_OBSERVER)observers_.append(value);
        if(observers_.isEmpty())return;
        // Only the verified Hammer build-dialog/control pair is an output source.
        // Unrelated consoles, add-on panels and editable text fields are excluded.
        for(auto* dialog:window_->findChildren<QWidget*>()) {
            if(!dialog->inherits("CQBuildMapDialog"))continue;
            QList<QTextEdit*> candidates;
            for(auto* text:dialog->findChildren<QTextEdit*>())
                if(text->inherits("CQAutoScrollingTextEdit"))candidates.append(text);
            if(candidates.size()!=1)continue;
            auto* document=candidates.front()->document();bool seen=false;
            for(const auto& source:sources_)if(source.document==document)seen=true;
            if(seen || sources_.size()>=16)continue;
            static uint64_t nextSource=0;
            Source source;source.dialog=dialog;source.document=document;source.id=++nextSource;
            sources_.push_back(std::move(source));
        }
    }
};
}
QObject* attach_build_output(QMainWindow* window,const HA_UiHost& host,uint64_t id) {
    return host.invoke_build ? new Observer(window,host,id) : nullptr;
}
void refresh_build_output(QObject* observer,const QJsonArray& definitions) {
    if(observer)static_cast<Observer*>(observer)->refresh(definitions);
}
