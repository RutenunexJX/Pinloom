#include "pinloom/clip/ClipboardCaptureService.h"
#include "pinloom/clip/ClipAction.h"
#include "pinloom/clip/ClipHotkeyService.h"
#include "pinloom/clip/HyperHotkeyService.h"
#include "pinloom/clip/ClipInsertionService.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include "pinloom/clip/PersistentClipService.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ClipRepositoryBackup.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/clip/ClipTrayController.h"
#include "pinloom/clip/PlatformPasteInvoker.h"
#include "pinloom/core/TextSelectionCapture.h"

#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>
#include <cstring>
#include <optional>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

ClipHotkeyConfig testHotkeyConfig()
{
    ClipHotkeyConfig config;
    config.key = Qt::Key_F8;
    config.modifiers = Qt::ControlModifier | Qt::AltModifier;
    return config;
}

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

#ifdef Q_OS_WIN

constexpr UINT TestSciGetCurrentPos = 2008;
constexpr UINT TestSciGetCodePage = 2137;
constexpr UINT TestSciGetSelectionStart = 2143;
constexpr UINT TestSciGetSelectionEnd = 2145;
constexpr UINT TestSciGetSelectedText = 2161;
constexpr UINT TestSciPointXFromPosition = 2164;
constexpr UINT TestSciPointYFromPosition = 2165;
constexpr char TestScintillaSelection[] = "keyboard-selected text";

LRESULT CALLBACK testScintillaWindowProcedure(HWND window,
                                              UINT message,
                                              WPARAM wordParameter,
                                              LPARAM longParameter)
{
    switch (message) {
    case TestSciGetCurrentPos:
    case TestSciGetSelectionEnd:
        return static_cast<LRESULT>(std::strlen(TestScintillaSelection));
    case TestSciGetSelectionStart:
        return 0;
    case TestSciGetCodePage:
        return 65001;
    case TestSciPointXFromPosition:
        return 24;
    case TestSciPointYFromPosition:
        return 36;
    case TestSciGetSelectedText:
        if (longParameter == 0) {
            return static_cast<LRESULT>(std::strlen(TestScintillaSelection));
        }
        std::memcpy(reinterpret_cast<void *>(longParameter),
                    TestScintillaSelection,
                    sizeof(TestScintillaSelection));
        return static_cast<LRESULT>(std::strlen(TestScintillaSelection));
    default:
        return DefWindowProcW(window, message, wordParameter, longParameter);
    }
}

#endif

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
    void sqliteMigratesLegacyPersistentClipSchema();
    void sqliteBacksUpDatabaseAndRetainsNewestCopies();
    void sqlitePrunesTemporaryHistoryPersistently();
    void sqliteCreatesSavedClipLocatorAnchorAfterRestart();
    void clipSearchRanksExactSavedNameFirst();
    void clipSearchFindsAliasTagHashTagPreviewAndText();
    void clipIdentitySearchUsesTagSemicolonAndNameAliasOnly();
    void sqliteIndexedSearchMatchesReferenceSearch();
    void rejectsDuplicateSavedClipNamesAndAliases();
    void wbTagResolvesDefaultBrowserUrl();
    void clipSearchUsesPinnedAndRecentForStableOrdering();
    void clipSearchDefaultsToSavedOnlyAndCanIncludeTemporary();
    void softDeletesSavedClipsAndRestoresSearch();
    void clipSearchEmptyQueryWithTemporaryHistoryHidesSavedClips();
    void clipSearchEmptyQueryReturnsPinnedThenRecentSavedClips();
    void savedClipContentCaptureIsIgnoredAsDuplicate();
    void sqliteSearchesSavedClipAfterRepositoryRestart();
    void obsidianStoreRoundTripsManagedMarkdown();
    void obsidianSyncImportsExternalEditsAndTracksRename();
    void obsidianSyncSoftDeletesMissingNotes();
    void obsidianStateAndForgetTombstoneRoundTrip();
    void persistentClipServiceCoordinatesRepositoryAndObsidian();
    void obsidianWatcherSynchronizesNewNotes();
    void obsidianRealVaultRoundTrip();
    void clipboardServiceCapturesTextIntoRepository();
    void clipboardServiceUsesDynamicForegroundSource();
    void clipboardServicePauseAndResumeCapture();
    void clipboardServiceUsesRepositoryPolicyForIgnoredText();
    void clipboardServiceSuppressesNextChange();
    void clipboardServicePersistsCapturedTextWithSqliteRepository();
    void hotkeyServiceRegistersWithFakeBackend();
    void hotkeyServiceActivationEmitsSignal();
    void hotkeyServiceStopUnregisters();
    void hotkeyServiceReportsRegisterFailure();
    void hotkeyServiceDuplicateStartStopIsStable();
    void hyperHotkeyStateMachineActivatesOnceAndConsumesTrigger();
    void hyperHotkeyStateMachineAcceptsF24AsCarrier();
    void hyperHotkeyStateMachineEmitsInsertAction();
    void hyperHotkeyStateMachineRejectsIncompleteChord();
    void textSelectionCaptureServiceUsesProvider();
    void foregroundTextTargetExpiresAfterConfiguredAge();
#ifdef Q_OS_WIN
    void capturesKeyboardSelectionFromScintillaControl();
#endif
    void trayControllerStartsAndStops();
    void trayControllerShowClipboardActionEmitsSignal();
    void trayControllerPauseResumeToggleUpdatesActionsAndSignals();
    void trayControllerQuitRequestEmitsSignal();
    void platformPasteInvokerSendsCtrlVSequence();
    void platformPasteInvokerReportsSenderFailure();
    void platformPasteInvokerReportsUnavailableSender();
#ifndef Q_OS_WIN
    void platformPasteInvokerDefaultFallbackReturnsFalse();
#endif
    void insertionServiceInsertsTextById();
    void insertionServiceSuppressesCaptureBeforeOwnClipboardWrites();
    void insertionServiceRestoresOriginalClipboardOnSuccess();
    void insertionServiceDefersClipboardRestore();
    void insertionServiceDoesNotOverwriteNewClipboardContent();
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

void ClipTest::sqliteMigratesLegacyPersistentClipSchema()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("legacy.sqlite3"));
    const QString connectionName = QStringLiteral("legacy_clip_test_%1")
                                       .arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString text = QStringLiteral("https://example.com/legacy");
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE clip_schema_migrations ("
            "version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE clips ("
            "id TEXT PRIMARY KEY, kind TEXT NOT NULL, state TEXT NOT NULL, text TEXT NOT NULL, "
            "preview TEXT NOT NULL, content_hash TEXT NOT NULL UNIQUE, name TEXT, aliases TEXT, "
            "tags TEXT, pinned INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL, used_at TEXT, expires_at TEXT, source_app TEXT, "
            "size_bytes INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO clip_schema_migrations(version, name, applied_at) "
            "VALUES (1, 'initial_clip_text_schema', '2026-01-01T00:00:00.000Z')")));
        query.prepare(QStringLiteral(
            "INSERT INTO clips(id, kind, state, text, preview, content_hash, name, aliases, tags, "
            "pinned, created_at, updated_at, used_at, expires_at, source_app, size_bytes) "
            "VALUES (?, 'text', 'saved', ?, ?, ?, ?, ?, ?, 0, ?, ?, NULL, NULL, ?, ?)"));
        query.addBindValue(QStringLiteral("legacy-web"));
        query.addBindValue(text);
        query.addBindValue(text);
        query.addBindValue(hash);
        query.addBindValue(QStringLiteral("Legacy web"));
        query.addBindValue(QStringLiteral("old alias"));
        query.addBindValue(QStringLiteral("wb\nreference"));
        query.addBindValue(QStringLiteral("2026-01-01T00:00:00.000Z"));
        query.addBindValue(QStringLiteral("2026-01-01T00:00:01.000Z"));
        query.addBindValue(QStringLiteral("Obsidian"));
        query.addBindValue(text.toUtf8().size());
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    SqliteClipRepository repository;
    QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
    const std::optional<Clip> migrated = repository.findClip(QStringLiteral("legacy-web"));
    QVERIFY(migrated.has_value());
    QCOMPARE(migrated->actionType, ClipActionType::OpenWebUrl);
    QCOMPARE(migrated->storageBackend, ClipStorageBackend::Obsidian);
    QVERIFY(migrated->sourceApp.isEmpty());

    Clip sameContent = migrated.value();
    sameContent.id = QStringLiteral("same-content-second-identity");
    sameContent.name = QStringLiteral("Second identity");
    sameContent.aliases = {QStringLiteral("second alias")};
    sameContent.storageBackend = ClipStorageBackend::Local;
    QVERIFY2(repository.upsertPersistentClip(sameContent), qPrintable(repository.lastError()));
    QCOMPARE(repository.savedClips().size(), 2);
    QCOMPARE(repository.findClip(sameContent.id)->contentHash, hash);

    Clip conflictingIdentity = sameContent;
    conflictingIdentity.id = QStringLiteral("identity-conflict");
    conflictingIdentity.name = QStringLiteral("old alias");
    conflictingIdentity.aliases.clear();
    QVERIFY(!repository.upsertPersistentClip(conflictingIdentity));
    QVERIFY(repository.lastError().contains(QStringLiteral("already exists"), Qt::CaseInsensitive));
    QVERIFY(!repository.findClip(conflictingIdentity.id).has_value());
}

void ClipTest::sqliteBacksUpDatabaseAndRetainsNewestCopies()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteClipRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("clip.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("backup content"));
    QVERIFY(captured.captured());
    QVERIFY(repository.saveClip(captured.clip->id, QStringLiteral("Backup clip")));

    const QString backupDirectory = dir.filePath(QStringLiteral("backups"));
    ClipRepositoryBackupResult latest;
    for (int index = 0; index < 3; ++index) {
        latest = createAutomaticClipRepositoryBackup(repository, backupDirectory, 2);
        QVERIFY2(latest.success, qPrintable(latest.error));
        QTest::qWait(2);
    }
    const QFileInfoList backups = QDir(backupDirectory).entryInfoList(
        {QStringLiteral("pinloom-clip-auto-*.sqlite3")}, QDir::Files, QDir::Time);
    QCOMPARE(backups.size(), 2);

    SqliteClipRepository restored;
    QVERIFY2(restored.open(backups.first().absoluteFilePath()), qPrintable(restored.lastError()));
    QVERIFY2(restored.initialize(), qPrintable(restored.lastError()));
    const std::optional<Clip> restoredClip = restored.findClip(captured.clip->id);
    QVERIFY(restoredClip.has_value());
    QCOMPARE(restoredClip->name, QStringLiteral("Backup clip"));
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
                                             {QStringLiteral("Deploy snippet alternate")},
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
    QCOMPARE(results.at(1).clipId, tagId);
    QCOMPARE(results.at(1).matchedField, QStringLiteral("tag"));
    QCOMPARE(results.at(2).clipId, aliasId);
    QCOMPARE(results.at(2).matchedField, QStringLiteral("alias"));
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

void ClipTest::clipIdentitySearchUsesTagSemicolonAndNameAliasOnly()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString firstId = saveInMemoryClip(repository,
                                             QStringLiteral("body-only needle"),
                                             QStringLiteral("Alpha Clip"),
                                             {QStringLiteral("first alias")},
                                             {QStringLiteral("xx")},
                                             false,
                                             base,
                                             base.addSecs(1));
    const QString secondId = saveInMemoryClip(repository,
                                              QStringLiteral("second body"),
                                              QStringLiteral("Alpha Other"),
                                              {QStringLiteral("second alias")},
                                              {QStringLiteral("yy")},
                                              false,
                                              base.addSecs(2),
                                              base.addSecs(3));
    QVERIFY(!firstId.isEmpty());
    QVERIFY(!secondId.isEmpty());

    ClipSearchOptions options;
    options.mode = ClipSearchMode::Identity;
    options.limit = -1;
    const ClipSearchService search(repository);

    QCOMPARE(search.search(QStringLiteral("Alpha"), options).size(), 2);
    QList<ClipSearchResult> results = search.search(QStringLiteral("xx;Alpha"), options);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, firstId);
    results = search.search(QStringLiteral("xx;first"), options);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().matchedField, QStringLiteral("alias"));
    results = search.search(QStringLiteral("xx;"), options);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, firstId);
    QVERIFY(search.search(QStringLiteral("xx;Other"), options).isEmpty());
    QVERIFY(search.search(QStringLiteral("body-only"), options).isEmpty());
    QVERIFY(search.search(QStringLiteral("#xx"), options).isEmpty());
}

void ClipTest::sqliteIndexedSearchMatchesReferenceSearch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteClipRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("indexed-search.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    struct Seed {
        QString text;
        QString name;
        QStringList aliases;
        QStringList tags;
    };
    const QList<Seed> seeds{
        {QStringLiteral("deployment body needle"),
         QStringLiteral("Alpha Deployment"),
         {QStringLiteral("launch alias")},
         {QStringLiteral("xx"), QStringLiteral("ops")}},
        {QStringLiteral("second reference body"),
         QStringLiteral("Beta Reference"),
         {QStringLiteral("alternate needle")},
         {QStringLiteral("yy")}},
        {QStringLiteral("中文正文检索"),
         QStringLiteral("中文归档"),
         {QStringLiteral("中文别名")},
         {QStringLiteral("资料")}}
    };
    for (const Seed &seed : seeds) {
        const ClipCaptureResult captured = repository.captureText(seed.text);
        QVERIFY(captured.captured());
        QVERIFY2(repository.saveClip(captured.clip->id,
                                     seed.name,
                                     seed.aliases,
                                     seed.tags),
                 qPrintable(repository.lastError()));
    }

    const ClipSearchService indexed(repository);
    const auto ids = [](const QList<ClipSearchResult> &results) {
        QStringList values;
        for (const ClipSearchResult &result : results) values.append(result.clipId);
        return values;
    };
    for (const QString &query : {QStringLiteral("needle"),
                                 QStringLiteral("deployment body"),
                                 QStringLiteral("#op"),
                                 QStringLiteral("中文"),
                                 QStringLiteral("中文正文")}) {
        QCOMPARE(ids(indexed.search(query)), ids(searchClips(repository.clips(), query)));
    }

    ClipSearchOptions identityOptions;
    identityOptions.mode = ClipSearchMode::Identity;
    identityOptions.limit = -1;
    for (const QString &query : {QStringLiteral("Alpha"),
                                 QStringLiteral("xx;Alpha"),
                                 QStringLiteral("xx;launch"),
                                 QStringLiteral("xx;"),
                                 QStringLiteral("资料;中文")}) {
        QCOMPARE(ids(indexed.search(query, identityOptions)),
                 ids(searchClips(repository.clips(), query, identityOptions)));
    }

    const ClipIdentityQuery parsed = parseClipIdentityQuery(QStringLiteral(" #xx ; launch "));
    QVERIFY(parsed.hasTagQualifier);
    QCOMPARE(parsed.requiredTag, QStringLiteral("xx"));
    QCOMPARE(parsed.nameOrAlias, QStringLiteral("launch"));
}

void ClipTest::rejectsDuplicateSavedClipNamesAndAliases()
{
    InMemoryClipRepository memory;
    const QString firstId = saveInMemoryClip(memory,
                                             QStringLiteral("first unique body"),
                                             QStringLiteral("Unique Name"),
                                             {QStringLiteral("unique alias")},
                                             {},
                                             false,
                                             QDateTime::currentDateTimeUtc(),
                                             QDateTime::currentDateTimeUtc());
    QVERIFY(!firstId.isEmpty());

    const ClipCaptureResult second = memory.captureText(QStringLiteral("second unique body"));
    QVERIFY(second.captured());
    QVERIFY(!memory.saveClip(second.clip->id, QStringLiteral("unique name")));
    QVERIFY(!memory.saveClip(second.clip->id,
                             QStringLiteral("Second Name"),
                             {QStringLiteral("UNIQUE ALIAS")}));
    QVERIFY(!memory.saveClip(second.clip->id,
                             QStringLiteral("Second Name"),
                             {QStringLiteral("second name")}));
    QVERIFY(memory.saveClip(second.clip->id,
                            QStringLiteral("Second Name"),
                            {QStringLiteral("second alias")}));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteClipRepository sqlite;
    QVERIFY2(sqlite.open(dir.filePath(QStringLiteral("clip.sqlite3"))), qPrintable(sqlite.lastError()));
    QVERIFY2(sqlite.initialize(), qPrintable(sqlite.lastError()));
    const ClipCaptureResult sqliteFirst = sqlite.captureText(QStringLiteral("sqlite first"));
    const ClipCaptureResult sqliteSecond = sqlite.captureText(QStringLiteral("sqlite second"));
    QVERIFY(sqliteFirst.captured());
    QVERIFY(sqliteSecond.captured());
    QVERIFY2(sqlite.saveClip(sqliteFirst.clip->id,
                             QStringLiteral("SQLite Name"),
                             {QStringLiteral("SQLite Alias")}),
             qPrintable(sqlite.lastError()));
    QVERIFY(!sqlite.saveClip(sqliteSecond.clip->id,
                             QStringLiteral("Other SQLite Name"),
                             {QStringLiteral("sqlite name")}));
    QVERIFY(sqlite.lastError().contains(QStringLiteral("already exists"), Qt::CaseInsensitive));
}

void ClipTest::wbTagResolvesDefaultBrowserUrl()
{
    Clip clip;
    clip.text = QStringLiteral("https://example.com/reference?q=pinloom");
    clip.tags = {QStringLiteral("WB")};
    QCOMPARE(taggedActionForTags(clip.tags), ClipActionType::OpenWebUrl);
    clip.actionType = ClipActionType::OpenWebUrl;
    QCOMPARE(actionForClip(clip), ClipActionType::OpenWebUrl);
    QString error;
    const std::optional<QUrl> url = webUrlForClip(clip, &error);
    QVERIFY2(url.has_value(), qPrintable(error));
    QCOMPARE(url->scheme(), QStringLiteral("https"));
    QCOMPARE(url->host(), QStringLiteral("example.com"));

    clip.text = QStringLiteral("not a single URL");
    QVERIFY(!webUrlForClip(clip, &error).has_value());
    QVERIFY(!error.isEmpty());
    clip.tags.clear();
    QCOMPARE(taggedActionForTags(clip.tags), ClipActionType::InsertText);
    QCOMPARE(actionForClip(clip), ClipActionType::OpenWebUrl);
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

void ClipTest::softDeletesSavedClipsAndRestoresSearch()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("soft delete me"));
    QVERIFY(captured.captured());
    QVERIFY(repository.saveClip(captured.clip->id,
                                QStringLiteral("Soft Clip"),
                                {QStringLiteral("restore alias")},
                                {QStringLiteral("restore")},
                                false));

    ClipSearchService service(repository);
    QCOMPARE(service.search(QStringLiteral("Soft Clip")).size(), 1);
    QCOMPARE(service.search(QStringLiteral("restore alias")).size(), 1);
    QCOMPARE(service.search(QStringLiteral("#restore")).size(), 1);
    QVERIFY(service.softDeleteClip(captured.clip->id));
    QVERIFY(service.search(QStringLiteral("Soft Clip")).isEmpty());
    QVERIFY(service.search(QStringLiteral("restore alias")).isEmpty());
    QVERIFY(service.search(QStringLiteral("#restore")).isEmpty());

    std::optional<Clip> deleted = service.findClip(captured.clip->id);
    QVERIFY(deleted.has_value());
    QVERIFY(deleted->state == ClipState::Deleted);

    ClipSearchOptions deletedOptions;
    deletedOptions.includeSaved = false;
    deletedOptions.includeDeleted = true;
    const QList<ClipSearchResult> deletedResults =
        service.search(QStringLiteral("restore alias"), deletedOptions);
    QCOMPARE(deletedResults.size(), 1);
    QVERIFY(deletedResults.first().state == ClipState::Deleted);

    QVERIFY(service.restoreClip(captured.clip->id));
    QCOMPARE(service.search(QStringLiteral("Soft Clip")).size(), 1);
    QCOMPARE(service.search(QStringLiteral("restore alias")).size(), 1);
    QCOMPARE(service.search(QStringLiteral("#restore")).size(), 1);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteClipRepository sqliteRepository;
    QVERIFY2(sqliteRepository.open(dir.filePath(QStringLiteral("clips.sqlite3"))),
             qPrintable(sqliteRepository.lastError()));
    QVERIFY2(sqliteRepository.initialize(), qPrintable(sqliteRepository.lastError()));

    const ClipCaptureResult sqliteCaptured = sqliteRepository.captureText(QStringLiteral("sqlite soft delete me"));
    QVERIFY(sqliteCaptured.captured());
    QVERIFY2(sqliteRepository.saveClip(sqliteCaptured.clip->id,
                                       QStringLiteral("SQLite Soft Clip"),
                                       {QStringLiteral("sqlite restore alias")},
                                       {},
                                       false),
             qPrintable(sqliteRepository.lastError()));

    ClipSearchService sqliteService(sqliteRepository);
    QCOMPARE(sqliteService.search(QStringLiteral("sqlite restore alias")).size(), 1);
    QVERIFY2(sqliteService.softDeleteClip(sqliteCaptured.clip->id),
             qPrintable(sqliteService.lastError()));
    QVERIFY(sqliteService.search(QStringLiteral("sqlite restore alias")).isEmpty());
    std::optional<Clip> sqliteDeleted = sqliteService.findClip(sqliteCaptured.clip->id);
    QVERIFY(sqliteDeleted.has_value());
    QVERIFY(sqliteDeleted->state == ClipState::Deleted);
    QVERIFY2(sqliteService.restoreClip(sqliteCaptured.clip->id),
             qPrintable(sqliteService.lastError()));
    QCOMPARE(sqliteService.search(QStringLiteral("sqlite restore alias")).size(), 1);
}

void ClipTest::clipSearchEmptyQueryWithTemporaryHistoryHidesSavedClips()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("temporary default history"),
                                                               {},
                                                               {},
                                                               base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    const QString savedId = saveInMemoryClip(repository,
                                             QStringLiteral("saved default history"),
                                             QStringLiteral("Saved Default History"),
                                             {QStringLiteral("saved default alias")},
                                             {QStringLiteral("saved-default")},
                                             true,
                                             base.addSecs(1),
                                             base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    ClipSearchOptions options;
    options.includeTemporary = true;
    const ClipSearchService search(repository);

    QList<ClipSearchResult> results = search.search(QString(), options);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, temporary.clip->id);
    QVERIFY(results.first().state == ClipState::Temporary);

    results = search.search(QStringLiteral("Saved Default History"), options);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, savedId);
    QVERIFY(results.first().state == ClipState::Saved);
    QCOMPARE(results.first().matchedField, QStringLiteral("name"));

    results = search.search(QStringLiteral("saved default alias"), options);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, savedId);
    QCOMPARE(results.first().matchedField, QStringLiteral("alias"));

    results = search.search(QStringLiteral("#saved-default"), options);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().clipId, savedId);
    QCOMPARE(results.first().matchedField, QStringLiteral("tag"));
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

void ClipTest::savedClipContentCaptureIsIgnoredAsDuplicate()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult captured = repository.captureText(QStringLiteral("saved duplicate text"),
                                                              {},
                                                              {},
                                                              base);
    QVERIFY(captured.captured());
    QVERIFY(captured.clip.has_value());
    QVERIFY(repository.saveClip(captured.clip->id,
                                QStringLiteral("Saved Duplicate"),
                                {QStringLiteral("duplicate alias")},
                                {QStringLiteral("duplicate")},
                                false,
                                base.addSecs(1)));
    QCOMPARE(repository.temporaryClips().size(), 0);
    QCOMPARE(repository.savedClips().size(), 1);

    const ClipCaptureResult duplicate = repository.captureText(QStringLiteral("saved duplicate text"),
                                                               {},
                                                               {},
                                                               base.addSecs(2));
    QVERIFY(!duplicate.captured());
    QVERIFY(duplicate.status == ClipCaptureStatus::IgnoredDuplicate);
    QCOMPARE(repository.temporaryClips().size(), 0);
    QCOMPARE(repository.savedClips().size(), 1);
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






void ClipTest::obsidianStoreRoundTripsManagedMarkdown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral(".obsidian")));

    InMemoryClipRepository repository;
    const QDateTime createdAt = QDateTime::fromString(QStringLiteral("2026-07-10T08:00:00Z"), Qt::ISODate);
    const QDateTime savedAt = createdAt.addSecs(60);
    const QString clipId = saveInMemoryClip(repository,
                                            QStringLiteral("alpha\nbeta\n\nmarkdown **body**"),
                                            QStringLiteral("Reusable: snippet / example"),
                                            {QStringLiteral("alpha alias"), QStringLiteral("quoted \"alias\"")},
                                            {QStringLiteral("engineering"), QStringLiteral("reference")},
                                            true,
                                            createdAt,
                                            savedAt);
    QVERIFY(!clipId.isEmpty());
    const std::optional<Clip> clip = repository.findClip(clipId);
    QVERIFY(clip.has_value());

    ObsidianClipStoreConfig config;
    config.vaultPath = dir.path();
    config.archiveDirectory = QStringLiteral("Pinloom Clips");
    ObsidianClipStore store(config);
    const ObsidianClipWriteResult written = store.writeClip(clip.value());
    QVERIFY2(written.succeeded(), qPrintable(written.error));
    QVERIFY(QFileInfo::exists(written.filePath));
    QVERIFY(written.relativePath.startsWith(QStringLiteral("Pinloom Clips/")));
    QCOMPARE(QFileInfo(written.filePath).fileName(),
             QStringLiteral("Reusable_ snippet _ example.md"));
    QVERIFY(!QFileInfo(written.filePath).fileName().contains(clipId));

    const QByteArray markdown = readClipTestFile(written.filePath);
    QVERIFY(markdown.startsWith("---\npinloom_id:"));
    QVERIFY(markdown.contains("pinloom_type: \"clip\""));
    QVERIFY(markdown.contains("aliases: ["));
    QVERIFY(markdown.endsWith("alpha\nbeta\n\nmarkdown **body**"));

    QString error;
    const std::optional<ObsidianClipDocument> loaded = store.readClipFile(written.filePath, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->clip.id, clipId);
    QCOMPARE(loaded->clip.text, clip->text);
    QCOMPARE(loaded->clip.name, clip->name);
    QCOMPARE(loaded->clip.aliases, clip->aliases);
    QCOMPARE(loaded->clip.tags, clip->tags);
    QCOMPARE(loaded->clip.pinned, clip->pinned);
    QCOMPARE(loaded->clip.storageBackend, ClipStorageBackend::Obsidian);
    QCOMPARE(loaded->clip.sourceApp, clip->sourceApp);

    const QUrl openUrl = store.openUrlForClip(clipId, &error);
    QVERIFY2(openUrl.isValid(), qPrintable(error));
    QCOMPARE(openUrl.scheme(), QStringLiteral("obsidian"));
    QCOMPARE(openUrl.host(), QStringLiteral("open"));
    QVERIFY(openUrl.toString().contains(QStringLiteral("path=")));

    const QString legacyPath = QDir(QFileInfo(written.filePath).absolutePath())
                                   .filePath(QStringLiteral("Reusable_ snippet _ example--%1.md")
                                                 .arg(clipId));
    QVERIFY(QFile::rename(written.filePath, legacyPath));
    const ObsidianClipWriteResult migrated = store.writeClip(clip.value());
    QVERIFY2(migrated.succeeded(), qPrintable(migrated.error));
    QCOMPARE(QFileInfo(migrated.filePath).fileName(),
             QStringLiteral("Reusable_ snippet _ example.md"));
    QVERIFY(!QFileInfo::exists(legacyPath));

    const QString duplicateNameId = saveInMemoryClip(repository,
                                                      QStringLiteral("different body"),
                                                      clip->name,
                                                      {},
                                                      {},
                                                      false,
                                                      savedAt.addSecs(1),
                                                      savedAt.addSecs(2));
    QVERIFY(duplicateNameId.isEmpty());
}

void ClipTest::obsidianSyncImportsExternalEditsAndTracksRename()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral(".obsidian")));

    InMemoryClipRepository source;
    const QString clipId = saveInMemoryClip(source,
                                            QStringLiteral("original body"),
                                            QStringLiteral("External edit"),
                                            {QStringLiteral("external")},
                                            {QStringLiteral("obsidian")},
                                            false,
                                            QDateTime::currentDateTimeUtc().addSecs(-60),
                                            QDateTime::currentDateTimeUtc().addSecs(-30));
    const std::optional<Clip> sourceClip = source.findClip(clipId);
    QVERIFY(sourceClip.has_value());

    ObsidianClipStoreConfig config;
    config.vaultPath = dir.path();
    ObsidianClipStore store(config);
    const ObsidianClipWriteResult written = store.writeClip(sourceClip.value());
    QVERIFY2(written.succeeded(), qPrintable(written.error));

    InMemoryClipRepository index;
    const ObsidianClipSyncResult initialSync = store.synchronize(index);
    QVERIFY2(initialSync.succeeded(), qPrintable(initialSync.fatalError));
    QCOMPARE(initialSync.imported, 1);
    QCOMPARE(index.findClip(clipId)->text, QStringLiteral("original body"));

    const QString renamedPath = QDir(QFileInfo(written.filePath).absolutePath())
                                    .filePath(QStringLiteral("renamed-by-obsidian.md"));
    QVERIFY(QFile::rename(written.filePath, renamedPath));
    QByteArray markdown = readClipTestFile(renamedPath);
    const int closingMarker = markdown.indexOf("---\n", 4);
    QVERIFY(closingMarker >= 0);
    markdown = markdown.left(closingMarker + 4) + QByteArrayLiteral("edited in Obsidian");
    QVERIFY(writeClipTestFile(renamedPath, markdown));

    const ObsidianClipSyncResult editedSync = store.synchronize(index);
    QVERIFY2(editedSync.succeeded(), qPrintable(editedSync.fatalError));
    QCOMPARE(editedSync.updated, 1);
    const std::optional<Clip> synchronized = index.findClip(clipId);
    QVERIFY(synchronized.has_value());
    QCOMPARE(synchronized->text, QStringLiteral("edited in Obsidian"));
    QCOMPARE(synchronized->storageBackend, ClipStorageBackend::Obsidian);
    QCOMPARE(synchronized->sourceApp, sourceClip->sourceApp);

    QString error;
    const std::optional<ObsidianClipDocument> renamed = store.findClip(clipId, &error);
    QVERIFY2(renamed.has_value(), qPrintable(error));
    QCOMPARE(QFileInfo(renamed->filePath).fileName(), QStringLiteral("renamed-by-obsidian.md"));
}

void ClipTest::obsidianSyncSoftDeletesMissingNotes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    InMemoryClipRepository source;
    const QString clipId = saveInMemoryClip(source,
                                            QStringLiteral("delete source note"),
                                            QStringLiteral("Delete source"),
                                            {},
                                            {},
                                            false,
                                            QDateTime::currentDateTimeUtc().addSecs(-30),
                                            QDateTime::currentDateTimeUtc().addSecs(-20));
    QVERIFY(!clipId.isEmpty());

    ObsidianClipStoreConfig config;
    config.vaultPath = dir.path();
    ObsidianClipStore store(config);
    const ObsidianClipWriteResult written = store.writeClip(source.findClip(clipId).value());
    QVERIFY2(written.succeeded(), qPrintable(written.error));

    InMemoryClipRepository index;
    QCOMPARE(store.synchronize(index).imported, 1);
    QVERIFY(QFile::remove(written.filePath));

    const ObsidianClipSyncResult deletedSync = store.synchronize(index);
    QVERIFY2(deletedSync.succeeded(), qPrintable(deletedSync.fatalError));
    QCOMPARE(deletedSync.deleted, 1);
    const std::optional<Clip> deleted = index.findClip(clipId);
    QVERIFY(deleted.has_value());
    QCOMPARE(deleted->state, ClipState::Deleted);
}

void ClipTest::obsidianStateAndForgetTombstoneRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral(".obsidian")));

    ObsidianClipStoreConfig config;
    config.vaultPath = dir.path();
    config.archiveDirectory = QStringLiteral("Pinloom Clips");
    ObsidianClipStore store(config);

    Clip clip;
    clip.id = QStringLiteral("stateful-clip");
    clip.state = ClipState::Saved;
    clip.text = QStringLiteral("https://example.com/stateful");
    clip.name = QStringLiteral("Stateful web clip");
    clip.aliases = {QStringLiteral("stateful alias")};
    clip.tags = {QStringLiteral("wb"), QStringLiteral("reference")};
    clip.actionType = ClipActionType::OpenWebUrl;
    clip.storageBackend = ClipStorageBackend::Obsidian;
    clip.sourceApp = QStringLiteral("notepad++.exe");
    clip.sourceWindowTitle = QStringLiteral("Source document");
    clip.sourceUri = QStringLiteral("file:///E:/notes/source.txt");
    clip.createdAt = QDateTime::fromString(QStringLiteral("2026-07-20T10:00:00Z"), Qt::ISODate);
    clip.updatedAt = clip.createdAt.addSecs(1);

    const ObsidianClipWriteResult saved = store.writeClip(clip);
    QVERIFY2(saved.succeeded(), qPrintable(saved.error));
    QString error;
    std::optional<ObsidianClipDocument> document = store.readClipFile(saved.filePath, &error);
    QVERIFY2(document.has_value(), qPrintable(error));
    QVERIFY(document->stateExplicit);
    QVERIFY(!document->forgotten);
    QCOMPARE(document->clip.state, ClipState::Saved);
    QCOMPARE(document->clip.actionType, ClipActionType::OpenWebUrl);
    QCOMPARE(document->clip.storageBackend, ClipStorageBackend::Obsidian);
    QCOMPARE(document->clip.sourceApp, clip.sourceApp);
    QCOMPARE(document->clip.sourceWindowTitle, clip.sourceWindowTitle);
    QCOMPARE(document->clip.sourceUri, clip.sourceUri);

    clip.state = ClipState::Deleted;
    clip.updatedAt = clip.updatedAt.addSecs(1);
    const ObsidianClipWriteResult deleted = store.writeClip(clip);
    QVERIFY2(deleted.succeeded(), qPrintable(deleted.error));
    document = store.readClipFile(deleted.filePath, &error);
    QVERIFY2(document.has_value(), qPrintable(error));
    QCOMPARE(document->clip.state, ClipState::Deleted);

    QByteArray markdown = readClipTestFile(deleted.filePath);
    const int closingMarker = markdown.indexOf("---\n", 4);
    QVERIFY(closingMarker >= 0);
    markdown = markdown.left(closingMarker + 4) + QByteArrayLiteral("edited while deleted");
    QVERIFY(writeClipTestFile(deleted.filePath, markdown));

    InMemoryClipRepository index;
    QVERIFY(index.upsertPersistentClip(clip));
    const ObsidianClipSyncResult editedSync = store.synchronize(index);
    QVERIFY2(editedSync.succeeded(), qPrintable(editedSync.fatalError));
    const std::optional<Clip> synchronized = index.findClip(clip.id);
    QVERIFY(synchronized.has_value());
    QCOMPARE(synchronized->state, ClipState::Deleted);
    QCOMPARE(synchronized->text, QStringLiteral("edited while deleted"));

    QVERIFY2(store.forgetClip(clip.id, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(deleted.filePath));
    document = store.readClipFile(deleted.filePath, &error);
    QVERIFY2(document.has_value(), qPrintable(error));
    QVERIFY(document->forgotten);
    QVERIFY(readClipTestFile(deleted.filePath).contains("pinloom_state: \"forgotten\""));
    QVERIFY(readClipTestFile(deleted.filePath).endsWith("edited while deleted"));

    const ObsidianClipSyncResult forgottenSync = store.synchronize(index);
    QVERIFY2(forgottenSync.succeeded(), qPrintable(forgottenSync.fatalError));
    QVERIFY(!index.findClip(clip.id).has_value());
}

void ClipTest::persistentClipServiceCoordinatesRepositoryAndObsidian()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral(".obsidian")));
    ObsidianClipStoreConfig config;
    config.vaultPath = dir.path();
    ObsidianClipStore store(config);
    InMemoryClipRepository repository;
    PersistentClipService service(repository, store);

    const ClipCaptureResult captured = repository.captureText(QStringLiteral("https://example.com/coordinated"));
    QVERIFY(captured.captured());
    Clip clip = captured.clip.value();
    clip.name = QStringLiteral("Coordinated URL");
    clip.tags = {QStringLiteral("wb"), QStringLiteral("reference")};
    clip.sourceApp = QStringLiteral("editor.exe");
    QString error;
    QVERIFY2(service.saveClip(clip, &error), qPrintable(error));
    std::optional<Clip> stored = service.findClip(clip.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->state, ClipState::Saved);
    QCOMPARE(stored->actionType, ClipActionType::OpenWebUrl);
    QCOMPARE(stored->storageBackend, ClipStorageBackend::Obsidian);
    QCOMPARE(stored->sourceApp, QStringLiteral("editor.exe"));

    QVERIFY2(service.changeState(clip.id, ClipState::Deleted, &error), qPrintable(error));
    QCOMPARE(service.findClip(clip.id)->state, ClipState::Deleted);
    const std::optional<ObsidianClipDocument> deletedNote = store.findClip(clip.id, &error);
    QVERIFY2(deletedNote.has_value(), qPrintable(error));
    QCOMPARE(deletedNote->clip.state, ClipState::Deleted);

    QVERIFY2(service.changeState(clip.id, ClipState::Saved, &error), qPrintable(error));
    QCOMPARE(service.findClip(clip.id)->state, ClipState::Saved);
    QVERIFY2(service.changeState(clip.id, ClipState::Deleted, &error), qPrintable(error));
    QVERIFY2(service.permanentlyRemove(clip.id, &error), qPrintable(error));
    QVERIFY(!service.findClip(clip.id).has_value());
    const std::optional<ObsidianClipDocument> forgotten = store.readClipFile(deletedNote->filePath, &error);
    QVERIFY2(forgotten.has_value(), qPrintable(error));
    QVERIFY(forgotten->forgotten);
}

void ClipTest::obsidianWatcherSynchronizesNewNotes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    InMemoryClipRepository index;
    ObsidianClipStoreConfig config;
    config.vaultPath = dir.path();
    ObsidianClipSyncService syncService(index, config);
    QVERIFY2(syncService.start(), qPrintable(syncService.lastError()));

    InMemoryClipRepository source;
    const QString clipId = saveInMemoryClip(source,
                                            QStringLiteral("watcher body"),
                                            QStringLiteral("Watcher clip"),
                                            {},
                                            {},
                                            false,
                                            QDateTime::currentDateTimeUtc(),
                                            QDateTime::currentDateTimeUtc());
    QVERIFY(!clipId.isEmpty());
    ObsidianClipStore writer(config);
    const ObsidianClipWriteResult written = writer.writeClip(source.findClip(clipId).value());
    QVERIFY2(written.succeeded(), qPrintable(written.error));

    QTRY_VERIFY_WITH_TIMEOUT(index.findClip(clipId).has_value(), 3000);
    QCOMPARE(index.findClip(clipId)->text, QStringLiteral("watcher body"));
    syncService.stop();
}

void ClipTest::obsidianRealVaultRoundTrip()
{
    const QString vaultPath = qEnvironmentVariable("PINLOOM_TEST_OBSIDIAN_VAULT").trimmed();
    if (vaultPath.isEmpty()) {
        QSKIP("PINLOOM_TEST_OBSIDIAN_VAULT is not configured");
    }
    QVERIFY2(QFileInfo(vaultPath).isDir(), qPrintable(vaultPath));

    Clip clip;
    clip.id = QStringLiteral("integration-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    clip.kind = ClipKind::Text;
    clip.state = ClipState::Saved;
    clip.text = QStringLiteral("Pinloom Obsidian integration probe\nsecond line");
    clip.name = QStringLiteral("Pinloom integration probe");
    clip.aliases = {QStringLiteral("integration probe")};
    clip.tags = {QStringLiteral("pinloom-test")};
    clip.createdAt = QDateTime::currentDateTimeUtc();
    clip.updatedAt = clip.createdAt;

    ObsidianClipStoreConfig config;
    config.vaultPath = vaultPath;
    ObsidianClipStore store(config);
    const ObsidianClipWriteResult written = store.writeClip(clip);
    QVERIFY2(written.succeeded(), qPrintable(written.error));

    QString error;
    const std::optional<ObsidianClipDocument> reloaded = store.findClip(clip.id, &error);
    if (!reloaded.has_value()) {
        QFile::remove(written.filePath);
    }
    QVERIFY2(reloaded.has_value(), qPrintable(error));
    QCOMPARE(reloaded->clip.text, clip.text);

    InMemoryClipRepository index;
    const ObsidianClipSyncResult synchronized = store.synchronize(index);
    if (!synchronized.succeeded() || !index.findClip(clip.id).has_value()) {
        QFile::remove(written.filePath);
    }
    QVERIFY2(synchronized.succeeded(), qPrintable(synchronized.fatalError));
    QVERIFY(index.findClip(clip.id).has_value());
    QCOMPARE(index.findClip(clip.id)->text, clip.text);
    QVERIFY(QFile::remove(written.filePath));
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

void ClipTest::clipboardServiceUsesDynamicForegroundSource()
{
    FakeClipboardTextSource clipboard;
    InMemoryClipRepository repository;
    ClipboardCaptureService service(&clipboard, repository);
    QString foregroundApp = QStringLiteral("secret.exe");
    service.setSourceAppProvider([&foregroundApp]() {
        return foregroundApp;
    });
    ClipCapturePolicy policy;
    policy.excludedSourceApps = {QStringLiteral("secret.exe")};
    service.setPolicy(policy);
    QVERIFY(service.start());

    clipboard.setText(QStringLiteral("excluded foreground content"));
    QCOMPARE(service.lastStatus(), ClipCaptureStatus::IgnoredExcludedSource);
    QVERIFY(repository.clips().isEmpty());

    foregroundApp = QStringLiteral("editor.exe");
    clipboard.setText(QStringLiteral("allowed foreground content"));
    QCOMPARE(service.lastStatus(), ClipCaptureStatus::Captured);
    QCOMPARE(repository.clips().size(), 1);
    QCOMPARE(repository.clips().first().sourceApp, QStringLiteral("editor.exe"));
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

void ClipTest::hotkeyServiceRegistersWithFakeBackend()
{
    FakeClipHotkeyBackend backend;
    const ClipHotkeyConfig config = testHotkeyConfig();
    ClipHotkeyService service(config, &backend);
    QList<bool> registeredChanges;
    QObject::connect(&service, &ClipHotkeyService::registeredChanged, [&](bool registered) {
        registeredChanges.append(registered);
    });

    QVERIFY(service.start());

    QVERIFY(service.isRegistered());
    QVERIFY(backend.registered());
    QCOMPARE(backend.registerCalls(), 1);
    QCOMPARE(backend.unregisterCalls(), 0);
    QVERIFY(backend.registeredConfig() == config);
    QCOMPARE(service.displayText(), QStringLiteral("Ctrl+Alt+F8"));
    QVERIFY(service.lastError().isEmpty());
    QCOMPARE(registeredChanges, (QList<bool>{true}));
}

void ClipTest::hotkeyServiceActivationEmitsSignal()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(testHotkeyConfig(), &backend);
    int signalCount = 0;
    QObject::connect(&service, &ClipHotkeyService::activated, [&]() {
        ++signalCount;
    });

    QVERIFY(service.start());
    backend.activate();

    QCOMPARE(signalCount, 1);

    service.stop();
    backend.activate();

    QCOMPARE(signalCount, 1);
}

void ClipTest::hotkeyServiceStopUnregisters()
{
    FakeClipHotkeyBackend backend;
    ClipHotkeyService service(testHotkeyConfig(), &backend);
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
    ClipHotkeyService service(testHotkeyConfig(), &backend);

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
    ClipHotkeyService service(testHotkeyConfig(), &backend);

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

void ClipTest::hyperHotkeyStateMachineActivatesOnceAndConsumesTrigger()
{
    HyperHotkeyStateMachine matcher;
    QVERIFY(!matcher.process(HyperKeyRole::Control, true).consume);
    QVERIFY(!matcher.process(HyperKeyRole::Alt, true).consume);
    QVERIFY(!matcher.process(HyperKeyRole::Shift, true).consume);

    const HyperHotkeyMatchResult layerDown = matcher.process(HyperKeyRole::Layer, true);
    QVERIFY(!layerDown.consume);
    QVERIFY(!layerDown.activated);
    QVERIFY(matcher.armed());

    const HyperHotkeyMatchResult triggerDown = matcher.process(HyperKeyRole::SaveTrigger, true);
    QVERIFY(triggerDown.consume);
    QVERIFY(triggerDown.activated);
    QCOMPARE(triggerDown.action, HyperHotkeyAction::Save);

    const HyperHotkeyMatchResult repeated = matcher.process(HyperKeyRole::SaveTrigger, true);
    QVERIFY(repeated.consume);
    QVERIFY(!repeated.activated);
    QCOMPARE(repeated.action, HyperHotkeyAction::None);

    const HyperHotkeyMatchResult triggerUp = matcher.process(HyperKeyRole::SaveTrigger, false);
    QVERIFY(triggerUp.consume);
    QVERIFY(!triggerUp.activated);

    const HyperHotkeyMatchResult layerUp = matcher.process(HyperKeyRole::Layer, false);
    QVERIFY(!layerUp.consume);
    QVERIFY(layerUp.chordReleased);
    QVERIFY(!matcher.armed());
    matcher.process(HyperKeyRole::Shift, false);
    matcher.process(HyperKeyRole::Alt, false);
    matcher.process(HyperKeyRole::Control, false);
}

void ClipTest::hyperHotkeyStateMachineAcceptsF24AsCarrier()
{
    HyperHotkeyStateMachine matcher;

    const HyperHotkeyMatchResult carrierDown = matcher.process(HyperKeyRole::F24, true);
    QVERIFY(!carrierDown.consume);
    QVERIFY(!carrierDown.activated);
    QVERIFY(matcher.armed());

    const HyperHotkeyMatchResult triggerDown = matcher.process(HyperKeyRole::SaveTrigger, true);
    QVERIFY(triggerDown.consume);
    QVERIFY(triggerDown.activated);
    QCOMPARE(triggerDown.action, HyperHotkeyAction::Save);

    const HyperHotkeyMatchResult repeated = matcher.process(HyperKeyRole::SaveTrigger, true);
    QVERIFY(repeated.consume);
    QVERIFY(!repeated.activated);

    const HyperHotkeyMatchResult triggerUp = matcher.process(HyperKeyRole::SaveTrigger, false);
    QVERIFY(triggerUp.consume);

    const HyperHotkeyMatchResult carrierUp = matcher.process(HyperKeyRole::F24, false);
    QVERIFY(!carrierUp.consume);
    QVERIFY(carrierUp.chordReleased);
    QVERIFY(!matcher.armed());
}

void ClipTest::hyperHotkeyStateMachineEmitsInsertAction()
{
    HyperHotkeyStateMachine matcher;
    matcher.process(HyperKeyRole::F24, true);

    const HyperHotkeyMatchResult insertDown = matcher.process(HyperKeyRole::InsertTrigger, true);
    QVERIFY(insertDown.consume);
    QVERIFY(insertDown.activated);
    QCOMPARE(insertDown.action, HyperHotkeyAction::Insert);

    const HyperHotkeyMatchResult repeated = matcher.process(HyperKeyRole::InsertTrigger, true);
    QVERIFY(repeated.consume);
    QVERIFY(!repeated.activated);
    QCOMPARE(repeated.action, HyperHotkeyAction::None);

    QVERIFY(matcher.process(HyperKeyRole::InsertTrigger, false).consume);
    const HyperHotkeyMatchResult secondActionInChord =
        matcher.process(HyperKeyRole::SaveTrigger, true);
    QVERIFY(secondActionInChord.consume);
    QVERIFY(!secondActionInChord.activated);
    QCOMPARE(secondActionInChord.action, HyperHotkeyAction::None);
    QVERIFY(matcher.process(HyperKeyRole::SaveTrigger, false).consume);
    QVERIFY(!matcher.process(HyperKeyRole::F24, false).consume);
    QVERIFY(!matcher.armed());
}

void ClipTest::hyperHotkeyStateMachineRejectsIncompleteChord()
{
    HyperHotkeyStateMachine matcher;
    matcher.process(HyperKeyRole::Control, true);
    matcher.process(HyperKeyRole::Alt, true);

    const HyperHotkeyMatchResult layerDown = matcher.process(HyperKeyRole::Layer, true);
    QVERIFY(!layerDown.consume);
    QVERIFY(!matcher.armed());

    const HyperHotkeyMatchResult triggerDown = matcher.process(HyperKeyRole::SaveTrigger, true);
    QVERIFY(!triggerDown.consume);
    QVERIFY(!triggerDown.activated);

    matcher.reset();
    matcher.process(HyperKeyRole::Control, true);
    matcher.process(HyperKeyRole::Alt, true);
    matcher.process(HyperKeyRole::Shift, true);
    const HyperHotkeyMatchResult plainS = matcher.process(HyperKeyRole::SaveTrigger, true);
    QVERIFY(!plainS.consume);
    QVERIFY(!plainS.activated);
}

void ClipTest::textSelectionCaptureServiceUsesProvider()
{
    TextSelectionCaptureResult selected;
    selected.state = TextSelectionState::TextSelected;
    selected.text = QStringLiteral("selected text");

    TextSelectionCaptureService service([selected]() {
        return selected;
    });
    QCOMPARE(service.capture().text, QStringLiteral("selected text"));
}

void ClipTest::foregroundTextTargetExpiresAfterConfiguredAge()
{
    ForegroundTextTarget target;
    target.windowHandle = 1;
    target.capturedAt = QDateTime::fromString(QStringLiteral("2026-07-20T10:00:00Z"), Qt::ISODate);
    QVERIFY(target.isValid());
    QVERIFY(!target.isExpired(600, target.capturedAt.addSecs(600)));
    QVERIFY(target.isExpired(600, target.capturedAt.addSecs(601)));
    QVERIFY(!target.isExpired(-1, target.capturedAt.addDays(1)));
}

#ifdef Q_OS_WIN

void ClipTest::capturesKeyboardSelectionFromScintillaControl()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = testScintillaWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"Scintilla";
    const ATOM registered = RegisterClassW(&windowClass);
    QVERIFY2(registered != 0, "Unable to register the test Scintilla window class");

    HWND control = CreateWindowExW(0,
                                   windowClass.lpszClassName,
                                   L"",
                                   0,
                                   0,
                                   0,
                                   320,
                                   200,
                                   HWND_MESSAGE,
                                   nullptr,
                                   instance,
                                   nullptr);
    QVERIFY2(control != nullptr, "Unable to create the test Scintilla window");

    ForegroundAppWindowContext context;
    context.windowHandle = reinterpret_cast<quintptr>(control);
    context.processId = GetCurrentProcessId();
    ForegroundTextTarget target;
    target.windowHandle = reinterpret_cast<quintptr>(control);
    target.focusHandle = reinterpret_cast<quintptr>(control);
    target.processId = GetCurrentProcessId();
    target.threadId = GetCurrentThreadId();

    const TextSelectionCaptureResult result = captureTextSelectionFromTarget(context, target);
    QCOMPARE(result.state, TextSelectionState::TextSelected);
    QCOMPARE(result.text, QStringLiteral("keyboard-selected text"));
    QCOMPARE(result.source, QStringLiteral("scintilla"));
    QVERIFY(result.diagnostics.isEmpty());

    DestroyWindow(control);
    UnregisterClassW(windowClass.lpszClassName, instance);
}

#endif

void ClipTest::trayControllerStartsAndStops()
{
    ClipTrayController controller;
    QList<bool> runningSignals;
    QObject::connect(&controller, &ClipTrayController::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });

    QVERIFY(controller.start());

    QVERIFY(controller.isRunning());
    QVERIFY(controller.lastError().isEmpty());
    QCOMPARE(controller.status(), QStringLiteral("Running"));
    controller.stop();
    controller.stop();
    QVERIFY(!controller.isRunning());
    QCOMPARE(controller.status(), QStringLiteral("Stopped"));
    QCOMPARE(runningSignals, (QList<bool>{true, false}));
}

void ClipTest::trayControllerShowClipboardActionEmitsSignal()
{
    ClipTrayController controller;
    int showSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::showClipboardRequested, [&]() {
        ++showSignalCount;
    });

    controller.requestShowClipboard();
    QCOMPARE(showSignalCount, 1);

    QVERIFY(controller.triggerAction(QStringLiteral("show_clipboard")));
    QCOMPARE(showSignalCount, 2);
}

void ClipTest::trayControllerPauseResumeToggleUpdatesActionsAndSignals()
{
    ClipTrayController controller;
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

    QCOMPARE(pausedSignals, (QList<bool>{true, false, true, false}));
    QCOMPARE(actionsChanged, 4);
}

void ClipTest::trayControllerQuitRequestEmitsSignal()
{
    ClipTrayController controller;
    int quitSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::quitRequested, [&]() {
        ++quitSignalCount;
    });

    controller.requestQuit();
    QVERIFY(controller.triggerAction(QStringLiteral("quit")));

    QCOMPARE(quitSignalCount, 2);
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
    ClipInsertionOptions options;
    options.clipboardRestoreDelayMs = 0;
    service.setOptions(options);

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

void ClipTest::insertionServiceDefersClipboardRestore()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Deferred paste text"));
    QVERIFY(captured.captured());

    FakeClipboardTextAccessor clipboard;
    clipboard.setInitialText(QStringLiteral("original clipboard"));
    ClipInsertionService service(&clipboard, repository, []() {
        return true;
    });
    ClipInsertionOptions options;
    options.clipboardRestoreDelayMs = 30;
    service.setOptions(options);

    int suppressCalls = 0;
    service.setSuppressClipboardCaptureCallback([&]() {
        ++suppressCalls;
    });

    const ClipInsertionResult result = service.insertClip(captured.clip->id);

    QVERIFY(result.inserted());
    QCOMPARE(clipboard.text(), QStringLiteral("Deferred paste text"));
    QCOMPARE(suppressCalls, 1);
    QTRY_COMPARE(clipboard.text(), QStringLiteral("original clipboard"));
    QCOMPARE(suppressCalls, 2);
}

void ClipTest::insertionServiceDoesNotOverwriteNewClipboardContent()
{
    InMemoryClipRepository repository;
    const ClipCaptureResult captured = repository.captureText(QStringLiteral("Transient paste text"));
    QVERIFY(captured.captured());

    FakeClipboardTextAccessor clipboard;
    clipboard.setInitialText(QStringLiteral("original clipboard"));
    ClipInsertionService service(&clipboard, repository, []() {
        return true;
    });
    ClipInsertionOptions options;
    options.clipboardRestoreDelayMs = 30;
    service.setOptions(options);

    const ClipInsertionResult result = service.insertClip(captured.clip->id);
    QVERIFY(result.inserted());
    clipboard.setInitialText(QStringLiteral("new external clipboard content"));

    QTest::qWait(60);
    QCOMPARE(clipboard.text(), QStringLiteral("new external clipboard content"));
    QCOMPARE(clipboard.writes(), QStringList{QStringLiteral("Transient paste text")});
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
