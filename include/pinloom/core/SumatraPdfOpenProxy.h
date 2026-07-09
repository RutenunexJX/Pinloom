#pragma once

#include <optional>

#include <QString>
#include <QStringList>

class QSettings;

namespace Pinloom {

QString pinloomSumatraPdfOpenMessageType();
QString normalizedSumatraPdfOpenFilePath(const QString &filePath);
QStringList sumatraPdfDocumentTitleMappingKeysForFile(const QString &filePath);
QString pinloomSumatraPdfOpenMessageForFile(const QString &filePath);
std::optional<QString> sumatraPdfOpenFileFromPinloomMessage(const QString &message);
bool rememberSumatraPdfOpenedFile(QSettings &settings, const QString &filePath, QString *error = nullptr);
bool rememberSumatraPdfDocumentTitlePath(QSettings &settings,
                                         const QString &documentTitle,
                                         const QString &filePath,
                                         QString *error = nullptr);
std::optional<QString> lookupRememberedSumatraPdfDocumentPath(QSettings &settings,
                                                              const QString &documentTitle,
                                                              const QString &normalizedTitleKey);

} // namespace Pinloom
