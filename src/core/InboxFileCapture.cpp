#include "pinloom/core/InboxFileCapture.h"

#include "pinloom/core/GlobalIdentity.h"
#include "pinloom/core/LibraryRoot.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <utility>

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

void appendRequestedIdentityValues(QStringList &stored, const QStringList &requested)
{
    QSet<QString> preexisting;
    for (const QString &value : std::as_const(stored)) {
        const QString key = normalizedGlobalIdentity(value);
        if (!key.isEmpty()) {
            preexisting.insert(key);
        }
    }
    for (const QString &value : requested) {
        const QString key = normalizedGlobalIdentity(value);
        if (!key.isEmpty() && !preexisting.contains(key)) {
            stored.append(value);
        }
    }
}

QString pathKeyForId(const QString &filePath)
{
    QString key = normalizedInboxFilePath(filePath);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

bool samePath(const QString &left, const QString &right)
{
    return normalizedInboxFilePath(left).compare(normalizedInboxFilePath(right),
#ifdef Q_OS_WIN
                                                  Qt::CaseInsensitive
#else
                                                  Qt::CaseSensitive
#endif
                                                  ) == 0;
}

bool moveFileWithFallback(const QString &source, const QString &destination)
{
    if (QFile::rename(source, destination)) {
        return true;
    }
    if (!QFile::copy(source, destination)) {
        return false;
    }
    if (QFile::remove(source)) {
        return true;
    }
    QFile::remove(destination);
    return false;
}

ResourceKind resourceKindForPath(const QFileInfo &info)
{
    if (info.isDir()) {
        return ResourceKind::Folder;
    }
    return info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0
        ? ResourceKind::Pdf
        : ResourceKind::File;
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
    const QString baseName = info.isDir()
        ? info.fileName().trimmed()
        : info.completeBaseName().trimmed();
    if (!baseName.isEmpty()) {
        return baseName;
    }
    const QString fileName = info.fileName().trimmed();
    if (!fileName.isEmpty()) {
        return fileName;
    }
    return normalizedInboxFilePath(filePath);
}

QString managedInboxFilePath(const QString &filePath, const QString &managedLibraryDirectory)
{
    const QString source = normalizedInboxFilePath(filePath);
    const QString managedRoot = normalizedInboxFilePath(managedLibraryDirectory);
    const QFileInfo sourceInfo(source);
    if (source.isEmpty() || managedRoot.isEmpty() || !sourceInfo.isFile()) {
        return {};
    }
    const QByteArray digest =
        QCryptographicHash::hash(pathKeyForId(source).toUtf8(), QCryptographicHash::Sha256).toHex();
    return normalizedInboxFilePath(
        QDir(managedRoot).filePath(QStringLiteral("files/%1/%2")
                                      .arg(QString::fromLatin1(digest.left(16)), sourceInfo.fileName())));
}

QString inboxFileSaveRequestError(const InboxFileSaveRequest &request)
{
    const QString normalizedPath = normalizedInboxFilePath(request.filePath);
    if (normalizedPath.isEmpty()) {
        return QStringLiteral("Inbox file path is required");
    }

    const QFileInfo info(normalizedPath);
    if (!info.exists()) {
        return QStringLiteral("Inbox item does not exist");
    }
    if (!info.isFile() && !info.isDir()) {
        return QStringLiteral("Inbox supports files and folders only");
    }
    if (info.isDir() && request.mode != InboxFileArchiveMode::Link) {
        return QStringLiteral("Folders can only remain in their original location");
    }
    if (request.registerAsLibraryRoot && !info.isDir()) {
        return QStringLiteral("Only a folder can be registered as a library root");
    }
    if (request.mode != InboxFileArchiveMode::Link
        && managedInboxFilePath(normalizedPath, request.managedLibraryDirectory).isEmpty()) {
        return QStringLiteral("A managed Pinloom library directory is required");
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

    const QString sourcePath = normalizedInboxFilePath(request.filePath);
    QString storedPath = sourcePath;
    bool copiedFile = false;
    bool movedFile = false;
    if (request.mode != InboxFileArchiveMode::Link) {
        storedPath = managedInboxFilePath(sourcePath, request.managedLibraryDirectory);
        if (!samePath(sourcePath, storedPath)) {
            const QFileInfo destinationInfo(storedPath);
            if (!QDir().mkpath(destinationInfo.absolutePath())) {
                result.status = QStringLiteral("Unable to create the managed Pinloom library directory");
                return result;
            }
            if (destinationInfo.exists()) {
                if (request.mode == InboxFileArchiveMode::Move) {
                    result.status = QStringLiteral("The managed Pinloom file already exists");
                    return result;
                }
            } else if (request.mode == InboxFileArchiveMode::Copy) {
                if (!QFile::copy(sourcePath, storedPath)) {
                    result.status = QStringLiteral("Unable to copy the file into the Pinloom library");
                    return result;
                }
                copiedFile = true;
            } else if (!moveFileWithFallback(sourcePath, storedPath)) {
                result.status = QStringLiteral("Unable to move the file into the Pinloom library");
                return result;
            } else {
                movedFile = true;
            }
        }
    }

    const QString resourceId = inboxResourceIdForPath(storedPath);
    if (resourceId.isEmpty()) {
        result.status = QStringLiteral("Inbox file path is required");
        return result;
    }

    const std::optional<Resource> existing = repository.findResource(resourceId);
    Resource resource = existing.value_or(Resource{});
    resource.id = resourceId;
    resource.kind = resourceKindForPath(QFileInfo(storedPath));
    resource.location = storedPath;

    const QString requestedName = request.name;
    if (!requestedName.trimmed().isEmpty()) {
        resource.title = requestedName;
    } else if (resource.title.trimmed().isEmpty()) {
        resource.title = defaultInboxFileName(storedPath);
    }

    appendRequestedIdentityValues(resource.aliases, request.aliases);

    const QStringList tags = cleanedValues(request.tags, true);
    for (const QString &tag : tags) {
        appendUniqueValue(resource.tags, tag);
    }

    resource.content.clear();
    if (!request.registerAsLibraryRoot) {
        resource.explicitlyRetained = true;
    }
    resource.updatedAt = QDateTime::currentDateTimeUtc();

    if (!repository.upsertResource(resource)) {
        if (copiedFile) {
            QFile::remove(storedPath);
        } else if (movedFile) {
            moveFileWithFallback(storedPath, sourcePath);
        }
        result.status = repository.lastError().trimmed().isEmpty()
            ? QStringLiteral("Unable to save Inbox file")
            : repository.lastError();
        return result;
    }
    if (request.pinned) {
        repository.setResourcePinned(resource.id, true);
    }

    if (request.registerAsLibraryRoot) {
        LibraryRoot root = makeLibraryRootForPath(storedPath);
        root.enabled = true;
        const QStringList requestedIgnores = cleanedValues(request.ignoredDirectoryNames);
        for (const LibraryRoot &existingRoot : repository.libraryRoots()) {
            if (!samePath(existingRoot.path, storedPath)) {
                continue;
            }
            root.syncRoot = existingRoot.syncRoot;
            if (request.name.trimmed().isEmpty()
                && !existingRoot.displayName.trimmed().isEmpty()) {
                root.displayName = existingRoot.displayName;
            }
            for (const QString &ignored : existingRoot.ignoredDirectoryNames) {
                appendUniqueValue(root.ignoredDirectoryNames, ignored);
            }
            break;
        }
        if (!request.name.trimmed().isEmpty()) {
            root.displayName = request.name;
        }
        for (const QString &ignored : requestedIgnores) {
            appendUniqueValue(root.ignoredDirectoryNames, ignored);
        }
        if (root.syncRoot) {
            appendUniqueValue(root.ignoredDirectoryNames, QStringLiteral("_PinloomData"));
        }
        if (!repository.upsertLibraryRoot(root)) {
            LibraryBatchMutation rollback;
            if (existing.has_value()) {
                rollback.upserts = {existing.value()};
            } else {
                rollback.permanentlyDeleteResourceIds = {resource.id};
            }
            repository.applyBatch(rollback);
            if (copiedFile) {
                QFile::remove(storedPath);
            } else if (movedFile) {
                moveFileWithFallback(storedPath, sourcePath);
            }
            result.status = QStringLiteral("Unable to register the library root");
            return result;
        }
    }

    result.ok = true;
    result.resourceId = resource.id;
    result.filePath = resource.location;
    result.displayName = resource.title;
    result.saveStatus = existing.has_value() ? InboxFileSaveStatus::Updated : InboxFileSaveStatus::Created;
    result.status = existing.has_value()
        ? QStringLiteral("Updated Inbox item \"%1\"").arg(resource.title)
        : (request.registerAsLibraryRoot
               ? QStringLiteral("Registered library root \"%1\"").arg(resource.title)
               : QStringLiteral("Saved Inbox item \"%1\"").arg(resource.title));
    return result;
}

} // namespace Pinloom
