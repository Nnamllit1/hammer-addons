#include "qt_compat.h"
#include "ui_bridge.h"
#include "ui_extensions.h"
#include <QApplication>
#include <QStatusBar>
#include <QFileInfo>
#include <QComboBox>
#include <QTabWidget>
#include <QDesktopServices>
#include <QDockWidget>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <atomic>
#include <cstring>

namespace {
std::atomic<bool> scheduled{false};
static QString tool_label(const QString& id) {
    if (id == "project_picker") return QStringLiteral("Workshop project picker");
    if (id == "all") return QStringLiteral("All tools");
    if (id == "asset_browser") return QStringLiteral("Asset Browser");
    if (id == "hammer") return QStringLiteral("Hammer");
    if (id == "modeldoc") return QStringLiteral("ModelDoc / Model Viewer");
    if (id == "material_editor") return QStringLiteral("Material Editor");
    if (id == "particle_editor") return QStringLiteral("Particle Editor");
    return QStringLiteral("Unspecified");
}
static bool is_hammer(const QString& title) {
    return title == "Hammer" || title.startsWith("Hammer -") || title.endsWith(" - Hammer");
}

static QString window_tool(const QString& title) {
    if (title == "Workshop Tools Add-ons") return QStringLiteral("project_picker");
    if (is_hammer(title)) return QStringLiteral("hammer");
    for (const auto& pair : {qMakePair(QStringLiteral("Asset Browser"),QStringLiteral("asset_browser")),
            qMakePair(QStringLiteral("Source 2 Tools"),QStringLiteral("asset_browser")),
            qMakePair(QStringLiteral("ModelDoc"),QStringLiteral("modeldoc")),
            qMakePair(QStringLiteral("ModelDoc Editor"),QStringLiteral("modeldoc")),
            qMakePair(QStringLiteral("Material Editor"),QStringLiteral("material_editor")),
            qMakePair(QStringLiteral("Particle Editor"),QStringLiteral("particle_editor"))})
        if (title == pair.first || (title.startsWith(pair.first+" -") || title.startsWith(pair.first+" ::")) || title.endsWith(" - "+pair.first))
            return pair.second;
    return {};
}

class Panel final : public QObject {
    HA_UiHost host_;
    QByteArray previous_;
    QPointer<QDockWidget> dock_;
    QLabel* summary_ = nullptr;
    QLabel* notice_ = nullptr;
    QTreeWidget* rows_ = nullptr;
    QString directory_;
    QComboBox* filter_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QJsonObject snapshot_;
    QObject* extensions_ = nullptr;
    QTreeWidget* extensionRows_ = nullptr;
    QJsonArray previousBindings_;

    void open_folder() {
        if (!directory_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(directory_));
    }
    void attach(QMainWindow* window) {
        auto* dock = new QDockWidget(QStringLiteral("Workshop Add-ons"), window);
        dock->setObjectName(QStringLiteral("HammerAddonsDock"));
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        tabs_ = new QTabWidget(dock);
        tabs_->setObjectName(QStringLiteral("HammerAddonsTabs"));
        auto* panel = new QWidget(tabs_);
        tabs_->addTab(panel, QStringLiteral("Add-ons"));
        auto* about = new QWidget(tabs_);
        auto* aboutLayout = new QVBoxLayout(about);
        auto* title = new QLabel(QStringLiteral("Hammer Addons " HA_VERSION), about);
        title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
        aboutLayout->addWidget(title);
        auto* description = new QLabel(QStringLiteral(
            "A shared add-on framework for CS2 Workshop Tools.\n\n"
            "Add-ons for the Workshop project picker, Asset Browser and editors.\n\n"
            "This is an unofficial project, independent of Valve."), about);
        description->setWordWrap(true);
        aboutLayout->addWidget(description);
        auto* link = new QLabel(QStringLiteral("<a href=\"https://github.com/Nnamllit1/hammer-addons\">Project and documentation</a>"), about);
        link->setOpenExternalLinks(true);
        aboutLayout->addWidget(link);
        aboutLayout->addStretch();
        tabs_->addTab(about, QStringLiteral("About"));
        extensionRows_ = new QTreeWidget(tabs_);
        extensionRows_->setObjectName("HammerAddonsExtensions");
        extensionRows_->setHeaderLabels({"Add-on", "Extension", "Target", "Status in this window"});
        extensionRows_->setRootIsDecorated(false);
        extensionRows_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        extensionRows_->header()->setStretchLastSection(true);
        tabs_->addTab(extensionRows_, "Extensions");
        auto* layout = new QVBoxLayout(panel);
        auto* heading = new QHBoxLayout;
        auto* active = new QLabel(QStringLiteral("Loader active"), panel);
        active->setStyleSheet(QStringLiteral("color: #66c58a; font-weight: bold;"));
        heading->addWidget(active);
        summary_ = new QLabel(panel);
        summary_->setObjectName(QStringLiteral("HammerAddonsSummary"));
        heading->addWidget(summary_);
        heading->addStretch();
        auto* folder = new QPushButton(QStringLiteral("Open add-ons folder"), panel);
        connect(folder, &QPushButton::clicked, this, [this] { open_folder(); });
        heading->addWidget(folder);
        layout->addLayout(heading);
        auto* filtering = new QHBoxLayout;
        filtering->addWidget(new QLabel(QStringLiteral("Tool:"), panel));
        filter_ = new QComboBox(panel);
        filter_->setObjectName(QStringLiteral("HammerAddonsFilter"));
        filter_->addItem(QStringLiteral("All add-ons"), QString());
        for (const auto& id : {"project_picker", "asset_browser", "hammer", "modeldoc", "material_editor", "particle_editor", "unspecified"})
            filter_->addItem(tool_label(QString::fromLatin1(id)), QString::fromLatin1(id));
        filtering->addWidget(filter_);
        filtering->addStretch();
        layout->addLayout(filtering);
        connect(filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { render(); });
        notice_ = new QLabel(panel);
        notice_->setWordWrap(true);
        notice_->setTextFormat(Qt::PlainText);
        notice_->setObjectName(QStringLiteral("HammerAddonsNotice"));
        layout->addWidget(notice_);
        rows_ = new QTreeWidget(panel);
        rows_->setObjectName(QStringLiteral("HammerAddonsList"));
        rows_->setHeaderLabels({QStringLiteral("Add-on"), QStringLiteral("Version"), QStringLiteral("Status"), QStringLiteral("Details"), QStringLiteral("Tools")});
        rows_->setRootIsDecorated(false);
        rows_->setAlternatingRowColors(true);
        rows_->setMinimumHeight(70);
        rows_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        rows_->header()->setStretchLastSection(true);
        layout->addWidget(rows_);
        auto* help = new QLabel(QStringLiteral("Drop add-ons into the folder, then restart Workshop Tools. Edit addon.ini to enable or disable an add-on."), panel);
        help->setWordWrap(true);
        layout->addWidget(help);
        dock->setWidget(tabs_);
        window->addDockWidget(Qt::BottomDockWidgetArea, dock);
        window->resizeDocks({dock}, {250}, Qt::Vertical);
        const bool hammer = is_hammer(window->windowTitle());
        QMenu* menu = nullptr;
        if (hammer) {
            // Reuse Hammer's Help menu; avoid another top-level editor menu.
            QMenu* helpMenu = nullptr;
            for (auto* action : window->menuBar()->actions()) {
                auto text = action->text();
                text.remove('&');
                if (action->menu() && text.compare(QStringLiteral("Help"), Qt::CaseInsensitive) == 0) {
                    helpMenu = action->menu();
                    break;
                }
            }
            if (!helpMenu) helpMenu = window->menuBar()->addMenu(QStringLiteral("Help"));
            menu = helpMenu->addMenu(QStringLiteral("Workshop Add-ons"));
        } else menu = window->menuBar()->addMenu(QStringLiteral("Workshop Add-ons"));
        menu->setObjectName(QStringLiteral("HammerAddonsMenu"));
        auto* show = menu->addAction(QStringLiteral("Show add-ons"));
        show->setObjectName(QStringLiteral("HammerAddonsShow"));
        // Context is the dock, so callbacks disappear with the editor window.
        connect(show, &QAction::triggered, this, [this, dock] { tabs_->setCurrentIndex(0); dock->show(); dock->raise(); });
        auto* aboutAction = menu->addAction(QStringLiteral("About Hammer Addons"));
        aboutAction->setObjectName(QStringLiteral("HammerAddonsAbout"));
        connect(aboutAction, &QAction::triggered, this, [this, dock] { tabs_->setCurrentIndex(1); dock->show(); dock->raise(); });
        connect(menu->addAction(QStringLiteral("Open add-ons folder")), &QAction::triggered, this, [this] { open_folder(); });
        dock_ = dock;
        previous_.clear();
        if (hammer) {
            filter_->setCurrentIndex(filter_->findData(QStringLiteral("hammer")));
            dock->hide();
        } else {
            if(window_tool(window->windowTitle())=="project_picker")
                filter_->setCurrentIndex(filter_->findData(QStringLiteral("project_picker")));
            dock->show();
        }
    }
    void tick() {
        if (!dock_) return;
        const size_t size = host_.read_status(host_.context, nullptr, 0);
        if (!size || size > 1024 * 1024) return;
        QByteArray data(static_cast<int>(size), '\0');
        const size_t actual = host_.read_status(host_.context, data.data(), size);
        if (!actual || actual > size) return; // Snapshot grew or callbacks are busy; retry next tick.
        data.resize(static_cast<int>(actual - 1));
        if (data == previous_) {
            if (extensions_) { refresh_extensions(extensions_,snapshot_["contributions"].toArray()); render_extensions(); }
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return;
        previous_ = data;
        snapshot_ = document.object();
        if (extensions_) { refresh_extensions(extensions_,snapshot_["contributions"].toArray()); render_extensions(); }
        render();
    }
    void render_extensions() {
        const auto bindings=extension_status(extensions_);
        if (bindings==previousBindings_) return;
        previousBindings_=bindings;
        extensionRows_->clear();
        for (const auto& entry:bindings) {
            const auto row=entry.toObject();
            auto* item=new QTreeWidgetItem(extensionRows_, {row["owner"].toString(),row["label"].toString(),
                row["target"].toString().isEmpty() ? QString("Workshop Add-ons") : row["target"].toString(),row["binding"].toString()});
            item->setToolTip(3,item->text(3));
        }
    }
    void render() {
        const auto& status = snapshot_;
        const auto selected = filter_->currentData().toString();
        directory_ = status.value(QStringLiteral("directory")).toString();
        rows_->clear();
        int loaded = 0, disabled = 0, failed = 0;
        const auto addons = status.value(QStringLiteral("addons")).toArray();
        for (const auto& item : addons) {
            const auto addon = item.toObject();
            const auto tools = addon.value(QStringLiteral("tools")).toArray();
            const bool unspecified = tools.isEmpty();
            if (!selected.isEmpty() && !(selected == "unspecified" ? unspecified :
                tools.contains(selected) || (selected != "project_picker" && tools.contains(QStringLiteral("all"))))) continue;
            QStringList labels;
            for (const auto& tool : tools) labels.append(tool_label(tool.toString()));
            if (labels.isEmpty()) labels.append(tool_label(QString()));
            const auto state = addon.value(QStringLiteral("state")).toString();
            loaded += state == QStringLiteral("Loaded");
            disabled += state == QStringLiteral("Disabled");
            failed += state == QStringLiteral("Failed");
            auto* row = new QTreeWidgetItem(rows_, {addon.value(QStringLiteral("id")).toString(),
                addon.value(QStringLiteral("version")).toString(), state, addon.value(QStringLiteral("detail")).toString(), labels.join(QStringLiteral(", "))});
            row->setToolTip(3, row->text(3));
        }
        summary_->setText(QStringLiteral("%1 loaded  |  %2 disabled  |  %3 failed").arg(loaded).arg(disabled).arg(failed));
        auto notice = status.value(QStringLiteral("notice")).toString();
        if (notice.isEmpty() && rows_->topLevelItemCount() == 0)
            notice = addons.isEmpty() ? QStringLiteral("No add-ons found. Open the folder to install your first add-on.") :
                QStringLiteral("No add-ons match this tool filter.");
        notice_->setText(notice);
        notice_->setVisible(!notice.isEmpty());
    }
public:
    void reopen(QMainWindow* window) {
        // Closing a floating dock hides it independently from its host window.
        // The picker button must restore content as well as the outer window.
        if(dock_) {
            dock_->setFloating(false);
            tabs_->setCurrentIndex(0);
            dock_->show();
            dock_->raise();
        }
        if(window->isMinimized()) window->showNormal();
        else window->show();
        window->raise();
        window->activateWindow();
    }
    Panel(const HA_UiHost& host, QMainWindow* window) : QObject(window), host_(host) {
        attach(window);
        extensions_ = attach_extensions(window,window_tool(window->windowTitle()),host_);
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { tick(); });
        timer->start(750);
        tick();
    }
};
class Controller final : public QObject {
    HA_UiHost host_;
    void discover() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            // QMainWindow's internal layout cannot accept arbitrary widgets.
            // Use its status bar, preserving Valve's central project-selection UI.
            if(QFileInfo(QCoreApplication::applicationFilePath()).fileName().compare("csgocfg.exe",Qt::CaseInsensitive)==0 &&
               widget->isVisible() && widget->windowTitle().trimmed()=="Workshop Tools" && widget->layout() &&
               !widget->property("ha_picker_attached").toBool()) {
                widget->setProperty("ha_picker_attached",true);
                auto* manager=new QMainWindow(widget,Qt::Tool);
                manager->setWindowTitle("Workshop Tools Add-ons");
                manager->resize(700,500);
                auto* panel=new Panel(host_,manager);
                auto* button=new QPushButton("Workshop Add-ons",widget);
                button->setToolTip("Add-on loader active. Open the add-on manager.");
                button->setMinimumSize(button->sizeHint());
                button->setObjectName("HammerAddonsPickerButton");
                if(auto* picker=qobject_cast<QMainWindow*>(widget)) {
                    picker->statusBar()->setSizeGripEnabled(false);
                    picker->statusBar()->showMessage("Add-on loader active");
                    picker->statusBar()->addPermanentWidget(button);
                } else if(auto* box=qobject_cast<QBoxLayout*>(widget->layout())) {
                    box->addWidget(button,0,Qt::AlignRight);
                }
                connect(button,&QPushButton::clicked,manager,[manager,panel]{panel->reopen(manager);});
            }
            auto* window = qobject_cast<QMainWindow*>(widget);
            if (!window || !window->isVisible() || window->findChild<QDockWidget*>("HammerAddonsDock"))
                continue;
            const bool supported = !window_tool(window->windowTitle()).isEmpty();
            if (supported) new Panel(host_, window);
        }
    }
public:
    Controller(const HA_UiHost& host, QApplication* app) : QObject(app), host_(host) {
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { discover(); });
        timer->start(750);
        if(host_.pump_jobs) {
            auto* jobs=new QTimer(this);
            connect(jobs,&QTimer::timeout,this,[this]{host_.pump_jobs(host_.context);});
            jobs->start(50);
        }
        discover();
    }
};

}

extern "C" __declspec(dllexport) int __cdecl HA_StartUi(const HA_UiHost* host) noexcept {
    if (!host || host->size < offsetof(HA_UiHost,invoke_editor) || !host->read_status ||
        std::strcmp(qVersion(), "5.15.2") != 0) return 0;
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return 0;
    if (scheduled.exchange(true)) return 1;
    HA_UiHost copy{};
    std::memcpy(&copy,host,std::min<size_t>(host->size,sizeof(copy)));
    // Create every widget on Qt's GUI thread, even if the factory runs elsewhere.
    if (!QMetaObject::invokeMethod(app, [copy, app] { new Controller(copy, app); }, Qt::QueuedConnection)) {
        scheduled = false;
        return 0;
    }
    return 1;
}
