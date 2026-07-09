#pragma once

#include <QString>

namespace Pinloom {

struct SumatraPdfDdeRequestResult {
    QString text;
    QString error;

    bool success() const;
};

struct SumatraPdfDdeFileState {
    QString path;
    int page = -1;
    int pageCount = -1;
    double zoom = -1.0;
    QString view;
    QString version;
    QString rawText;
    QString error;

    bool success() const;
};

struct SumatraPdfDdeMousePosition {
    int page = -1;
    double x = 0.0;
    double y = 0.0;
    double yPdf = 0.0;
    bool hasYPdf = false;
    QString rawText;
    QString error;

    bool success() const;
};

SumatraPdfDdeRequestResult requestSumatraPdfDdeCommand(
    const QString &command,
    int timeoutMilliseconds = 3000);
SumatraPdfDdeFileState parseSumatraPdfDdeFileState(const QString &text);
SumatraPdfDdeMousePosition parseSumatraPdfDdeMousePosition(const QString &text);
SumatraPdfDdeFileState requestSumatraPdfDdeFileState(int timeoutMilliseconds = 3000);
SumatraPdfDdeMousePosition requestSumatraPdfDdeMousePosition(int timeoutMilliseconds = 3000);

} // namespace Pinloom
