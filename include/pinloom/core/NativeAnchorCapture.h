#pragma once

#include "pinloom/core/AnchorCaptureDraft.h"

#include <QString>
#include <functional>

namespace Pinloom {

enum class NativeAnchorApplication {
    Unknown,
    Word,
    Visio,
    Excel
};

struct NativeCaptureScriptResult {
    QString standardOutput;
    QString standardError;
    QString error;

    bool success() const;
};

struct NativeAnchorCaptureResult {
    AnchorCaptureDraft draft;
    QString error;

    bool success() const;
};

using NativeCaptureScriptRunner =
    std::function<NativeCaptureScriptResult(const QString &script,
                                            int timeoutMilliseconds)>;

NativeAnchorApplication nativeAnchorApplicationForProcess(
    const QString &processName);
NativeCaptureScriptResult runNativeCapturePowerShell(
    const QString &script,
    int timeoutMilliseconds);

class NativeAnchorCaptureAdapter {
public:
    explicit NativeAnchorCaptureAdapter(
        NativeCaptureScriptRunner runner = runNativeCapturePowerShell);

    NativeAnchorCaptureResult capture(NativeAnchorApplication application) const;
    NativeAnchorCaptureResult captureForProcess(const QString &processName) const;
    NativeAnchorCaptureResult finalize(const AnchorCaptureDraft &draft) const;

private:
    NativeCaptureScriptRunner runner_;
};

} // namespace Pinloom
