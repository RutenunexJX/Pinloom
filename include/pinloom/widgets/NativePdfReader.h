#pragma once
#include "pinloom/core/SumatraPdfDdeClient.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

namespace Pinloom {
struct NativePdfReaderResult {
    bool located = false;
    QString error;
};

QString nativePdfReaderProfilePath(const QString &libraryDirectory);
QString validateNativePdfFiles(const QStringList &files);
NativePdfReaderResult openNativePdfFiles(const QStringList &files,
    const QString &libraryDirectory, bool explicitLibrary);

// Reads the configured reader's active document without selecting or opening a tab.
// Readers without the native bridge retain the existing DDE verification path.
SumatraPdfDdeFileState requestConfiguredPdfReaderState(
    const QString &executable, const QString &file, int expectedPage,
    int timeoutMilliseconds);

NativePdfReaderResult openNativePdfReader(
    const QString &executable, const QString &profileDirectory,
    const QString &file, const QJsonObject &locator,
    const std::shared_ptr<std::atomic_bool> &canceled);
}
