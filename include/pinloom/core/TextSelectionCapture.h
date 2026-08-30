#pragma once

#include "pinloom/core/ForegroundApplicationContext.h"

#include <QPoint>
#include <QDateTime>
#include <QString>
#include <functional>

namespace Pinloom {

enum class TextSelectionState {
    TextSelected,
    CaretOnly,
    Unknown
};

struct ForegroundTextTarget {
    quintptr windowHandle = 0;
    quintptr focusHandle = 0;
    quint32 processId = 0;
    quint32 threadId = 0;
    QPoint insertionPoint;
    bool hasInsertionPoint = false;
    QDateTime capturedAt;

    bool isValid() const;
    bool isExpired(qint64 maximumAgeSeconds = 10 * 60,
                   const QDateTime &now = {}) const;
};

struct TextSelectionCaptureResult {
    TextSelectionState state = TextSelectionState::Unknown;
    QString text;
    ForegroundAppWindowContext context;
    ForegroundTextTarget target;
    QString source;
    QString diagnostics;

    bool hasSelectedText() const;
};

TextSelectionCaptureResult captureForegroundTextSelection();
ForegroundTextTarget captureForegroundTextTarget();
TextSelectionCaptureResult captureTextSelectionFromTarget(const ForegroundAppWindowContext &context,
                                                           const ForegroundTextTarget &target);

class TextSelectionCaptureService {
public:
    using CaptureProvider = std::function<TextSelectionCaptureResult()>;

    explicit TextSelectionCaptureService(CaptureProvider provider = captureForegroundTextSelection);

    TextSelectionCaptureResult capture() const;

private:
    CaptureProvider provider_;
};

QString textSelectionStateText(TextSelectionState state);
bool restoreForegroundTextTarget(const ForegroundTextTarget &target, QString *error = nullptr);

} // namespace Pinloom
