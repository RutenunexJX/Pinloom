#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"

#include "pinloom/core/SumatraPdfForegroundCapture.h"

#include <QCursor>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QWindow>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Pinloom {

namespace {

QRect targetClientGeometry(quintptr windowHandle)
{
    if (windowHandle != 0) {
        QWindow *foreignWindow = QWindow::fromWinId(static_cast<WId>(windowHandle));
        if (foreignWindow) {
            const QRect geometry = foreignWindow->geometry();
            delete foreignWindow;
            if (geometry.isValid()) {
                return geometry;
            }
        }
    }

#ifdef Q_OS_WIN
    if (windowHandle != 0) {
        HWND window = reinterpret_cast<HWND>(windowHandle);
        RECT clientRect{};
        POINT topLeft{};
        if (IsWindow(window)
            && GetClientRect(window, &clientRect)
            && ClientToScreen(window, &topLeft)) {
            return QRect(topLeft.x,
                         topLeft.y,
                         clientRect.right - clientRect.left,
                         clientRect.bottom - clientRect.top);
        }
    }
#else
    Q_UNUSED(windowHandle);
#endif

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? screen->geometry() : QRect();
}

void activateTargetWindow(quintptr windowHandle)
{
#ifdef Q_OS_WIN
    if (windowHandle == 0) {
        return;
    }
    HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!IsWindow(window)) {
        return;
    }
    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    }
    BringWindowToTop(window);
    SetForegroundWindow(window);
#else
    Q_UNUSED(windowHandle);
#endif
}

#if 0 // Legacy jump-only overlay path; rectangle jumps use PdfAnchorPresenter.
class SumatraPdfRectHighlightOverlay final : public QWidget {
public:
    explicit SumatraPdfRectHighlightOverlay(const QRect &screenRect)
        : QWidget(nullptr,
                  Qt::Tool
                      | Qt::FramelessWindowHint
                      | Qt::WindowStaysOnTopHint
                      | Qt::WindowTransparentForInput
                      | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        setGeometry(screenRect.adjusted(-5, -5, 5, 5));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF highlight = QRectF(rect()).adjusted(3.0, 3.0, -3.0, -3.0);
        painter.setBrush(QColor(255, 222, 0, 46));
        painter.setPen(QPen(QColor(245, 184, 0), 3.0));
        painter.drawRect(highlight);
    }
};

bool intersectsAvailableScreen(const QRect &rect)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen && screen->geometry().intersects(rect)) {
            return true;
        }
    }
    return false;
}

QString normalizedPdfPath(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

bool samePdfPath(const QString &left, const QString &right)
{
    return !normalizedPdfPath(left).isEmpty()
        && normalizedPdfPath(left) == normalizedPdfPath(right);
}

bool nativeWindowExists(quintptr windowHandle)
{
#ifdef Q_OS_WIN
    return windowHandle != 0
        && IsWindow(reinterpret_cast<HWND>(windowHandle));
#else
    return windowHandle != 0;
#endif
}

QStringList openSumatraPdfFiles(bool *available)
{
    if (available) {
        *available = false;
    }
    const SumatraPdfDdeRequestResult response =
        requestSumatraPdfDdeCommand(QStringLiteral("GetOpenFiles()"), 180);
    if (!response.success()) {
        return {};
    }
    if (available) {
        *available = true;
    }
    QStringList files;
    for (QString path : response.text.split(QRegularExpression(
             QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
        path = path.trimmed();
        if (!path.isEmpty()) {
            files.append(path);
        }
    }
    return files;
}

bool containsPdfPath(const QStringList &paths, const QString &target)
{
    return std::any_of(paths.cbegin(), paths.cend(), [&](const QString &path) {
        return samePdfPath(path, target);
    });
}

QRect screenRectForPdfRegion(const QRectF &pdfRect,
                             double zoom,
                             const QPoint &cursor,
                             const SumatraPdfDdeMousePosition &mouse)
{
    constexpr double LogicalPixelsPerPdfPointAt100Percent = 96.0 / 72.0;
    const double scale = LogicalPixelsPerPdfPointAt100Percent * zoom / 100.0;
    const QPointF pageOrigin(cursor.x() - mouse.x * scale,
                             cursor.y() - mouse.y * scale);
    return QRectF(pageOrigin.x() + pdfRect.left() * scale,
                  pageOrigin.y() + pdfRect.top() * scale,
                  std::max(2.0, pdfRect.width() * scale),
                  std::max(2.0, pdfRect.height() * scale))
        .toAlignedRect();
}

bool materiallyDifferentRect(const QRect &left, const QRect &right)
{
    constexpr int TolerancePixels = 3;
    return std::abs(left.x() - right.x()) > TolerancePixels
        || std::abs(left.y() - right.y()) > TolerancePixels
        || std::abs(left.width() - right.width()) > TolerancePixels
        || std::abs(left.height() - right.height()) > TolerancePixels;
}

SumatraPdfHighlightRefreshState querySumatraPdfHighlightState(
    const ForegroundAppWindowContext &context,
    const QRect &clientGeometry,
    const QPoint &sampledCursor,
    bool needsInitialPlacement,
    bool needsLiveZoom)
{
    SumatraPdfHighlightRefreshState state;
    state.sumatraForeground = true;
    state.foregroundWindowHandle = context.windowHandle;
    state.clientGeometry = clientGeometry;
    state.sampledCursor = sampledCursor;
    state.fileState = requestSumatraPdfDdeFileState(140);
    if (needsLiveZoom
        && (!std::isfinite(state.fileState.zoom)
            || state.fileState.zoom <= 0.0)) {
        const SumatraPdfViewState viewState = captureSumatraPdfViewState(context);
        if (viewState.hasZoom()) {
            state.fileState.zoom = viewState.zoom;
        }
    }

#ifdef Q_OS_WIN
    if (needsInitialPlacement && clientGeometry.isValid()) {
        POINT originalCursor{};
        if (GetCursorPos(&originalCursor)) {
            const int left = clientGeometry.left();
            const int top = clientGeometry.top();
            const int width = clientGeometry.width();
            const int height = clientGeometry.height();
            const QList<QPoint> candidates = {
                clientGeometry.center(),
                QPoint(left + width * 3 / 4, top + height / 2),
                QPoint(left + width / 4, top + height / 2),
                QPoint(left + width * 3 / 4, top + height / 3),
                QPoint(left + width / 4, top + height / 3),
                QPoint(left + width * 3 / 4, top + height * 2 / 3),
                QPoint(left + width / 4, top + height * 2 / 3),
            };
            for (const QPoint &candidate : candidates) {
                if (!SetCursorPos(candidate.x(), candidate.y())) {
                    continue;
                }
                QThread::msleep(45);
                const SumatraPdfDdeMousePosition mouse =
                    requestSumatraPdfDdeMousePosition(140);
                if (mouse.success() && mouse.page == state.fileState.page) {
                    state.sampledCursor = candidate;
                    state.mousePosition = mouse;
                    break;
                }
                state.mousePosition = mouse;
            }
            SetCursorPos(originalCursor.x, originalCursor.y);
        } else {
            state.mousePosition = requestSumatraPdfDdeMousePosition(120);
        }
    } else {
        POINT cursorBefore{};
        POINT cursorAfter{};
        const bool haveCursorBefore = GetCursorPos(&cursorBefore);
        if (haveCursorBefore) {
            state.sampledCursor = QPoint(cursorBefore.x, cursorBefore.y);
        }
        state.mousePosition = requestSumatraPdfDdeMousePosition(120);
        if (haveCursorBefore && GetCursorPos(&cursorAfter)
            && (std::abs(cursorAfter.x - cursorBefore.x) > 2
                || std::abs(cursorAfter.y - cursorBefore.y) > 2)) {
            state.mousePosition.error = QStringLiteral(
                "Cursor moved while sampling SumatraPDF coordinates");
        }
    }
#else
    Q_UNUSED(needsInitialPlacement);
    state.mousePosition = requestSumatraPdfDdeMousePosition(120);
#endif
    return state;
}
#endif

} // namespace

#if 0 // Legacy jump-only overlay state is intentionally excluded from the capture API.
struct SumatraPdfHighlightManager::Entry {
    SumatraPdfPersistentHighlight highlight;
    QPointer<SumatraPdfRectHighlightOverlay> overlay;
    QRect lastClientGeometry;
    QRect lastScreenRect;
    QRect pendingScreenRect;
    double displayedZoom = -1.0;
    int candidateConfirmations = 0;
    int consecutiveStateMisses = 0;
    int presentationCount = 0;
    int consecutiveOpenFileMisses = 0;
};
#endif

bool SumatraPdfRegionCaptureResult::success() const
{
    return !canceled && region.success();
}

SumatraPdfRegionCaptureOverlay::SumatraPdfRegionCaptureOverlay(
    quintptr targetWindowHandle,
    MousePositionProvider mousePositionProvider,
    QWidget *parent,
    int fallbackPage,
    double fallbackZoom)
    : QDialog(parent,
              Qt::Window
                  | Qt::FramelessWindowHint
                  | Qt::WindowStaysOnTopHint)
    , targetWindowHandle_(targetWindowHandle)
    , mousePositionProvider_(std::move(mousePositionProvider))
    , usingDefaultMousePositionProvider_(!mousePositionProvider_)
    , fallbackPage_(std::max(1, fallbackPage))
    , fallbackZoom_(std::isfinite(fallbackZoom) && fallbackZoom > 0.0
                        ? fallbackZoom
                        : -1.0)
{
    if (!mousePositionProvider_) {
        mousePositionProvider_ = []() {
            return requestSumatraPdfDdeMousePosition(300);
        };
    }

    setWindowTitle(QStringLiteral("Pinloom PDF Region Capture"));
    setObjectName(QStringLiteral("sumatraPdfRegionCaptureOverlay"));
    setModal(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setGeometry(targetClientGeometry(targetWindowHandle_));
}

SumatraPdfRegionCaptureResult SumatraPdfRegionCaptureOverlay::captureResult() const
{
    return result_;
}

void SumatraPdfRegionCaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event && event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SumatraPdfRegionCaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }
    if (event->button() == Qt::RightButton) {
        reject();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }

    result_.region = {};
    setAccessibleDescription({});
    setWindowTitle(QStringLiteral("Pinloom PDF Region Capture"));

    dragStart_ = event->position().toPoint();
    dragCurrent_ = dragStart_;
    dragging_ = true;
    update();
}

void SumatraPdfRegionCaptureOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!event || !dragging_) {
        return;
    }
    dragCurrent_ = event->position().toPoint();
    update();
}

void SumatraPdfRegionCaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton || !dragging_) {
        return;
    }

    dragCurrent_ = event->position().toPoint();
    dragging_ = false;
    const QPoint globalStart = mapToGlobal(dragStart_);
    const QPoint globalEnd = mapToGlobal(dragCurrent_);
    const auto positions = sampleRegionPositions(globalStart, globalEnd);
    result_.region = sumatraPdfDdeRegionFromMousePositions(positions.first,
                                                           positions.second);
    if (!result_.region.success()) {
        result_.diagnostics = result_.region.error;
        result_.canceled = false;
        accept();
        return;
    }

    result_.canceled = false;
    accept();
}

void SumatraPdfRegionCaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 18));
    if (!dragging_) {
        return;
    }

    const QRect selection = QRect(dragStart_, dragCurrent_).normalized();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(255, 222, 0, 52));
    painter.setPen(QPen(QColor(245, 184, 0), 2.0));
    painter.drawRect(selection);
}

void SumatraPdfRegionCaptureOverlay::reject()
{
    result_.canceled = true;
    QDialog::reject();
}

QPair<SumatraPdfDdeMousePosition, SumatraPdfDdeMousePosition>
SumatraPdfRegionCaptureOverlay::sampleRegionPositions(const QPoint &globalStart,
                                                      const QPoint &globalEnd)
{
    // SumatraPDF 3.7 GetMousePos() resolves the document below the system
    // cursor. The top-most capture dialog must be removed from hit testing
    // before sampling, otherwise both endpoints can report page 0.
    releaseMouse();
    hide();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (targetWindowHandle_ == 0) {
        return {mousePositionProvider_(), mousePositionProvider_()};
    }

#ifdef Q_OS_WIN
    activateTargetWindow(targetWindowHandle_);
    const QPoint originalCursorPosition = QCursor::pos();
    const SumatraPdfDdeMousePosition start = sampleMousePositionAt(globalStart);
    const SumatraPdfDdeMousePosition end = sampleMousePositionAt(globalEnd);
    QCursor::setPos(originalCursorPosition);
    return {start, end};
#else
    Q_UNUSED(globalStart);
    Q_UNUSED(globalEnd);
    return {mousePositionProvider_(), mousePositionProvider_()};
#endif
}

SumatraPdfDdeMousePosition SumatraPdfRegionCaptureOverlay::sampleMousePositionAt(
    const QPoint &globalPosition)
{
    SumatraPdfDdeMousePosition sampled;
    const int attempts = usingDefaultMousePositionProvider_ ? 4 : 1;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        QCursor::setPos(globalPosition);
        if (usingDefaultMousePositionProvider_) {
            // SetCursorPos is asynchronous with respect to SumatraPDF's mouse
            // tracking. Let the target consume WM_MOUSEMOVE before GetMousePos.
            QThread::msleep(static_cast<unsigned long>(40 + attempt * 20));
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        sampled = mousePositionProvider_();
        if (sampled.success()) {
            return sampled;
        }
    }
    return sampled;
}

SumatraPdfDdeRegion SumatraPdfRegionCaptureOverlay::fallbackRegionForSelection(
    const QPoint &globalStart,
    const QPoint &globalEnd,
    const SumatraPdfDdeMousePosition &start,
    const SumatraPdfDdeMousePosition &end)
{
    SumatraPdfDdeRegion region;
    const QRect selection = QRect(globalStart, globalEnd).normalized();
    const QPoint selectionCenter = selection.center();

    const QPoint originalCursorPosition = QCursor::pos();
    const SumatraPdfDdeMousePosition center = sampleMousePositionAt(selectionCenter);
    QCursor::setPos(originalCursorPosition);

    SumatraPdfDdeMousePosition reference;
    QPoint referenceScreenPosition;
    if (center.success()) {
        reference = center;
        referenceScreenPosition = selectionCenter;
    } else if (start.success()) {
        reference = start;
        referenceScreenPosition = globalStart;
    } else if (end.success()) {
        reference = end;
        referenceScreenPosition = globalEnd;
    }

    const bool hasReferencePage = reference.success();
    int page = hasReferencePage ? reference.page : fallbackPage_;
    double zoom = fallbackZoom_;
    if ((!std::isfinite(zoom) || zoom <= 0.0) && usingDefaultMousePositionProvider_) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            const SumatraPdfDdeFileState state = requestSumatraPdfDdeFileState(600);
            if (state.page > 0 && !hasReferencePage) {
                page = state.page;
            }
            if (std::isfinite(state.zoom) && state.zoom > 0.0) {
                zoom = state.zoom;
                break;
            }
            QThread::msleep(35);
        }
    }
    page = std::max(1, page);

    if (reference.success() && std::isfinite(zoom) && zoom > 0.0) {
        constexpr double LogicalPixelsPerPdfPointAt100Percent = 96.0 / 72.0;
        const double scale = LogicalPixelsPerPdfPointAt100Percent * zoom / 100.0;
        const double screenLeft = std::min(globalStart.x(), globalEnd.x());
        const double screenTop = std::min(globalStart.y(), globalEnd.y());
        const double screenRight = std::max(globalStart.x(), globalEnd.x());
        const double screenBottom = std::max(globalStart.y(), globalEnd.y());
        region.page = page;
        region.rect.left = std::max(0.0,
                                    reference.x
                                        + (screenLeft - referenceScreenPosition.x()) / scale);
        region.rect.top = std::max(0.0,
                                   reference.y
                                       + (screenTop - referenceScreenPosition.y()) / scale);
        region.rect.right = std::max(region.rect.left + 1.0,
                                     reference.x
                                         + (screenRight - referenceScreenPosition.x()) / scale);
        region.rect.bottom = std::max(region.rect.top + 1.0,
                                      reference.y
                                          + (screenBottom - referenceScreenPosition.y()) / scale);
        if (region.success()) {
            return region;
        }
    }

    QRect clientGeometry = geometry();
    if (!clientGeometry.isValid()) {
        clientGeometry = targetClientGeometry(targetWindowHandle_);
    }
    if (!clientGeometry.isValid()) {
        clientGeometry = QRect(selectionCenter.x() - 1,
                               selectionCenter.y() - 1,
                               2,
                               2);
    }

    // The fallback is stored in conservative PDF-point space. It guarantees a
    // durable Anchor and a configuration dialog even if DDE is completely down.
    constexpr double FallbackPageWidthPoints = 612.0;
    constexpr double FallbackPageHeightPoints = 792.0;
    const double width = std::max(1, clientGeometry.width());
    const double height = std::max(1, clientGeometry.height());
    const auto normalizedX = [&](double x) {
        return std::clamp((x - clientGeometry.left()) / width, 0.0, 1.0)
            * FallbackPageWidthPoints;
    };
    const auto normalizedY = [&](double y) {
        return std::clamp((y - clientGeometry.top()) / height, 0.0, 1.0)
            * FallbackPageHeightPoints;
    };

    region.page = page;
    region.rect.left = normalizedX(std::min(globalStart.x(), globalEnd.x()));
    region.rect.top = normalizedY(std::min(globalStart.y(), globalEnd.y()));
    region.rect.right = std::max(region.rect.left + 1.0,
                                 normalizedX(std::max(globalStart.x(), globalEnd.x())));
    region.rect.bottom = std::max(region.rect.top + 1.0,
                                  normalizedY(std::max(globalStart.y(), globalEnd.y())));
    region.rect.right = std::min(FallbackPageWidthPoints, region.rect.right);
    region.rect.bottom = std::min(FallbackPageHeightPoints, region.rect.bottom);
    if (!region.rect.isValid()) {
        region.rect = {0.0, 0.0, 1.0, 1.0};
    }
    return region;
}

void SumatraPdfRegionCaptureOverlay::showCaptureError(const QString &message)
{
    const QString text = message.trimmed().isEmpty()
        ? tr("Select a region inside one PDF page")
        : message.trimmed();
    setAccessibleDescription(text);
    setWindowTitle(QStringLiteral("Pinloom PDF Region Capture | %1").arg(text));
    QToolTip::showText(QCursor::pos(), text, this, rect(), 1800);
}

SumatraPdfRegionCaptureResult captureSumatraPdfRegion(quintptr targetWindowHandle,
                                                      int fallbackPage,
                                                      double fallbackZoom)
{
    SumatraPdfRegionCaptureResult result;
    activateTargetWindow(targetWindowHandle);
    SumatraPdfRegionCaptureOverlay overlay(targetWindowHandle,
                                           {},
                                           nullptr,
                                           fallbackPage,
                                           fallbackZoom);
    if (overlay.geometry().isEmpty()) {
        result.region.error = QStringLiteral("SumatraPDF window geometry is unavailable");
        return result;
    }
    overlay.exec();
    return overlay.captureResult();
}

#if 0 // Legacy jump-only overlay implementation is disabled.
bool SumatraPdfPersistentHighlight::isValid() const
{
    return !key.trimmed().isEmpty()
        && !targetFile.trimmed().isEmpty()
        && pdfRect.isValid()
        && page > 0;
}

SumatraPdfHighlightManager &SumatraPdfHighlightManager::instance()
{
    static SumatraPdfHighlightManager *manager =
        new SumatraPdfHighlightManager(QCoreApplication::instance());
    return *manager;
}

SumatraPdfHighlightManager::SumatraPdfHighlightManager(QObject *parent)
    : QObject(parent)
    , refreshTimer_(new QTimer(this))
    , refreshWatcher_(new QFutureWatcher<SumatraPdfHighlightRefreshState>(this))
{
    refreshTimer_->setInterval(300);
    connect(refreshTimer_, &QTimer::timeout,
            this, &SumatraPdfHighlightManager::refreshNow);
    connect(refreshWatcher_,
            &QFutureWatcher<SumatraPdfHighlightRefreshState>::finished,
            this,
            [this]() {
        const SumatraPdfHighlightRefreshState result = refreshWatcher_->result();
        if (inFlightGeneration_ != refreshGeneration_) {
            return;
        }
        SumatraPdfHighlightRefreshState state = result;
        const ForegroundAppWindowContext foreground =
            currentForegroundAppWindowContext();
        if (!isSumatraPdfForegroundWindow(foreground)
            || foreground.windowHandle != result.foregroundWindowHandle) {
            state.sumatraForeground = false;
            state.foregroundWindowHandle = foreground.windowHandle;
        }
        state.windowExists = [](quintptr windowHandle) {
            return nativeWindowExists(windowHandle);
        };
        refreshWithState(state);
    });
}

SumatraPdfHighlightManager::~SumatraPdfHighlightManager()
{
    clear();
}

bool SumatraPdfHighlightManager::addOrUpdate(
    const SumatraPdfPersistentHighlight &highlight)
{
    if (!highlight.isValid()) {
        return false;
    }
    const QString key = highlight.key.trimmed();
    Entry *entry = entries_.value(key, nullptr);
    if (!entry) {
        entry = new Entry;
        entries_.insert(key, entry);
    } else if (entry->overlay) {
        entry->overlay->close();
    }
    entry->highlight = highlight;
    entry->highlight.key = key;
    entry->overlay = nullptr;
    entry->lastClientGeometry = {};
    entry->lastScreenRect = {};
    entry->pendingScreenRect = {};
    entry->displayedZoom = -1.0;
    entry->candidateConfirmations = 0;
    entry->consecutiveStateMisses = 0;
    entry->presentationCount = 0;
    entry->consecutiveOpenFileMisses = 0;
    ++refreshGeneration_;
    if (!refreshTimer_->isActive()) {
        refreshTimer_->start();
    }
    QTimer::singleShot(0, this, &SumatraPdfHighlightManager::refreshNow);
    return true;
}

bool SumatraPdfHighlightManager::remove(const QString &key)
{
    Entry *entry = entries_.take(key.trimmed());
    if (!entry) {
        return false;
    }
    if (entry->overlay) {
        entry->overlay->close();
    }
    delete entry;
    ++refreshGeneration_;
    if (entries_.isEmpty()) {
        refreshTimer_->stop();
    }
    return true;
}

void SumatraPdfHighlightManager::clear()
{
    const QStringList keys = entries_.keys();
    removeEntries(keys);
}

bool SumatraPdfHighlightManager::contains(const QString &key) const
{
    return entries_.contains(key.trimmed());
}

int SumatraPdfHighlightManager::count() const
{
    return entries_.size();
}

bool SumatraPdfHighlightManager::isOverlayVisible(const QString &key) const
{
    const Entry *entry = entries_.value(key.trimmed(), nullptr);
    return entry && entry->overlay && entry->overlay->isVisible();
}

QRect SumatraPdfHighlightManager::overlayScreenRect(const QString &key) const
{
    const Entry *entry = entries_.value(key.trimmed(), nullptr);
    return entry ? entry->lastScreenRect : QRect{};
}

int SumatraPdfHighlightManager::overlayPresentationCount(
    const QString &key) const
{
    const Entry *entry = entries_.value(key.trimmed(), nullptr);
    return entry ? entry->presentationCount : 0;
}

void SumatraPdfHighlightManager::removeEntries(const QStringList &keys)
{
    for (const QString &key : keys) {
        remove(key);
    }
}

void SumatraPdfHighlightManager::refreshNow()
{
    if (entries_.isEmpty()) {
        refreshTimer_->stop();
        return;
    }

    // A closed SumatraPDF window is authoritative. Remove its overlays before
    // issuing DDE requests so a dead DDE server cannot block Pinloom's UI.
    QStringList closedWindowEntries;
    for (auto iterator = entries_.cbegin(); iterator != entries_.cend(); ++iterator) {
        const Entry *entry = iterator.value();
        if (entry && entry->highlight.targetWindowHandle != 0
            && !nativeWindowExists(entry->highlight.targetWindowHandle)) {
            closedWindowEntries.append(iterator.key());
        }
    }
    removeEntries(closedWindowEntries);
    if (entries_.isEmpty()) {
        return;
    }

    SumatraPdfHighlightRefreshState state;
    state.windowExists = [](quintptr windowHandle) {
        return nativeWindowExists(windowHandle);
    };
    const ForegroundAppWindowContext context =
        currentForegroundAppWindowContext();
    state.sumatraForeground = isSumatraPdfForegroundWindow(context);
    state.foregroundWindowHandle = context.windowHandle;
    if (!state.sumatraForeground) {
        refreshWithState(state);
        return;
    }

    if (refreshWatcher_->isRunning()) {
        return;
    }

    state.clientGeometry = targetClientGeometry(context.windowHandle);
    state.sampledCursor = QCursor::pos();
    const bool needsInitialPlacement = std::any_of(
        entries_.cbegin(), entries_.cend(), [](const Entry *entry) {
            return entry && !entry->lastScreenRect.isValid();
        });
    const bool needsLiveZoom = std::any_of(
        entries_.cbegin(), entries_.cend(), [](const Entry *entry) {
            return entry
                && (!std::isfinite(entry->highlight.zoom)
                    || entry->highlight.zoom <= 0.0);
        });
    inFlightGeneration_ = refreshGeneration_;
    refreshWatcher_->setFuture(QtConcurrent::run(
        [context, clientGeometry = state.clientGeometry,
         sampledCursor = state.sampledCursor,
         needsInitialPlacement, needsLiveZoom]() {
            return querySumatraPdfHighlightState(
                context,
                clientGeometry,
                sampledCursor,
                needsInitialPlacement,
                needsLiveZoom);
        }));
}

void SumatraPdfHighlightManager::refreshWithState(
    const SumatraPdfHighlightRefreshState &state)
{
    if (entries_.isEmpty()) return;
    QStringList removals;
    for (auto iterator = entries_.begin(); iterator != entries_.end(); ++iterator) {
        Entry *entry = iterator.value();
        if (!entry) {
            removals.append(iterator.key());
            continue;
        }

        const bool targetFileOpen = state.openFilesAvailable
            && containsPdfPath(state.openFiles, entry->highlight.targetFile);
        if (state.openFilesAvailable) {
            if (targetFileOpen) {
                entry->consecutiveOpenFileMisses = 0;
            } else {
                ++entry->consecutiveOpenFileMisses;
                if (entry->consecutiveOpenFileMisses >= 3) {
                    removals.append(iterator.key());
                    continue;
                }
            }
        }
        if (entry->highlight.targetWindowHandle != 0
            && state.windowExists
            && !state.windowExists(entry->highlight.targetWindowHandle)) {
            removals.append(iterator.key());
            continue;
        }

        const bool matchingForegroundWindow = state.sumatraForeground
            && (entry->highlight.targetWindowHandle == 0
                || entry->highlight.targetWindowHandle
                    == state.foregroundWindowHandle);
        if (!matchingForegroundWindow) {
            if (entry->overlay) {
                entry->overlay->hide();
            }
            continue;
        }

        const bool matchingDocument = state.fileState.success()
            && samePdfPath(state.fileState.path, entry->highlight.targetFile);
        if (!matchingDocument) {
            entry->pendingScreenRect = {};
            entry->candidateConfirmations = 0;
            const bool transientStateFailure = !state.fileState.success();
            if (entry->highlight.targetWindowHandle != 0
                && entry->highlight.targetWindowHandle
                    == state.foregroundWindowHandle) {
                ++entry->consecutiveStateMisses;
                if (entry->overlay
                    && (!transientStateFailure
                        || entry->consecutiveStateMisses >= 2)) {
                    entry->overlay->hide();
                }
                if (entry->consecutiveStateMisses >= 3) {
                    removals.append(iterator.key());
                }
            } else if (entry->overlay) {
                entry->overlay->hide();
            }
            continue;
        }
        entry->consecutiveStateMisses = 0;
        if (state.fileState.page != entry->highlight.page) {
            removals.append(iterator.key());
            continue;
        }

        if (entry->highlight.targetWindowHandle == 0) {
            entry->highlight.targetWindowHandle =
                state.foregroundWindowHandle;
        }
        const QRect clientGeometry = state.clientGeometry;
        if (!clientGeometry.isValid()) {
            if (entry->overlay) entry->overlay->hide();
            continue;
        }
        if (entry->lastClientGeometry.isValid()
            && entry->lastClientGeometry.size() != clientGeometry.size()) {
            removals.append(iterator.key());
            continue;
        }
        if (entry->lastScreenRect.isValid()
            && entry->lastClientGeometry.isValid()
            && clientGeometry.topLeft() != entry->lastClientGeometry.topLeft()) {
            const QPoint windowDelta =
                clientGeometry.topLeft() - entry->lastClientGeometry.topLeft();
            entry->lastScreenRect.translate(windowDelta);
            if (entry->pendingScreenRect.isValid()) {
                entry->pendingScreenRect.translate(windowDelta);
            }
            if (entry->overlay) {
                entry->overlay->setGeometry(
                    entry->lastScreenRect.adjusted(-5, -5, 5, 5));
            }
        }
        entry->lastClientGeometry = clientGeometry;

        const SumatraPdfDdeMousePosition &mouse = state.mousePosition;
        const double zoom = std::isfinite(state.fileState.zoom)
                && state.fileState.zoom > 0.0
            ? state.fileState.zoom
            : entry->highlight.zoom;
        if (!std::isfinite(zoom) || zoom <= 0.0) {
            if (entry->overlay) entry->overlay->hide();
            continue;
        }
        if (std::isfinite(entry->displayedZoom)
            && entry->displayedZoom > 0.0
            && std::abs(entry->displayedZoom - zoom) > 0.25) {
            removals.append(iterator.key());
            continue;
        }
        if (mouse.success() && mouse.page == entry->highlight.page) {
            entry->highlight.zoom = zoom;
            const QRect candidate = screenRectForPdfRegion(
                entry->highlight.pdfRect,
                zoom,
                state.sampledCursor,
                mouse);
            if (candidate.isValid() && intersectsAvailableScreen(candidate)) {
                if (entry->lastScreenRect.isValid()
                    && materiallyDifferentRect(entry->lastScreenRect, candidate)) {
                    // Scrolling changes the PDF-to-screen mapping. A stale
                    // rectangle is more misleading than no rectangle, so the
                    // highlight ends instead of following or drifting.
                    removals.append(iterator.key());
                    continue;
                }
                if (!entry->lastScreenRect.isValid()) {
                    if (entry->pendingScreenRect.isValid()
                        && !materiallyDifferentRect(entry->pendingScreenRect,
                                                    candidate)) {
                        ++entry->candidateConfirmations;
                    } else {
                        entry->pendingScreenRect = candidate;
                        entry->candidateConfirmations = 1;
                    }
                    if (entry->candidateConfirmations >= 2) {
                        entry->lastScreenRect = entry->pendingScreenRect;
                        entry->pendingScreenRect = {};
                        entry->candidateConfirmations = 0;
                        entry->displayedZoom = zoom;
                    }
                }
            } else if (!entry->lastScreenRect.isValid()) {
                entry->pendingScreenRect = {};
                entry->candidateConfirmations = 0;
                continue;
            }
        } else if (!entry->lastScreenRect.isValid()) {
            entry->pendingScreenRect = {};
            entry->candidateConfirmations = 0;
        }
        if (!entry->lastScreenRect.isValid()) {
            if (entry->overlay) {
                entry->overlay->hide();
            }
            continue;
        }

        if (!entry->overlay) {
            entry->overlay = new SumatraPdfRectHighlightOverlay(
                entry->lastScreenRect);
        } else {
            entry->overlay->setGeometry(
                entry->lastScreenRect.adjusted(-5, -5, 5, 5));
        }
        if (!entry->overlay->isVisible()) {
            entry->overlay->show();
            entry->overlay->raise();
            ++entry->presentationCount;
        }
    }
    removeEntries(removals);
}

bool registerSumatraPdfPersistentHighlight(
    const SumatraPdfPersistentHighlight &highlight)
{
    return SumatraPdfHighlightManager::instance().addOrUpdate(highlight);
}

bool showSumatraPdfRectHighlight(const QRectF &pdfRect,
                                 int page,
                                 double zoom,
                                 int durationMilliseconds)
{
    if (!pdfRect.isValid() || page <= 0 || !std::isfinite(zoom) || zoom <= 0.0) {
        return false;
    }

    const ForegroundAppWindowContext context = currentForegroundAppWindowContext();
    if (!isSumatraPdfForegroundWindow(context)) {
        return false;
    }

    const SumatraPdfDdeMousePosition mouse = requestSumatraPdfDdeMousePosition(600);
    if (!mouse.success() || mouse.page != page) {
        return false;
    }

    const QPoint cursor = QCursor::pos();
    const QRect screenRect = screenRectForPdfRegion(pdfRect, zoom, cursor, mouse);
    if (!intersectsAvailableScreen(screenRect)) {
        return false;
    }

    auto *overlay = new SumatraPdfRectHighlightOverlay(screenRect);
    overlay->show();
    QTimer::singleShot(std::max(250, durationMilliseconds), overlay, &QWidget::close);
    return true;
}
#endif

} // namespace Pinloom
