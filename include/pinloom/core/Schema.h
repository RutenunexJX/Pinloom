#pragma once

#include <QStringList>

namespace Pinloom {

class Schema {
public:
    static int currentVersion();
    static QStringList sqliteFts5Draft();
};

} // namespace Pinloom
