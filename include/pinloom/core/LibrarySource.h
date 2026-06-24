#pragma once

#include "pinloom/core/Resource.h"

#include <QList>
#include <QString>

namespace Pinloom {

class ILibrarySource {
public:
    virtual ~ILibrarySource() = default;

    virtual QString displayName() const = 0;
    virtual QList<Resource> scan(QString *errorMessage = nullptr) const = 0;
};

} // namespace Pinloom
