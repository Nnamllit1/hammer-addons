#include "qt_compat.h"
#include "ui_bridge.h"
#include "ui_pointer.h"
#include <QApplication>
#include <QComboBox>
#include <QTabWidget>
#include <QMenuBar>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QThread>
#include <QTreeWidget>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

extern "C" __declspec(dllimport) int __cdecl HA_StartUi(const HA_UiHost*);
static QByteArray status = R"json({"directory":"C:/addons","notice":"","addons":[{"id":"hello","version":"0.1.0","state":"Loaded","detail":"Ready","tools":["all"],"signature":"Valid (locally pinned key)","publisher":"Example <img src=https://example.invalid/a>","publisher_contact":"author@example.invalid","publisher_fingerprint":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},{"id":"off","version":"1.0.0","state":"Disabled","detail":"Disabled in addon.ini","tools":["modeldoc"]},{"id":"broken","version":"","state":"Failed","detail":"Missing DLL","tools":["asset_browser","hammer"]}]})json";
static bool busy = false;
static size_t __cdecl read_status(void*, char* out, size_t capacity) {
    if (busy) return 0;
    auto size = static_cast<size_t>(status.size() + 1);
    if (out && capacity >= size) std::memcpy(out, status.constData(), size);
    return size;
}
static void check(bool ok) { if (!ok) throw std::runtime_error("UI check failed"); }
static void pump() {
    QElapsedTimer time; time.start();
    while (time.elapsed() < 900) { QApplication::processEvents(); QThread::msleep(10); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        // Guards outlive their QObject and may be copied into maps/callbacks.
        // Releasing them must never free Qt's external weak-reference storage.
        for(int cycle=0;cycle<100;++cycle) {
            auto* owner=new QWidget;
            auto* action=new QAction(owner);
            ha::UiPointer<QAction> guard(action), independent(action);
            auto copied=guard;
            auto moved=std::move(copied);
            check(!copied && moved.data()==action);
            { auto temporary=guard; check(temporary.data()==action); }
            auto* other=new QAction(owner);
            guard=other;
            delete action;
            check(!moved && !independent && guard.data()==other);
            delete owner;
            check(!guard);
        }
        QMainWindow unrelated; unrelated.setWindowTitle("Preferences"); unrelated.show();
        QMainWindow browser; browser.setWindowTitle("Asset Browser"); browser.resize(900, 700); browser.show();
        HA_UiHost host{sizeof(HA_UiHost), nullptr, read_status};
        check(HA_StartUi(nullptr) == 0);
        int started = 0;
        std::thread worker([&] { started = HA_StartUi(&host); }); worker.join();
        check(started == 1);
        pump();
        auto* browserDock = browser.findChild<QDockWidget*>("HammerAddonsDock");
        check(browserDock && browserDock->isVisible());
        check(unrelated.findChild<QDockWidget*>("HammerAddonsDock") == nullptr);
        check(browserDock->windowTitle() == "Workshop Add-ons");
        check(browserDock->findChild<QTreeWidget*>("HammerAddonsList")->topLevelItemCount() == 3);
        auto* hammer = new QMainWindow;
        auto* help = hammer->menuBar()->addMenu("&Help");
        help->addAction("Existing help");
        hammer->setWindowTitle("Hammer"); hammer->resize(900, 600); hammer->show();
        pump();
        auto* dock = hammer->findChild<QDockWidget*>("HammerAddonsDock");
        check(dock && dock != browserDock && !dock->isVisible() && dock->thread() == app.thread());
        check(hammer->menuBar()->actions().size() == 1);
        check(help->actions().size() == 2);
        check(help->actions().last()->menu()->objectName() == "HammerAddonsMenu");
        hammer->findChild<QAction*>("HammerAddonsShow")->trigger();
        check(dock->isVisible());
        auto* rows = dock->findChild<QTreeWidget*>("HammerAddonsList");
        check(rows && rows->topLevelItemCount() == 2);
        check(rows->topLevelItem(1)->text(2) == "Failed");
        const auto* signedRow=rows->topLevelItem(0);
        check(signedRow->text(5)=="Valid (locally pinned key)");
        check(signedRow->text(7)=="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        check(signedRow->toolTip(6).contains("author@example.invalid") && signedRow->toolTip(6).contains("&lt;img") && !signedRow->toolTip(6).contains("<img"));
        check(rows->topLevelItem(1)->text(5)=="Not checked");
        check(dock->findChild<QLabel*>("HammerAddonsSummary")->text().startsWith("1 loaded"));
        check(HA_StartUi(&host) == 1); pump();
        check(hammer->findChildren<QDockWidget*>("HammerAddonsDock").size() == 1);
        check(browser.findChildren<QDockWidget*>("HammerAddonsDock").size() == 1);
        dock->close(); pump(); check(!dock->isVisible() && browserDock->isVisible());
        hammer->findChild<QAction*>("HammerAddonsShow")->trigger();
        check(dock->isVisible());
        auto* filter = browserDock->findChild<QComboBox*>("HammerAddonsFilter");
        auto* browserRows = browserDock->findChild<QTreeWidget*>("HammerAddonsList");
        filter->setCurrentIndex(filter->findData("modeldoc"));
        check(browserRows->topLevelItemCount() == 2);
        check(browserRows->topLevelItem(1)->text(0) == "off");
        check(rows->topLevelItemCount() == 2 && rows->topLevelItem(1)->text(0) == "broken");
        pump(); check(filter->currentData() == "modeldoc");
        filter->setCurrentIndex(filter->findData("unspecified"));
        check(browserRows->topLevelItemCount() == 0);
        check(browserDock->findChild<QLabel*>("HammerAddonsNotice")->text().contains("filter"));
        filter->setCurrentIndex(0);
        check(browserRows->topLevelItemCount() == 3);
        hammer->findChild<QAction*>("HammerAddonsAbout")->trigger();
        check(dock->findChild<QTabWidget*>("HammerAddonsTabs")->currentIndex() == 1);
        hammer->findChild<QAction*>("HammerAddonsShow")->trigger();
        check(dock->findChild<QTabWidget*>("HammerAddonsTabs")->currentIndex() == 0);
        // Missing tags stay discoverable without claiming support for every editor.
        status.replace(",\"tools\":[\"modeldoc\"]", "");
        pump();
        filter->setCurrentIndex(filter->findData("unspecified"));
        check(browserRows->topLevelItemCount() == 1 && browserRows->topLevelItem(0)->text(0) == "off");
        busy = true; pump(); check(rows->topLevelItemCount() == 2); busy = false;
        status = R"({"directory":"C:/addons","notice":"Globally disabled","addons":[]})";
        pump(); check(rows->topLevelItemCount() == 0);
        check(browserDock->findChild<QTreeWidget*>("HammerAddonsList")->topLevelItemCount() == 0);
        check(dock->findChild<QLabel*>("HammerAddonsNotice")->text() == "Globally disabled");
        delete hammer; pump(); check(browserDock->isVisible());
        browserDock->close(); pump(); check(!browserDock->isVisible());
        browser.findChild<QAction*>("HammerAddonsShow")->trigger(); check(browserDock->isVisible());
        QMainWindow reopened; reopened.setWindowTitle("test.vmap - Hammer"); reopened.show();
        pump(); check(reopened.findChild<QDockWidget*>("HammerAddonsDock") != nullptr);
        check(!reopened.findChild<QDockWidget*>("HammerAddonsDock")->isVisible());
        std::cout << "UI integration passed: Hammer Help placement, hidden startup, About, tool filters, all/unspecified tags, independent panels and refresh.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
