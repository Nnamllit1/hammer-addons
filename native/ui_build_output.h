#pragma once
#include "ui_bridge.h"
#include <QJsonArray>
class QObject;
class QMainWindow;
QObject* attach_build_output(QMainWindow*, const HA_UiHost&, uint64_t window_id);
void refresh_build_output(QObject*, const QJsonArray&);
