#include "pinloom/core/InboxFileCapture.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace Pinloom {

namespace {

constexpr const char *InboxResourcePrefix = "inbox:file:";

bool containsValueCaseInsensitive(const QStringList &values, const QString &needle)
{
    return std::any_of(values.cbegin(), values.cend(), [&](const QString &value) {
        return value.compare(needle, Qt::CaseInsensitive) == 0;
    });
}

void appendUniqueValue(QStringList &values, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !containsValueCaseInsensitive(values, trimmed)) {
        values.append(trimmed);
    }
}

QString cleanTag(QString tag)
{
    tag = tag.trimmed();
    while (tag.startsWith(QLatin1Char('#'))) {
        tag.remove(0, 1);
        tag = tag.trimmed();
    }
    return tag;
}

QStringList cleanedValues(const QStringList &source, bool tags = false)
{
    QStringList values;
    for (const QString &value : source) {
        appendUniqueValue(values, tags ? cleanTag(value) : value);
    }
    return values;
}

QString pathKeyForId(const QString &filePath)
{
    QString key = normalizedInboxFilePath(filePath);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

} // namespace

bool InboxFileSaveResult::success() const
{
    return ok;
}

QString normalizedInboxFilePath(const QString &filePath)
{
    const QString trimmed = filePath.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QString path = QFileInfo(trimmed).absoluteFilePath();
    path = QDir::cleanPath(path);
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

QString inboxResourceIdForPath(const QString &filePath)
{
    const QString key = pathKeyForId(filePath);
    if (key.isEmpty()) {
        return {};
    }

    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("%1%2").arg(QString::fromLatin1(InboxResourcePrefix),
                                      QString::fromLatin1(digest.left(32)));
}

bool isInboxResourceId(const QString &resourceId)
{
    return resourceId.startsWith(QString::fromLatin1(InboxResourcePrefix), Qt::CaseInsensitive);
}

bool isInboxResource(const Resource &resource)
{
    return isInboxResourceId(resource.id);
}

QString defaultInboxFileName(const QString &filePath)
{
    const QFileInfo info(filePath.trimmed());
    const QString baseName = info.completeBaseName().trimmed();
    if (!baseName.isEmpty()) {
        return baseName;
    }
    const QString fileName = info.fileName().trimmed();
    if (!fileName.isEmpty()) {
        return fileName;
    }
    return normalizedInboxFilePath(filePath);
}

QString inboxFileSaveRequestError(const InboxFileSaveRequest &request)
{
    if (request.mode != InboxFileArchiveMode::Link) {
        return QStringLiteral("Inbox MVP supports Link mode only");
    }

    const QString normalizedPath = normalizedInboxFilePath(request.filePath);
    if (normalizedPath.isEmpty()) {
        return QStringLiteral("Inbox file path is required");
    }

    const QFileInfo info(normalizedPath);
    if (info.exists() && !info.isFile()) {
        return QStringLiteral("Inbox captures files only");
    }

    return {};
}

InboxFileSaveResult saveInboxFile(ILibraryRepository &repository, const InboxFileSaveRequest &request)
{
    InboxFileSaveResult result;
    const QString error = inboxFileSaveRequestError(request);
    if (!error.isEmpty()) {
        result.status = error;
        return result;
    }

    const QString normalizedPath = normalizedInboxFilePath(request.filePath);
    const QString resourceId = inboxResourceIdForPath(normalizedPath);
    if (resourceId.isEmpty()) {
        result.status = QStringLiteral("Inbox file path is required");
        return result;
    }

    const std::optional<Resource> existing = repository.findResource(resourceId);
    Resource resource = existing.value_or(Resource{});
    resource.id = resourceId;
    resource.kind = ResourceKind::File;
    resource.location = normalizedPath;

    const QString requestedName = request.name.trimmed();
    if (!requestedName.isEmpty()) {
        resource.title = requestedName;
    } else if (resource.title.trimmed().isEmpty()) {
        resource.title = defaultInboxFileName(normalizedPath);
    }

    const QStringList aliases = cleanedValues(request.aliases);
    for (const QString &alias : aliases) {
        appendUniqueValue(resource.aliases, alias);
    }

    const QStringList tags = cleanedValues(request.tags, true);
    for (const QString &tag : tags) {
        appendUniqueValue(resource.tags, tag);
    }

    resource.content.clear();
    resource.updatedAt = QDateTime::currentDateTimeUtc();

    if (!repository.upsertResource(resource)) {
        result.status = QStringLiteral("Unable to save Inbox file");
        return result;
    }
    if (request.pinned) {
        repository.setResourcePinned(resource.id, true);
    }

    result.ok = true;
    result.resourceId = resource.id;
    result.filePath = resource.location;
    result.displayName = resource.title;
    result.saveStatus = existing.has_value() ? InboxFileSaveStatus::Updated : InboxFileSaveStatus::Created;
    result.status = existing.has_value()
        ? QStringLiteral("Updated Inbox file \"%1\"").arg(resource.title)
        : QStringLiteral("Saved Inbox file \"%1\"").arg(resource.title);
    return result;
}

} // namespace Pinloom
