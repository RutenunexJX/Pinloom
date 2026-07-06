#pragma once

#include <optional>

#include <QString>
#include <QStringList>

class QSettings;

namespace Pinloom {

QString pinloomPdfXChangeOpenMessageType();
QString normalizedPdfXChangeOpenFilePath(const QString &filePath);
QStringList pdfXChangeDocumentTitleMappingKeysForFile(const QString &filePath);
QString pinloomPdfXChangeOpenMessageForFile(const QString &filePath);
std::optional<QString> pdfXChangeOpenFileFromPinloomMessage(const QString &message);
bool rememberPdfXChangeOpenedFile(QSettings &settings, const QString &filePath, QString *error = nullptr);
bool rememberPdfXChangeDocumentTitlePath(QSettings &settings,
                                         const QString &documentTitle,
                                         const QString &filePath,
                                         QString *error = nullptr);
std::optional<QString> lookupRememberedPdfXChangeDocumentPath(QSettings &settings,
                                                              const QString &documentTitle,
                                                              const QString &normalizedTitleKey);

} // namespace Pinloom
