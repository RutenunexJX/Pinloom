#pragma once

#include <QString>

namespace Pinloom {

struct ResourceRelation {
    QString sourceResourceId;
    QString targetResourceId;
    QString label;
    QString note;
};

} // namespace Pinloom
