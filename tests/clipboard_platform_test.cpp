#include "pinloom/clip/ClipInsertionService.h"
#include "pinloom/clip/ClipboardCaptureService.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTest>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace Pinloom;

class ClipboardPlatformTest : public QObject {
    Q_OBJECT
private slots:
    void restoresBurstAndHonorsOtherWriter();
    void checksRevisionInsideNativeLock();
    void suppressesCaptureOfOwnNativeWrites();
};

void ClipboardPlatformTest::restoresBurstAndHonorsOtherWriter()
{
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
    QtClipboardTextAccessor clipboard;
    QVERIFY(clipboard.setText(QStringLiteral("private station original")));
    InMemoryClipRepository repository;
    QStringList pasted;
    ClipInsertionService service(&clipboard, repository, [&]() {
        pasted.append(QGuiApplication::clipboard()->text());
        return true;
    });
    ClipInsertionOptions options;
    options.clipboardRestoreDelayMs = 40;
    service.setOptions(options);
    Clip clip;
    clip.text = QStringLiteral("first 中文");
    QVERIFY(service.insertClip(clip).inserted());
    clip.text = QStringLiteral("second\nline");
    QVERIFY(service.insertClip(clip).inserted());
    clip.text = QStringLiteral("third");
    QVERIFY(service.insertClip(clip).inserted());
    QCOMPARE(pasted, (QStringList{QStringLiteral("first 中文"), QStringLiteral("second\nline"), clip.text}));
    QTRY_COMPARE(clipboard.text(), QStringLiteral("private station original"));

    QVERIFY(service.insertClip(clip).inserted());
    const quint64 ours = clipboard.revision();
    QGuiApplication::clipboard()->setText(clip.text); // Independent Qt writer, same text.
    QVERIFY(clipboard.revision() != ours);
    QTest::qWait(100);
    QCOMPARE(clipboard.text(), clip.text);

    QGuiApplication::clipboard()->setText(QStringLiteral("second origin"));
    {
        ClipInsertionService dying(&clipboard, repository, []() { return true; });
        QVERIFY(dying.insertClip(clip).inserted());
    }
    QCOMPARE(clipboard.text(), QStringLiteral("second origin"));
    service.setPasteInvoker([]() { return false; });
    QCOMPARE(service.insertClip(clip).status, ClipInsertionStatus::PasteFailed);
    QCOMPARE(clipboard.text(), QStringLiteral("second origin"));
    {
        ClipInsertionService ownedAccessorService(repository);
        ownedAccessorService.setPasteInvoker([]() { return true; });
        QVERIFY(ownedAccessorService.insertClip(clip).inserted());
    }
    QCOMPARE(clipboard.text(), QStringLiteral("second origin"));
}

void ClipboardPlatformTest::checksRevisionInsideNativeLock()
{
    QtClipboardTextAccessor clipboard;
    QVERIFY(clipboard.setText(QStringLiteral("owned")));
    const quint64 owned = clipboard.revision();
    QVERIFY(clipboard.setText(QStringLiteral("external")));
    const auto rejected = clipboard.setTextIfUnchanged(QStringLiteral("stale restore"), owned);
    QCOMPARE(rejected.status, ClipboardTextWriteResult::Changed);
    QCOMPARE(clipboard.text(), QStringLiteral("external"));
    const auto written = clipboard.setTextIfUnchanged(QStringLiteral("new owned"), clipboard.revision());
    QCOMPARE(written.status, ClipboardTextWriteResult::Written);
    QCOMPARE(written.revision, clipboard.revision());
    QCOMPARE(clipboard.text(), QStringLiteral("new owned"));
    const auto restored = clipboard.setTextIfUnchanged(QStringLiteral("restored"), written.revision);
    QCOMPARE(restored.status, ClipboardTextWriteResult::Written);
    QCOMPARE(clipboard.text(), QStringLiteral("restored"));
}

void ClipboardPlatformTest::suppressesCaptureOfOwnNativeWrites()
{
    QtClipboardTextAccessor clipboard;
    QVERIFY(clipboard.setText(QStringLiteral("before capture")));
    QTest::qWait(30);
    InMemoryClipRepository repository;
    ClipboardCaptureService capture(repository);
    QVERIFY(capture.start());
    ClipInsertionService service(&clipboard, repository, []() { return true; });
    service.setSuppressClipboardCaptureCallback([&]() { capture.suppressNextChange(); });
    capture.setChangeSuppressionPredicate([&]() { return service.ownsCurrentClipboardWrite(); });
    ClipInsertionOptions options;
    options.clipboardRestoreDelayMs = 40;
    service.setOptions(options);
    Clip clip;
    for (int i = 0; i < 3; ++i) {
        clip.text = QStringLiteral("own %1").arg(i);
        QVERIFY(service.insertClip(clip).inserted());
    }
    QTRY_COMPARE(clipboard.text(), QStringLiteral("before capture"));
    QTest::qWait(40);
    QCOMPARE(repository.temporaryClips().size(), 0);
    QVERIFY(clipboard.setText(QStringLiteral("user copied text")));
    QTRY_COMPARE(repository.temporaryClips().size(), 1);
    QCOMPARE(repository.temporaryClips().first().text, QStringLiteral("user copied text"));
}

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("PINLOOM_TEST_ISOLATED_WINDOWS_CLIPBOARD")) {
        std::puts("SKIP: enable PINLOOM_TEST_ISOLATED_WINDOWS_CLIPBOARD under the shared GUI lock");
        return 77;
    }
    // Window stations have separate clipboards. Never read, write or snapshot WinSta0.
    // Refuse the test if isolation fails, before constructing QGuiApplication.
    HWINSTA originalStation = GetProcessWindowStation();
    HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !SetProcessWindowStation(station)) {
        std::fprintf(stderr, "Window station isolation failed: %lu\n", GetLastError());
        if (station) CloseWindowStation(station);
        return 1;
    }
    HDESK desktop = CreateDesktopW(L"PinloomClipboardTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) {
        std::fprintf(stderr, "Desktop isolation failed: %lu\n", GetLastError());
        SetProcessWindowStation(originalStation);
        if (desktop) CloseDesktop(desktop);
        CloseWindowStation(station);
        return 1;
    }
    qputenv("QT_QPA_PLATFORM", "windows");
    int result;
    {
        QGuiApplication app(argc, argv);
        ClipboardPlatformTest test;
        result = QTest::qExec(&test, argc, argv);
    }
    SetThreadDesktop(originalDesktop);
    SetProcessWindowStation(originalStation);
    CloseDesktop(desktop);
    CloseWindowStation(station);
    return result;
}

#include "clipboard_platform_test.moc"
