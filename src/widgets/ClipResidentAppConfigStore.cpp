#include "pinloom/widgets/ClipResidentAppConfigStore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeySequence>
#include <QStringList>

namespace Pinloom {

namespace {

QString requiredPathError()
{
    return QStringLiteral("Clip resident app config path is required");
}

ClipResidentAppConfigLoadResult failedLoadResult(const QString &error)
{
    ClipResidentAppConfigLoadResult result;
    result.error = error;
    return result;
}

QString repositoryKindName(ClipResidentRepositoryKind kind)
{
    switch (kind) {
    case ClipResidentRepositoryKind::InMemory:
        return QStringLiteral("inMemory");
    case ClipResidentRepositoryKind::SQLite:
        return QStringLiteral("sqlite");
    }

    return {};
}

bool parseRepositoryKind(const QString &value, ClipResidentRepositoryKind *kind, QString *error)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("inmemory") || normalized == QStringLiteral("in-memory")) {
        *kind = ClipResidentRepositoryKind::InMemory;
        return true;
    }
    if (normalized == QStringLiteral("sqlite")) {
        *kind = ClipResidentRepositoryKind::SQLite;
        return true;
    }

    if (error) {
        *error = QStringLiteral("Unknown clip repository kind: %1").arg(value);
    }
    return false;
}

QString hotkeyKeyName(Qt::Key key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString(QChar(QLatin1Char(static_cast<char>('A' + static_cast<int>(key - Qt::Key_A)))));
    }

    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QChar(QLatin1Char(static_cast<char>('0' + static_cast<int>(key - Qt::Key_0)))));
    }

    switch (key) {
    case Qt::Key_Space:
        return QStringLiteral("Space");
    case Qt::Key_Return:
        return QStringLiteral("Return");
    case Qt::Key_Enter:
        return QStringLiteral("Enter");
    case Qt::Key_Escape:
        return QStringLiteral("Escape");
    case Qt::Key_Tab:
        return QStringLiteral("Tab");
    default:
        break;
    }

    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return QStringLiteral("F%1").arg(static_cast<int>(key - Qt::Key_F1) + 1);
    }

    return QKeySequence(static_cast<int>(key)).toString(QKeySequence::PortableText);
}

bool parseHotkeyKey(const QString &value, Qt::Key *key, QString *error)
{
    const QString trimmed = value.trimmed();
    const QString upper = trimmed.toUpper();
    if (trimmed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Hotkey key is required");
        }
        return false;
    }

    if (upper.size() == 1) {
        const QChar ch = upper.at(0);
        if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) {
            *key = static_cast<Qt::Key>(Qt::Key_A + ch.toLatin1() - 'A');
            return true;
        }
        if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) {
            *key = static_cast<Qt::Key>(Qt::Key_0 + ch.toLatin1() - '0');
            return true;
        }
    }

    if (upper == QStringLiteral("SPACE")) {
        *key = Qt::Key_Space;
        return true;
    }
    if (upper == QStringLiteral("RETURN")) {
        *key = Qt::Key_Return;
        return true;
    }
    if (upper == QStringLiteral("ENTER")) {
        *key = Qt::Key_Enter;
        return true;
    }
    if (upper == QStringLiteral("ESCAPE") || upper == QStringLiteral("ESC")) {
        *key = Qt::Key_Escape;
        return true;
    }
    if (upper == QStringLiteral("TAB")) {
        *key = Qt::Key_Tab;
        return true;
    }

    if (upper.startsWith(QLatin1Char('F'))) {
        bool ok = false;
        const int functionKey = upper.mid(1).toInt(&ok);
        if (ok && functionKey >= 1 && functionKey <= 24) {
            *key = static_cast<Qt::Key>(Qt::Key_F1 + functionKey - 1);
            return true;
        }
    }

    if (error) {
        *error = QStringLiteral("Unsupported hotkey key: %1").arg(value);
    }
    return false;
}

QJsonArray modifiersToJson(Qt::KeyboardModifiers modifiers)
{
    QJsonArray array;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        array.append(QStringLiteral("control"));
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        array.append(QStringLiteral("alt"));
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        array.append(QStringLiteral("shift"));
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        array.append(QStringLiteral("meta"));
    }
    return array;
}

bool parseModifiers(const QJsonValue &value, Qt::KeyboardModifiers *modifiers, QString *error)
{
    if (!value.isArray()) {
        if (error) {
            *error = QStringLiteral("hotkey.modifiers must be an array");
        }
        return false;
    }

    Qt::KeyboardModifiers parsed;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isString()) {
            if (error) {
                *error = QStringLiteral("hotkey.modifiers entries must be strings");
            }
            return false;
        }

        const QString modifier = entry.toString().trimmed().toLower();
        if (modifier == QStringLiteral("control") || modifier == QStringLiteral("ctrl")) {
            parsed |= Qt::ControlModifier;
        } else if (modifier == QStringLiteral("alt")) {
            parsed |= Qt::AltModifier;
        } else if (modifier == QStringLiteral("shift")) {
            parsed |= Qt::ShiftModifier;
        } else if (modifier == QStringLiteral("meta") || modifier == QStringLiteral("win")
                   || modifier == QStringLiteral("windows") || modifier == QStringLiteral("command")) {
            parsed |= Qt::MetaModifier;
        } else {
            if (error) {
                *error = QStringLiteral("Unknown hotkey modifier: %1").arg(entry.toString());
            }
            return false;
        }
    }

    *modifiers = parsed;
    return true;
}

bool readObject(const QJsonObject &parent, const QString &field, QJsonObject *object, QString *error)
{
    const QJsonValue value = parent.value(field);
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isObject()) {
        if (error) {
            *error = QStringLiteral("%1 must be an object").arg(field);
        }
        return false;
    }

    *object = value.toObject();
    return true;
}

bool readString(const QJsonObject &object, const QString &field, QString *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined()) {
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

bool readBool(const QJsonObject &object, const QString &field, bool *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined()) {
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

bool readInt(const QJsonObject &object, const QString &field, int *target, QString *error)
{
    const QJsonValue value = object.value(field);
    if (value.isUndefined()) {
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

QJsonObject configToJson(const ClipResidentAppConfig &config)
{
    QJsonObject repository;
    repository.insert(QStringLiteral("kind"), repositoryKindName(config.repositoryKind));
    repository.insert(QStringLiteral("sqliteDatabasePath"), config.sqliteDatabasePath);
    repository.insert(QStringLiteral("initializeSqlite"), config.initializeSqlite);

    QJsonObject hotkey;
    hotkey.insert(QStringLiteral("key"), hotkeyKeyName(config.hotkeyConfig.key));
    hotkey.insert(QStringLiteral("modifiers"), modifiersToJson(config.hotkeyConfig.modifiers));

    QJsonObject pickerSearchOptions;
    pickerSearchOptions.insert(QStringLiteral("includeSaved"), config.pickerSearchOptions.includeSaved);
    pickerSearchOptions.insert(QStringLiteral("includeTemporary"), config.pickerSearchOptions.includeTemporary);
    pickerSearchOptions.insert(QStringLiteral("emptyQueryReturnsPinnedAndRecent"),
                               config.pickerSearchOptions.emptyQueryReturnsPinnedAndRecent);
    pickerSearchOptions.insert(QStringLiteral("limit"), config.pickerSearchOptions.limit);

    QJsonObject insertionOptions;
    insertionOptions.insert(QStringLiteral("restoreOriginalClipboardOnSuccess"),
                            config.insertionOptions.restoreOriginalClipboardOnSuccess);
    insertionOptions.insert(QStringLiteral("markClipUsedOnSuccess"), config.insertionOptions.markClipUsedOnSuccess);

    QJsonObject behavior;
    behavior.insert(QStringLiteral("closePickerOnActivationSuccess"), config.closePickerOnActivationSuccess);
    behavior.insert(QStringLiteral("showTrayOnStart"), config.showTrayOnStart);
    behavior.insert(QStringLiteral("hideTrayOnStop"), config.hideTrayOnStop);
    behavior.insert(QStringLiteral("hidePickerOnStop"), config.hidePickerOnStop);
    behavior.insert(QStringLiteral("stopOnQuitRequested"), config.stopOnQuitRequested);

    QJsonObject root;
    root.insert(QStringLiteral("repository"), repository);
    root.insert(QStringLiteral("hotkey"), hotkey);
    root.insert(QStringLiteral("pickerSearchOptions"), pickerSearchOptions);
    root.insert(QStringLiteral("insertionOptions"), insertionOptions);
    root.insert(QStringLiteral("behavior"), behavior);
    return root;
}

bool configFromJson(const QJsonObject &object, ClipResidentAppConfig *config, QString *error)
{
    ClipResidentAppConfig parsed;

    QJsonObject repository;
    if (!readObject(object, QStringLiteral("repository"), &repository, error)) {
        return false;
    }
    if (!repository.isEmpty()) {
        QString kindName = repositoryKindName(parsed.repositoryKind);
        if (!readString(repository, QStringLiteral("kind"), &kindName, error)
            || !parseRepositoryKind(kindName, &parsed.repositoryKind, error)
            || !readString(repository, QStringLiteral("sqliteDatabasePath"), &parsed.sqliteDatabasePath, error)
            || !readBool(repository, QStringLiteral("initializeSqlite"), &parsed.initializeSqlite, error)) {
            return false;
        }
    }

    QJsonObject hotkey;
    if (!readObject(object, QStringLiteral("hotkey"), &hotkey, error)) {
        return false;
    }
    if (!hotkey.isEmpty()) {
        QString keyName = hotkeyKeyName(parsed.hotkeyConfig.key);
        if (!readString(hotkey, QStringLiteral("key"), &keyName, error)
            || !parseHotkeyKey(keyName, &parsed.hotkeyConfig.key, error)) {
            return false;
        }
        if (hotkey.contains(QStringLiteral("modifiers"))
            && !parseModifiers(hotkey.value(QStringLiteral("modifiers")), &parsed.hotkeyConfig.modifiers, error)) {
            return false;
        }
    }

    QJsonObject pickerSearchOptions;
    if (!readObject(object, QStringLiteral("pickerSearchOptions"), &pickerSearchOptions, error)) {
        return false;
    }
    if (!pickerSearchOptions.isEmpty()
        && (!readBool(pickerSearchOptions,
                      QStringLiteral("includeSaved"),
                      &parsed.pickerSearchOptions.includeSaved,
                      error)
            || !readBool(pickerSearchOptions,
                         QStringLiteral("includeTemporary"),
                         &parsed.pickerSearchOptions.includeTemporary,
                         error)
            || !readBool(pickerSearchOptions,
                         QStringLiteral("emptyQueryReturnsPinnedAndRecent"),
                         &parsed.pickerSearchOptions.emptyQueryReturnsPinnedAndRecent,
                         error)
            || !readInt(pickerSearchOptions, QStringLiteral("limit"), &parsed.pickerSearchOptions.limit, error))) {
        return false;
    }

    QJsonObject insertionOptions;
    if (!readObject(object, QStringLiteral("insertionOptions"), &insertionOptions, error)) {
        return false;
    }
    if (!insertionOptions.isEmpty()
        && (!readBool(insertionOptions,
                      QStringLiteral("restoreOriginalClipboardOnSuccess"),
                      &parsed.insertionOptions.restoreOriginalClipboardOnSuccess,
                      error)
            || !readBool(insertionOptions,
                         QStringLiteral("markClipUsedOnSuccess"),
                         &parsed.insertionOptions.markClipUsedOnSuccess,
                         error))) {
        return false;
    }

    QJsonObject behavior;
    if (!readObject(object, QStringLiteral("behavior"), &behavior, error)) {
        return false;
    }
    if (!behavior.isEmpty()
        && (!readBool(behavior,
                      QStringLiteral("closePickerOnActivationSuccess"),
                      &parsed.closePickerOnActivationSuccess,
                      error)
            || !readBool(behavior, QStringLiteral("showTrayOnStart"), &parsed.showTrayOnStart, error)
            || !readBool(behavior, QStringLiteral("hideTrayOnStop"), &parsed.hideTrayOnStop, error)
            || !readBool(behavior, QStringLiteral("hidePickerOnStop"), &parsed.hidePickerOnStop, error)
            || !readBool(behavior, QStringLiteral("stopOnQuitRequested"), &parsed.stopOnQuitRequested, error))) {
        return false;
    }

    const QString validationError = validateClipResidentAppConfig(parsed);
    if (!validationError.isEmpty()) {
        if (error) {
            *error = validationError;
        }
        return false;
    }

    *config = parsed;
    return true;
}

void setError(QString *target, const QString &error)
{
    if (target) {
        *target = error;
    }
}

} // namespace

bool ClipResidentAppConfigLoadResult::succeeded() const
{
    return error.isEmpty();
}

ClipResidentAppConfigLoadResult ClipResidentAppConfigStore::load(const QString &filePath) const
{
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        return failedLoadResult(requiredPathError());
    }

    const QFileInfo fileInfo(trimmedPath);
    if (!fileInfo.exists()) {
        ClipResidentAppConfigLoadResult result;
        result.config = ClipResidentAppConfig{};
        result.loadedFromFile = false;
        return result;
    }

    QFile file(trimmedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return failedLoadResult(QStringLiteral("Unable to open clip resident app config file: %1").arg(file.errorString()));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return failedLoadResult(QStringLiteral("Invalid clip resident app config JSON: %1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return failedLoadResult(QStringLiteral("Clip resident app config JSON must be an object"));
    }

    ClipResidentAppConfig parsed;
    QString error;
    if (!configFromJson(document.object(), &parsed, &error)) {
        return failedLoadResult(error);
    }

    ClipResidentAppConfigLoadResult result;
    result.config = parsed;
    result.loadedFromFile = true;
    return result;
}

bool ClipResidentAppConfigStore::save(const QString &filePath, const ClipResidentAppConfig &config, QString *error) const
{
    setError(error, {});

    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        setError(error, requiredPathError());
        return false;
    }

    const QString validationError = validateClipResidentAppConfig(config);
    if (!validationError.isEmpty()) {
        setError(error, validationError);
        return false;
    }

    QFile file(trimmedPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        setError(error,
                 QStringLiteral("Unable to write clip resident app config file: %1").arg(file.errorString()));
        return false;
    }

    const QJsonDocument document(configToJson(config));
    const QByteArray content = document.toJson(QJsonDocument::Indented);
    if (file.write(content) != static_cast<qint64>(content.size())) {
        setError(error,
                 QStringLiteral("Unable to write clip resident app config file: %1").arg(file.errorString()));
        return false;
    }

    return true;
}

bool configureClipResidentAppFromConfigFile(ClipResidentApp &app,
                                            const QString &filePath,
                                            const ClipResidentAppConfigStore &store,
                                            QString *error)
{
    setError(error, {});

    const ClipResidentAppConfigLoadResult result = store.load(filePath);
    if (!result.succeeded()) {
        setError(error, result.error);
        return false;
    }

    if (!app.configure(result.config)) {
        setError(error, app.lastError());
        return false;
    }

    return true;
}

} // namespace Pinloom
