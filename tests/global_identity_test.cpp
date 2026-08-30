#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include "pinloom/core/AnchorLibraryArchive.h"
#include "pinloom/core/GlobalIdentity.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/SqliteLibraryRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>
#include <future>

using namespace Pinloom;

namespace {

Resource fileResource(const QString &id,
                      const QString &name,
                      const QStringList &aliases = {})
{
    Resource resource;
    resource.id = id;
    resource.kind = ResourceKind::File;
    resource.title = name;
    resource.aliases = aliases;
    resource.location = QStringLiteral("E:/identity/%1.txt").arg(id);
    resource.updatedAt = QDateTime::currentDateTimeUtc();
    return resource;
}

Resource anchorResource(const QString &id,
                        const QString &name,
                        const QStringList &aliases = {})
{
    Resource resource = fileResource(QStringLiteral("container-%1").arg(id),
                                     QStringLiteral("Container %1").arg(id));
    Anchor anchor;
    anchor.id = QStringLiteral("anchor-%1").arg(id);
    anchor.name = name;
    anchor.aliases = aliases;
    anchor.targetApp = QStringLiteral("identity-test");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("manual");
    anchor.locatorJson = QStringLiteral("{}");
    anchor.createdAt = resource.updatedAt;
    anchor.updatedAt = resource.updatedAt;
    resource.anchors = {anchor};
    return resource;
}

Clip savedClip(const QString &id,
               const QString &name,
               const QStringList &aliases = {})
{
    Clip clip;
    clip.id = id;
    clip.state = ClipState::Saved;
    clip.text = QStringLiteral("Identity test body for %1").arg(id);
    clip.name = name;
    clip.aliases = aliases;
    clip.createdAt = QDateTime::currentDateTimeUtc();
    clip.updatedAt = clip.createdAt;
    return clip;
}

bool storeIdentity(GlobalIdentityObjectType type,
                   const QString &id,
                   const QString &name,
                   const QStringList &aliases,
                   InMemoryLibraryRepository &library,
                   InMemoryClipRepository &clips)
{
    switch (type) {
    case GlobalIdentityObjectType::File:
        return library.upsertResource(fileResource(QStringLiteral("file-%1").arg(id),
                                                   name,
                                                   aliases));
    case GlobalIdentityObjectType::Anchor:
        return library.upsertResource(anchorResource(id, name, aliases));
    case GlobalIdentityObjectType::Clip:
        return clips.upsertPersistentClip(savedClip(QStringLiteral("clip-%1").arg(id),
                                                    name,
                                                    aliases));
    }
    return false;
}

std::optional<GlobalIdentityConflict> lastConflict(
    GlobalIdentityObjectType type,
    const InMemoryLibraryRepository &library,
    const InMemoryClipRepository &clips)
{
    return type == GlobalIdentityObjectType::Clip
        ? clips.lastIdentityConflict()
        : library.lastIdentityConflict();
}

QString lastError(GlobalIdentityObjectType type,
                  const InMemoryLibraryRepository &library,
                  const InMemoryClipRepository &clips)
{
    return type == GlobalIdentityObjectType::Clip
        ? clips.lastError()
        : library.lastError();
}

struct RaceResult {
    bool initialized = false;
    bool stored = false;
    QString error;
    std::optional<GlobalIdentityConflict> conflict;
};

} // namespace

class GlobalIdentityTest : public QObject {
    Q_OBJECT

private slots:
    void normalizesWithTrimNfkcAndCaseFoldWithoutCollapsingInternalWhitespace();
    void inMemoryRejectsCompleteCrossObjectMatrixWithStructuredConflicts();
    void inMemoryRejectsSelfDuplicatesAndPreservesDisplayValues();
    void inMemorySupportsSelfEditBatchSwapReleaseReuseAndTrashRestore();
    void sqliteMatchesInMemoryAndKeepsBatchMutationsAtomic();
    void sqliteSerializesCrossDatabaseRaces();
    void sqliteMigrationReportsHistoricalConflictsAndAllowsRepair();
    void sqliteDatabaseRestoreRebuildsHistoricalConflicts();
    void serviceWritePathsPreserveDisplayValuesAndSurfaceConflicts();
    void obsidianSyncReceivesRepositoryConflict();
};

void GlobalIdentityTest::normalizesWithTrimNfkcAndCaseFoldWithoutCollapsingInternalWhitespace()
{
    QCOMPARE(normalizedGlobalIdentity(QStringLiteral("  ＦｏＯ  ")), QStringLiteral("foo"));
    QCOMPARE(normalizedGlobalIdentity(QStringLiteral("Straße")),
             normalizedGlobalIdentity(QStringLiteral("STRASSE")));
    QCOMPARE(normalizedGlobalIdentity(QStringLiteral("A  B")), QStringLiteral("a  b"));
    QVERIFY(normalizedGlobalIdentity(QStringLiteral("A  B"))
            != normalizedGlobalIdentity(QStringLiteral("A B")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QCOMPARE(defaultGlobalIdentityRegistryPath(
                 directory.filePath(QStringLiteral("library.sqlite3"))),
             defaultGlobalIdentityRegistryPath(
                 directory.filePath(QStringLiteral("clips.sqlite3"))));

    const QUrl fileLocator(globalIdentityLocator(GlobalIdentityObjectType::File,
                                                 QStringLiteral("file id")));
    QCOMPARE(fileLocator.scheme(), QStringLiteral("pinloom"));
    QCOMPARE(fileLocator.host(), QStringLiteral("entry"));
    QCOMPARE(QUrlQuery(fileLocator).queryItemValue(QStringLiteral("resource")),
             QStringLiteral("file id"));
    const QUrl clipLocator(globalIdentityLocator(GlobalIdentityObjectType::Clip,
                                                 QStringLiteral("clip id")));
    QCOMPARE(clipLocator.host(), QStringLiteral("clip"));
    QCOMPARE(clipLocator.path(QUrl::FullyDecoded), QStringLiteral("/clip id"));
    const QUrl anchorLocator(globalIdentityLocator(GlobalIdentityObjectType::Anchor,
                                                   QStringLiteral("anchor id"),
                                                   QStringLiteral("parent id")));
    QCOMPARE(anchorLocator.host(), QStringLiteral("anchor"));
    QCOMPARE(anchorLocator.path(QUrl::FullyDecoded), QStringLiteral("/anchor id"));
}

void GlobalIdentityTest::inMemoryRejectsCompleteCrossObjectMatrixWithStructuredConflicts()
{
    struct FieldPattern {
        bool existingName = false;
        bool candidateName = false;
        const char *label = nullptr;
    };
    const QList<FieldPattern> patterns = {
        {true, true, "name-name"},
        {true, false, "name-alias"},
        {false, true, "alias-name"},
        {false, false, "alias-alias"}
    };
    const QList<GlobalIdentityObjectType> types = {
        GlobalIdentityObjectType::File,
        GlobalIdentityObjectType::Clip,
        GlobalIdentityObjectType::Anchor
    };

    int caseIndex = 0;
    for (GlobalIdentityObjectType existingType : types) {
        for (GlobalIdentityObjectType candidateType : types) {
            for (const FieldPattern &pattern : patterns) {
                ++caseIndex;
                const auto registry = createInMemoryGlobalIdentityRegistry();
                InMemoryLibraryRepository library(registry);
                InMemoryClipRepository clips(registry);
                const QString collision = QStringLiteral("Shared Identity %1").arg(caseIndex);
                const QString existingName = pattern.existingName
                    ? collision
                    : QStringLiteral("Existing unique name %1").arg(caseIndex);
                const QStringList existingAliases = pattern.existingName
                    ? QStringList{QStringLiteral("Existing unique alias %1").arg(caseIndex)}
                    : QStringList{collision};
                const QString candidateName = pattern.candidateName
                    ? QStringLiteral("  %1  ").arg(collision.toUpper())
                    : QStringLiteral("Candidate unique name %1").arg(caseIndex);
                const QStringList candidateAliases = pattern.candidateName
                    ? QStringList{QStringLiteral("Candidate unique alias %1").arg(caseIndex)}
                    : QStringList{QStringLiteral("  %1  ").arg(collision.toUpper())};
                const QString context = QStringLiteral("case %1 (%2)")
                                            .arg(caseIndex)
                                            .arg(QString::fromLatin1(pattern.label));

                QVERIFY2(storeIdentity(existingType,
                                       QStringLiteral("existing-%1").arg(caseIndex),
                                       existingName,
                                       existingAliases,
                                       library,
                                       clips),
                         qPrintable(context + QStringLiteral(": failed to seed existing identity")));
                QVERIFY2(!storeIdentity(candidateType,
                                        QStringLiteral("candidate-%1").arg(caseIndex),
                                        candidateName,
                                        candidateAliases,
                                        library,
                                        clips),
                         qPrintable(context + QStringLiteral(": conflicting identity was accepted")));

                const std::optional<GlobalIdentityConflict> conflict =
                    lastConflict(candidateType, library, clips);
                QVERIFY2(conflict.has_value(), qPrintable(context));
                QCOMPARE(conflict->normalizedValue, normalizedGlobalIdentity(collision));
                QVERIFY(conflict->attemptedClaim.has_value());
                QCOMPARE(conflict->attemptedClaim->owner.objectType, candidateType);
                QVERIFY(!conflict->attemptedClaim->owner.objectId.isEmpty());
                QVERIFY(!conflict->attemptedClaim->owner.locator.isEmpty());
                QVERIFY(!conflict->conflictingClaims.isEmpty());
                QCOMPARE(conflict->conflictingClaims.first().owner.objectType, existingType);
                QVERIFY(!conflict->conflictingClaims.first().owner.objectId.isEmpty());
                QVERIFY(!conflict->conflictingClaims.first().owner.displayName.isEmpty());
                QVERIFY(!conflict->conflictingClaims.first().owner.locator.isEmpty());
                const QString error = lastError(candidateType, library, clips);
                QVERIFY(error.contains(QStringLiteral("conflicts with"), Qt::CaseInsensitive));
                QVERIFY(error.contains(conflict->conflictingClaims.first().owner.objectId));
            }
        }
    }
}

void GlobalIdentityTest::inMemoryRejectsSelfDuplicatesAndPreservesDisplayValues()
{
    const auto registry = createInMemoryGlobalIdentityRegistry();
    InMemoryLibraryRepository library(registry);
    InMemoryClipRepository clips(registry);

    Resource file = fileResource(QStringLiteral("self-file"),
                                 QStringLiteral("File Identity"),
                                 {QStringLiteral(" file identity ")});
    QVERIFY(!library.upsertResource(file));
    QVERIFY(library.lastIdentityConflict().has_value());
    QVERIFY(!library.findResource(file.id).has_value());

    Resource anchor = anchorResource(QStringLiteral("self-anchor"),
                                     QStringLiteral("Anchor Identity"),
                                     {QStringLiteral("Anchor Alias"),
                                      QStringLiteral(" anchor alias ")});
    QVERIFY(!library.upsertResource(anchor));
    QVERIFY(library.lastIdentityConflict().has_value());
    QVERIFY(!library.findResource(anchor.id).has_value());

    Clip clip = savedClip(QStringLiteral("self-clip"),
                          QStringLiteral("Clip Identity"),
                          {QStringLiteral(" clip identity ")});
    QVERIFY(!clips.upsertPersistentClip(clip));
    QVERIFY(clips.lastIdentityConflict().has_value());
    QVERIFY(!clips.findClip(clip.id).has_value());

    const QString displayedFileName = QStringLiteral("  Display  Name  ");
    Resource displayed = fileResource(QStringLiteral("display-file"), displayedFileName);
    QVERIFY2(library.upsertResource(displayed), qPrintable(library.lastError()));
    QCOMPARE(library.findResource(displayed.id)->title, displayedFileName);

    Clip displayedClip = savedClip(QStringLiteral("display-clip"),
                                   QStringLiteral("A B"),
                                   {QStringLiteral("  Alias  Value  ")});
    QVERIFY2(clips.upsertPersistentClip(displayedClip), qPrintable(clips.lastError()));
    QCOMPARE(clips.findClip(displayedClip.id)->aliases,
             QStringList{QStringLiteral("  Alias  Value  ")});

    Resource internalWhitespace = fileResource(QStringLiteral("internal-space"),
                                               QStringLiteral("A  B"));
    QVERIFY2(library.upsertResource(internalWhitespace), qPrintable(library.lastError()));

    Clip compatibility = savedClip(QStringLiteral("compatibility"),
                                   QStringLiteral("ＤＩＳＰＬＡＹ  ＮＡＭＥ"));
    QVERIFY(!clips.upsertPersistentClip(compatibility));
    QCOMPARE(clips.lastIdentityConflict()->normalizedValue,
             normalizedGlobalIdentity(displayedFileName));
}

void GlobalIdentityTest::inMemorySupportsSelfEditBatchSwapReleaseReuseAndTrashRestore()
{
    const auto registry = createInMemoryGlobalIdentityRegistry();
    InMemoryLibraryRepository library(registry);
    InMemoryClipRepository clips(registry);

    Resource first = fileResource(QStringLiteral("swap-a"), QStringLiteral("Alpha Identity"));
    Resource second = fileResource(QStringLiteral("swap-b"), QStringLiteral("Beta Identity"));
    QVERIFY(library.upsertResource(first));
    QVERIFY(library.upsertResource(second));
    QVERIFY(library.upsertResource(first));

    Resource swappedFirst = first;
    Resource swappedSecond = second;
    swappedFirst.title = second.title;
    swappedSecond.title = first.title;
    LibraryBatchMutation swap;
    swap.upserts = {swappedFirst, swappedSecond};
    QVERIFY2(library.applyBatch(swap), qPrintable(library.lastError()));
    QCOMPARE(library.findResource(first.id)->title, QStringLiteral("Beta Identity"));
    QCOMPARE(library.findResource(second.id)->title, QStringLiteral("Alpha Identity"));

    Resource reserved = fileResource(QStringLiteral("reserved"), QStringLiteral("Reserved Identity"));
    QVERIFY(library.upsertResource(reserved));
    LibraryBatchMutation atomicFailure;
    atomicFailure.upserts = {
        fileResource(QStringLiteral("atomic-unique"), QStringLiteral("Atomic Unique")),
        fileResource(QStringLiteral("atomic-conflict"),
                     QStringLiteral("Atomic Other"),
                     {QStringLiteral(" reserved identity ")})
    };
    QVERIFY(!library.applyBatch(atomicFailure));
    QVERIFY(!library.findResource(QStringLiteral("atomic-unique")).has_value());
    QVERIFY(!library.findResource(QStringLiteral("atomic-conflict")).has_value());

    Resource file = fileResource(QStringLiteral("trash-file"), QStringLiteral("Reusable File"));
    QVERIFY(library.upsertResource(file));
    QVERIFY(library.softDeleteResource(file.id));
    Clip fileReplacement = savedClip(QStringLiteral("file-replacement"), QStringLiteral("reusable file"));
    QVERIFY(clips.upsertPersistentClip(fileReplacement));
    QVERIFY(!library.restoreResource(file.id));
    QVERIFY(library.lastIdentityConflict().has_value());
    QVERIFY(clips.softDeleteSavedClip(fileReplacement.id));
    QVERIFY2(library.restoreResource(file.id), qPrintable(library.lastError()));

    Resource anchored = anchorResource(QStringLiteral("trash-anchor"), QStringLiteral("Reusable Anchor"));
    QVERIFY(library.upsertResource(anchored));
    const Anchor anchor = anchored.anchors.first();
    QVERIFY(library.softDeleteAnchor(anchored.id, anchor));
    Resource anchorReplacement = fileResource(QStringLiteral("anchor-replacement"),
                                              QStringLiteral("reusable anchor"));
    QVERIFY(library.upsertResource(anchorReplacement));
    QVERIFY(!library.restoreAnchor(anchored.id, anchor));
    QVERIFY(library.softDeleteResource(anchorReplacement.id));
    QVERIFY2(library.restoreAnchor(anchored.id, anchor), qPrintable(library.lastError()));

    Clip trashedClip = savedClip(QStringLiteral("trash-clip"), QStringLiteral("Reusable Clip"));
    QVERIFY(clips.upsertPersistentClip(trashedClip));
    QVERIFY(clips.softDeleteSavedClip(trashedClip.id));
    Resource clipReplacement = anchorResource(QStringLiteral("clip-replacement"),
                                              QStringLiteral("reusable clip"));
    QVERIFY(library.upsertResource(clipReplacement));
    QVERIFY(!clips.restoreClip(trashedClip.id));
    QVERIFY(library.softDeleteAnchor(clipReplacement.id,
                                     clipReplacement.anchors.first()));
    QVERIFY2(clips.restoreClip(trashedClip.id), qPrintable(clips.lastError()));
}

void GlobalIdentityTest::sqliteMatchesInMemoryAndKeepsBatchMutationsAtomic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString registryPath = dir.filePath(QStringLiteral("identity.sqlite3"));
    SqliteLibraryRepository library;
    SqliteClipRepository clips;
    QVERIFY2(library.open(dir.filePath(QStringLiteral("library.sqlite3")), registryPath),
             qPrintable(library.lastError()));
    QVERIFY2(library.initialize(), qPrintable(library.lastError()));
    QVERIFY2(clips.open(dir.filePath(QStringLiteral("clips.sqlite3")), registryPath),
             qPrintable(clips.lastError()));
    QVERIFY2(clips.initialize(), qPrintable(clips.lastError()));

    Resource file = fileResource(QStringLiteral("sqlite-file"), QStringLiteral("Ｆｏｏ Identity"));
    QVERIFY2(library.upsertResource(file), qPrintable(library.lastError()));
    Clip conflict = savedClip(QStringLiteral("sqlite-clip"), QStringLiteral(" foo identity "));
    QVERIFY(!clips.upsertPersistentClip(conflict));
    QVERIFY(clips.lastIdentityConflict().has_value());
    QCOMPARE(clips.lastIdentityConflict()->conflictingClaims.first().owner.objectType,
             GlobalIdentityObjectType::File);
    QVERIFY(!clips.findClip(conflict.id).has_value());

    QVERIFY(library.softDeleteResource(file.id));
    QVERIFY2(clips.upsertPersistentClip(conflict), qPrintable(clips.lastError()));
    QVERIFY(!library.restoreResource(file.id));
    QVERIFY(clips.softDeleteSavedClip(conflict.id));
    QVERIFY2(library.restoreResource(file.id), qPrintable(library.lastError()));

    Resource reserved = fileResource(QStringLiteral("sqlite-reserved"),
                                     QStringLiteral("SQLite Reserved"));
    QVERIFY(library.upsertResource(reserved));
    LibraryBatchMutation batch;
    batch.upserts = {
        fileResource(QStringLiteral("sqlite-atomic-unique"), QStringLiteral("SQLite Atomic Unique")),
        fileResource(QStringLiteral("sqlite-atomic-conflict"),
                     QStringLiteral("SQLite Atomic Other"),
                     {QStringLiteral("sqlite reserved")})
    };
    QVERIFY(!library.applyBatch(batch));
    QVERIFY(!library.findResource(QStringLiteral("sqlite-atomic-unique")).has_value());
    QVERIFY(!library.findResource(QStringLiteral("sqlite-atomic-conflict")).has_value());

    Resource swapA = fileResource(QStringLiteral("sqlite-swap-a"), QStringLiteral("SQLite Swap A"));
    Resource swapB = fileResource(QStringLiteral("sqlite-swap-b"), QStringLiteral("SQLite Swap B"));
    QVERIFY(library.upsertResource(swapA));
    QVERIFY(library.upsertResource(swapB));
    std::swap(swapA.title, swapB.title);
    LibraryBatchMutation swap;
    swap.upserts = {swapA, swapB};
    QVERIFY2(library.applyBatch(swap), qPrintable(library.lastError()));
    QVERIFY(library.integrityCheck());
    QVERIFY(clips.integrityCheck());
}

void GlobalIdentityTest::sqliteSerializesCrossDatabaseRaces()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString libraryPath = dir.filePath(QStringLiteral("race-library.sqlite3"));
    const QString clipPath = dir.filePath(QStringLiteral("race-clips.sqlite3"));
    const QString registryPath = dir.filePath(QStringLiteral("race-identity.sqlite3"));
    {
        SqliteLibraryRepository library;
        QVERIFY(library.open(libraryPath, registryPath));
        QVERIFY(library.initialize());
        SqliteClipRepository clips;
        QVERIFY(clips.open(clipPath, registryPath));
        QVERIFY(clips.initialize());
    }

    QMutex initializeMutex;
    QSemaphore ready;
    QSemaphore start;
    auto fileFuture = std::async(std::launch::async, [&]() {
        SqliteLibraryRepository library;
        RaceResult result;
        {
            QMutexLocker locker(&initializeMutex);
            result.initialized = library.open(libraryPath, registryPath) && library.initialize();
            result.error = library.lastError();
        }
        ready.release();
        start.acquire();
        if (result.initialized) {
            result.stored = library.upsertResource(
                fileResource(QStringLiteral("race-file"), QStringLiteral("Race Identity")));
            result.error = library.lastError();
            result.conflict = library.lastIdentityConflict();
        }
        return result;
    });
    auto clipFuture = std::async(std::launch::async, [&]() {
        SqliteClipRepository clips;
        RaceResult result;
        {
            QMutexLocker locker(&initializeMutex);
            result.initialized = clips.open(clipPath, registryPath) && clips.initialize();
            result.error = clips.lastError();
        }
        ready.release();
        start.acquire();
        if (result.initialized) {
            result.stored = clips.upsertPersistentClip(
                savedClip(QStringLiteral("race-clip"), QStringLiteral(" race identity ")));
            result.error = clips.lastError();
            result.conflict = clips.lastIdentityConflict();
        }
        return result;
    });
    ready.acquire(2);
    start.release(2);
    const RaceResult fileResult = fileFuture.get();
    const RaceResult clipResult = clipFuture.get();
    QVERIFY2(fileResult.initialized, qPrintable(fileResult.error));
    QVERIFY2(clipResult.initialized, qPrintable(clipResult.error));
    QCOMPARE(static_cast<int>(fileResult.stored) + static_cast<int>(clipResult.stored), 1);
    const RaceResult loser = fileResult.stored ? clipResult : fileResult;
    QVERIFY(loser.conflict.has_value());
    QCOMPARE(loser.conflict->normalizedValue, QStringLiteral("race identity"));
    QVERIFY(loser.error.contains(QStringLiteral("conflicts with"), Qt::CaseInsensitive));
}

void GlobalIdentityTest::sqliteMigrationReportsHistoricalConflictsAndAllowsRepair()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString libraryPath = dir.filePath(QStringLiteral("legacy-library.sqlite3"));
    const QString clipPath = dir.filePath(QStringLiteral("legacy-clips.sqlite3"));
    {
        SqliteLibraryRepository library;
        QVERIFY(library.open(libraryPath,
                             dir.filePath(QStringLiteral("legacy-library-identity.sqlite3"))));
        QVERIFY(library.initialize());
        QVERIFY(library.upsertResource(
            fileResource(QStringLiteral("legacy-file"), QStringLiteral("Legacy Shared"))));
    }
    {
        SqliteClipRepository clips;
        QVERIFY(clips.open(clipPath,
                           dir.filePath(QStringLiteral("legacy-clip-identity.sqlite3"))));
        QVERIFY(clips.initialize());
        QVERIFY(clips.upsertPersistentClip(
            savedClip(QStringLiteral("legacy-clip"), QStringLiteral(" legacy shared "))));
    }

    const QString sharedRegistry = dir.filePath(QStringLiteral("migrated-identity.sqlite3"));
    SqliteLibraryRepository library;
    SqliteClipRepository clips;
    QVERIFY2(library.open(libraryPath, sharedRegistry), qPrintable(library.lastError()));
    QVERIFY2(library.initialize(), qPrintable(library.lastError()));
    QVERIFY2(clips.open(clipPath, sharedRegistry), qPrintable(clips.lastError()));
    QVERIFY2(clips.initialize(), qPrintable(clips.lastError()));
    QVERIFY2(clips.initialize(), qPrintable(clips.lastError()));

    const QList<GlobalIdentityConflict> conflicts = clips.identityConflicts();
    QCOMPARE(conflicts.size(), 1);
    QCOMPARE(conflicts.first().normalizedValue, QStringLiteral("legacy shared"));
    QCOMPARE(conflicts.first().conflictingClaims.size(), 2);
    QVERIFY(conflicts.first().message().contains(QStringLiteral("Historical identity conflict")));
    QVERIFY(library.findResource(QStringLiteral("legacy-file")).has_value());
    QVERIFY(clips.findClip(QStringLiteral("legacy-clip")).has_value());
    QCOMPARE(library.findResource(QStringLiteral("legacy-file"))->title,
             QStringLiteral("Legacy Shared"));
    QCOMPARE(clips.findClip(QStringLiteral("legacy-clip"))->name,
             QStringLiteral(" legacy shared "));

    Clip repaired = clips.findClip(QStringLiteral("legacy-clip")).value();
    repaired.name = QStringLiteral("Legacy Clip Repaired");
    QVERIFY2(clips.upsertPersistentClip(repaired), qPrintable(clips.lastError()));
    QVERIFY(clips.identityConflicts().isEmpty());
    QVERIFY(library.integrityCheck());
    QVERIFY(clips.integrityCheck());
}

void GlobalIdentityTest::sqliteDatabaseRestoreRebuildsHistoricalConflicts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString registryPath = dir.filePath(QStringLiteral("identity.sqlite3"));
    const QString libraryPath = dir.filePath(QStringLiteral("library.sqlite3"));
    const QString clipPath = dir.filePath(QStringLiteral("clips.sqlite3"));
    const QString backupPath = dir.filePath(QStringLiteral("backups/library.sqlite3"));

    SqliteLibraryRepository library;
    SqliteClipRepository clips;
    QVERIFY2(library.open(libraryPath, registryPath), qPrintable(library.lastError()));
    QVERIFY2(library.initialize(), qPrintable(library.lastError()));
    QVERIFY2(clips.open(clipPath, registryPath), qPrintable(clips.lastError()));
    QVERIFY2(clips.initialize(), qPrintable(clips.lastError()));
    QVERIFY2(clips.upsertPersistentClip(
                 savedClip(QStringLiteral("restore-clip"), QStringLiteral("Restored Identity"))),
             qPrintable(clips.lastError()));

    {
        SqliteLibraryRepository source;
        QVERIFY2(source.open(dir.filePath(QStringLiteral("source.sqlite3")),
                             dir.filePath(QStringLiteral("source-identity.sqlite3"))),
                 qPrintable(source.lastError()));
        QVERIFY2(source.initialize(), qPrintable(source.lastError()));
        QVERIFY2(source.upsertResource(
                     fileResource(QStringLiteral("restored-file"),
                                  QStringLiteral(" restored identity "))),
                 qPrintable(source.lastError()));
        QVERIFY2(source.backupDatabase(backupPath), qPrintable(source.lastError()));
    }

    QVERIFY2(library.restoreDatabase(backupPath), qPrintable(library.lastError()));
    QVERIFY(library.findResource(QStringLiteral("restored-file")).has_value());
    QVERIFY(clips.findClip(QStringLiteral("restore-clip")).has_value());
    const QList<GlobalIdentityConflict> conflicts = library.identityConflicts();
    QCOMPARE(conflicts.size(), 1);
    QCOMPARE(conflicts.first().normalizedValue, QStringLiteral("restored identity"));
    QCOMPARE(conflicts.first().conflictingClaims.size(), 2);

    Resource repaired = library.findResource(QStringLiteral("restored-file")).value();
    repaired.title = QStringLiteral("Restored File Repaired");
    QVERIFY2(library.upsertResource(repaired), qPrintable(library.lastError()));
    QVERIFY(library.identityConflicts().isEmpty());
    QVERIFY(library.integrityCheck());
    QVERIFY(clips.integrityCheck());
}

void GlobalIdentityTest::serviceWritePathsPreserveDisplayValuesAndSurfaceConflicts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto registry = createInMemoryGlobalIdentityRegistry();
    InMemoryLibraryRepository library(registry);
    InMemoryClipRepository clips(registry);

    QVERIFY(library.upsertResource(
        fileResource(QStringLiteral("clipboard-file"), QStringLiteral("Clipboard Collision"))));
    const ClipCaptureResult captured = clips.captureText(QStringLiteral("clipboard body"));
    QVERIFY(captured.captured());
    QVERIFY(!clips.saveClip(captured.clip->id, QStringLiteral(" clipboard collision ")));
    QCOMPARE(clips.findClip(captured.clip->id)->state, ClipState::Temporary);
    QVERIFY(clips.lastError().contains(QStringLiteral("clipboard-file")));

    const QString importedPath = dir.filePath(QStringLiteral("imported.txt"));
    QFile importedFile(importedPath);
    QVERIFY(importedFile.open(QIODevice::WriteOnly));
    QCOMPARE(importedFile.write("imported"), qint64(8));
    importedFile.close();
    InboxFileSaveRequest importedRequest;
    importedRequest.filePath = importedPath;
    importedRequest.name = QStringLiteral("  Imported  Display  ");
    importedRequest.aliases = {QStringLiteral("  Imported Alias  ")};
    const InboxFileSaveResult imported = saveInboxFile(library, importedRequest);
    QVERIFY2(imported.success(), qPrintable(imported.status));
    const std::optional<Resource> importedResource = library.findResource(imported.resourceId);
    QVERIFY(importedResource.has_value());
    QCOMPARE(importedResource->title, importedRequest.name);
    QCOMPARE(importedResource->aliases, importedRequest.aliases);

    QVERIFY2(clips.upsertPersistentClip(
                 savedClip(QStringLiteral("inbox-conflict-clip"),
                           QStringLiteral("Inbox Collision"))),
             qPrintable(clips.lastError()));
    const QString conflictingPath = dir.filePath(QStringLiteral("conflicting.txt"));
    QFile conflictingFile(conflictingPath);
    QVERIFY(conflictingFile.open(QIODevice::WriteOnly));
    QCOMPARE(conflictingFile.write("conflict"), qint64(8));
    conflictingFile.close();
    InboxFileSaveRequest conflictingRequest;
    conflictingRequest.filePath = conflictingPath;
    conflictingRequest.name = QStringLiteral(" inbox collision ");
    const InboxFileSaveResult conflicting = saveInboxFile(library, conflictingRequest);
    QVERIFY(!conflicting.success());
    QVERIFY(conflicting.status.contains(QStringLiteral("inbox-conflict-clip")));
    QVERIFY(!library.findResource(inboxResourceIdForPath(conflictingPath)).has_value());

    QVERIFY2(clips.upsertPersistentClip(
                 savedClip(QStringLiteral("archive-conflict-clip"),
                           QStringLiteral("Archive Collision"))),
             qPrintable(clips.lastError()));
    InMemoryLibraryRepository staging;
    QVERIFY(staging.upsertResource(
        fileResource(QStringLiteral("archive-unique"), QStringLiteral("Archive Unique"))));
    QVERIFY(staging.upsertResource(
        fileResource(QStringLiteral("archive-conflict"), QStringLiteral(" archive collision "))));
    const QString archivePath = dir.filePath(QStringLiteral("archive.json"));
    AnchorLibraryArchiveService stagingArchive(staging);
    const AnchorLibraryOperationResult exported = stagingArchive.exportJson(archivePath);
    QVERIFY2(exported.success, qPrintable(exported.message));
    AnchorLibraryArchiveService targetArchive(library);
    const AnchorLibraryOperationResult importedArchive =
        targetArchive.importJson(archivePath, AnchorLibraryImportMode::Merge);
    QVERIFY(!importedArchive.success);
    QVERIFY(importedArchive.message.contains(QStringLiteral("archive-conflict-clip")));
    QVERIFY(!library.findResource(QStringLiteral("archive-unique")).has_value());
    QVERIFY(!library.findResource(QStringLiteral("archive-conflict")).has_value());
}

void GlobalIdentityTest::obsidianSyncReceivesRepositoryConflict()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral(".obsidian")));
    const auto registry = createInMemoryGlobalIdentityRegistry();
    InMemoryLibraryRepository library(registry);
    InMemoryClipRepository clips(registry);
    QVERIFY(library.upsertResource(
        fileResource(QStringLiteral("sync-file"), QStringLiteral("Synchronized Identity"))));

    ObsidianClipStoreConfig config;
    config.vaultPath = dir.path();
    config.archiveDirectory = QStringLiteral("Pinloom Clips");
    ObsidianClipStore store(config);
    Clip document = savedClip(QStringLiteral("sync-clip"),
                              QStringLiteral(" synchronized identity "));
    document.storageBackend = ClipStorageBackend::Obsidian;
    const ObsidianClipWriteResult written = store.writeClip(document);
    QVERIFY2(written.succeeded(), qPrintable(written.error));
    QVERIFY(QFileInfo::exists(written.filePath));

    const ObsidianClipSyncResult result = store.synchronize(clips);
    QCOMPARE(result.skipped, 1);
    QVERIFY(!result.errors.isEmpty());
    QVERIFY(result.errors.first().contains(QStringLiteral("conflicts with"),
                                           Qt::CaseInsensitive));
    QVERIFY(!clips.findClip(document.id).has_value());
    QVERIFY(clips.lastIdentityConflict().has_value());
}

QTEST_GUILESS_MAIN(GlobalIdentityTest)

#include "global_identity_test.moc"
