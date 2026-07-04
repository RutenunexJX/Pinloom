#include "pinloom/clip/ClipRepository.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <optional>

using namespace Pinloom;

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

QTEST_MAIN(ClipTest)

#include "clip_test.moc"
