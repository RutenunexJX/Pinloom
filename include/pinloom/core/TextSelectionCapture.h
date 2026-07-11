#pragma once

#include "pinloom/core/SumatraPdfForegroundCapture.h"

#include <QString>
#include <functional>

namespace Pinloom {

enum class TextSelectionState {
    TextSelected,
    CaretOnly,
    Unknown
};

enum class ContextualClipIntent {
    ArchiveSelection,
    OpenInsertionPicker
};

struct ForegroundTextTarget {
    quintptr windowHandle = 0;
    quintptr focusHandle = 0;
    quint32 processId = 0;
    quint32 threadId = 0;

    bool isValid() const;
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

class TextSelectionCaptureService {
public:
    using CaptureProvider = std::function<TextSelectionCaptureResult()>;

    explicit TextSelectionCaptureService(CaptureProvider provider = captureForegroundTextSelection);

    TextSelectionCaptureResult capture() const;

private:
    CaptureProvider provider_;
};

QString textSelectionStateText(TextSelectionState state);
ContextualClipIntent contextualClipIntent(const TextSelectionCaptureResult &selection);
bool restoreForegroundTextTarget(const ForegroundTextTarget &target, QString *error = nullptr);

} // namespace Pinloom
