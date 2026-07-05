#include "pinloom/clip/ClipHotkeyService.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QKeySequence>
#include <QStringList>
#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Pinloom {

namespace {

QString keyText(Qt::Key key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString(QChar(QLatin1Char(static_cast<char>('A' + static_cast<int>(key - Qt::Key_A)))));
    }

    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QChar(QLatin1Char(static_cast<char>('0' + static_cast<int>(key - Qt::Key_0)))));
    }

    return QKeySequence(static_cast<int>(key)).toString(QKeySequence::PortableText);
}

#ifdef Q_OS_WIN

constexpr int ClipHotkeyId = 0x50434c50;

UINT windowsModifiers(Qt::KeyboardModifiers modifiers)
{
    UINT result = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        result |= MOD_CONTROL;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        result |= MOD_ALT;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        result |= MOD_SHIFT;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        result |= MOD_WIN;
    }
#ifdef MOD_NOREPEAT
    result |= MOD_NOREPEAT;
#endif
    return result;
}

UINT windowsVirtualKey(Qt::Key key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UINT>('A' + static_cast<int>(key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UINT>('0' + static_cast<int>(key - Qt::Key_0));
    }

    switch (key) {
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Escape:
        return VK_ESCAPE;
    case Qt::Key_Tab:
        return VK_TAB;
    default:
        break;
    }

    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<UINT>(VK_F1 + static_cast<int>(key - Qt::Key_F1));
    }

    return 0;
}

QString windowsLastErrorText(const QString &action)
{
    return QStringLiteral("%1 (Win32 error %2)").arg(action).arg(GetLastError());
}

class WindowsClipHotkeyBackend final : public ClipHotkeyBackend, public QAbstractNativeEventFilter {
public:
    explicit WindowsClipHotkeyBackend(int hotkeyId, QObject *parent = nullptr)
        : ClipHotkeyBackend(parent)
        , hotkeyId_(hotkeyId)
    {
    }

    ~WindowsClipHotkeyBackend() override
    {
        unregisterHotkey();
    }

    bool registerHotkey(const ClipHotkeyConfig &config, QString *error) override
    {
        unregisterHotkey();

        QCoreApplication *application = QCoreApplication::instance();
        if (!application) {
            setError(error, QStringLiteral("A Qt application instance is required for global hotkey events"));
            return false;
        }

        if (!config.isValid()) {
            setError(error, QStringLiteral("Hotkey key is required"));
            return false;
        }

        const UINT virtualKey = windowsVirtualKey(config.key);
        if (virtualKey == 0) {
            setError(error, QStringLiteral("Hotkey key is not supported by the Windows backend"));
            return false;
        }

        if (!RegisterHotKey(nullptr, hotkeyId_, windowsModifiers(config.modifiers), virtualKey)) {
            setError(error, windowsLastErrorText(QStringLiteral("Unable to register global hotkey")));
            return false;
        }

        application->installNativeEventFilter(this);
        filterInstalled_ = true;
        registered_ = true;
        return true;
    }

    void unregisterHotkey() override
    {
        if (registered_) {
            UnregisterHotKey(nullptr, hotkeyId_);
            registered_ = false;
        }

        if (filterInstalled_) {
            if (QCoreApplication *application = QCoreApplication::instance()) {
                application->removeNativeEventFilter(this);
            }
            filterInstalled_ = false;
        }
    }

    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override
    {
        if (!registered_) {
            return false;
        }

        const MSG *nativeMessage = static_cast<MSG *>(message);
        if (!nativeMessage || nativeMessage->message != WM_HOTKEY ||
            static_cast<int>(nativeMessage->wParam) != hotkeyId_) {
            return false;
        }

        emit hotkeyActivated();
        if (result) {
            *result = 0;
        }
        return true;
    }

private:
    void setError(QString *error, const QString &message) const
    {
        if (error) {
            *error = message;
        }
    }

    int hotkeyId_ = 0;
    bool registered_ = false;
    bool filterInstalled_ = false;
};

#else

class UnavailableClipHotkeyBackend final : public ClipHotkeyBackend {
public:
    explicit UnavailableClipHotkeyBackend(QObject *parent = nullptr)
        : ClipHotkeyBackend(parent)
    {
    }

    bool isAvailable() const override
    {
        return false;
    }

    bool registerHotkey(const ClipHotkeyConfig &, QString *error) override
    {
        if (error) {
            *error = QStringLiteral("Global hotkeys are unavailable on this platform");
        }
        return false;
    }

    void unregisterHotkey() override
    {
    }
};

#endif

} // namespace

bool ClipHotkeyConfig::isValid() const
{
    return key != Qt::Key_unknown;
}

QString ClipHotkeyConfig::displayText() const
{
    QStringList parts;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        parts.append(QStringLiteral("Ctrl"));
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        parts.append(QStringLiteral("Alt"));
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        parts.append(QStringLiteral("Shift"));
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        parts.append(QStringLiteral("Meta"));
    }

    const QString text = keyText(key);
    if (!text.isEmpty()) {
        parts.append(text);
    }
    return parts.join(QLatin1Char('+'));
}

bool ClipHotkeyConfig::operator==(const ClipHotkeyConfig &other) const
{
    return key == other.key && modifiers == other.modifiers;
}

bool ClipHotkeyConfig::operator!=(const ClipHotkeyConfig &other) const
{
    return !(*this == other);
}

ClipHotkeyConfig defaultClipHotkeyConfig()
{
    return {};
}

ClipHotkeyBackend::ClipHotkeyBackend(QObject *parent)
    : QObject(parent)
{
}

bool ClipHotkeyBackend::isAvailable() const
{
    return true;
}

ClipHotkeyService::ClipHotkeyService(QObject *parent)
    : ClipHotkeyService(defaultClipHotkeyConfig(), defaultClipHotkeyBackend(), parent)
{
}

ClipHotkeyService::ClipHotkeyService(ClipHotkeyBackend *backend, QObject *parent)
    : ClipHotkeyService(defaultClipHotkeyConfig(), backend, parent)
{
}

ClipHotkeyService::ClipHotkeyService(ClipHotkeyConfig config, ClipHotkeyBackend *backend, QObject *parent)
    : QObject(parent)
    , config_(config)
    , backend_(backend)
{
}

ClipHotkeyService::~ClipHotkeyService()
{
    stop();
}

void ClipHotkeyService::setConfig(const ClipHotkeyConfig &config)
{
    if (config_ == config) {
        return;
    }

    if (registered_) {
        stop();
    }
    config_ = config;
}

ClipHotkeyConfig ClipHotkeyService::config() const
{
    return config_;
}

QString ClipHotkeyService::displayText() const
{
    return config_.displayText();
}

void ClipHotkeyService::setActivationHandler(ActivationHandler handler)
{
    activationHandler_ = std::move(handler);
}

bool ClipHotkeyService::start()
{
    if (registered_) {
        return true;
    }

    if (!backend_) {
        setLastError(QStringLiteral("Hotkey backend is required"));
        return false;
    }

    if (!backend_->isAvailable()) {
        setLastError(QStringLiteral("Global hotkey backend is unavailable"));
        return false;
    }

    if (!config_.isValid()) {
        setLastError(QStringLiteral("Hotkey key is required"));
        return false;
    }

    QString error;
    if (!backend_->registerHotkey(config_, &error)) {
        setLastError(error.trimmed().isEmpty() ? QStringLiteral("Unable to register global hotkey") : error.trimmed());
        return false;
    }

    backendConnection_ = connect(backend_,
                                 &ClipHotkeyBackend::hotkeyActivated,
                                 this,
                                 &ClipHotkeyService::handleBackendActivated);
    registered_ = true;
    setLastError({});
    emit registeredChanged(registered_);
    return true;
}

void ClipHotkeyService::stop()
{
    if (!registered_) {
        return;
    }

    disconnect(backendConnection_);
    backendConnection_ = {};
    if (backend_) {
        backend_->unregisterHotkey();
    }

    registered_ = false;
    emit registeredChanged(registered_);
}

bool ClipHotkeyService::isRegistered() const
{
    return registered_;
}

QString ClipHotkeyService::lastError() const
{
    return lastError_;
}

void ClipHotkeyService::handleBackendActivated()
{
    if (!registered_) {
        return;
    }

    if (activationHandler_) {
        activationHandler_();
    }
    emit activated();
}

void ClipHotkeyService::setLastError(const QString &error)
{
    if (lastError_ == error) {
        return;
    }

    lastError_ = error;
    emit errorChanged(lastError_);
}

ClipPickerHotkeyController::ClipPickerHotkeyController(ClipHotkeyService &service, QObject *parent)
    : ClipPickerHotkeyController(service, {}, parent)
{
}

ClipPickerHotkeyController::ClipPickerHotkeyController(ClipHotkeyService &service,
                                                       ShowHandler showHandler,
                                                       QObject *parent)
    : QObject(parent)
    , service_(service)
    , showHandler_(std::move(showHandler))
{
    connect(&service_, &ClipHotkeyService::activated, this, &ClipPickerHotkeyController::handleHotkeyActivated);
}

void ClipPickerHotkeyController::setShowHandler(ShowHandler showHandler)
{
    showHandler_ = std::move(showHandler);
}

void ClipPickerHotkeyController::handleHotkeyActivated()
{
    if (showHandler_) {
        showHandler_();
    }
    emit showRequested();
}

ClipHotkeyBackend *defaultClipHotkeyBackend()
{
#ifdef Q_OS_WIN
    static WindowsClipHotkeyBackend backend(ClipHotkeyId);
#else
    static UnavailableClipHotkeyBackend backend;
#endif
    return &backend;
}

std::unique_ptr<ClipHotkeyBackend> createClipHotkeyBackend(int hotkeyId)
{
#ifdef Q_OS_WIN
    return std::make_unique<WindowsClipHotkeyBackend>(hotkeyId);
#else
    Q_UNUSED(hotkeyId);
    return std::make_unique<UnavailableClipHotkeyBackend>();
#endif
}

} // namespace Pinloom
