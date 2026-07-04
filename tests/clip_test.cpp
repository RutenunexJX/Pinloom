#include "pinloom/clip/ClipboardCaptureService.h"
#include "pinloom/clip/ClipInsertionService.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/PlatformPasteInvoker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <optional>

using namespace Pinloom;

class FakeClipboardTextSource : public ClipboardTextSource {
    Q_OBJECT

public:
    QString text() const override
    {
        return text_;
    }

    void setText(const QString &text)
    {
        text_ = text;
        emit textChanged();
    }

private:
    QString text_;
};

class FakeClipboardTextAccessor : public ClipboardTextAccessor {
public:
    QString text() const override
    {
        return text_;
    }

    bool setText(const QString &text) override
    {
        if (failNextSetText_ || failSetText_) {
            failNextSetText_ = false;
            return false;
        }

        text_ = text;
        writes_.append(text);
        return true;
    }

    bool isAvailable() const override
    {
        return available_;
    }

    void setInitialText(const QString &text)
    {
        text_ = text;
    }

    void setAvailable(bool available)
    {
        available_ = available;
    }

    void setFailSetText(bool failSetText)
    {
        failSetText_ = failSetText;
    }

    void setFailNextSetText()
    {
        failNextSetText_ = true;
    }

    QStringList writes() const
    {
        return writes_;
    }

private:
    QString text_;
    QStringList writes_;
    bool available_ = true;
    bool failSetText_ = false;
    bool failNextSetText_ = false;
};

class FakePasteKeySender : public PasteKeySender {
public:
    bool isAvailable() const override
    {
        return available_;
    }

    bool sendKeys(const PasteKeySequence &sequence) override
    {
        ++sendCalls_;
        sentKeys_ = sequence;
        return sendResult_;
    }

    void setAvailable(bool available)
    {
        available_ = available;
    }

    void setSendResult(bool sendResult)
    {
        sendResult_ = sendResult;
    }

    int sendCalls() const
    {
        return sendCalls_;
    }

    PasteKeySequence sentKeys() const
    {
        return sentKeys_;
    }

private:
    bool available_ = true;
    bool sendResult_ = true;
    int sendCalls_ = 0;
    PasteKeySequence sentKeys_;
};

void verifyCtrlVPasteSequence(const PasteKeySequence &sequence)
{
    const PasteKeySequence expected = PlatformPasteInvoker::ctrlVPasteSequence();
    QCOMPARE(sequence.size(), expected.size());
    for (qsizetype index = 0; index < expected.size(); ++index) {
        QVERIFY(sequence.at(index) == expected.at(index));
    }
}

class ClipTest : public QObject {
    Q_OBJECT

private slots:
    void ignoresBlankText();
    void rejectsTextOverSizeLimit();
    void deduplicatesSameText();
    void distinguishesTemporaryAndSavedClips();
    void prunesTemporaryHistoryByTtlAndCount();
    void createsSavedClipLocatorAnchor();
    void sqliteInitializesIdempotently();
    void sqlitePersistsCapturedTextAcrossRepositoryRestart();
    void sqliteAppliesCapturePolicies();
    void sqlitePersistsSavedClipMetadataAcrossRepositoryRestart();
    void sqlitePrunesTemporaryHistoryPersistently();
    void sqliteCreatesSavedClipLocatorAnchorAfterRestart();
    void clipboardServiceCapturesTextIntoRepository();
    void clipboardServicePauseAndResumeCapture();
    void clipboardServiceUsesRepositoryPolicyForIgnoredText();
    void clipboardServiceSuppressesNextChange();
    void clipboardServicePersistsCapturedTextWithSqliteRepository();
    void platformPasteInvokerSendsCtrlVSequence();
    void platformPasteInvokerReportsSenderFailure();
    void platformPasteInvokerReportsUnavailableSender();
#ifndef Q_OS_WIN
    void platformPasteInvokerDefaultFallbackReturnsFalse();
#endif
    void insertionServiceInsertsTextById();
    void insertionServiceSuppressesCaptureBeforeOwnClipboardWrites();
    void insertionServiceRestoresOriginalClipboardOnSuccess();
    void insertionServiceUsesPlatformPasteInvoker();
    void insertionServiceReportsErrors();
    void insertionServiceInsertsSqliteTemporaryAndSavedClips();
};

void ClipTest::ignoresBlankText()
{
    InMemoryClipRepository repository;

    const ClipCaptureResult result = repository.captureText(QStringLiteral(" \n\t "));

    QVERIFY(!result.captured());
    QVERIFY(result.status == ClipCaptureStatus::IgnoredBlank);
    QVERIFY(repository.clips().isEmpty());
}

void ClipTest::rejectsTextOverSizeLimit()
{
    InMemoryClipRepository repository;
    ClipCapturePolicy policy;
    policy.maxTextBytes = 4;

    const ClipCaptureResult result = repository.captureText(QStringLiteral("hello"), policy);

    QVERIFY(!result.captured());
    QVERIFY(result.status == ClipCaptureStatus::IgnoredTooLarge);
    QVERIFY(repository.clips().isEmpty());
}

void ClipTest::deduplicatesSameText()
{
    InMemoryClipRepository repository;

    const ClipCaptureResult first = repository.captureText(QStringLiteral("same text"));
    const ClipCaptureResult second = repository.captureText(QStringLiteral("same text"));

    QVERIFY(first.captured());
    QVERIFY(!second.captured());
    QVERIFY(second.status == ClipCaptureStatus::IgnoredDuplicate);
    QCOMPARE(repository.clips().size(), 1);
}

void ClipTest::distinguishesTemporaryAndSavedClips()
{
    InMemoryClipRepository repository;
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Reusable launch command"), {}, {}, now);
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());
    QCOMPARE(repository.temporaryClips().size(), 1);
    QCOMPARE(repository.savedClips().size(), 0);

    QVERIFY(repository.saveClip(captured.clip->id,
                                QStringLiteral("Launch command"),
                                {QStringLiteral("run app")},
                                {QStringLiteral("ops")},
                                true,
                                now.addSecs(10)));

    const std::optional<Clip> stored = repository.findClip(captured.clip->id);
    QVERIFY(stored.has_value());
    QVERIFY(stored->state == ClipState::Saved);
    QCOMPARE(stored->name, QStringLiteral("Launch command"));
    QCOMPARE(stored->aliases, QStringList{QStringLiteral("run app")});
    QCOMPARE(stored->tags, QStringList{QStringLiteral("ops")});
    QVERIFY(stored->pinned);
    QVERIFY(!stored->expiresAt.isValid());
    QCOMPARE(repository.temporaryClips().size(), 0);
    QCOMPARE(repository.savedClips().size(), 1);
}

void ClipTest::prunesTemporaryHistoryByTtlAndCount()
{
    InMemoryClipRepository repository;
    ClipCapturePolicy policy;
    policy.maxTemporaryClips = 2;
    policy.temporaryTtlSeconds = 30;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    QVERIFY(repository.captureText(QStringLiteral("first"), policy, {}, base).captured());
    QVERIFY(repository.captureText(QStringLiteral("second"), policy, {}, base.addSecs(1)).captured());
    QVERIFY(repository.captureText(QStringLiteral("third"), policy, {}, base.addSecs(2)).captured());

    QList<Clip> temporary = repository.temporaryClips();
    QCOMPARE(temporary.size(), 2);
    QVERIFY(std::none_of(temporary.cbegin(), temporary.cend(), [](const Clip &clip) {
        return clip.text == QStringLiteral("first");
    }));

    repository.pruneTemporaryHistory(policy, base.addSecs(40));
    QVERIFY(repository.temporaryClips().isEmpty());
}

void ClipTest::createsSavedClipLocatorAnchor()
{
    InMemoryClipRepository repository;
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("paste me"), {}, {}, now);
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());
    QVERIFY(!savedClipAnchor(captured.clip.value()).has_value());

    QVERIFY(repository.saveClip(captured.clip->id,
                                QStringLiteral("Paste greeting"),
                                {QStringLiteral("hello")},
                                {QStringLiteral("text")},
                                true,
                                now.addSecs(1)));
    const std::optional<Clip> saved = repository.findClip(captured.clip->id);
    QVERIFY(saved.has_value());

    const std::optional<Anchor> anchor = savedClipAnchor(saved.value());
    QVERIFY(anchor.has_value());
    QCOMPARE(anchor->type, AnchorType::Manual);
    QCOMPARE(anchor->id, QStringLiteral("clip:%1").arg(saved->id));
    QCOMPARE(anchor->name, QStringLiteral("Paste greeting"));
    QCOMPARE(anchor->targetApp, QStringLiteral("pinloom.clip"));
    QCOMPARE(anchor->targetUri, QStringLiteral("clip://%1").arg(saved->id));
    QCOMPARE(anchor->locatorType, QStringLiteral("clip.insert"));
    QCOMPARE(anchor->aliases, QStringList{QStringLiteral("hello")});
    QCOMPARE(anchor->tags, QStringList{QStringLiteral("text")});
    QVERIFY(anchor->pinned);

    const QJsonDocument locator = QJsonDocument::fromJson(anchor->locatorJson.toUtf8());
    QVERIFY(locator.isObject());
    QCOMPARE(locator.object().value(QStringLiteral("clip_id")).toString(), saved->id);
    QCOMPARE(locator.object().value(QStringLiteral("mode")).toString(), QStringLiteral("paste"));
}

void ClipTest::sqliteInitializesIdempotently()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteClipRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom_clip.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
}

void ClipTest::sqlitePersistsCapturedTextAcrossRepositoryRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    QString capturedId;
    {
        SqliteClipRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        const ClipCaptureResult captured =
            repository.captureText(QStringLiteral("Persistent clip text"), {}, QStringLiteral("notepad.exe"), now);
        QVERIFY2(captured.captured(), qPrintable(repository.lastError()));
        QVERIFY(captured.clip.has_value());
        capturedId = captured.clip->id;
    }

    SqliteClipRepository restarted;
    QVERIFY2(restarted.open(databasePath), qPrintable(restarted.lastError()));
    QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));

    const std::optional<Clip> stored = restarted.findClip(capturedId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->state == ClipState::Temporary);
    QVERIFY(stored->kind == ClipKind::Text);
    QCOMPARE(stored->text, QStringLiteral("Persistent clip text"));
    QCOMPARE(stored->preview, QStringLiteral("Persistent clip text"));
    QCOMPARE(stored->sourceApp, QStringLiteral("notepad.exe"));
    QCOMPARE(static_cast<qlonglong>(stored->sizeBytes),
             static_cast<qlonglong>(QStringLiteral("Persistent clip text").toUtf8().size()));
    QCOMPARE(restarted.temporaryClips().size(), 1);
    QCOMPARE(restarted.savedClips().size(), 0);
}

void ClipTest::sqliteAppliesCapturePolicies()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteClipRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom_clip.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    const ClipCaptureResult blank = repository.captureText(QStringLiteral(" \n\t "));
    QVERIFY(!blank.captured());
    QVERIFY(blank.status == ClipCaptureStatus::IgnoredBlank);

    ClipCapturePolicy smallPolicy;
    smallPolicy.maxTextBytes = 4;
    const ClipCaptureResult tooLarge = repository.captureText(QStringLiteral("hello"), smallPolicy);
    QVERIFY(!tooLarge.captured());
    QVERIFY(tooLarge.status == ClipCaptureStatus::IgnoredTooLarge);

    const ClipCaptureResult first = repository.captureText(QStringLiteral("same text"));
    const ClipCaptureResult duplicate = repository.captureText(QStringLiteral("same text"));
    QVERIFY2(first.captured(), qPrintable(repository.lastError()));
    QVERIFY(!duplicate.captured());
    QVERIFY(duplicate.status == ClipCaptureStatus::IgnoredDuplicate);

    ClipCapturePolicy pausedPolicy;
    pausedPolicy.capturePaused = true;
    const ClipCaptureResult paused = repository.captureText(QStringLiteral("paused text"), pausedPolicy);
    QVERIFY(!paused.captured());
    QVERIFY(paused.status == ClipCaptureStatus::IgnoredPaused);

    ClipCapturePolicy excludedPolicy;
    excludedPolicy.excludedSourceApps = {QStringLiteral("secrets.app")};
    const ClipCaptureResult excluded =
        repository.captureText(QStringLiteral("secret text"), excludedPolicy, QStringLiteral("SECRETS.APP"));
    QVERIFY(!excluded.captured());
    QVERIFY(excluded.status == ClipCaptureStatus::IgnoredExcludedSource);

    QCOMPARE(repository.clips().size(), 1);
}

void ClipTest::sqlitePersistsSavedClipMetadataAcrossRepositoryRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    QString clipId;
    {
        SqliteClipRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        const ClipCaptureResult captured = repository.captureText(QStringLiteral("Reusable launch command"), {}, {}, now);
        QVERIFY2(captured.captured(), qPrintable(repository.lastError()));
        clipId = captured.clip->id;
        QVERIFY2(repository.saveClip(clipId,
                                     QStringLiteral("Launch command"),
                                     {QStringLiteral("run app"), QStringLiteral(" run app "), QStringLiteral("launcher")},
                                     {QStringLiteral("ops"), QStringLiteral(" "), QStringLiteral("clipboard")},
                                     true,
                                     now.addSecs(10)),
                 qPrintable(repository.lastError()));
    }

    SqliteClipRepository restarted;
    QVERIFY2(restarted.open(databasePath), qPrintable(restarted.lastError()));
    QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));

    const std::optional<Clip> stored = restarted.findClip(clipId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->state == ClipState::Saved);
    QCOMPARE(stored->name, QStringLiteral("Launch command"));
    QCOMPARE(stored->aliases, (QStringList{QStringLiteral("run app"), QStringLiteral("launcher")}));
    QCOMPARE(stored->tags, (QStringList{QStringLiteral("ops"), QStringLiteral("clipboard")}));
    QVERIFY(stored->pinned);
    QVERIFY(!stored->expiresAt.isValid());
    QCOMPARE(restarted.temporaryClips().size(), 0);
    QCOMPARE(restarted.savedClips().size(), 1);
}

void ClipTest::sqlitePrunesTemporaryHistoryPersistently()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));

    ClipCapturePolicy policy;
    policy.maxTemporaryClips = 2;
    policy.temporaryTtlSeconds = 30;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    {
        SqliteClipRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
        QVERIFY2(repository.captureText(QStringLiteral("first"), policy, {}, base).captured(),
                 qPrintable(repository.lastError()));
        QVERIFY2(repository.captureText(QStringLiteral("second"), policy, {}, base.addSecs(1)).captured(),
                 qPrintable(repository.lastError()));
        QVERIFY2(repository.captureText(QStringLiteral("third"), policy, {}, base.addSecs(2)).captured(),
                 qPrintable(repository.lastError()));
    }

    {
        SqliteClipRepository restarted;
        QVERIFY2(restarted.open(databasePath), qPrintable(restarted.lastError()));
        QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));

        const QList<Clip> temporary = restarted.temporaryClips();
        QCOMPARE(temporary.size(), 2);
        QVERIFY(std::none_of(temporary.cbegin(), temporary.cend(), [](const Clip &clip) {
            return clip.text == QStringLiteral("first");
        }));

        restarted.pruneTemporaryHistory(policy, base.addSecs(40));
        QVERIFY2(restarted.lastError().isEmpty(), qPrintable(restarted.lastError()));
    }

    SqliteClipRepository restartedAgain;
    QVERIFY2(restartedAgain.open(databasePath), qPrintable(restartedAgain.lastError()));
    QVERIFY2(restartedAgain.initialize(), qPrintable(restartedAgain.lastError()));
    QVERIFY(restartedAgain.temporaryClips().isEmpty());
    QVERIFY(restartedAgain.clips().isEmpty());
}

void ClipTest::sqliteCreatesSavedClipLocatorAnchorAfterRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    QString clipId;
    {
        SqliteClipRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        const ClipCaptureResult captured = repository.captureText(QStringLiteral("paste me"), {}, {}, now);
        QVERIFY2(captured.captured(), qPrintable(repository.lastError()));
        clipId = captured.clip->id;
        QVERIFY2(repository.saveClip(clipId,
                                     QStringLiteral("Paste greeting"),
                                     {QStringLiteral("hello")},
                                     {QStringLiteral("text")},
                                     true,
                                     now.addSecs(1)),
                 qPrintable(repository.lastError()));
    }

    SqliteClipRepository restarted;
    QVERIFY2(restarted.open(databasePath), qPrintable(restarted.lastError()));
    QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));
    const std::optional<Clip> saved = restarted.findClip(clipId);
    QVERIFY(saved.has_value());

    const std::optional<Anchor> anchor = savedClipAnchor(saved.value());
    QVERIFY(anchor.has_value());
    QCOMPARE(anchor->targetApp, QStringLiteral("pinloom.clip"));
    QCOMPARE(anchor->targetUri, QStringLiteral("clip://%1").arg(saved->id));
    QCOMPARE(anchor->locatorType, QStringLiteral("clip.insert"));
    QCOMPARE(anchor->aliases, QStringList{QStringLiteral("hello")});
    QCOMPARE(anchor->tags, QStringList{QStringLiteral("text")});
    QVERIFY(anchor->pinned);

    const QJsonDocument locator = QJsonDocument::fromJson(anchor->locatorJson.toUtf8());
    QVERIFY(locator.isObject());
    QCOMPARE(locator.object().value(QStringLiteral("clip_id")).toString(), saved->id);
    QCOMPARE(locator.object().value(QStringLiteral("mode")).toString(), QStringLiteral("paste"));
}

void ClipTest::clipboardServiceCapturesTextIntoRepository()
{
    FakeClipboardTextSource clipboard;
    InMemoryClipRepository repository;
    ClipboardCaptureService service(&clipboard, repository);
    service.setSourceApp(QStringLiteral(" notepad.exe "));

    int capturedSignals = 0;
    QString capturedId;
    QObject::connect(&service, &ClipboardCaptureService::captured, [&](const Clip &clip) {
        ++capturedSignals;
        capturedId = clip.id;
    });

    QVERIFY(service.start());
    clipboard.setText(QStringLiteral("Qt clipboard text"));

    const QList<Clip> clips = repository.clips();
    QCOMPARE(clips.size(), 1);
    QCOMPARE(clips.first().text, QStringLiteral("Qt clipboard text"));
    QCOMPARE(clips.first().sourceApp, QStringLiteral("notepad.exe"));
    QCOMPARE(service.lastCapturedId(), clips.first().id);
    QCOMPARE(capturedId, clips.first().id);
    QCOMPARE(capturedSignals, 1);
    QVERIFY(service.lastCapturedClip().has_value());
    QVERIFY(service.lastStatus() == ClipCaptureStatus::Captured);
    QVERIFY(service.lastError().isEmpty());
}

void ClipTest::clipboardServicePauseAndResumeCapture()
{
    FakeClipboardTextSource clipboard;
    InMemoryClipRepository repository;
    ClipboardCaptureService service(&clipboard, repository);

    QVERIFY(service.start());
    service.pauseCapture();
    QVERIFY(service.capturePaused());
    clipboard.setText(QStringLiteral("paused clipboard text"));

    QVERIFY(repository.clips().isEmpty());
    QVERIFY(service.lastStatus() == ClipCaptureStatus::IgnoredPaused);

    service.resumeCapture();
    QVERIFY(!service.capturePaused());
    clipboard.setText(QStringLiteral("resumed clipboard text"));

    const QList<Clip> clips = repository.clips();
    QCOMPARE(clips.size(), 1);
    QCOMPARE(clips.first().text, QStringLiteral("resumed clipboard text"));
    QVERIFY(service.lastStatus() == ClipCaptureStatus::Captured);
}

void ClipTest::clipboardServiceUsesRepositoryPolicyForIgnoredText()
{
    FakeClipboardTextSource clipboard;
    InMemoryClipRepository repository;
    ClipboardCaptureService service(&clipboard, repository);

    ClipCapturePolicy policy;
    policy.maxTextBytes = 5;
    service.setPolicy(policy);

    int ignoredSignals = 0;
    QObject::connect(&service, &ClipboardCaptureService::captureIgnored, [&](ClipCaptureStatus) {
        ++ignoredSignals;
    });

    QVERIFY(service.start());

    clipboard.setText(QStringLiteral(" \n\t "));
    QVERIFY(repository.clips().isEmpty());
    QVERIFY(service.lastStatus() == ClipCaptureStatus::IgnoredBlank);

    clipboard.setText(QStringLiteral("123456"));
    QVERIFY(repository.clips().isEmpty());
    QVERIFY(service.lastStatus() == ClipCaptureStatus::IgnoredTooLarge);

    clipboard.setText(QStringLiteral("same"));
    QCOMPARE(repository.clips().size(), 1);
    QVERIFY(service.lastStatus() == ClipCaptureStatus::Captured);

    clipboard.setText(QStringLiteral("same"));
    QCOMPARE(repository.clips().size(), 1);
    QVERIFY(service.lastStatus() == ClipCaptureStatus::IgnoredDuplicate);
    QCOMPARE(ignoredSignals, 3);
}

void ClipTest::clipboardServiceSuppressesNextChange()
{
    FakeClipboardTextSource clipboard;
    InMemoryClipRepository repository;
    ClipboardCaptureService service(&clipboard, repository);

    QVERIFY(service.start());
    service.suppressNextChange();
    QVERIFY(service.suppressingNextChange());
    clipboard.setText(QStringLiteral("self-written clipboard text"));

    QVERIFY(!service.suppressingNextChange());
    QVERIFY(repository.clips().isEmpty());

    clipboard.setText(QStringLiteral("external clipboard text"));

    const QList<Clip> clips = repository.clips();
    QCOMPARE(clips.size(), 1);
    QCOMPARE(clips.first().text, QStringLiteral("external clipboard text"));
}

void ClipTest::clipboardServicePersistsCapturedTextWithSqliteRepository()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));

    QString capturedId;
    {
        FakeClipboardTextSource clipboard;
        SqliteClipRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        ClipboardCaptureService service(&clipboard, repository);
        service.setSourceApp(QStringLiteral("terminal.exe"));
        QVERIFY(service.start());

        clipboard.setText(QStringLiteral("Persistent clipboard service text"));
        QVERIFY2(service.lastError().isEmpty(), qPrintable(service.lastError()));
        capturedId = service.lastCapturedId();
        QVERIFY(!capturedId.isEmpty());
        QCOMPARE(repository.clips().size(), 1);
    }

    SqliteClipRepository restarted;
    QVERIFY2(restarted.open(databasePath), qPrintable(restarted.lastError()));
    QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));

    const std::optional<Clip> stored = restarted.findClip(capturedId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->text, QStringLiteral("Persistent clipboard service text"));
    QCOMPARE(stored->sourceApp, QStringLiteral("terminal.exe"));
    QVERIFY(stored->state == ClipState::Temporary);
}

void ClipTest::platformPasteInvokerSendsCtrlVSequence()
{
    FakePasteKeySender sender;
    PlatformPasteInvoker invoker(&sender);

    const PlatformPasteResult result = invoker.invoke();

    QVERIFY(result.pasted());
    QVERIFY(result.status == PlatformPasteStatus::Invoked);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(sender.sendCalls(), 1);
    verifyCtrlVPasteSequence(sender.sentKeys());
}

void ClipTest::platformPasteInvokerReportsSenderFailure()
{
    FakePasteKeySender sender;
    sender.setSendResult(false);
    PlatformPasteInvoker invoker(&sender);

    const PlatformPasteResult result = invoker.invoke();

    QVERIFY(!result.pasted());
    QVERIFY(result.status == PlatformPasteStatus::SendFailed);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(sender.sendCalls(), 1);
    verifyCtrlVPasteSequence(sender.sentKeys());
}

void ClipTest::platformPasteInvokerReportsUnavailableSender()
{
    FakePasteKeySender sender;
    sender.setAvailable(false);
    PlatformPasteInvoker invoker(&sender);

    const PlatformPasteResult result = invoker.invoke();

    QVERIFY(!result.pasted());
    QVERIFY(result.status == PlatformPasteStatus::Unavailable);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(sender.sendCalls(), 0);
    QVERIFY(sender.sentKeys().isEmpty());
}

#ifndef Q_OS_WIN
void ClipTest::platformPasteInvokerDefaultFallbackReturnsFalse()
{
    PlatformPasteInvoker invoker;

    const PlatformPasteResult result = invoker.invoke();

    QVERIFY(!result.pasted());
    QVERIFY(result.status == PlatformPasteStatus::Unavailable);
    QVERIFY(!result.error.isEmpty());
}
#endif

void ClipTest::insertionServiceInsertsTextById()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Insert this text"));
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());

    FakeClipboardTextAccessor clipboard;
    clipboard.setInitialText(QStringLiteral("original clipboard"));
    int pasteCalls = 0;
    ClipInsertionService service(&clipboard, repository, [&]() {
        ++pasteCalls;
        return true;
    });
    ClipInsertionOptions options;
    options.restoreOriginalClipboardOnSuccess = false;
    service.setOptions(options);

    int insertedSignals = 0;
    QString insertedSignalId;
    QObject::connect(&service, &ClipInsertionService::inserted, [&](const Clip &clip) {
        ++insertedSignals;
        insertedSignalId = clip.id;
    });

    const ClipInsertionResult result = service.insertClip(captured.clip->id);

    QVERIFY(result.inserted());
    QVERIFY(result.status == ClipInsertionStatus::Inserted);
    QCOMPARE(result.clipId, captured.clip->id);
    QCOMPARE(clipboard.text(), QStringLiteral("Insert this text"));
    QCOMPARE(clipboard.writes(), QStringList{QStringLiteral("Insert this text")});
    QCOMPARE(pasteCalls, 1);
    QCOMPARE(insertedSignals, 1);
    QCOMPARE(insertedSignalId, captured.clip->id);
    QCOMPARE(service.lastInsertedId(), captured.clip->id);
    QVERIFY(service.lastInsertedClip().has_value());
    QVERIFY(service.lastStatus() == ClipInsertionStatus::Inserted);
    QVERIFY(service.lastError().isEmpty());
}

void ClipTest::insertionServiceSuppressesCaptureBeforeOwnClipboardWrites()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Self-written text"));
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());

    FakeClipboardTextAccessor clipboard;
    clipboard.setInitialText(QStringLiteral("original clipboard"));
    ClipInsertionService service(&clipboard, repository, []() {
        return true;
    });
    ClipInsertionOptions options;
    options.restoreOriginalClipboardOnSuccess = false;
    service.setOptions(options);

    QStringList clipboardTextAtSuppression;
    service.setSuppressClipboardCaptureCallback([&]() {
        clipboardTextAtSuppression.append(clipboard.text());
    });

    const ClipInsertionResult result = service.insertClip(captured.clip->id);

    QVERIFY(result.inserted());
    QCOMPARE(clipboardTextAtSuppression, QStringList{QStringLiteral("original clipboard")});
    QCOMPARE(clipboard.writes(), QStringList{QStringLiteral("Self-written text")});
}

void ClipTest::insertionServiceRestoresOriginalClipboardOnSuccess()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Temporary paste text"));
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());

    FakeClipboardTextAccessor clipboard;
    clipboard.setInitialText(QStringLiteral("original clipboard"));
    int pasteCalls = 0;
    ClipInsertionService service(&clipboard, repository, [&]() {
        ++pasteCalls;
        return true;
    });

    int suppressCalls = 0;
    service.setSuppressClipboardCaptureCallback([&]() {
        ++suppressCalls;
    });

    const ClipInsertionResult result = service.insertClip(captured.clip->id);

    QVERIFY(result.inserted());
    QCOMPARE(pasteCalls, 1);
    QCOMPARE(suppressCalls, 2);
    QCOMPARE(clipboard.text(), QStringLiteral("original clipboard"));
    QCOMPARE(clipboard.writes(),
             (QStringList{QStringLiteral("Temporary paste text"), QStringLiteral("original clipboard")}));
}

void ClipTest::insertionServiceUsesPlatformPasteInvoker()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Platform paste text"));
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());

    FakeClipboardTextAccessor clipboard;
    clipboard.setInitialText(QStringLiteral("original clipboard"));
    FakePasteKeySender sender;
    ClipInsertionService service(&clipboard, repository, createPlatformPasteInvoker(&sender));
    ClipInsertionOptions options;
    options.restoreOriginalClipboardOnSuccess = false;
    service.setOptions(options);

    const ClipInsertionResult result = service.insertClip(captured.clip->id);

    QVERIFY2(result.inserted(), qPrintable(result.error));
    QCOMPARE(clipboard.text(), QStringLiteral("Platform paste text"));
    QCOMPARE(clipboard.writes(), QStringList{QStringLiteral("Platform paste text")});
    QCOMPARE(sender.sendCalls(), 1);
    verifyCtrlVPasteSequence(sender.sentKeys());
    QVERIFY(service.lastStatus() == ClipInsertionStatus::Inserted);
}

void ClipTest::insertionServiceReportsErrors()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Failure text"));
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());

    FakeClipboardTextAccessor missingClipboard;
    int missingPasteCalls = 0;
    ClipInsertionService missingService(&missingClipboard, repository, [&]() {
        ++missingPasteCalls;
        return true;
    });

    const ClipInsertionResult missing = missingService.insertClip(QStringLiteral("missing-id"));
    QVERIFY(!missing.inserted());
    QVERIFY(missing.status == ClipInsertionStatus::MissingClip);
    QVERIFY(missingService.lastStatus() == ClipInsertionStatus::MissingClip);
    QVERIFY(!missingService.lastError().isEmpty());
    QCOMPARE(missingPasteCalls, 0);
    QVERIFY(missingClipboard.writes().isEmpty());

    FakeClipboardTextAccessor pasteClipboard;
    ClipInsertionService pasteService(&pasteClipboard, repository, []() {
        return false;
    });
    ClipInsertionOptions noRestore;
    noRestore.restoreOriginalClipboardOnSuccess = false;
    pasteService.setOptions(noRestore);

    const ClipInsertionResult pasteFailed = pasteService.insertClip(captured.clip->id);
    QVERIFY(!pasteFailed.inserted());
    QVERIFY(pasteFailed.status == ClipInsertionStatus::PasteFailed);
    QVERIFY(pasteService.lastStatus() == ClipInsertionStatus::PasteFailed);
    QVERIFY(!pasteService.lastError().isEmpty());
    QCOMPARE(pasteClipboard.writes(), QStringList{QStringLiteral("Failure text")});

    FakeClipboardTextAccessor unavailableClipboard;
    unavailableClipboard.setAvailable(false);
    int unavailablePasteCalls = 0;
    ClipInsertionService unavailableService(&unavailableClipboard, repository, [&]() {
        ++unavailablePasteCalls;
        return true;
    });

    const ClipInsertionResult unavailable = unavailableService.insertClip(captured.clip->id);
    QVERIFY(!unavailable.inserted());
    QVERIFY(unavailable.status == ClipInsertionStatus::ClipboardUnavailable);
    QVERIFY(unavailableService.lastStatus() == ClipInsertionStatus::ClipboardUnavailable);
    QVERIFY(!unavailableService.lastError().isEmpty());
    QCOMPARE(unavailablePasteCalls, 0);
    QVERIFY(unavailableClipboard.writes().isEmpty());

    FakeClipboardTextAccessor directClipboard;
    ClipInsertionService directService(&directClipboard,
                                       [](const QString &) -> std::optional<Clip> {
                                           return std::nullopt;
                                       },
                                       []() {
                                           return true;
                                       });
    Clip emptyClip;
    emptyClip.id = QStringLiteral("empty");
    emptyClip.text = QStringLiteral(" \n\t ");
    const ClipInsertionResult empty = directService.insertClip(emptyClip);
    QVERIFY(!empty.inserted());
    QVERIFY(empty.status == ClipInsertionStatus::EmptyText);
    QVERIFY(directService.lastStatus() == ClipInsertionStatus::EmptyText);

    Clip unsupportedClip;
    unsupportedClip.id = QStringLiteral("unsupported");
    unsupportedClip.kind = static_cast<ClipKind>(999);
    unsupportedClip.text = QStringLiteral("non-text bytes");
    const ClipInsertionResult unsupported = directService.insertClip(unsupportedClip);
    QVERIFY(!unsupported.inserted());
    QVERIFY(unsupported.status == ClipInsertionStatus::UnsupportedKind);
    QVERIFY(directService.lastStatus() == ClipInsertionStatus::UnsupportedKind);

    FakeClipboardTextAccessor writeClipboard;
    writeClipboard.setFailSetText(true);
    ClipInsertionService writeService(&writeClipboard, repository, []() {
        return true;
    });

    const ClipInsertionResult writeFailed = writeService.insertClip(captured.clip->id);
    QVERIFY(!writeFailed.inserted());
    QVERIFY(writeFailed.status == ClipInsertionStatus::ClipboardWriteFailed);
    QVERIFY(writeService.lastStatus() == ClipInsertionStatus::ClipboardWriteFailed);
    QVERIFY(!writeService.lastError().isEmpty());
}

void ClipTest::insertionServiceInsertsSqliteTemporaryAndSavedClips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteClipRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom_clip.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("SQLite temporary insert"), {}, {}, now);
    QVERIFY2(temporary.captured(), qPrintable(repository.lastError()));
    QVERIFY(temporary.clip.has_value());

    const ClipCaptureResult saved = repository.captureText(QStringLiteral("SQLite saved insert"), {}, {}, now.addSecs(1));
    QVERIFY2(saved.captured(), qPrintable(repository.lastError()));
    QVERIFY(saved.clip.has_value());
    QVERIFY2(repository.saveClip(saved.clip->id, QStringLiteral("Saved insert"), {}, {}, false, now.addSecs(2)),
             qPrintable(repository.lastError()));

    FakeClipboardTextAccessor clipboard;
    int pasteCalls = 0;
    ClipInsertionService service(&clipboard, repository, [&]() {
        ++pasteCalls;
        return true;
    });
    ClipInsertionOptions options;
    options.restoreOriginalClipboardOnSuccess = false;
    service.setOptions(options);

    const ClipInsertionResult temporaryResult = service.insertClip(temporary.clip->id);
    QVERIFY2(temporaryResult.inserted(), qPrintable(temporaryResult.error));
    QCOMPARE(clipboard.text(), QStringLiteral("SQLite temporary insert"));

    const ClipInsertionResult savedResult = service.insertClip(saved.clip->id);
    QVERIFY2(savedResult.inserted(), qPrintable(savedResult.error));
    QCOMPARE(clipboard.text(), QStringLiteral("SQLite saved insert"));

    QCOMPARE(pasteCalls, 2);
    QCOMPARE(service.lastInsertedId(), saved.clip->id);
    QVERIFY(service.lastStatus() == ClipInsertionStatus::Inserted);

    const std::optional<Clip> usedTemporary = repository.findClip(temporary.clip->id);
    QVERIFY(usedTemporary.has_value());
    QVERIFY(usedTemporary->usedAt != temporary.clip->usedAt);

    const std::optional<Clip> usedSaved = repository.findClip(saved.clip->id);
    QVERIFY(usedSaved.has_value());
    QVERIFY(usedSaved->usedAt != saved.clip->usedAt);
    QVERIFY(usedSaved->state == ClipState::Saved);
}

QTEST_MAIN(ClipTest)

#include "clip_test.moc"
