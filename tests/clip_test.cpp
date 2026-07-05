#include "pinloom/clip/ClipboardCaptureService.h"
#include "pinloom/clip/ClipArchive.h"
#include "pinloom/clip/ClipHotkeyService.h"
#include "pinloom/clip/ClipInsertionService.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/clip/ClipTrayController.h"
#include "pinloom/clip/PlatformPasteInvoker.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
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

class FakeClipHotkeyBackend : public ClipHotkeyBackend {
public:
    bool isAvailable() const override
    {
        return available_;
    }

    bool registerHotkey(const ClipHotkeyConfig &config, QString *error) override
    {
        ++registerCalls_;
        registeredConfig_ = config;
        if (!registerResult_) {
            if (error) {
                *error = registerError_;
            }
            return false;
        }

        registered_ = true;
        if (error) {
            error->clear();
        }
        return true;
    }

    void unregisterHotkey() override
    {
        ++unregisterCalls_;
        registered_ = false;
    }

    void setAvailable(bool available)
    {
        available_ = available;
    }

    void setRegisterResult(bool registerResult, const QString &error)
    {
        registerResult_ = registerResult;
        registerError_ = error;
    }

    void activate()
    {
        emit hotkeyActivated();
    }

    int registerCalls() const
    {
        return registerCalls_;
    }

    int unregisterCalls() const
    {
        return unregisterCalls_;
    }

    bool registered() const
    {
        return registered_;
    }

    ClipHotkeyConfig registeredConfig() const
    {
        return registeredConfig_;
    }

private:
    bool available_ = true;
    bool registerResult_ = true;
    bool registered_ = false;
    int registerCalls_ = 0;
    int unregisterCalls_ = 0;
    QString registerError_ = QStringLiteral("fake hotkey registration failed");
    ClipHotkeyConfig registeredConfig_;
};

void verifyCtrlVPasteSequence(const PasteKeySequence &sequence)
{
    const PasteKeySequence expected = PlatformPasteInvoker::ctrlVPasteSequence();
    QCOMPARE(sequence.size(), expected.size());
    for (qsizetype index = 0; index < expected.size(); ++index) {
        QVERIFY(sequence.at(index) == expected.at(index));
    }
}

std::optional<ClipTrayAction> trayActionById(const QList<ClipTrayAction> &actions, const QString &id)
{
    const auto it = std::find_if(actions.cbegin(), actions.cend(), [&](const ClipTrayAction &action) {
        return action.id == id;
    });
    if (it == actions.cend()) {
        return std::nullopt;
    }
    return *it;
}

QString saveInMemoryClip(InMemoryClipRepository &repository,
                         const QString &text,
                         const QString &name,
                         const QStringList &aliases,
                         const QStringList &tags,
                         bool pinned,
                         const QDateTime &capturedAt,
                         const QDateTime &savedAt)
{
    const ClipCaptureResult captured = repository.captureText(text, {}, {}, capturedAt);
    if (!captured.captured() || !captured.clip.has_value()) {
        return {};
    }

    if (!repository.saveClip(captured.clip->id, name, aliases, tags, pinned, savedAt)) {
        return {};
    }

    return captured.clip->id;
}

bool writeClipTestFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    return file.write(content) == static_cast<qint64>(content.size());
}

QByteArray readClipTestFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return file.readAll();
}

class ClipTest : public QObject {
    Q_OBJECT

private slots:
    void ignoresBlankText();
    void rejectsTextOverSizeLimit();
    void filtersDefaultSensitiveTextInMemory();
    void sqliteFiltersSensitiveTextBeforePersistence();
    void supportsCustomSensitiveTextMarkersAndDefaultOptOut();
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
    void clipSearchRanksExactSavedNameFirst();
    void clipSearchFindsAliasTagHashTagPreviewAndText();
    void clipSearchUsesPinnedAndRecentForStableOrdering();
    void clipSearchDefaultsToSavedOnlyAndCanIncludeTemporary();
    void clipSearchEmptyQueryReturnsPinnedThenRecentSavedClips();
    void sqliteSearchesSavedClipAfterRepositoryRestart();
    void clipArchiveExportsSavedOnly();
    void clipArchiveImportsIntoEmptyRepositoryAndSearchesMetadata();
    void clipArchiveSkipsExistingIdOnRepeatedImport();
    void clipArchiveImportsSameTextWithDifferentIds();
    void clipArchiveRejectsInvalidJsonAndSchemaVersion();
    void clipboardServiceCapturesTextIntoRepository();
    void clipboardServicePauseAndResumeCapture();
    void clipboardServiceUsesRepositoryPolicyForIgnoredText();
    void clipboardServiceSuppressesNextChange();
    void clipboardServicePersistsCapturedTextWithSqliteRepository();
    void hotkeyConfigDefaultsToCtrlShiftV();
    void hotkeyServiceRegistersWithFakeBackend();
    void hotkeyServiceActivationEmitsSignalAndHandler();
    void hotkeyServiceStopUnregisters();
    void hotkeyServiceReportsRegisterFailure();
    void hotkeyServiceDuplicateStartStopIsStable();
    void clipPickerHotkeyControllerRequestsShow();
    void trayControllerStartsHotkeyRuntime();
    void trayControllerHotkeyActivationShowsPicker();
    void trayControllerManualShowUsesSamePath();
    void trayControllerHotkeyStartFailureDoesNotShowPicker();
    void trayControllerPauseResumeToggleUpdatesActionsAndSignals();
    void trayControllerQuitRequestEmitsSignal();
    void trayControllerStopStopsHotkeyRuntime();
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

void ClipTest::filtersDefaultSensitiveTextInMemory()
{
    InMemoryClipRepository repository;

    const ClipCaptureResult password = repository.captureText(QStringLiteral("password=hunter2"));
    QVERIFY(!password.captured());
    QVERIFY(password.status == ClipCaptureStatus::IgnoredSensitiveContent);

    const ClipCaptureResult bearer =
        repository.captureText(QStringLiteral("Authorization: Bearer abc.def.ghi"));
    QVERIFY(!bearer.captured());
    QVERIFY(bearer.status == ClipCaptureStatus::IgnoredSensitiveContent);

    const ClipCaptureResult privateKey =
        repository.captureText(QStringLiteral("-----BEGIN PRIVATE KEY-----\nnot-real-test-key"));
    QVERIFY(!privateKey.captured());
    QVERIFY(privateKey.status == ClipCaptureStatus::IgnoredSensitiveContent);

    const ClipCaptureResult normal = repository.captureText(QStringLiteral("Release note: refresh dashboard cache"));
    QVERIFY(normal.captured());
    QCOMPARE(repository.clips().size(), 1);
    QCOMPARE(repository.clips().first().text, QStringLiteral("Release note: refresh dashboard cache"));
}

void ClipTest::sqliteFiltersSensitiveTextBeforePersistence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));

    {
        SqliteClipRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        const ClipCaptureResult sensitive = repository.captureText(QStringLiteral("api_key=abc123"));
        QVERIFY(!sensitive.captured());
        QVERIFY(sensitive.status == ClipCaptureStatus::IgnoredSensitiveContent);
        QVERIFY(repository.clips().isEmpty());
    }

    SqliteClipRepository restarted;
    QVERIFY2(restarted.open(databasePath), qPrintable(restarted.lastError()));
    QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));
    QVERIFY(restarted.clips().isEmpty());
}

void ClipTest::supportsCustomSensitiveTextMarkersAndDefaultOptOut()
{
    ClipCapturePolicy customPolicy;
    customPolicy.sensitiveTextMarkers = {QStringLiteral(" Internal-Only ")};

    InMemoryClipRepository customRepository;
    const ClipCaptureResult custom =
        customRepository.captureText(QStringLiteral("Share INTERNAL-only launch note"), customPolicy);
    QVERIFY(!custom.captured());
    QVERIFY(custom.status == ClipCaptureStatus::IgnoredSensitiveContent);
    QVERIFY(customRepository.clips().isEmpty());

    ClipCapturePolicy optOutPolicy;
    optOutPolicy.excludeSensitiveText = false;

    InMemoryClipRepository optOutRepository;
    const ClipCaptureResult captured = optOutRepository.captureText(QStringLiteral("password=hunter2"), optOutPolicy);
    QVERIFY(captured.captured());
    QVERIFY(captured.status == ClipCaptureStatus::Captured);
    QCOMPARE(optOutRepository.clips().size(), 1);
    QCOMPARE(optOutRepository.clips().first().text, QStringLiteral("password=hunter2"));
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

void ClipTest::clipSearchRanksExactSavedNameFirst()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const QString nameId = saveInMemoryClip(repository,
                                            QStringLiteral("Text body for the exact-name deploy snippet"),
                                            QStringLiteral("Deploy snippet"),
                                            {},
                                            {},
                                            false,
                                            base,
                                            base.addSecs(1));
    const QString aliasId = saveInMemoryClip(repository,
                                             QStringLiteral("Alias body for deploy snippet"),
                                             QStringLiteral("Alias holder"),
                                             {QStringLiteral("Deploy snippet")},
                                             {},
                                             false,
                                             base.addSecs(2),
                                             base.addSecs(3));
    const QString tagId = saveInMemoryClip(repository,
                                           QStringLiteral("Tagged body for deploy snippet"),
                                           QStringLiteral("Tag holder"),
                                           {},
                                           {QStringLiteral("Deploy snippet")},
                                           false,
                                           base.addSecs(4),
                                           base.addSecs(5));
    const QString textId = saveInMemoryClip(repository,
                                            QStringLiteral("This body contains Deploy snippet as plain text"),
                                            QStringLiteral("Text holder"),
                                            {},
                                            {},
                                            false,
                                            base.addSecs(6),
                                            base.addSecs(7));
    QVERIFY(!nameId.isEmpty());
    QVERIFY(!aliasId.isEmpty());
    QVERIFY(!tagId.isEmpty());
    QVERIFY(!textId.isEmpty());

    const QList<ClipSearchResult> results = ClipSearchService(repository).search(QStringLiteral("Deploy snippet"));

    QCOMPARE(results.size(), 4);
    QCOMPARE(results.first().clipId, nameId);
    QCOMPARE(results.first().matchedField, QStringLiteral("name"));
    QCOMPARE(results.first().rank, 1);
    QVERIFY(results.first().score > results.at(1).score);
    QCOMPARE(results.at(1).clipId, aliasId);
    QCOMPARE(results.at(1).matchedField, QStringLiteral("alias"));
    QCOMPARE(results.at(2).clipId, tagId);
    QCOMPARE(results.at(2).matchedField, QStringLiteral("tag"));
}

void ClipTest::clipSearchFindsAliasTagHashTagPreviewAndText()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const QString metadataId = saveInMemoryClip(repository,
                                                QStringLiteral("Metadata clip body"),
                                                QStringLiteral("Metadata holder"),
                                                {QStringLiteral("quick alias")},
                                                {QStringLiteral("ops")},
                                                false,
                                                base,
                                                base.addSecs(1));
    const QString previewId = saveInMemoryClip(repository,
                                               QStringLiteral("Secret launch preview text for picker search"),
                                               QStringLiteral("Preview holder"),
                                               {},
                                               {},
                                               false,
                                               base.addSecs(2),
                                               base.addSecs(3));
    const QString textId = saveInMemoryClip(
        repository,
        QStringLiteral("This clip begins with a deliberately long preview sentence that will be truncated before the "
                       "hidden body needle appears near the end. body-needle-value"),
        QStringLiteral("Body holder"),
        {},
        {},
        false,
        base.addSecs(4),
        base.addSecs(5));
    QVERIFY(!metadataId.isEmpty());
    QVERIFY(!previewId.isEmpty());
    QVERIFY(!textId.isEmpty());

    const ClipSearchService search(repository);

    QList<ClipSearchResult> results = search.search(QStringLiteral("quick alias"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, metadataId);
    QCOMPARE(results.first().matchedField, QStringLiteral("alias"));

    results = search.search(QStringLiteral("ops"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, metadataId);
    QCOMPARE(results.first().matchedField, QStringLiteral("tag"));

    results = search.search(QStringLiteral("#ops"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, metadataId);
    QCOMPARE(results.first().matchedField, QStringLiteral("tag"));

    results = search.search(QStringLiteral("secret launch"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, previewId);
    QCOMPARE(results.first().matchedField, QStringLiteral("preview"));

    results = search.search(QStringLiteral("body-needle-value"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, textId);
    QCOMPARE(results.first().matchedField, QStringLiteral("text"));
}

void ClipTest::clipSearchUsesPinnedAndRecentForStableOrdering()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const QString pinnedId = saveInMemoryClip(repository,
                                              QStringLiteral("shared needle pinned result"),
                                              QStringLiteral("Pinned result"),
                                              {},
                                              {},
                                              true,
                                              base,
                                              base.addSecs(1));
    const QString recentId = saveInMemoryClip(repository,
                                              QStringLiteral("shared needle recent result"),
                                              QStringLiteral("Recent result"),
                                              {},
                                              {},
                                              false,
                                              base.addSecs(2),
                                              base.addSecs(3));
    const QString staleId = saveInMemoryClip(repository,
                                             QStringLiteral("shared needle stale result"),
                                             QStringLiteral("Stale result"),
                                             {},
                                             {},
                                             false,
                                             base.addSecs(4),
                                             base.addSecs(5));
    QVERIFY(!pinnedId.isEmpty());
    QVERIFY(!recentId.isEmpty());
    QVERIFY(!staleId.isEmpty());

    QVERIFY(repository.markClipUsed(staleId, base.addSecs(10)));
    QVERIFY(repository.markClipUsed(recentId, base.addSecs(100)));

    const QList<ClipSearchResult> results = ClipSearchService(repository).search(QStringLiteral("shared needle"));

    QCOMPARE(results.size(), 3);
    QCOMPARE(results.at(0).clipId, pinnedId);
    QVERIFY(results.at(0).pinned);
    QCOMPARE(results.at(1).clipId, recentId);
    QCOMPARE(results.at(2).clipId, staleId);
    QCOMPARE(results.at(0).rank, 1);
    QCOMPARE(results.at(1).rank, 2);
    QCOMPARE(results.at(2).rank, 3);
}

void ClipTest::clipSearchDefaultsToSavedOnlyAndCanIncludeTemporary()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary =
        repository.captureText(QStringLiteral("temporary picker needle"), {}, {}, base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    const QString savedId = saveInMemoryClip(repository,
                                             QStringLiteral("saved picker needle"),
                                             QStringLiteral("Saved picker needle"),
                                             {},
                                             {},
                                             false,
                                             base.addSecs(1),
                                             base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    const ClipSearchService search(repository);
    QList<ClipSearchResult> results = search.search(QStringLiteral("picker needle"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, savedId);
    QVERIFY(results.first().state == ClipState::Saved);

    ClipSearchOptions options;
    options.includeTemporary = true;
    results = search.search(QStringLiteral("picker needle"), options);
    QCOMPARE(results.size(), 2);
    QVERIFY(std::any_of(results.cbegin(), results.cend(), [&](const ClipSearchResult &result) {
        return result.clipId == temporary.clip->id && result.state == ClipState::Temporary;
    }));
    QVERIFY(std::any_of(results.cbegin(), results.cend(), [&](const ClipSearchResult &result) {
        return result.clipId == savedId && result.state == ClipState::Saved;
    }));
}

void ClipTest::clipSearchEmptyQueryReturnsPinnedThenRecentSavedClips()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("temporary empty-query text"),
                                                               {},
                                                               {},
                                                               base);
    QVERIFY(temporary.captured());

    const QString pinnedId = saveInMemoryClip(repository,
                                              QStringLiteral("empty query pinned"),
                                              QStringLiteral("Pinned empty"),
                                              {},
                                              {},
                                              true,
                                              base.addSecs(1),
                                              base.addSecs(2));
    const QString recentId = saveInMemoryClip(repository,
                                              QStringLiteral("empty query recent"),
                                              QStringLiteral("Recent empty"),
                                              {},
                                              {},
                                              false,
                                              base.addSecs(3),
                                              base.addSecs(4));
    const QString staleId = saveInMemoryClip(repository,
                                             QStringLiteral("empty query stale"),
                                             QStringLiteral("Stale empty"),
                                             {},
                                             {},
                                             false,
                                             base.addSecs(5),
                                             base.addSecs(6));
    QVERIFY(!pinnedId.isEmpty());
    QVERIFY(!recentId.isEmpty());
    QVERIFY(!staleId.isEmpty());
    QVERIFY(repository.markClipUsed(staleId, base.addSecs(10)));
    QVERIFY(repository.markClipUsed(recentId, base.addSecs(100)));

    const QList<ClipSearchResult> results = ClipSearchService(repository).search(QString());

    QCOMPARE(results.size(), 3);
    QCOMPARE(results.at(0).clipId, pinnedId);
    QCOMPARE(results.at(0).matchedField, QStringLiteral("empty"));
    QCOMPARE(results.at(1).clipId, recentId);
    QCOMPARE(results.at(2).clipId, staleId);
    QVERIFY(std::none_of(results.cbegin(), results.cend(), [&](const ClipSearchResult &result) {
        return result.clipId == temporary.clip->id;
    }));
}

void ClipTest::sqliteSearchesSavedClipAfterRepositoryRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    QString clipId;
    {
        SqliteClipRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        const ClipCaptureResult captured = repository.captureText(
            QStringLiteral("SQLite clip starts with enough ordinary text to make the preview trim before the searchable "
                           "persisted body token appears near the end. sqlite-body-token"),
            {},
            {},
            base);
        QVERIFY2(captured.captured(), qPrintable(repository.lastError()));
        QVERIFY(captured.clip.has_value());
        clipId = captured.clip->id;
        QVERIFY2(repository.saveClip(clipId,
                                     QStringLiteral("SQLite Search Clip"),
                                     {QStringLiteral("sql alias")},
                                     {QStringLiteral("database")},
                                     true,
                                     base.addSecs(1)),
                 qPrintable(repository.lastError()));
    }

    SqliteClipRepository restarted;
    QVERIFY2(restarted.open(databasePath), qPrintable(restarted.lastError()));
    QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));

    const ClipSearchService search(restarted);
    QList<ClipSearchResult> results = search.search(QStringLiteral("SQLite Search Clip"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, clipId);
    QCOMPARE(results.first().matchedField, QStringLiteral("name"));

    results = search.search(QStringLiteral("sql alias"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, clipId);
    QCOMPARE(results.first().matchedField, QStringLiteral("alias"));

    results = search.search(QStringLiteral("database"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, clipId);
    QCOMPARE(results.first().matchedField, QStringLiteral("tag"));

    results = search.search(QStringLiteral("#database"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, clipId);
    QCOMPARE(results.first().matchedField, QStringLiteral("tag"));

    results = search.search(QStringLiteral("sqlite-body-token"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, clipId);
    QCOMPARE(results.first().matchedField, QStringLiteral("text"));
}

void ClipTest::clipArchiveExportsSavedOnly()
{
    InMemoryClipRepository repository;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary =
        repository.captureText(QStringLiteral("temporary private archive text"), {}, {}, base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    const QString savedId = saveInMemoryClip(repository,
                                             QStringLiteral("portable saved archive text"),
                                             QStringLiteral("Portable Saved"),
                                             {QStringLiteral("portable alias")},
                                             {QStringLiteral("archive")},
                                             true,
                                             base.addSecs(1),
                                             base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    const QString archivePath = dir.filePath(QStringLiteral("saved-clips.json"));
    const ClipArchiveResult exported = ClipArchive(repository).exportSavedClips(archivePath);

    QVERIFY2(exported.succeeded(), qPrintable(exported.error));
    QCOMPARE(exported.exported, 1);

    const QByteArray archiveBytes = readClipTestFile(archivePath);
    QVERIFY(!archiveBytes.isEmpty());
    QVERIFY(!archiveBytes.contains("temporary private archive text"));

    const QJsonDocument document = QJsonDocument::fromJson(archiveBytes);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("schema")).toString(), ClipArchive::schemaName());
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), ClipArchive::schemaVersion());
    QCOMPARE(root.value(QStringLiteral("temporaryHistoryExported")).toBool(true), false);

    const QJsonArray clips = root.value(QStringLiteral("clips")).toArray();
    QCOMPARE(clips.size(), 1);
    const QJsonObject saved = clips.first().toObject();
    QCOMPARE(saved.value(QStringLiteral("id")).toString(), savedId);
    QCOMPARE(saved.value(QStringLiteral("state")).toString(), QStringLiteral("saved"));
    QCOMPARE(saved.value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
    QCOMPARE(saved.value(QStringLiteral("text")).toString(), QStringLiteral("portable saved archive text"));
    QCOMPARE(saved.value(QStringLiteral("savedName")).toString(), QStringLiteral("Portable Saved"));
}

void ClipTest::clipArchiveImportsIntoEmptyRepositoryAndSearchesMetadata()
{
    InMemoryClipRepository source;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const QString sourceId = saveInMemoryClip(source,
                                             QStringLiteral("portable import body with metadata needle"),
                                             QStringLiteral("Portable Import"),
                                             {QStringLiteral("carry alias")},
                                             {QStringLiteral("portable-tag")},
                                             true,
                                             base,
                                             base.addSecs(1));
    QVERIFY(!sourceId.isEmpty());

    const QString archivePath = dir.filePath(QStringLiteral("saved-clips.json"));
    const ClipArchiveResult exported = ClipArchive(source).exportSavedClips(archivePath);
    QVERIFY2(exported.succeeded(), qPrintable(exported.error));

    SqliteClipRepository imported;
    QVERIFY2(imported.open(dir.filePath(QStringLiteral("imported.sqlite3"))), qPrintable(imported.lastError()));
    QVERIFY2(imported.initialize(), qPrintable(imported.lastError()));

    const ClipArchiveResult importedResult = ClipArchive(imported).importSavedClips(archivePath);
    QVERIFY2(importedResult.succeeded(), qPrintable(importedResult.error));
    QCOMPARE(importedResult.imported, 1);
    QCOMPARE(importedResult.skippedConflictingIds, 0);
    QCOMPARE(imported.savedClips().size(), 1);
    QVERIFY(imported.temporaryClips().isEmpty());

    const ClipSearchService search(imported);
    QList<ClipSearchResult> results = search.search(QStringLiteral("Portable Import"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, sourceId);
    QCOMPARE(results.first().matchedField, QStringLiteral("name"));

    results = search.search(QStringLiteral("carry alias"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, sourceId);
    QCOMPARE(results.first().matchedField, QStringLiteral("alias"));

    results = search.search(QStringLiteral("#portable-tag"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, sourceId);
    QCOMPARE(results.first().matchedField, QStringLiteral("tag"));
}

void ClipTest::clipArchiveSkipsExistingIdOnRepeatedImport()
{
    InMemoryClipRepository source;
    InMemoryClipRepository imported;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const QString sourceId = saveInMemoryClip(source,
                                             QStringLiteral("original conflict text"),
                                             QStringLiteral("Original Conflict"),
                                             {},
                                             {QStringLiteral("conflict")},
                                             false,
                                             base,
                                             base.addSecs(1));
    QVERIFY(!sourceId.isEmpty());

    const QString archivePath = dir.filePath(QStringLiteral("saved-clips.json"));
    const ClipArchiveResult exported = ClipArchive(source).exportSavedClips(archivePath);
    QVERIFY2(exported.succeeded(), qPrintable(exported.error));

    ClipArchive importArchive(imported);
    ClipArchiveResult firstImport = importArchive.importSavedClips(archivePath);
    QVERIFY2(firstImport.succeeded(), qPrintable(firstImport.error));
    QCOMPARE(firstImport.imported, 1);
    QCOMPARE(firstImport.skippedConflictingIds, 0);

    QJsonDocument document = QJsonDocument::fromJson(readClipTestFile(archivePath));
    QVERIFY(document.isObject());
    QJsonObject root = document.object();
    QJsonArray clips = root.value(QStringLiteral("clips")).toArray();
    QCOMPARE(clips.size(), 1);
    QJsonObject conflictingClip = clips.first().toObject();
    conflictingClip.insert(QStringLiteral("savedName"), QStringLiteral("Changed Conflict"));
    conflictingClip.insert(QStringLiteral("text"), QStringLiteral("changed conflict text"));
    clips.replace(0, conflictingClip);
    root.insert(QStringLiteral("clips"), clips);
    QVERIFY(writeClipTestFile(archivePath, QJsonDocument(root).toJson(QJsonDocument::Indented)));

    ClipArchiveResult duplicateImport = importArchive.importSavedClips(archivePath);
    QVERIFY2(duplicateImport.succeeded(), qPrintable(duplicateImport.error));
    QCOMPARE(duplicateImport.imported, 0);
    QCOMPARE(duplicateImport.skippedConflictingIds, 1);
    QCOMPARE(imported.savedClips().size(), 1);

    const std::optional<Clip> stored = imported.findClip(sourceId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->name, QStringLiteral("Original Conflict"));
    QCOMPARE(stored->text, QStringLiteral("original conflict text"));
}

void ClipTest::clipArchiveImportsSameTextWithDifferentIds()
{
    InMemoryClipRepository repository;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonArray clips;
    QJsonObject first;
    first.insert(QStringLiteral("id"), QStringLiteral("same-text-one"));
    first.insert(QStringLiteral("kind"), QStringLiteral("text"));
    first.insert(QStringLiteral("state"), QStringLiteral("saved"));
    first.insert(QStringLiteral("text"), QStringLiteral("same portable text"));
    first.insert(QStringLiteral("savedName"), QStringLiteral("Same Text One"));
    first.insert(QStringLiteral("aliases"), QJsonArray{QStringLiteral("first alias")});
    first.insert(QStringLiteral("tags"), QJsonArray{QStringLiteral("portable")});
    first.insert(QStringLiteral("pinned"), false);
    clips.append(first);

    QJsonObject second = first;
    second.insert(QStringLiteral("id"), QStringLiteral("same-text-two"));
    second.insert(QStringLiteral("savedName"), QStringLiteral("Same Text Two"));
    second.insert(QStringLiteral("aliases"), QJsonArray{QStringLiteral("second alias")});
    clips.append(second);

    QJsonObject root;
    root.insert(QStringLiteral("schema"), ClipArchive::schemaName());
    root.insert(QStringLiteral("version"), ClipArchive::schemaVersion());
    root.insert(QStringLiteral("clips"), clips);

    const QString archivePath = dir.filePath(QStringLiteral("same-text.json"));
    QVERIFY(writeClipTestFile(archivePath, QJsonDocument(root).toJson(QJsonDocument::Indented)));

    const ClipArchiveResult result = ClipArchive(repository).importSavedClips(archivePath);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.imported, 2);
    QCOMPARE(result.skippedConflictingIds, 0);
    QCOMPARE(repository.savedClips().size(), 2);

    const std::optional<Clip> firstClip = repository.findClip(QStringLiteral("same-text-one"));
    const std::optional<Clip> secondClip = repository.findClip(QStringLiteral("same-text-two"));
    QVERIFY(firstClip.has_value());
    QVERIFY(secondClip.has_value());
    QCOMPARE(firstClip->text, QStringLiteral("same portable text"));
    QCOMPARE(secondClip->text, QStringLiteral("same portable text"));

    QList<ClipSearchResult> results = ClipSearchService(repository).search(QStringLiteral("same portable text"));
    QCOMPARE(results.size(), 2);
    QVERIFY(std::any_of(results.cbegin(), results.cend(), [](const ClipSearchResult &searchResult) {
        return searchResult.clipId == QStringLiteral("same-text-one");
    }));
    QVERIFY(std::any_of(results.cbegin(), results.cend(), [](const ClipSearchResult &searchResult) {
        return searchResult.clipId == QStringLiteral("same-text-two");
    }));
}

void ClipTest::clipArchiveRejectsInvalidJsonAndSchemaVersion()
{
    InMemoryClipRepository repository;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString invalidJsonPath = dir.filePath(QStringLiteral("invalid.json"));
    QVERIFY(writeClipTestFile(invalidJsonPath, QByteArray("{ broken json")));
    ClipArchiveResult result = ClipArchive(repository).importSavedClips(invalidJsonPath);
    QVERIFY(!result.succeeded());
    QVERIFY(result.error.contains(QStringLiteral("Invalid saved clip archive JSON")));

    const QString invalidSchemaPath = dir.filePath(QStringLiteral("invalid-schema.json"));
    QVERIFY(writeClipTestFile(invalidSchemaPath,
                              QByteArray(R"({"schema":"pinloom.clip.other","version":1,"clips":[]})")));
    result = ClipArchive(repository).importSavedClips(invalidSchemaPath);
    QVERIFY(!result.succeeded());
    QVERIFY(result.error.contains(QStringLiteral("Unsupported saved clip archive schema/version")));

    const QString invalidVersionPath = dir.filePath(QStringLiteral("invalid-version.json"));
    QVERIFY(writeClipTestFile(invalidVersionPath,
                              QByteArray(R"({"schema":"pinloom.clip.savedClips","version":2,"clips":[]})")));
    result = ClipArchive(repository).importSavedClips(invalidVersionPath);
    QVERIFY(!result.succeeded());
    QVERIFY(result.error.contains(QStringLiteral("Unsupported saved clip archive schema/version")));
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

void ClipTest::hotkeyConfigDefaultsToCtrlShiftV()
{
    const ClipHotkeyConfig config = defaultClipHotkeyConfig();

    QVERIFY(config.isValid());
    QCOMPARE(config.key, Qt::Key_V);
    QVERIFY(config.modifiers.testFlag(Qt::ControlModifier));
    QVERIFY(config.modifiers.testFlag(Qt::ShiftModifier));
    QCOMPARE(config.displayText(), QStringLiteral("Ctrl+Shift+V"));
}

void ClipTest::hotkeyServiceRegistersWithFakeBackend()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    QList<bool> registeredChanges;
    QObject::connect(&service, &ClipHotkeyService::registeredChanged, [&](bool registered) {
        registeredChanges.append(registered);
    });

    QVERIFY(service.start());

    QVERIFY(service.isRegistered());
    QVERIFY(backend.registered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 0);
    QVERIFY(backend.registeredConfig() == defaultClipHotkeyConfig());
    QCOMPARE(service.displayText(), QStringLiteral("Ctrl+Shift+V"));
    QVERIFY(service.lastError().isEmpty());
    QCOMPARE(registeredChanges, (QList<bool>{true}));
}

void ClipTest::hotkeyServiceActivationEmitsSignalAndHandler()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    int signalCount = 0;
    int handlerCount = 0;
    QObject::connect(&service, &ClipHotkeyService::activated, [&]() {
        ++signalCount;
    });
    service.setActivationHandler([&]() {
        ++handlerCount;
    });

    QVERIFY(service.start());
    backend.activate();

    QCOMPARE(handlerCount, 1);
    QCOMPARE(signalCount, 1);

    service.stop();
    backend.activate();

    QCOMPARE(handlerCount, 1);
    QCOMPARE(signalCount, 1);
}

void ClipTest::hotkeyServiceStopUnregisters()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    QList<bool> registeredChanges;
    QObject::connect(&service, &ClipHotkeyService::registeredChanged, [&](bool registered) {
        registeredChanges.append(registered);
    });

    QVERIFY(service.start());
    service.stop();

    QVERIFY(!service.isRegistered());
    QVERIFY(!backend.registered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 1);
    QCOMPARE(registeredChanges, (QList<bool>{true, false}));
}

void ClipTest::hotkeyServiceReportsRegisterFailure()
{
    FakeClipHotkeyBackend backend;
    backend.setRegisterResult(false, QStringLiteral("fake hotkey already registered"));
    ClipHotkeyService service(&backend);

    QVERIFY(!service.start());

    QVERIFY(!service.isRegistered());
    QVERIFY(!backend.registered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 0);
    QCOMPARE(service.lastError(), QStringLiteral("fake hotkey already registered"));
}

void ClipTest::hotkeyServiceDuplicateStartStopIsStable()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);

    QVERIFY(service.start());
    QVERIFY(service.start());

    QVERIFY(service.isRegistered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 0);

    service.stop();
    service.stop();

    QVERIFY(!service.isRegistered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 1);
}

void ClipTest::clipPickerHotkeyControllerRequestsShow()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    int showHandlerCount = 0;
    ClipPickerHotkeyController controller(service, [&]() {
        ++showHandlerCount;
    });
    int showSignalCount = 0;
    QObject::connect(&controller, &ClipPickerHotkeyController::showRequested, [&]() {
        ++showSignalCount;
    });

    QVERIFY(service.start());
    backend.activate();

    QCOMPARE(showHandlerCount, 1);
    QCOMPARE(showSignalCount, 1);
}

void ClipTest::trayControllerStartsHotkeyRuntime()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    ClipTrayController controller(service);
    QList<bool> runningSignals;
    QObject::connect(&controller, &ClipTrayController::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });

    QVERIFY(controller.start());

    QVERIFY(controller.isRunning());
    QVERIFY(service.isRegistered());
    QVERIFY(backend.registered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 0);
    QVERIFY(controller.lastError().isEmpty());
    QCOMPARE(controller.status(), QStringLiteral("Running"));
    QCOMPARE(runningSignals, (QList<bool>{true}));
}

void ClipTest::trayControllerHotkeyActivationShowsPicker()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    int showHandlerCount = 0;
    ClipTrayControllerOptions options;
    options.showPickerHandler = [&]() {
        ++showHandlerCount;
    };
    ClipTrayController controller(service, options);
    int showSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::showPickerRequested, [&]() {
        ++showSignalCount;
    });

    QVERIFY(controller.start());
    backend.activate();

    QCOMPARE(showHandlerCount, 1);
    QCOMPARE(showSignalCount, 1);
    QCOMPARE(controller.pickerShownCount(), 1);
}

void ClipTest::trayControllerManualShowUsesSamePath()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    int showHandlerCount = 0;
    ClipTrayControllerOptions options;
    options.showPickerHandler = [&]() {
        ++showHandlerCount;
    };
    ClipTrayController controller(service, options);
    int showSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::showPickerRequested, [&]() {
        ++showSignalCount;
    });

    controller.requestShowPicker();

    QCOMPARE(showHandlerCount, 1);
    QCOMPARE(showSignalCount, 1);
    QCOMPARE(controller.pickerShownCount(), 1);

    QVERIFY(controller.triggerAction(QStringLiteral("show_picker")));
    QCOMPARE(showHandlerCount, 2);
    QCOMPARE(showSignalCount, 2);
    QCOMPARE(controller.pickerShownCount(), 2);
}

void ClipTest::trayControllerHotkeyStartFailureDoesNotShowPicker()
{
    FakeClipHotkeyBackend backend;
    backend.setRegisterResult(false, QStringLiteral("fake hotkey already registered"));
    ClipHotkeyService service(&backend);
    int showHandlerCount = 0;
    ClipTrayControllerOptions options;
    options.showPickerHandler = [&]() {
        ++showHandlerCount;
    };
    ClipTrayController controller(service, options);
    int showSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::showPickerRequested, [&]() {
        ++showSignalCount;
    });

    QVERIFY(!controller.start());
    backend.activate();

    QVERIFY(!controller.isRunning());
    QVERIFY(!service.isRegistered());
    QVERIFY(!backend.registered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(controller.lastError(), QStringLiteral("fake hotkey already registered"));
    QCOMPARE(controller.status(), QStringLiteral("Stopped: fake hotkey already registered"));
    QCOMPARE(showHandlerCount, 0);
    QCOMPARE(showSignalCount, 0);
    QCOMPARE(controller.pickerShownCount(), 0);
}

void ClipTest::trayControllerPauseResumeToggleUpdatesActionsAndSignals()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    QList<bool> pausedHandlerStates;
    ClipTrayControllerOptions options;
    options.capturePausedHandler = [&](bool paused) {
        pausedHandlerStates.append(paused);
    };
    ClipTrayController controller(service, options);
    QList<bool> pausedSignals;
    int actionsChanged = 0;
    QObject::connect(&controller, &ClipTrayController::capturePausedChanged, [&](bool paused) {
        pausedSignals.append(paused);
    });
    QObject::connect(&controller, &ClipTrayController::trayActionsChanged, [&]() {
        ++actionsChanged;
    });

    std::optional<ClipTrayAction> toggleAction =
        trayActionById(controller.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Pause Capture"));
    QVERIFY(!toggleAction->checked);

    controller.pauseCapture();

    QVERIFY(controller.capturePaused());
    toggleAction = trayActionById(controller.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Resume Capture"));
    QVERIFY(toggleAction->checked);

    controller.resumeCapture();
    QVERIFY(!controller.capturePaused());
    toggleAction = trayActionById(controller.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Pause Capture"));
    QVERIFY(!toggleAction->checked);

    controller.toggleCapturePaused();
    QVERIFY(controller.capturePaused());
    QVERIFY(controller.triggerAction(QStringLiteral("toggle_capture")));
    QVERIFY(!controller.capturePaused());

    QCOMPARE(pausedHandlerStates, (QList<bool>{true, false, true, false}));
    QCOMPARE(pausedSignals, (QList<bool>{true, false, true, false}));
    QCOMPARE(actionsChanged, 4);
}

void ClipTest::trayControllerQuitRequestEmitsSignal()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    ClipTrayController controller(service);
    int quitSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::quitRequested, [&]() {
        ++quitSignalCount;
    });

    controller.requestQuit();
    QVERIFY(controller.triggerAction(QStringLiteral("quit")));

    QCOMPARE(quitSignalCount, 2);
}

void ClipTest::trayControllerStopStopsHotkeyRuntime()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(&backend);
    ClipTrayController controller(service);
    QList<bool> runningSignals;
    QObject::connect(&controller, &ClipTrayController::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });

    QVERIFY(controller.start());
    controller.stop();
    controller.stop();

    QVERIFY(!controller.isRunning());
    QVERIFY(!service.isRegistered());
    QVERIFY(!backend.registered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 1);
    QCOMPARE(controller.status(), QStringLiteral("Stopped"));
    QCOMPARE(runningSignals, (QList<bool>{true, false}));
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
