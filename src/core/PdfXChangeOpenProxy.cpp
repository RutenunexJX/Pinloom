#include "pinloom/core/PdfXChangeOpenProxy.h"

#include "pinloom/core/PdfXChangeForegroundCapture.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>

namespace Pinloom {

namespace {

constexpr auto PdfXChangeDocumentTitlePathsGroup = "PdfXChangeDocumentTitlePaths";
constexpr auto PdfXChangeOpenMessageType = "pdfxchange.opened";

QString cleanLocalFilePath(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    if (value.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(value);
        if (url.isLocalFile()) {
            value = url.toLocalFile();
        }
    }

    QFileInfo info(QDir::fromNativeSeparators(value));
    QString path = info.isAbsolute() ? info.filePath() : info.absoluteFilePath();
    path = QDir::cleanPath(path);
    return path;
}

void appendUniqueKey(QStringList &keys, const QString &key)
{
    const QString trimmed = key.trimmed();
    if (!trimmed.isEmpty() && !keys.contains(trimmed, Qt::CaseInsensitive)) {
        keys.append(trimmed);
    }
}

void appendTitleKey(QStringList &keys, const QString &value)
{
    appendUniqueKey(keys, normalizedPdfXChangeDocumentTitleKey(value));
}

std::optional<QString> lookupKey(QSettings &settings, const QString &key)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    settings.beginGroup(QString::fromLatin1(PdfXChangeDocumentTitlePathsGroup));
    const bool hasMapping = settings.contains(trimmed);
    const QString mappedPath = settings.value(trimmed).toString();
    settings.endGroup();

    if (!hasMapping) {
        return std::nullopt;
    }

    const QString normalizedPath = normalizedPdfXChangeOpenFilePath(mappedPath);
    if (normalizedPath.isEmpty()) {
        return std::nullopt;
    }
    return normalizedPath;
}

} // namespace

QString pinloomPdfXChangeOpenMessageType()
{
    return QString::fromLatin1(PdfXChangeOpenMessageType);
}

QString normalizedPdfXChangeOpenFilePath(const QString &filePath)
{
    const QString path = cleanLocalFilePath(filePath);
    if (path.isEmpty() || !path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        return {};
    }
    return path;
}

QStringList pdfXChangeDocumentTitleMappingKeysForFile(const QString &filePath)
{
    const QString path = normalizedPdfXChangeOpenFilePath(filePath);
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo info(path);
    QStringList keys;
    appendTitleKey(keys, path);
    appendTitleKey(keys, QDir::toNativeSeparators(path));
    appendTitleKey(keys, info.fileName());
    appendTitleKey(keys, info.completeBaseName());
    return keys;
}

QString pinloomPdfXChangeOpenMessageForFile(const QString &filePath)
{
    const QString path = normalizedPdfXChangeOpenFilePath(filePath);
    if (path.isEmpty()) {
        return {};
    }

    QJsonObject object;
    object.insert(QStringLiteral("type"), pinloomPdfXChangeOpenMessageType());
    object.insert(QStringLiteral("file"), path);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<QString> pdfXChangeOpenFileFromPinloomMessage(const QString &message)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.trimmed().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("type")).toString() != pinloomPdfXChangeOpenMessageType()) {
        return std::nullopt;
    }

    const QString path = normalizedPdfXChangeOpenFilePath(object.value(QStringLiteral("file")).toString());
    if (path.isEmpty()) {
        return std::nullopt;
    }
    return path;
}

bool rememberPdfXChangeOpenedFile(QSettings &settings, const QString &filePath, QString *error)
{
    const QString path = normalizedPdfXChangeOpenFilePath(filePath);
    if (path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("PDF path is missing or not a .pdf file");
        }
        return false;
    }

    const QStringList keys = pdfXChangeDocumentTitleMappingKeysForFile(path);
    if (keys.isEmpty()) {
        if (error) {
            *error = QStringLiteral("PDF title mapping key is empty");
        }
        return false;
    }

    settings.beginGroup(QString::fromLatin1(PdfXChangeDocumentTitlePathsGroup));
    for (const QString &key : keys) {
        settings.setValue(key, path);
    }
    settings.endGroup();
    settings.sync();

    if (error) {
        error->clear();
    }
    return true;
}

bool rememberPdfXChangeDocumentTitlePath(QSettings &settings,
                                         const QString &documentTitle,
                                         const QString &filePath,
                                         QString *error)
{
    const QString path = normalizedPdfXChangeOpenFilePath(filePath);
    if (path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("PDF path is missing or not a .pdf file");
        }
        return false;
    }

    QStringList keys = pdfXChangeDocumentTitleMappingKeysForFile(path);
    appendTitleKey(keys, documentTitle);

    if (keys.isEmpty()) {
        if (error) {
            *error = QStringLiteral("PDF title mapping key is empty");
        }
        return false;
    }

    settings.beginGroup(QString::fromLatin1(PdfXChangeDocumentTitlePathsGroup));
    for (const QString &key : keys) {
        settings.setValue(key, path);
    }
    settings.endGroup();
    settings.sync();

    if (error) {
        error->clear();
    }
    return true;
}

std::optional<QString> lookupRememberedPdfXChangeDocumentPath(QSettings &settings,
                                                              const QString &documentTitle,
                                                              const QString &normalizedTitleKey)
{
    const std::optional<QString> directMatch = lookupKey(settings, normalizedTitleKey);
    if (directMatch.has_value()) {
        return directMatch;
    }

    const QString title = documentTitle.trimmed();
    if (title.isEmpty()) {
        return std::nullopt;
    }

    QStringList keys;
    appendTitleKey(keys, title);

    const QFileInfo info(QDir::fromNativeSeparators(title));
    appendTitleKey(keys, info.fileName());
    appendTitleKey(keys, info.completeBaseName());

    for (const QString &key : keys) {
        const std::optional<QString> match = lookupKey(settings, key);
        if (match.has_value()) {
            return match;
        }
    }
    return std::nullopt;
}

} // namespace Pinloom
