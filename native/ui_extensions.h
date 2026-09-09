#pragma once
#include "qt_compat.h"
#include "ui_bridge.h"
#include <QJsonArray>
#include <QMainWindow>
#include <QObject>
#include <QString>

// UI objects stay in this DLL. The runtime bridge only copies C data.
QObject* attach_extensions(QMainWindow* window, const QString& tool, const HA_UiHost& host);
void refresh_extensions(QObject* controller, const QJsonArray& contributions);

QJsonArray extension_status(QObject* controller);
