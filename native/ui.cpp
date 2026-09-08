#include "qt_compat.h"
#include "ui_bridge.h"
#include <QApplication>
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

class Panel final : public QObject {
    HA_UiHost host_;
    QByteArray previous_;
    QPointer<QDockWidget> dock_;
    QLabel* summary_ = nullptr;
    QLabel* notice_ = nullptr;
    QTreeWidget* rows_ = nullptr;
    QString directory_;

    void open_folder() {
        if (!directory_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(directory_));
    }
    void attach(QMainWindow* window) {
        auto* dock = new QDockWidget(QStringLiteral("Workshop Add-ons"), window);
        dock->setObjectName(QStringLiteral("HammerAddonsDock"));
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        auto* panel = new QWidget(dock);
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
        notice_ = new QLabel(panel);
        notice_->setWordWrap(true);
        notice_->setTextFormat(Qt::PlainText);
        notice_->setObjectName(QStringLiteral("HammerAddonsNotice"));
        layout->addWidget(notice_);
        rows_ = new QTreeWidget(panel);
        rows_->setObjectName(QStringLiteral("HammerAddonsList"));
        rows_->setHeaderLabels({QStringLiteral("Add-on"), QStringLiteral("Version"), QStringLiteral("Status"), QStringLiteral("Details")});
        rows_->setRootIsDecorated(false);
        rows_->setAlternatingRowColors(true);
        rows_->setMinimumHeight(70);
        rows_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        rows_->header()->setStretchLastSection(true);
        layout->addWidget(rows_);
        auto* help = new QLabel(QStringLiteral("Drop add-ons into the folder, then restart Workshop Tools. Edit addon.ini to enable or disable an add-on."), panel);
        help->setWordWrap(true);
        layout->addWidget(help);
        dock->setWidget(panel);
        window->addDockWidget(Qt::BottomDockWidgetArea, dock);
        window->resizeDocks({dock}, {190}, Qt::Vertical);
        auto* menu = window->menuBar()->addMenu(QStringLiteral("Workshop Add-ons"));
        menu->setObjectName(QStringLiteral("HammerAddonsMenu"));
        auto* show = menu->addAction(QStringLiteral("Show add-ons"));
        show->setObjectName(QStringLiteral("HammerAddonsShow"));
        // Context is the dock, so callbacks disappear with the editor window.
        connect(show, &QAction::triggered, dock, [dock] { dock->show(); dock->raise(); });
        connect(menu->addAction(QStringLiteral("Open add-ons folder")), &QAction::triggered, this, [this] { open_folder(); });
        dock_ = dock;
        previous_.clear();
        dock->show();
    }
    void tick() {
        if (!dock_) return;
        const size_t size = host_.read_status(host_.context, nullptr, 0);
        if (!size || size > 1024 * 1024) return;
        QByteArray data(static_cast<int>(size), '\0');
        const size_t actual = host_.read_status(host_.context, data.data(), size);
        if (!actual || actual > size) return; // Snapshot grew or callbacks are busy; retry next tick.
        data.resize(static_cast<int>(actual - 1));
        if (data == previous_) return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return;
        previous_ = data;
        const auto status = document.object();
        directory_ = status.value(QStringLiteral("directory")).toString();
        rows_->clear();
        int loaded = 0, disabled = 0, failed = 0;
        for (const auto& item : status.value(QStringLiteral("addons")).toArray()) {
            const auto addon = item.toObject();
            const auto state = addon.value(QStringLiteral("state")).toString();
            loaded += state == QStringLiteral("Loaded");
            disabled += state == QStringLiteral("Disabled");
            failed += state == QStringLiteral("Failed");
            auto* row = new QTreeWidgetItem(rows_, {addon.value(QStringLiteral("id")).toString(),
                addon.value(QStringLiteral("version")).toString(), state, addon.value(QStringLiteral("detail")).toString()});
            row->setToolTip(3, row->text(3));
        }
        summary_->setText(QStringLiteral("%1 loaded  |  %2 disabled  |  %3 failed").arg(loaded).arg(disabled).arg(failed));
        auto notice = status.value(QStringLiteral("notice")).toString();
        if (notice.isEmpty() && rows_->topLevelItemCount() == 0)
            notice = QStringLiteral("No add-ons found. Open the folder to install your first add-on.");
        notice_->setText(notice);
        notice_->setVisible(!notice.isEmpty());
    }
public:
    Panel(const HA_UiHost& host, QMainWindow* window) : QObject(window), host_(host) {
        attach(window);
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
            auto* window = qobject_cast<QMainWindow*>(widget);
            if (!window || !window->isVisible() || window->findChild<QDockWidget*>("HammerAddonsDock"))
                continue;
            const auto title = window->windowTitle();
            bool supported = false;
            for (const auto& name : {QStringLiteral("Asset Browser"), QStringLiteral("Hammer"), QStringLiteral("Source 2 Tools")}) {
                supported |= title == name || title.startsWith(name + QStringLiteral(" -")) ||
                    title.endsWith(QStringLiteral(" - ") + name);
            }
            if (supported) new Panel(host_, window);
        }
    }
public:
    Controller(const HA_UiHost& host, QApplication* app) : QObject(app), host_(host) {
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { discover(); });
        timer->start(750);
        discover();
    }
};

}

extern "C" __declspec(dllexport) int __cdecl HA_StartUi(const HA_UiHost* host) noexcept {
    if (!host || host->size < sizeof(HA_UiHost) || !host->read_status ||
        std::strcmp(qVersion(), "5.15.2") != 0) return 0;
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return 0;
    if (scheduled.exchange(true)) return 1;
    const auto copy = *host;
    // Create every widget on Qt's GUI thread, even if the factory runs elsewhere.
    if (!QMetaObject::invokeMethod(app, [copy, app] { new Controller(copy, app); }, Qt::QueuedConnection)) {
        scheduled = false;
        return 0;
    }
    return 1;
}
