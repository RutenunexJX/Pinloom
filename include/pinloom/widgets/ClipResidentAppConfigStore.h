#pragma once

#include "pinloom/widgets/ClipResidentApp.h"

#include <QString>

namespace Pinloom {

struct ClipResidentAppConfigLoadResult {
    ClipResidentAppConfig config;
    bool loadedFromFile = false;
    QString error;

    bool succeeded() const;
};

class ClipResidentAppConfigStore {
public:
    ClipResidentAppConfigLoadResult load(const QString &filePath) const;
    bool save(const QString &filePath, const ClipResidentAppConfig &config, QString *error = nullptr) const;
};

bool configureClipResidentAppFromConfigFile(ClipResidentApp &app,
                                            const QString &filePath,
                                            const ClipResidentAppConfigStore &store = ClipResidentAppConfigStore{},
                                            QString *error = nullptr);

} // namespace Pinloom
