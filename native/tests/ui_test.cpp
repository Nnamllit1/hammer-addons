#include "qt_compat.h"
#include "ui_bridge.h"
#include <QApplication>
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
static QByteArray status = R"({"directory":"C:/addons","notice":"","addons":[{"id":"hello","version":"0.1.0","state":"Loaded","detail":"Ready"},{"id":"off","version":"1.0.0","state":"Disabled","detail":"Disabled in addon.ini"},{"id":"broken","version":"","state":"Failed","detail":"Missing DLL"}]})";
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
        hammer->setWindowTitle("Hammer"); hammer->resize(900, 600); hammer->show();
        pump();
        auto* dock = hammer->findChild<QDockWidget*>("HammerAddonsDock");
        check(dock && dock != browserDock && dock->isVisible() && dock->thread() == app.thread());
        auto* rows = dock->findChild<QTreeWidget*>("HammerAddonsList");
        check(rows && rows->topLevelItemCount() == 3);
        check(rows->topLevelItem(2)->text(2) == "Failed");
        check(dock->findChild<QLabel*>("HammerAddonsSummary")->text().startsWith("1 loaded"));
        check(HA_StartUi(&host) == 1); pump();
        check(hammer->findChildren<QDockWidget*>("HammerAddonsDock").size() == 1);
        check(browser.findChildren<QDockWidget*>("HammerAddonsDock").size() == 1);
        dock->close(); pump(); check(!dock->isVisible() && browserDock->isVisible());
        hammer->findChild<QAction*>("HammerAddonsShow")->trigger();
        check(dock->isVisible());
        busy = true; pump(); check(rows->topLevelItemCount() == 3); busy = false;
        status = R"({"directory":"C:/addons","notice":"Globally disabled","addons":[]})";
        pump(); check(rows->topLevelItemCount() == 0);
        check(browserDock->findChild<QTreeWidget*>("HammerAddonsList")->topLevelItemCount() == 0);
        check(dock->findChild<QLabel*>("HammerAddonsNotice")->text() == "Globally disabled");
        delete hammer; pump(); check(browserDock->isVisible());
        browserDock->close(); pump(); check(!browserDock->isVisible());
        browser.findChild<QAction*>("HammerAddonsShow")->trigger(); check(browserDock->isVisible());
        QMainWindow reopened; reopened.setWindowTitle("test.vmap - Hammer"); reopened.show();
        pump(); check(reopened.findChild<QDockWidget*>("HammerAddonsDock") != nullptr);
        std::cout << "UI integration passed: Asset Browser before Hammer, simultaneous panels, independent close/reopen, shared statuses, GUI thread and recreated editor.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
