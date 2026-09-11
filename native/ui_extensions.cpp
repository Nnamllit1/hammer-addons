#include "ui_extensions.h"
#include "hammer_extensions.h"
#include "hammer_editor.h"
#include "ui_build_output.h"
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QScrollArea>
#include <QHeaderView>
#include <QUuid>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenuBar>
#include "ui_pointer.h"
#include <QPushButton>
#include <QStatusBar>
#include <QSet>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace {
quint64 handle(const QJsonObject& value) { return static_cast<quint64>(value["handle"].toDouble()); }
QString clean(QString text) {
    text = text.section('\t',0,0);
    text.remove('&');
    if (text.endsWith("...")) text.chop(3);
    if (text.endsWith(QChar(0x2026))) text.chop(1);
    return text.trimmed();
}
QAction* match(const QList<QAction*>& actions, const QString& part) {
    QAction* found = nullptr;
    for (auto* action : actions) {
        const bool matches = part.startsWith('@') ? action->objectName() == part.mid(1) :
            clean(action->text()) == clean(part);
        if (!matches) continue;
        if (found) return nullptr; // Ambiguous labels are not an integration point.
        found = action;
    }
    return found;
}
struct Target { QMenu* menu = nullptr; QAction* action = nullptr; };
Target locate(QMainWindow* window, const QString& path, bool leaf) {
    const auto parts = path.split('/');
    auto actions = window->menuBar()->actions();
    QMenu* parent = nullptr;
    for (int i = 0; i < parts.size(); ++i) {
        auto* action = match(actions,parts[i]);
        if (!action) return {};
        if (leaf && i == parts.size()-1) return {parent,action};
        parent = action->menu();
        if (!parent) return {};
        actions = parent->actions();
    }
    return {parent,nullptr};
}
void update_table(QTreeWidget* table,const QString& rows) {
    if(table->property("ha_rows").toString()==rows)return;
    const QSignalBlocker blocker(table);
    const QString selected=table->currentItem()?table->currentItem()->data(0,Qt::UserRole).toString():QString();
    table->clear();
    for(const auto& line:rows.split('\n',Qt::SkipEmptyParts)) {
        auto cells=line.split('\t');const auto key=cells.takeFirst();
        auto* item=new QTreeWidgetItem(table,cells);item->setData(0,Qt::UserRole,key);
        for(int i=0;i<cells.size();++i)item->setToolTip(i,cells[i]);
        if(key==selected)table->setCurrentItem(item);
    }
    table->setProperty("ha_rows",rows);
}
class Controller;
class Hook final : public QObject {
public:
    Controller* owner;
    ha::UiPointer<QMenu> menu;
    ha::UiPointer<QAction> original, proxy;
    QList<QKeySequence> shortcuts;
    QJsonArray definitions;
    bool running = false;
    Hook(Controller* owner, QMenu* menu, QAction* original);
    ~Hook() override;
    void trigger();
};
struct Binding {
    ha::UiPointer<QAction> action;
    ha::UiPointer<QDockWidget> dock;
};
class Controller final : public QObject {
public:
    ha::UiPointer<QMainWindow> window;
    QString tool;
    HA_UiHost host;
    QObject* buildOutput=nullptr;
    QMap<quint64,Binding> bindings;
    QMap<QString,Hook*> hooks;
    QMap<quint64,QString> reports;
    QJsonArray snapshot;
    QByteArray session, title, document;
    quint64 windowId=0, sequence=0;
    uint32_t editorFlags=0;
    QMap<quint64,quint64> delivered;
    void publish(bool closing=false) {
        if(!host.invoke_editor)return;
        if(!closing && window) {
            const auto nextTitle=window->windowTitle().toUtf8();
            const auto nextDocument=window->windowFilePath().toUtf8();
            const uint32_t nextFlags=(window->isVisible()?HA_EDITOR_VISIBLE:0u) |
                (window->isActiveWindow()?HA_EDITOR_ACTIVE:0u) |
                (window->isWindowModified()?HA_EDITOR_WINDOW_MODIFIED:0u) |
                (!nextDocument.isEmpty()?HA_EDITOR_HAS_DOCUMENT_PATH:0u);
            if(!sequence || title!=nextTitle || document!=nextDocument || editorFlags!=nextFlags) {
                title=nextTitle;document=nextDocument;editorFlags=nextFlags;++sequence;
            }
        } else if(closing) {++sequence;editorFlags&=~(HA_EDITOR_VISIBLE|HA_EDITOR_ACTIVE);}
        const HA_EditorStateV1 state{sizeof(HA_EditorStateV1),editorFlags,windowId,sequence,
            session.constData(),title.constData(),document.constData()};
        const auto t=tool.toUtf8();
        for(const auto& entry:snapshot) {
            const auto c=entry.toObject();const auto key=handle(c);
            if(c["kind"].toInt()!=HA_EDITOR_OBSERVER || (closing && !delivered.contains(key)) ||
               (!closing && delivered.value(key)==sequence))continue;
            const auto* phase=closing?"editor.closed":(delivered.contains(key)?"editor.changed":"editor.opened");
            const int result=host.invoke_editor(host.context,key,t.constData(),phase,&state);
            if(result!=HA_BUSY)delivered[key]=sequence; // Retry a busy observer with the newest snapshot.
        }
    }
    Controller(QMainWindow* w, QString t, const HA_UiHost& h) : QObject(w), window(w), tool(std::move(t)), host(h) {
        static const auto processSession=QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
        static quint64 nextWindow=0;
        session=processSession;windowId=++nextWindow;
        if(tool=="hammer")buildOutput=attach_build_output(w,host,windowId);
        if(host.editor_window)host.editor_window(host.context,windowId,1);
        // Use cached metadata during destruction: the QMainWindow subobject is gone.
        connect(w,&QObject::destroyed,this,[this]{
            if(host.editor_window)host.editor_window(host.context,windowId,0);
            publish(true);
        });
    }
    void report(const QJsonObject& c, const QString& text) {
        const auto key = handle(c);
        if (reports.value(key) == text) return;
        reports[key] = text;
        if (host.report_binding) {
            const auto t = tool.toUtf8(), message = text.toUtf8();
            host.report_binding(host.context,key,t.constData(),message.constData());
        }
    }
    int invoke(const QJsonObject& c, const char* phase, const QString& control = {}, const QString& value = {}) {
        if (!host.invoke) return HA_ERROR;
        char response[4096]{};
        const auto t = tool.toUtf8(), id = control.toUtf8(), v = value.toUtf8();
        const auto result = host.invoke(host.context,handle(c),t.constData(),phase,id.constData(),v.constData(),response,sizeof(response));
        response[sizeof(response)-1] = 0;
        if (!window) return result;
        if (result == HA_BUSY) window->statusBar()->showMessage("Add-on is busy. Try again.",5000);
        else if (*response) window->statusBar()->showMessage(QString::fromUtf8(response),10000);
        else if (result == HA_ERROR) window->statusBar()->showMessage(c["label"].toString()+": action failed; see add-on diagnostics.",8000);
        return result;
    }
    void import_file(const QJsonObject& c) {
        // The picker only selects a source file. The add-on owns conversion/import.
        auto* picker = new QFileDialog(window,c["label"].toString());
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setOption(QFileDialog::DontUseNativeDialog);
        picker->setFileMode(QFileDialog::ExistingFile);
        QStringList patterns;
        for (const auto& extension : c["options"].toString().split(',')) patterns << "*."+extension;
        picker->setNameFilter(c["label"].toString()+" ("+patterns.join(' ')+")");
        picker->setObjectName("HA.ImportPicker");
        connect(picker,&QFileDialog::fileSelected,this,[this,c](const QString& file) {
            if (invoke(c,"import",{},QFileInfo(file).absoluteFilePath()) == HA_CONTINUE && window)
                window->statusBar()->showMessage(c["label"].toString()+": file was not handled.",8000);
        });
        picker->open();
    }
    void import_routes(Hook* hook, const QJsonArray& routes) {
        auto* dialog = new QDialog(window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle("Choose importer");
        dialog->setObjectName("HA.ImportRoutes");
        auto* layout = new QVBoxLayout(dialog);
        layout->addWidget(new QLabel("Choose how to import this file.",dialog));
        auto* builtin = new QPushButton("Built-in importer",dialog);
        builtin->setObjectName("HA.BuiltinImporter");
        layout->addWidget(builtin);
        ha::UiPointer<Hook> weak(hook);
        connect(builtin,&QPushButton::clicked,dialog,[dialog,weak] {
            dialog->close();
            if (weak && weak->original && weak->original->isEnabled()) weak->original->trigger();
        });
        for (const auto& entry : routes) {
            const auto c = entry.toObject();
            auto* button = new QPushButton(c["label"].toString()+" (."+c["options"].toString().replace(',',", .")+")",dialog);
            button->setObjectName("HA.Route."+QString::number(handle(c)));
            layout->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,dialog,c] { dialog->close(); import_file(c); });
        }
        auto* cancel = new QPushButton("Cancel",dialog);
        connect(cancel,&QPushButton::clicked,dialog,&QDialog::close);
        layout->addWidget(cancel);
        dialog->show();
    }
    void add_panel(const QJsonObject& c, QAction* action, Binding& binding) {
        auto* dock = new QDockWidget(c["label"].toString(),window);
        dock->setObjectName("HA.Panel."+c["owner"].toString()+"."+c["id"].toString());
        binding.dock = dock;
        auto* body = new QWidget(dock);
        auto* form = new QFormLayout(body);
        for (const auto& entry : c["controls"].toArray()) {
            const auto control = entry.toObject();
            const auto id = control["id"].toString(), label = control["label"].toString(), initial = control["value"].toString();
            QWidget* widget = nullptr;
            switch (control["kind"].toInt()) {
            case HA_LABEL: {
                auto* text = new QLabel(initial,body); text->setTextFormat(Qt::PlainText); text->setWordWrap(true);
                form->addRow(label,text); widget=text; break;
            }
            case HA_BUTTON: {
                auto* button = new QPushButton(label,body); form->addRow(button); widget=button;
                connect(button,&QPushButton::clicked,this,[this,c,id] { invoke(c,"panel.click",id); }); break;
            }
            case HA_TABLE: {
                auto* table=new QTreeWidget(body);table->setRootIsDecorated(false);
                table->setHeaderLabels(control["options"].toString().split('\t'));
                table->setSelectionMode(QAbstractItemView::SingleSelection);
                table->setEditTriggers(QAbstractItemView::NoEditTriggers);
                table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
                table->header()->setStretchLastSection(true);table->setMinimumHeight(140);
                update_table(table,initial);form->addRow(label,table);widget=table;
                connect(table,&QTreeWidget::currentItemChanged,this,[this,c,id](QTreeWidgetItem* row,QTreeWidgetItem*) {
                    if(row)invoke(c,"panel.change",id,row->data(0,Qt::UserRole).toString());
                });break;
            }
            case HA_TEXT_VIEW: {
                auto* text=new QPlainTextEdit(initial,body);text->setReadOnly(true);
                text->setMinimumHeight(140);form->addRow(label,text);widget=text;break;
            }
            case HA_TEXT: {
                auto* text = new QLineEdit(initial,body); text->setMaxLength(4096); form->addRow(label,text); widget=text;
                connect(text,&QLineEdit::editingFinished,this,[this,c,id,text] { invoke(c,"panel.change",id,text->text()); }); break;
            }
            case HA_CHECKBOX: {
                auto* check = new QCheckBox(label,body); check->setChecked(initial=="1"); form->addRow(check); widget=check;
                connect(check,&QCheckBox::toggled,this,[this,c,id](bool checked) { invoke(c,"panel.change",id,checked?"1":"0"); }); break;
            }
            case HA_CHOICE: {
                auto* choice = new QComboBox(body); choice->addItems(control["options"].toString().split('\n'));
                choice->setCurrentIndex(initial.toInt()); form->addRow(label,choice); widget=choice;
                connect(choice,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this,c,id](int index) {
                    invoke(c,"panel.change",id,QString::number(index)); }); break;
            }
            }
            if (widget) widget->setObjectName("HA.Control."+id);
        }
        auto* scroll=new QScrollArea(dock);scroll->setWidgetResizable(true);scroll->setWidget(body);
        dock->setWidget(scroll);
        window->addDockWidget(Qt::RightDockWidgetArea,dock);
        dock->hide(); // Add-on UI is opened explicitly, never forced over the user's editor.
        connect(action,&QAction::triggered,dock,[dock] { dock->show(); dock->raise(); });
    }
    void refresh(const QJsonArray& all) {
        QJsonArray selected;
        QMap<QString,QJsonArray> desiredHooks;
        QSet<quint64> alive;
        for (const auto& entry : all) {
            const auto c=entry.toObject();
            if (c["tool"] != "all" && c["tool"] != tool) continue;
            selected.append(c); alive.insert(handle(c));
            if (c["kind"].toInt()==HA_MENU_HOOK || c["kind"].toInt()==HA_IMPORT_ROUTE)
                desiredHooks[c["target"].toString()].append(c);
        }
        for (auto it=bindings.begin();it!=bindings.end();) {
            if (!alive.contains(it.key())) {
                if (it->action) delete it->action;
                if (it->dock) delete it->dock;
                it=bindings.erase(it);
            } else ++it;
        }
        for (auto it=hooks.begin();it!=hooks.end();) {
            if (!desiredHooks.contains(it.key()) || !it.value()->original || !it.value()->proxy) {
                delete it.value(); it=hooks.erase(it);
            } else ++it;
        }
        for (auto it=desiredHooks.begin();it!=desiredHooks.end();++it) {
            if (hooks.contains(it.key())) { hooks[it.key()]->definitions=it.value(); continue; }
            const auto target=locate(window,it.key(),true);
            // Checkable/group actions have state semantics that this adapter does not override.
            if (!target.menu || !target.action || target.action->menu() || target.action->isCheckable() || target.action->actionGroup()) {
                for (const auto& c:it.value()) report(c.toObject(),"Waiting: action target missing, ambiguous or unsupported: "+it.key());
                continue;
            }
            auto* hook=new Hook(this,target.menu,target.action);
            hook->definitions=it.value();
            hooks[it.key()]=hook;
            for (const auto& c:it.value()) report(c.toObject(),"Attached to "+it.key());
        }
        for (const auto& entry:selected) {
            const auto c=entry.toObject();
            const auto kind=c["kind"].toInt(); const auto key=handle(c);
            if (kind==HA_MENU_HOOK || kind==HA_IMPORT_ROUTE) continue;
            if(kind==HA_BUILD_OBSERVER) {
                report(c,host.invoke_build ? "Observing Hammer build dialogs; waiting for displayed output" : "Unavailable: build output bridge missing");
                continue;
            }
            if(kind==HA_EDITOR_OBSERVER) {
                report(c,host.invoke_editor ? "Observing editor metadata" : "Unavailable: editor event bridge missing");
                continue;
            }
            if (bindings.contains(key)) {
                if (bindings[key].action) {
                    if(auto* dock=bindings[key].dock.data()) for(const auto& item:c["controls"].toArray()) {
                        const auto v=item.toObject();const auto object="HA.Control."+v["id"].toString();
                        if(v["kind"].toInt()==HA_TABLE) {
                            if(auto* table=dock->findChild<QTreeWidget*>(object))update_table(table,v["value"].toString());
                        } else if(v["kind"].toInt()==HA_TEXT_VIEW) {
                            auto* view=dock->findChild<QPlainTextEdit*>(object);
                            if(view && view->toPlainText()!=v["value"].toString())view->setPlainText(v["value"].toString());
                        } else if(v["kind"].toInt()==HA_LABEL) {
                            if(auto* label=dock->findChild<QLabel*>(object))label->setText(v["value"].toString());
                        }
                    }
                    continue;
                }
                if (bindings[key].dock) delete bindings[key].dock;
                bindings.remove(key); // Reattach when an editor rebuilds a menu.
            }
            QMenu* menu = nullptr;
            const auto target=c["target"].toString();
            if (target.isEmpty()) menu=window->findChild<QMenu*>("HammerAddonsMenu");
            else menu=locate(window,target,false).menu;
            if (!menu) { report(c,"Waiting: menu target missing or ambiguous: "+target); continue; }
            const auto shortcut=QKeySequence(c["options"].toString(),QKeySequence::PortableText);
            bool conflict=false;
            if (kind==HA_COMMAND && !shortcut.isEmpty())
                for (auto* a:window->findChildren<QAction*>()) if (a->shortcuts().contains(shortcut)) conflict=true;
            if (conflict) { report(c,"Waiting: keyboard shortcut conflicts with an existing action"); continue; }
            auto* action=new QAction(c["label"].toString(),menu);
            action->setObjectName("HA.Action."+c["owner"].toString()+"."+c["id"].toString());
            action->setToolTip("Added by "+c["owner"].toString());
            menu->addAction(action);
            Binding binding; binding.action=action;
            if (kind==HA_PANEL) add_panel(c,action,binding);
            else if (kind==HA_IMPORTER) connect(action,&QAction::triggered,this,[this,c] { import_file(c); });
            else {
                if (!shortcut.isEmpty()) { action->setShortcut(shortcut); action->setShortcutContext(Qt::WindowShortcut); }
                connect(action,&QAction::triggered,this,[this,c] { invoke(c,"command"); });
            }
            bindings[key]=binding;
            report(c,"Attached");
        }
        snapshot=selected;
        refresh_build_output(buildOutput,selected);
        publish();
    }
};
Hook::Hook(Controller* o,QMenu* m,QAction* a) : QObject(o),owner(o),menu(m),original(a),shortcuts(a->shortcuts()) {
    proxy=new QAction(a->icon(),a->text(),m);
    proxy->setObjectName("HA.Hook."+a->objectName());
    proxy->setShortcuts(shortcuts); proxy->setShortcutContext(a->shortcutContext());
    auto sync=[this] {
        if (!original || !proxy) return;
        // Native shortcut changes must not create a bypass around the wrapper.
        if (!original->shortcuts().isEmpty()) {
            shortcuts=original->shortcuts();
            const QSignalBlocker blocker(original);
            original->setShortcuts({});
            proxy->setShortcuts(shortcuts);
        }
        proxy->setShortcutContext(original->shortcutContext());
        proxy->setText(original->text()); proxy->setIcon(original->icon());
        proxy->setEnabled(original->isEnabled()); proxy->setVisible(original->isVisible());
        proxy->setStatusTip(original->statusTip()); proxy->setToolTip(original->toolTip());
    };
    a->setShortcuts({});
    m->insertAction(a,proxy); m->removeAction(a);
    sync();
    connect(a,&QAction::changed,this,sync);
    connect(proxy,&QAction::triggered,this,[this] { trigger(); });
    connect(a,&QObject::destroyed,this,[this] { if(proxy) proxy->setEnabled(false); });
}
Hook::~Hook() {
    if (original) disconnect(original,nullptr,this,nullptr);
    if (menu && original && proxy) {
        menu->insertAction(proxy,original);
        original->setShortcuts(shortcuts);
    }
    if (proxy) delete proxy;
}
void Hook::trigger() {
    if (running || !original || !original->isEnabled()) return;
    running=true;
    // A modal native action may run nested event loops. The adapter defers refresh
    // while this invocation is active so this wrapper remains alive until it returns.
    ha::UiPointer<Hook> self(this);
    const auto copied = definitions;
    QJsonArray routes;
    for (const auto& entry:copied) {
        const auto c=entry.toObject();
        if (c["kind"].toInt()==HA_IMPORT_ROUTE) routes.append(c);
        else {
            const int result=owner->invoke(c,"hook.before");
            if (!self) return;
            if (result != HA_CONTINUE) { running=false; return; }
        }
    }
    if (!routes.isEmpty()) owner->import_routes(this,routes);
    else original->trigger();
    if (!self) return;
    for (const auto& entry:copied) {
        if (entry.toObject()["kind"].toInt()==HA_MENU_HOOK) owner->invoke(entry.toObject(),"hook.after");
        if (!self) return;
    }
    running=false;
}
}
QObject* attach_extensions(QMainWindow* window,const QString& tool,const HA_UiHost& host) {
    return new Controller(window,tool,host);
}
void refresh_extensions(QObject* object,const QJsonArray& contributions) {
    auto* controller=static_cast<Controller*>(object);
    for (auto* hook:controller->hooks) if(hook->running) return;
    controller->refresh(contributions);
}

QJsonArray extension_status(QObject* object) {
    auto* controller=static_cast<Controller*>(object);
    QJsonArray result;
    for (const auto& entry:controller->snapshot) {
        auto row=entry.toObject();
        row["binding"]=controller->reports.value(handle(row),"Waiting for this window");
        result.append(row);
    }
    return result;
}
