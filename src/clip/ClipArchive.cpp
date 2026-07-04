#include "pinloom/clip/ClipArchive.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <utility>

namespace Pinloom {

namespace {

constexpr int kSavedClipArchiveVersion = 1;

ClipArchiveResult failedResult(const QString &error)
{
    ClipArchiveResult result;
    result.error = error;
    return result;
}

QString requiredPathError()
{
    return QStringLiteral("Saved clip archive path is required");
}

QString clipKindToJson(ClipKind kind)
{
    switch (kind) {
    case ClipKind::Text:
        return QStringLiteral("text");
    }
    return QStringLiteral("text");
}

QString clipStateToJson(ClipState state)
{
    switch (state) {
    case ClipState::Temporary:
        return QStringLiteral("temporary");
    case ClipState::Saved:
        return QStringLiteral("saved");
    }
    return QStringLiteral("temporary");
}

QJsonValue dateTimeToJson(const QDateTime &dateTime)
{
    if (!dateTime.isValid()) {
        return QJsonValue();
    }
    return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

bool parseDateTime(const QString &value, QDateTime *dateTime, QString *error, const QString &field)
{
    const QDateTime parsed = QDateTime::fromString(value, Qt::ISODate).toUTC();
    if (!parsed.isValid()) {
        if (error) {
            *error = QStringLiteral("%1 must be an ISO datetime").arg(field);
        }
        return false;
    }

    *dateTime = parsed;
    return true;
}

QJsonArray stringListToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

bool readRequiredString(const QJsonObject &object, const QString &field, QString *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (!value.isString()) {
        if (error) {
            *error = QStringLiteral("%1 must be a string").arg(field);
        }
        return false;
    }

    *target = value.toString();
    return true;
}

bool readOptionalString(const QJsonObject &object, const QString &field, QString *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isString()) {
        if (error) {
            *error = QStringLiteral("%1 must be a string").arg(field);
        }
        return false;
    }

    *target = value.toString();
    return true;
}

bool readOptionalBool(const QJsonObject &object, const QString &field, bool *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isBool()) {
        if (error) {
            *error = QStringLiteral("%1 must be a bool").arg(field);
        }
        return false;
    }

    *target = value.toBool();
    return true;
}

bool readOptionalInt(const QJsonObject &object, const QString &field, int *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isDouble()) {
        if (error) {
            *error = QStringLiteral("%1 must be an integer").arg(field);
        }
        return false;
    }

    const double number = value.toDouble();
    const int integer = static_cast<int>(number);
    if (number != static_cast<double>(integer)) {
        if (error) {
            *error = QStringLiteral("%1 must be an integer").arg(field);
        }
        return false;
    }

    *target = integer;
    return true;
}

bool readStringArray(const QJsonObject &object, const QString &field, QStringList *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isArray()) {
        if (error) {
            *error = QStringLiteral("%1 must be an array").arg(field);
        }
        return false;
    }

    QStringList values;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isString()) {
            if (error) {
                *error = QStringLiteral("%1 entries must be strings").arg(field);
            }
            return false;
        }
        values.append(entry.toString());
    }

    *target = values;
    return true;
}

bool readOptionalDateTime(const QJsonObject &object, const QString &field, QDateTime *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isString()) {
        if (error) {
            *error = QStringLiteral("%1 must be an ISO datetime string").arg(field);
        }
        return false;
    }

    return parseDateTime(value.toString(), target, error, field);
}

QJsonObject clipToJson(const Clip &clip)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), clip.id);
    object.insert(QStringLiteral("kind"), clipKindToJson(clip.kind));
    object.insert(QStringLiteral("state"), clipStateToJson(clip.state));
    object.insert(QStringLiteral("text"), clip.text);
    object.insert(QStringLiteral("preview"), clip.preview);
    object.insert(QStringLiteral("contentHash"), clip.contentHash);
    object.insert(QStringLiteral("savedName"), clip.name);
    object.insert(QStringLiteral("aliases"), stringListToJson(clip.aliases));
    object.insert(QStringLiteral("tags"), stringListToJson(clip.tags));
    object.insert(QStringLiteral("pinned"), clip.pinned);
    object.insert(QStringLiteral("createdAt"), dateTimeToJson(clip.createdAt));
    object.insert(QStringLiteral("updatedAt"), dateTimeToJson(clip.updatedAt));
    object.insert(QStringLiteral("usedAt"), dateTimeToJson(clip.usedAt));
    object.insert(QStringLiteral("expiresAt"), dateTimeToJson(clip.expiresAt));
    object.insert(QStringLiteral("sourceApp"), clip.sourceApp);
    object.insert(QStringLiteral("sizeBytes"), static_cast<int>(clip.sizeBytes));
    return object;
}

bool clipFromJson(const QJsonObject &object, Clip *clip, QString *error)
{
    Clip parsed;
    QString kind;
    QString state;
    if (!readRequiredString(object, QStringLiteral("id"), &parsed.id, error)
        || !readRequiredString(object, QStringLiteral("kind"), &kind, error)
        || !readRequiredString(object, QStringLiteral("state"), &state, error)
        || !readRequiredString(object, QStringLiteral("text"), &parsed.text, error)) {
        return false;
    }

    if (kind.trimmed().toLower() != QStringLiteral("text")) {
        if (error) {
            *error = QStringLiteral("Only text clips are supported");
        }
        return false;
    }
    if (state.trimmed().toLower() != QStringLiteral("saved")) {
        if (error) {
            *error = QStringLiteral("Saved clip archive can only import saved clips");
        }
        return false;
    }

    parsed.kind = ClipKind::Text;
    parsed.state = ClipState::Saved;

    if (!readOptionalString(object, QStringLiteral("preview"), &parsed.preview, error)
        || !readOptionalString(object, QStringLiteral("contentHash"), &parsed.contentHash, error)
        || !readOptionalString(object, QStringLiteral("savedName"), &parsed.name, error)
        || !readStringArray(object, QStringLiteral("aliases"), &parsed.aliases, error)
        || !readStringArray(object, QStringLiteral("tags"), &parsed.tags, error)
        || !readOptionalBool(object, QStringLiteral("pinned"), &parsed.pinned, error)
        || !readOptionalDateTime(object, QStringLiteral("createdAt"), &parsed.createdAt, error)
        || !readOptionalDateTime(object, QStringLiteral("updatedAt"), &parsed.updatedAt, error)
        || !readOptionalDateTime(object, QStringLiteral("usedAt"), &parsed.usedAt, error)
        || !readOptionalDateTime(object, QStringLiteral("expiresAt"), &parsed.expiresAt, error)
        || !readOptionalString(object, QStringLiteral("sourceApp"), &parsed.sourceApp, error)) {
        return false;
    }

    int sizeBytes = 0;
    if (!readOptionalInt(object, QStringLiteral("sizeBytes"), &sizeBytes, error)) {
        return false;
    }
    parsed.sizeBytes = sizeBytes;

    *clip = parsed;
    return true;
}

bool readArchiveVersion(const QJsonObject &root, int *version, QString *error)
{
    const QJsonValue value = root.value(QStringLiteral("version"));
    if (!value.isDouble()) {
        if (error) {
            *error = QStringLiteral("version must be an integer");
        }
        return false;
    }

    const double number = value.toDouble();
    const int integer = static_cast<int>(number);
    if (number != static_cast<double>(integer)) {
        if (error) {
            *error = QStringLiteral("version must be an integer");
        }
        return false;
    }

    *version = integer;
    return true;
}

} // namespace

bool ClipArchiveResult::succeeded() const
{
    return success && error.isEmpty();
}

ClipArchive::ClipArchive(InMemoryClipRepository &repository)
    : ClipArchive([&repository]() {
                      return repository.savedClips();
                  },
                  [&repository](const QString &id) {
                      return repository.findClip(id);
                  },
                  [&repository](const Clip &clip, QString *error) {
                      if (repository.importSavedClip(clip)) {
                          return true;
                      }
                      if (error) {
                          *error = QStringLiteral("Unable to import saved clip");
                      }
                      return false;
                  })
{
}

ClipArchive::ClipArchive(SqliteClipRepository &repository)
    : ClipArchive([&repository]() {
                      return repository.savedClips();
                  },
                  [&repository](const QString &id) {
                      return repository.findClip(id);
                  },
                  [&repository](const Clip &clip, QString *error) {
                      if (repository.importSavedClip(clip)) {
                          return true;
                      }
                      if (error) {
                          *error = repository.lastError();
                      }
                      return false;
                  })
{
}

ClipArchive::ClipArchive(ListSavedClipsCallback listSavedClips,
                         FindClipCallback findClip,
                         ImportSavedClipCallback importSavedClip)
    : listSavedClips_(std::move(listSavedClips))
    , findClip_(std::move(findClip))
    , importSavedClip_(std::move(importSavedClip))
{
}

ClipArchiveResult ClipArchive::exportSavedClips(const QString &filePath) const
{
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        return failedResult(requiredPathError());
    }
    if (!listSavedClips_) {
        return failedResult(QStringLiteral("Saved clip archive export is not configured"));
    }

    QJsonArray clips;
    for (const Clip &clip : listSavedClips_()) {
        if (clip.kind == ClipKind::Text && clip.state == ClipState::Saved) {
            clips.append(clipToJson(clip));
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), schemaName());
    root.insert(QStringLiteral("version"), schemaVersion());
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("temporaryHistoryExported"), false);
    root.insert(QStringLiteral("clips"), clips);

    QFile file(trimmedPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return failedResult(QStringLiteral("Unable to write saved clip archive: %1").arg(file.errorString()));
    }

    const QByteArray content = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(content) != static_cast<qint64>(content.size())) {
        return failedResult(QStringLiteral("Unable to write saved clip archive: %1").arg(file.errorString()));
    }

    ClipArchiveResult result;
    result.success = true;
    result.exported = clips.size();
    return result;
}

ClipArchiveResult ClipArchive::importSavedClips(const QString &filePath) const
{
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        return failedResult(requiredPathError());
    }
    if (!findClip_ || !importSavedClip_) {
        return failedResult(QStringLiteral("Saved clip archive import is not configured"));
    }

    QFile file(trimmedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return failedResult(QStringLiteral("Unable to open saved clip archive: %1").arg(file.errorString()));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return failedResult(QStringLiteral("Invalid saved clip archive JSON: %1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return failedResult(QStringLiteral("Saved clip archive JSON must be an object"));
    }

    const QJsonObject root = document.object();
    const QJsonValue schemaValue = root.value(QStringLiteral("schema"));
    if (!schemaValue.isString()) {
        return failedResult(QStringLiteral("schema must be a string"));
    }

    int version = 0;
    QString error;
    if (!readArchiveVersion(root, &version, &error)) {
        return failedResult(error);
    }

    if (schemaValue.toString() != schemaName() || version != schemaVersion()) {
        return failedResult(QStringLiteral("Unsupported saved clip archive schema/version"));
    }

    const QJsonValue clipsValue = root.value(QStringLiteral("clips"));
    if (!clipsValue.isArray()) {
        return failedResult(QStringLiteral("clips must be an array"));
    }

    ClipArchiveResult result;
    result.success = true;
    const QJsonArray clips = clipsValue.toArray();
    for (int index = 0; index < clips.size(); ++index) {
        const QJsonValue clipValue = clips.at(index);
        if (!clipValue.isObject()) {
            return failedResult(QStringLiteral("clips[%1] must be an object").arg(index));
        }

        Clip clip;
        if (!clipFromJson(clipValue.toObject(), &clip, &error)) {
            return failedResult(QStringLiteral("clips[%1]: %2").arg(index).arg(error));
        }

        if (findClip_(clip.id).has_value()) {
            ++result.skippedConflictingIds;
            continue;
        }

        if (!importSavedClip_(clip, &error)) {
            return failedResult(QStringLiteral("clips[%1]: %2").arg(index).arg(error));
        }
        ++result.imported;
    }

    return result;
}

QString ClipArchive::schemaName()
{
    return QStringLiteral("pinloom.clip.savedClips");
}

int ClipArchive::schemaVersion()
{
    return kSavedClipArchiveVersion;
}

} // namespace Pinloom
