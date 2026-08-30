#pragma once

#include <QString>
#include <QtGlobal>

namespace Pinloom {

struct ForegroundAppWindowContext {
    QString windowTitle;
    QString processName;
    QString processPath;
    quintptr windowHandle = 0;
    quint32 processId = 0;

    bool isValid() const;
};

ForegroundAppWindowContext currentForegroundAppWindowContext();

} // namespace Pinloom
