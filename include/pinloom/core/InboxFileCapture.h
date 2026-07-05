#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

enum class InboxFileArchiveMode {
    Link,
    Copy,
    Move
};

enum class InboxFileSaveStatus {
    Created,
    Updated
};

struct InboxFileSaveRequest {
    QString filePath;
    QString name;
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
    InboxFileArchiveMode mode = InboxFileArchiveMode::Link;
};

struct InboxFileSaveResult {
    QString resourceId;
    QString filePath;
    QString displayName;
    QString status;
    InboxFileSaveStatus saveStatus = InboxFileSaveStatus::Created;
    bool ok = false;

    bool success() const;
};

QString normalizedInboxFilePath(const QString &filePath);
QString inboxResourceIdForPath(const QString &filePath);
bool isInboxResourceId(const QString &resourceId);
bool isInboxResource(const Resource &resource);
QString defaultInboxFileName(const QString &filePath);
QString inboxFileSaveRequestError(const InboxFileSaveRequest &request);
InboxFileSaveResult saveInboxFile(ILibraryRepository &repository, const InboxFileSaveRequest &request);

} // namespace Pinloom
