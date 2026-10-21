#pragma once

#include "pinloom/core/AnchorLibraryManagement.h"
#include <QJsonObject>

namespace Pinloom {

class SqliteLibraryRepository;

// The archive and public command API share the persisted model representation.
QJsonObject anchorArchiveJson(const Anchor &anchor);
Anchor anchorFromArchiveJson(const QJsonObject &object);
QJsonObject resourceArchiveJson(const Resource &resource);
std::optional<Resource> resourceFromArchiveJson(const QJsonValue &value, QString *error);

enum class AnchorLibraryImportMode {
    Merge,
    Replace
};

class AnchorLibraryArchiveService {
public:
    explicit AnchorLibraryArchiveService(ILibraryRepository &repository,
                                         QString safetyBackupDirectory = {});

    AnchorLibraryOperationResult exportJson(const QString &filePath) const;
    AnchorLibraryOperationResult importJson(const QString &filePath,
                                             AnchorLibraryImportMode mode);
    AnchorLibraryOperationResult backupDatabase(const QString &filePath) const;
    AnchorLibraryOperationResult restoreDatabase(const QString &filePath);
    AnchorLibraryOperationResult createAutomaticBackup(const QString &directoryPath,
                                                        int retainedBackupCount = 10) const;

private:
    QList<Resource> allResources() const;
    QString automaticBackupPath(const QString &directoryPath, const QString &prefix) const;
    QString safetyBackupDirectory(const SqliteLibraryRepository &repository) const;

    ILibraryRepository &repository_;
    QString safetyBackupDirectory_;
};

} // namespace Pinloom
