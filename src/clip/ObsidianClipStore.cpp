#include "pinloom/clip/ObsidianClipStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>

namespace Pinloom {

namespace {

constexpr int FrontmatterVersion = 2;

struct ParsedFrontmatter {
    bool managed = false;
    QMap<QString, QString> values;
    QMap<QString, QStringList> lists;
    QString body;
    QString error;
};

QString normalizedVaultPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

QString normalizedArchiveDirectory(const QString &directory)
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(directory.trimmed()));
    if (cleaned.isEmpty() || cleaned == QLatin1String(".")) {
        return QStringLiteral("Pinloom Clips");
    }
    if (QDir::isAbsolutePath(cleaned)
        || cleaned == QLatin1String("..")
        || cleaned.startsWith(QStringLiteral("../"))) {
        return {};
    }
    return cleaned;
}

QString scalarFromYamlValue(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
        const QByteArray json = QByteArrayLiteral("[") + value.toUtf8() + QByteArrayLiteral("]");
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
        if (parseError.error == QJsonParseError::NoError
            && document.isArray()
            && document.array().size() == 1
            && document.array().first().isString()) {
            return document.array().first().toString();
        }
    }
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))) {
        value = value.mid(1, value.size() - 2);
        return value.replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    return value;
}

QStringList listFromYamlValue(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isArray()) {
        QStringList values;
        for (const QJsonValue &item : document.array()) {
            if (item.isString() && !item.toString().trimmed().isEmpty()) {
                values.append(item.toString().trimmed());
            }
        }
        values.removeDuplicates();
        return values;
    }

    QStringList values;
    for (const QString &item : trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString parsed = scalarFromYamlValue(item).trimmed();
        if (!parsed.isEmpty() && !values.contains(parsed, Qt::CaseInsensitive)) {
            values.append(parsed);
        }
    }
    return values;
}

QString yamlScalar(const QString &value);

ParsedFrontmatter parseFrontmatter(const QString &contents)
{
    ParsedFrontmatter parsed;
    const int firstLineEnd = contents.indexOf(QLatin1Char('\n'));
    if (firstLineEnd < 0 || contents.left(firstLineEnd).trimmed() != QLatin1String("---")) {
        return parsed;
    }

    int lineStart = firstLineEnd + 1;
    int closingStart = -1;
    int bodyStart = contents.size();
    while (lineStart <= contents.size()) {
        int lineEnd = contents.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0) {
            lineEnd = contents.size();
        }
        if (contents.mid(lineStart, lineEnd - lineStart).trimmed() == QLatin1String("---")) {
            closingStart = lineStart;
            bodyStart = lineEnd < contents.size() ? lineEnd + 1 : contents.size();
            break;
        }
        if (lineEnd >= contents.size()) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    if (closingStart < 0) {
        parsed.error = QStringLiteral("Unterminated YAML frontmatter");
        return parsed;
    }

    const QString metadata = contents.mid(firstLineEnd + 1, closingStart - firstLineEnd - 1);
    QString currentListKey;
    for (QString line : metadata.split(QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (!currentListKey.isEmpty() && trimmed.startsWith(QLatin1Char('-'))) {
            const QString item = scalarFromYamlValue(trimmed.mid(1)).trimmed();
            if (!item.isEmpty()) {
                parsed.lists[currentListKey].append(item);
            }
            continue;
        }

        currentListKey.clear();
        const int separator = trimmed.indexOf(QLatin1Char(':'));
        if (separator <= 0) {
            continue;
        }
        const QString key = trimmed.left(separator).trimmed().toLower();
        const QString value = trimmed.mid(separator + 1).trimmed();
        if ((key == QLatin1String("aliases") || key == QLatin1String("tags")) && value.isEmpty()) {
            currentListKey = key;
            parsed.lists.insert(key, {});
        } else if (key == QLatin1String("aliases") || key == QLatin1String("tags")) {
            parsed.lists.insert(key, listFromYamlValue(value));
        } else {
            parsed.values.insert(key, scalarFromYamlValue(value));
        }
    }

    const QString type = parsed.values.value(QStringLiteral("pinloom_type"));
    if (type.compare(QStringLiteral("clip"), Qt::CaseInsensitive) != 0) {
        parsed.error.clear();
        return parsed;
    }

    parsed.managed = true;
    parsed.body = contents.mid(bodyStart);
    return parsed;
}

QString withFrontmatterScalar(QString contents, const QString &key, const QString &value)
{
    const int firstLineEnd = contents.indexOf(QLatin1Char('\n'));
    if (firstLineEnd < 0 || contents.left(firstLineEnd).trimmed() != QLatin1String("---")) {
        return {};
    }
    int closingStart = contents.indexOf(QStringLiteral("\n---"), firstLineEnd);
    if (closingStart < 0) {
        return {};
    }

    const QString prefix = key.trimmed() + QLatin1Char(':');
    int lineStart = firstLineEnd + 1;
    while (lineStart < closingStart) {
        int lineEnd = contents.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0 || lineEnd > closingStart) {
            lineEnd = closingStart;
        }
        const QString line = contents.mid(lineStart, lineEnd - lineStart).trimmed();
        if (line.startsWith(prefix, Qt::CaseInsensitive)) {
            contents.replace(lineStart,
                             lineEnd - lineStart,
                             QStringLiteral("%1: %2").arg(key.trimmed(), yamlScalar(value)));
            return contents;
        }
        lineStart = lineEnd + 1;
    }

    contents.insert(closingStart + 1,
                    QStringLiteral("%1: %2\n").arg(key.trimmed(), yamlScalar(value)));
    return contents;
}

QString yamlScalar(const QString &value)
{
    const QJsonArray array{value};
    const QByteArray encoded = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

QString yamlStringList(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            array.append(trimmed);
        }
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString previewForText(const QString &text)
{
    const QString simplified = text.simplified();
    constexpr qsizetype maxLength = 80;
    return simplified.size() <= maxLength
        ? simplified
        : simplified.left(maxLength - 3) + QStringLiteral("...");
}

QString contentHashForText(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString persistentStateText(ClipState state)
{
    return state == ClipState::Deleted ? QStringLiteral("deleted") : QStringLiteral("saved");
}

QString actionTypeText(ClipActionType actionType)
{
    return actionType == ClipActionType::OpenWebUrl
        ? QStringLiteral("open_web_url")
        : QStringLiteral("insert_text");
}

QDateTime dateTimeValue(const QString &value)
{
    const QDateTime parsed = QDateTime::fromString(value.trimmed(), Qt::ISODate);
    return parsed.isValid() ? parsed.toUTC() : QDateTime{};
}

QString sanitizedFileStem(QString name)
{
    name = name.simplified();
    QString sanitized;
    sanitized.reserve(name.size());
    const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (const QChar character : name) {
        if (character.unicode() < 0x20 || invalid.contains(character)) {
            sanitized.append(QLatin1Char('_'));
        } else {
            sanitized.append(character);
        }
    }
    while (sanitized.endsWith(QLatin1Char('.')) || sanitized.endsWith(QLatin1Char(' '))) {
        sanitized.chop(1);
    }
    if (sanitized.isEmpty()) {
        sanitized = QStringLiteral("clip");
    }
    if (sanitized.size() > 72) {
        sanitized = sanitized.left(72).trimmed();
    }

    static const QSet<QString> reservedNames = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    if (reservedNames.contains(sanitized.toUpper())) {
        sanitized.prepend(QLatin1Char('_'));
    }
    return sanitized;
}

QString safeIdForFileName(QString id)
{
    for (QChar &character : id) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') && character != QLatin1Char('_')) {
            character = QLatin1Char('_');
        }
    }
    return id.left(64);
}

QString availableClipFilePath(const QString &directory,
                              const QString &displayName,
                              const QString &currentPath = {})
{
    const QString stem = sanitizedFileStem(displayName);
    const QString normalizedCurrent = QDir::cleanPath(currentPath);
    for (int suffix = 1; ; ++suffix) {
        const QString fileName = suffix == 1
            ? QStringLiteral("%1.md").arg(stem)
            : QStringLiteral("%1 (%2).md").arg(stem).arg(suffix);
        const QString candidate = QDir(directory).filePath(fileName);
        if ((!normalizedCurrent.isEmpty()
             && QDir::cleanPath(candidate).compare(normalizedCurrent, Qt::CaseInsensitive) == 0)
            || !QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

bool clipsEquivalent(const Clip &left, const Clip &right)
{
    return left.id == right.id
        && left.state == right.state
        && left.text == right.text
        && left.name == right.name
        && left.aliases == right.aliases
        && left.tags == right.tags
        && left.actionType == right.actionType
        && left.storageBackend == right.storageBackend
        && left.pinned == right.pinned
        && left.sourceApp == right.sourceApp
        && left.sourceWindowTitle == right.sourceWindowTitle
        && left.sourceUri == right.sourceUri
        && left.createdAt.toUTC() == right.createdAt.toUTC()
        && left.updatedAt.toUTC() == right.updatedAt.toUTC();
}

QString repositoryError(const InMemoryClipRepository &)
{
    return QStringLiteral("Unable to update in-memory Clip repository");
}

QString repositoryError(const SqliteClipRepository &repository)
{
    return repository.lastError().trimmed().isEmpty()
        ? QStringLiteral("Unable to update Clip database")
        : repository.lastError().trimmed();
}

template <typename Repository>
ObsidianClipSyncResult synchronizeRepository(const ObsidianClipStore &store, Repository &repository)
{
    ObsidianClipSyncResult result;
    const ObsidianClipScanResult scan = store.scan();
    if (!scan.succeeded()) {
        result.fatalError = scan.fatalError;
        result.errors = scan.errors;
        return result;
    }

    result.discovered = scan.documents.size();
    result.skipped = scan.skippedUnmanagedFiles;
    result.errors = scan.errors;
    QSet<QString> discoveredIds;

    for (const ObsidianClipDocument &document : scan.documents) {
        discoveredIds.insert(document.clip.id);
        const std::optional<Clip> existing = repository.findClip(document.clip.id);
        if (document.forgotten) {
            if (existing.has_value()) {
                if (existing->state == ClipState::Saved
                    && !repository.softDeleteSavedClip(existing->id)) {
                    result.errors.append(QStringLiteral("%1: %2")
                                             .arg(document.relativePath, repositoryError(repository)));
                    ++result.skipped;
                    continue;
                }
                if (!repository.permanentlyDeleteClip(existing->id)) {
                    result.errors.append(QStringLiteral("%1: %2")
                                             .arg(document.relativePath, repositoryError(repository)));
                    ++result.skipped;
                    continue;
                }
                ++result.deleted;
            } else {
                ++result.unchanged;
            }
            continue;
        }

        Clip synchronizedClip = document.clip;
        if (existing.has_value()) {
            synchronizedClip.usedAt = existing->usedAt;
            if (existing->state == ClipState::Deleted && !document.stateExplicit) {
                synchronizedClip.state = ClipState::Deleted;
            }
            if (clipsEquivalent(existing.value(), synchronizedClip)) {
                ++result.unchanged;
                continue;
            }
        }

        if (!repository.upsertPersistentClip(synchronizedClip)) {
            result.errors.append(QStringLiteral("%1: %2")
                                     .arg(document.relativePath, repositoryError(repository)));
            ++result.skipped;
            continue;
        }
        if (existing.has_value()) {
            ++result.updated;
        } else {
            ++result.imported;
        }
    }

    if (scan.errors.isEmpty()) {
        const QList<Clip> storedClips = repository.clips();
        for (const Clip &stored : storedClips) {
            if (stored.state != ClipState::Saved
                || !isObsidianBackedClip(stored)
                || discoveredIds.contains(stored.id)) {
                continue;
            }
            if (repository.softDeleteSavedClip(stored.id)) {
                ++result.deleted;
            } else {
                result.errors.append(QStringLiteral("%1: %2")
                                         .arg(stored.id, repositoryError(repository)));
            }
        }
    }

    return result;
}

} // namespace

bool ObsidianClipStoreConfig::isEnabled() const
{
    return !vaultPath.trimmed().isEmpty();
}

bool ObsidianClipWriteResult::succeeded() const
{
    return success;
}

bool ObsidianClipScanResult::succeeded() const
{
    return fatalError.trimmed().isEmpty();
}

bool ObsidianClipSyncResult::succeeded() const
{
    return fatalError.trimmed().isEmpty();
}

ObsidianClipStore::ObsidianClipStore(ObsidianClipStoreConfig config)
    : config_(std::move(config))
{
}

void ObsidianClipStore::setConfig(const ObsidianClipStoreConfig &config)
{
    config_ = config;
}

ObsidianClipStoreConfig ObsidianClipStore::config() const
{
    return config_;
}

QString ObsidianClipStore::vaultPath() const
{
    return normalizedVaultPath(config_.vaultPath);
}

QString ObsidianClipStore::archivePath() const
{
    const QString root = vaultPath();
    const QString relative = normalizedArchiveDirectory(config_.archiveDirectory);
    if (root.isEmpty() || relative.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QDir(root).filePath(relative));
}

bool ObsidianClipStore::ensureArchiveDirectory(QString *error) const
{
    if (!config_.isEnabled()) {
        if (error) {
            *error = QStringLiteral("Obsidian Vault path is not configured");
        }
        return false;
    }

    const QString root = vaultPath();
    const QFileInfo vaultInfo(root);
    if (!vaultInfo.exists() || !vaultInfo.isDir()) {
        if (error) {
            *error = QStringLiteral("Obsidian Vault does not exist: %1").arg(root);
        }
        return false;
    }

    const QString target = archivePath();
    if (target.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Obsidian archive directory must stay inside the Vault");
        }
        return false;
    }
    if (!QDir().mkpath(target)) {
        if (error) {
            *error = QStringLiteral("Unable to create Obsidian archive directory: %1").arg(target);
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

ObsidianClipWriteResult ObsidianClipStore::writeClip(const Clip &clip) const
{
    ObsidianClipWriteResult result;
    QString error;
    if (!ensureArchiveDirectory(&error)) {
        result.error = error;
        return result;
    }
    if (clip.id.trimmed().isEmpty()) {
        result.error = QStringLiteral("Clip id is required");
        return result;
    }
    if (clip.text.trimmed().isEmpty()) {
        result.error = QStringLiteral("Clip text is required");
        return result;
    }

    const QString displayName = clip.name.trimmed().isEmpty() ? previewForText(clip.text) : clip.name.trimmed();
    QString filePath;
    const std::optional<ObsidianClipDocument> existing = findClip(clip.id, &error);
    if (existing.has_value()) {
        filePath = existing->filePath;
        const QString legacySuffix = QStringLiteral("--%1.md").arg(safeIdForFileName(clip.id));
        if (QFileInfo(filePath).fileName().endsWith(legacySuffix, Qt::CaseInsensitive)) {
            const QString migratedPath = availableClipFilePath(archivePath(), displayName, filePath);
            if (QDir::cleanPath(migratedPath).compare(QDir::cleanPath(filePath), Qt::CaseInsensitive) != 0) {
                if (!QFile::rename(filePath, migratedPath)) {
                    result.error = QStringLiteral("Unable to rename legacy Obsidian Clip note: %1")
                                       .arg(QFileInfo(filePath).fileName());
                    return result;
                }
                filePath = migratedPath;
            }
        }
    } else {
        filePath = availableClipFilePath(archivePath(), displayName);
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime createdAt = clip.createdAt.isValid() ? clip.createdAt.toUTC() : now;
    const QDateTime updatedAt = clip.updatedAt.isValid() ? clip.updatedAt.toUTC() : now;
    QString contents;
    contents += QStringLiteral("---\n");
    contents += QStringLiteral("pinloom_id: %1\n").arg(yamlScalar(clip.id.trimmed()));
    contents += QStringLiteral("pinloom_type: %1\n").arg(yamlScalar(QStringLiteral("clip")));
    contents += QStringLiteral("pinloom_version: %1\n").arg(FrontmatterVersion);
    contents += QStringLiteral("pinloom_state: %1\n").arg(yamlScalar(persistentStateText(clip.state)));
    contents += QStringLiteral("action_type: %1\n").arg(yamlScalar(actionTypeText(clip.actionType)));
    contents += QStringLiteral("name: %1\n").arg(yamlScalar(displayName));
    contents += QStringLiteral("aliases: %1\n").arg(yamlStringList(clip.aliases));
    contents += QStringLiteral("tags: %1\n").arg(yamlStringList(clip.tags));
    contents += QStringLiteral("pinned: %1\n").arg(clip.pinned ? QStringLiteral("true") : QStringLiteral("false"));
    contents += QStringLiteral("created: %1\n").arg(yamlScalar(createdAt.toString(Qt::ISODateWithMs)));
    contents += QStringLiteral("updated: %1\n").arg(yamlScalar(updatedAt.toString(Qt::ISODateWithMs)));
    contents += QStringLiteral("source_app: %1\n").arg(yamlScalar(clip.sourceApp.trimmed()));
    contents += QStringLiteral("source_window_title: %1\n")
                    .arg(yamlScalar(clip.sourceWindowTitle.trimmed()));
    contents += QStringLiteral("source_uri: %1\n").arg(yamlScalar(clip.sourceUri.trimmed()));
    contents += QStringLiteral("---\n");
    contents += clip.text;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.write(contents.toUtf8()) < 0 || !file.commit()) {
        result.error = file.errorString().trimmed().isEmpty()
            ? QStringLiteral("Unable to commit Obsidian Clip note")
            : file.errorString();
        return result;
    }

    result.success = true;
    result.filePath = QDir::cleanPath(filePath);
    result.relativePath = QDir(vaultPath()).relativeFilePath(result.filePath);
    return result;
}

std::optional<ObsidianClipDocument> ObsidianClipStore::readClipFile(const QString &filePath,
                                                                    QString *error) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return std::nullopt;
    }

    const ParsedFrontmatter parsed = parseFrontmatter(QString::fromUtf8(file.readAll()));
    if (!parsed.managed) {
        if (error) {
            *error = parsed.error;
        }
        return std::nullopt;
    }
    if (!parsed.error.isEmpty()) {
        if (error) {
            *error = parsed.error;
        }
        return std::nullopt;
    }

    bool versionOk = false;
    const int version = parsed.values.value(QStringLiteral("pinloom_version")).toInt(&versionOk);
    if (!versionOk || version < 1 || version > FrontmatterVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported Pinloom Clip frontmatter version");
        }
        return std::nullopt;
    }

    const QString id = parsed.values.value(QStringLiteral("pinloom_id")).trimmed();
    if (id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Managed Obsidian Clip is missing pinloom_id");
        }
        return std::nullopt;
    }
    const QString persistentState = parsed.values.value(QStringLiteral("pinloom_state"))
                                        .trimmed()
                                        .toLower();
    const bool forgotten = persistentState == QLatin1String("forgotten");
    if (!forgotten && parsed.body.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Managed Obsidian Clip has an empty body");
        }
        return std::nullopt;
    }

    const QFileInfo fileInfo(filePath);
    Clip clip;
    clip.id = id;
    clip.kind = ClipKind::Text;
    clip.state = persistentState == QLatin1String("deleted")
        ? ClipState::Deleted
        : ClipState::Saved;
    clip.text = parsed.body;
    clip.preview = previewForText(clip.text);
    clip.contentHash = contentHashForText(clip.text);
    clip.name = parsed.values.value(QStringLiteral("name")).trimmed();
    if (clip.name.isEmpty()) {
        clip.name = fileInfo.completeBaseName();
    }
    clip.aliases = parsed.lists.value(QStringLiteral("aliases"));
    clip.tags = parsed.lists.value(QStringLiteral("tags"));
    const QString actionType = parsed.values.value(QStringLiteral("action_type"));
    clip.actionType = actionType.compare(QStringLiteral("open_web_url"), Qt::CaseInsensitive) == 0
            || (actionType.trimmed().isEmpty()
                && clip.tags.contains(QStringLiteral("wb"), Qt::CaseInsensitive))
        ? ClipActionType::OpenWebUrl
        : ClipActionType::InsertText;
    clip.storageBackend = ClipStorageBackend::Obsidian;
    clip.pinned = parsed.values.value(QStringLiteral("pinned")).compare(QStringLiteral("true"),
                                                                        Qt::CaseInsensitive) == 0;
    clip.createdAt = dateTimeValue(parsed.values.value(QStringLiteral("created")));
    clip.updatedAt = dateTimeValue(parsed.values.value(QStringLiteral("updated")));
    const QDateTime fileModifiedAt = fileInfo.lastModified().toUTC();
    if (!clip.createdAt.isValid()) {
        clip.createdAt = fileModifiedAt;
    }
    if (!clip.updatedAt.isValid() || fileModifiedAt > clip.updatedAt) {
        clip.updatedAt = fileModifiedAt;
    }
    clip.sourceApp = parsed.values.value(QStringLiteral("source_app")).trimmed();
    clip.sourceWindowTitle = parsed.values.value(QStringLiteral("source_window_title")).trimmed();
    clip.sourceUri = parsed.values.value(QStringLiteral("source_uri")).trimmed();
    clip.sizeBytes = clip.text.toUtf8().size();

    ObsidianClipDocument document;
    document.clip = clip;
    document.filePath = QDir::cleanPath(fileInfo.absoluteFilePath());
    document.relativePath = QDir(vaultPath()).relativeFilePath(document.filePath);
    document.stateExplicit = parsed.values.contains(QStringLiteral("pinloom_state"));
    document.forgotten = forgotten;
    if (error) {
        error->clear();
    }
    return document;
}

ObsidianClipScanResult ObsidianClipStore::scan() const
{
    ObsidianClipScanResult result;
    if (!config_.isEnabled()) {
        result.fatalError = QStringLiteral("Obsidian Vault path is not configured");
        return result;
    }

    const QFileInfo vaultInfo(vaultPath());
    if (!vaultInfo.exists() || !vaultInfo.isDir()) {
        result.fatalError = QStringLiteral("Obsidian Vault does not exist: %1").arg(vaultPath());
        return result;
    }

    const QString root = archivePath();
    if (root.isEmpty()) {
        result.fatalError = QStringLiteral("Obsidian archive directory must stay inside the Vault");
        return result;
    }
    if (!QFileInfo::exists(root)) {
        return result;
    }

    QStringList files;
    QDirIterator iterator(root,
                          QStringList{QStringLiteral("*.md")},
                          QDir::Files | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        files.append(iterator.next());
    }
    std::sort(files.begin(), files.end(), [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });

    QSet<QString> clipIds;
    for (const QString &filePath : files) {
        QString error;
        const std::optional<ObsidianClipDocument> document = readClipFile(filePath, &error);
        if (!document.has_value()) {
            if (error.trimmed().isEmpty()) {
                ++result.skippedUnmanagedFiles;
            } else {
                result.errors.append(QStringLiteral("%1: %2")
                                         .arg(QDir(vaultPath()).relativeFilePath(filePath), error));
            }
            continue;
        }
        if (clipIds.contains(document->clip.id)) {
            result.errors.append(QStringLiteral("Duplicate pinloom_id %1 in %2")
                                     .arg(document->clip.id, document->relativePath));
            continue;
        }
        clipIds.insert(document->clip.id);
        result.documents.append(document.value());
    }
    return result;
}

std::optional<ObsidianClipDocument> ObsidianClipStore::findClip(const QString &clipId,
                                                                QString *error) const
{
    const QString normalizedId = clipId.trimmed();
    if (normalizedId.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Clip id is required");
        }
        return std::nullopt;
    }

    const ObsidianClipScanResult result = scan();
    if (!result.succeeded()) {
        if (error) {
            *error = result.fatalError;
        }
        return std::nullopt;
    }
    for (const ObsidianClipDocument &document : result.documents) {
        if (document.clip.id == normalizedId && !document.forgotten) {
            if (error) {
                error->clear();
            }
            return document;
        }
    }
    if (error) {
        error->clear();
    }
    return std::nullopt;
}

bool ObsidianClipStore::forgetClip(const QString &clipId, QString *error) const
{
    const QString normalizedId = clipId.trimmed();
    if (normalizedId.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Clip id is required");
        }
        return false;
    }
    const ObsidianClipScanResult result = scan();
    if (!result.succeeded()) {
        if (error) {
            *error = result.fatalError;
        }
        return false;
    }

    for (const ObsidianClipDocument &document : result.documents) {
        if (document.clip.id != normalizedId) {
            continue;
        }
        if (document.forgotten) {
            if (error) {
                error->clear();
            }
            return true;
        }
        QFile source(document.filePath);
        if (!source.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = source.errorString();
            }
            return false;
        }
        QString contents = QString::fromUtf8(source.readAll());
        source.close();
        contents = withFrontmatterScalar(contents,
                                         QStringLiteral("pinloom_state"),
                                         QStringLiteral("forgotten"));
        contents = withFrontmatterScalar(contents,
                                         QStringLiteral("updated"),
                                         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        if (contents.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Unable to update Obsidian Clip frontmatter");
            }
            return false;
        }
        QSaveFile destination(document.filePath);
        if (!destination.open(QIODevice::WriteOnly)
            || destination.write(contents.toUtf8()) < 0
            || !destination.commit()) {
            if (error) {
                *error = destination.errorString().trimmed().isEmpty()
                    ? QStringLiteral("Unable to commit forgotten Clip state")
                    : destination.errorString();
            }
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QUrl ObsidianClipStore::openUrlForClip(const QString &clipId, QString *error) const
{
    const std::optional<ObsidianClipDocument> document = findClip(clipId, error);
    if (!document.has_value()) {
        if (error && error->trimmed().isEmpty()) {
            *error = QStringLiteral("Obsidian Clip note was not found");
        }
        return {};
    }

    QUrl url;
    url.setScheme(QStringLiteral("obsidian"));
    url.setHost(QStringLiteral("open"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), document->filePath);
    url.setQuery(query);
    if (error) {
        error->clear();
    }
    return url;
}

ObsidianClipSyncResult ObsidianClipStore::synchronize(InMemoryClipRepository &repository) const
{
    return synchronizeRepository(*this, repository);
}

ObsidianClipSyncResult ObsidianClipStore::synchronize(SqliteClipRepository &repository) const
{
    return synchronizeRepository(*this, repository);
}

ObsidianClipSyncService::ObsidianClipSyncService(InMemoryClipRepository &repository,
                                                 ObsidianClipStoreConfig config,
                                                 QObject *parent)
    : ObsidianClipSyncService([&repository](const ObsidianClipStore &store) {
          return store.synchronize(repository);
      },
      std::move(config),
      parent)
{
}

ObsidianClipSyncService::ObsidianClipSyncService(SqliteClipRepository &repository,
                                                 ObsidianClipStoreConfig config,
                                                 QObject *parent)
    : ObsidianClipSyncService([&repository](const ObsidianClipStore &store) {
          return store.synchronize(repository);
      },
      std::move(config),
      parent)
{
}

ObsidianClipSyncService::ObsidianClipSyncService(SynchronizeCallback synchronize,
                                                 ObsidianClipStoreConfig config,
                                                 QObject *parent)
    : QObject(parent)
    , store_(std::move(config))
    , synchronize_(std::move(synchronize))
    , watcher_(std::make_unique<QFileSystemWatcher>())
    , debounceTimer_(std::make_unique<QTimer>())
{
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(300);
    connect(debounceTimer_.get(), &QTimer::timeout, this, [this]() {
        if (running_) {
            synchronizeNow();
        }
    });
    connect(watcher_.get(), &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        scheduleSynchronize();
    });
    connect(watcher_.get(), &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        scheduleSynchronize();
    });
}

ObsidianClipSyncService::~ObsidianClipSyncService() = default;

void ObsidianClipSyncService::setConfig(const ObsidianClipStoreConfig &config)
{
    const bool restart = running_;
    if (restart) {
        stop();
    }
    store_.setConfig(config);
    if (restart && config.isEnabled()) {
        start();
    }
}

ObsidianClipStoreConfig ObsidianClipSyncService::config() const
{
    return store_.config();
}

const ObsidianClipStore &ObsidianClipSyncService::store() const
{
    return store_;
}

bool ObsidianClipSyncService::start()
{
    if (running_) {
        return true;
    }
    QString error;
    if (!store_.ensureArchiveDirectory(&error)) {
        setLastError(error);
        return false;
    }

    running_ = true;
    const ObsidianClipSyncResult result = synchronizeNow();
    if (!result.succeeded()) {
        running_ = false;
        return false;
    }
    refreshWatchPaths();
    return true;
}

void ObsidianClipSyncService::stop()
{
    running_ = false;
    debounceTimer_->stop();
    const QStringList paths = watcher_->files() + watcher_->directories();
    if (!paths.isEmpty()) {
        watcher_->removePaths(paths);
    }
}

bool ObsidianClipSyncService::isRunning() const
{
    return running_;
}

ObsidianClipSyncResult ObsidianClipSyncService::synchronizeNow()
{
    ObsidianClipSyncResult result;
    if (!synchronize_) {
        result.fatalError = QStringLiteral("Obsidian synchronization callback is not configured");
        setLastError(result.fatalError);
        return result;
    }

    result = synchronize_(store_);
    if (!result.succeeded()) {
        setLastError(result.fatalError);
    } else if (!result.errors.isEmpty()) {
        setLastError(result.errors.join(QLatin1Char('\n')));
    } else {
        setLastError({});
    }
    if (running_) {
        refreshWatchPaths();
    }
    emit synchronized(result.imported, result.updated, result.deleted);
    return result;
}

QString ObsidianClipSyncService::lastError() const
{
    return lastError_;
}

void ObsidianClipSyncService::scheduleSynchronize()
{
    if (running_) {
        debounceTimer_->start();
    }
}

void ObsidianClipSyncService::refreshWatchPaths()
{
    const QStringList oldPaths = watcher_->files() + watcher_->directories();
    if (!oldPaths.isEmpty()) {
        watcher_->removePaths(oldPaths);
    }

    const QString archiveRoot = store_.archivePath();
    if (!running_ || archiveRoot.isEmpty() || !QFileInfo::exists(archiveRoot)) {
        return;
    }

    QStringList paths{archiveRoot};
    QDirIterator directoryIterator(archiveRoot,
                                   QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                   QDirIterator::Subdirectories);
    while (directoryIterator.hasNext()) {
        paths.append(directoryIterator.next());
    }
    QDirIterator fileIterator(archiveRoot,
                              QStringList{QStringLiteral("*.md")},
                              QDir::Files | QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
    while (fileIterator.hasNext()) {
        paths.append(fileIterator.next());
    }
    watcher_->addPaths(paths);
}

void ObsidianClipSyncService::setLastError(const QString &error)
{
    const QString normalized = error.trimmed();
    if (lastError_ == normalized) {
        return;
    }
    lastError_ = normalized;
    emit errorChanged(lastError_);
}

bool isObsidianBackedClip(const Clip &clip)
{
    return clip.storageBackend == ClipStorageBackend::Obsidian;
}

} // namespace Pinloom
