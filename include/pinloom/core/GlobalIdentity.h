#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <memory>
#include <optional>

namespace Pinloom {

enum class GlobalIdentityObjectType {
    File,
    Clip,
    Anchor
};

enum class GlobalIdentityFieldKind {
    Name,
    Alias
};

struct GlobalIdentityOwner {
    GlobalIdentityObjectType objectType = GlobalIdentityObjectType::File;
    QString objectId;
    QString parentId;
    QString displayName;
    QString locator;
};

struct GlobalIdentityValue {
    GlobalIdentityFieldKind fieldKind = GlobalIdentityFieldKind::Name;
    QString displayValue;
};

struct GlobalIdentityObject {
    GlobalIdentityOwner owner;
    QList<GlobalIdentityValue> values;
    bool active = true;
};

struct GlobalIdentityClaim {
    QString normalizedValue;
    GlobalIdentityOwner owner;
    GlobalIdentityFieldKind fieldKind = GlobalIdentityFieldKind::Name;
    QString displayValue;
    int fieldIndex = 0;
};

struct GlobalIdentityConflict {
    QString normalizedValue;
    std::optional<GlobalIdentityClaim> attemptedClaim;
    QList<GlobalIdentityClaim> conflictingClaims;

    QString message() const;
};

QString normalizedGlobalIdentity(const QString &value);
QString globalIdentityOwnerKey(const GlobalIdentityOwner &owner);
QString globalIdentityObjectTypeName(GlobalIdentityObjectType type);
QString globalIdentityFieldKindName(GlobalIdentityFieldKind kind);
QString globalIdentityLocator(GlobalIdentityObjectType type,
                              const QString &objectId,
                              const QString &parentId = {});
QString defaultGlobalIdentityRegistryPath(const QString &databasePath);

// Submit only changed owners. Unchanged historical conflicts must not prevent
// removing another owner's claims (for example, moving its Anchor to Trash).
void collectGlobalIdentityReplacements(
    QHash<QString, GlobalIdentityObject> &updates,
    const QList<GlobalIdentityObject> &before,
    const QList<GlobalIdentityObject> &after);

class InMemoryGlobalIdentityRegistry final {
public:
    bool replaceObjects(const QList<GlobalIdentityObject> &objects,
                        std::optional<GlobalIdentityConflict> *conflict = nullptr);
    void rebuildHistoricalObjects(const QList<GlobalIdentityObjectType> &objectTypes,
                                  const QList<GlobalIdentityObject> &objects);
    QList<GlobalIdentityConflict> conflicts() const;

private:
    mutable QMutex mutex_;
    QHash<QString, QList<GlobalIdentityClaim>> claims_;
};

using SharedInMemoryGlobalIdentityRegistry = std::shared_ptr<InMemoryGlobalIdentityRegistry>;

SharedInMemoryGlobalIdentityRegistry createInMemoryGlobalIdentityRegistry();

bool attachSqliteGlobalIdentityRegistry(QSqlDatabase &database,
                                        const QString &registryPath,
                                        QString *error = nullptr);
bool initializeSqliteGlobalIdentityRegistry(QSqlDatabase &database,
                                            QString *error = nullptr);
bool replaceSqliteGlobalIdentityObjects(QSqlDatabase &database,
                                        const QList<GlobalIdentityObject> &objects,
                                        std::optional<GlobalIdentityConflict> *conflict = nullptr,
                                        QString *error = nullptr);
bool rebuildSqliteHistoricalIdentityObjects(
    QSqlDatabase &database,
    const QList<GlobalIdentityObjectType> &objectTypes,
    const QList<GlobalIdentityObject> &objects,
    QString *error = nullptr);
QList<GlobalIdentityConflict> sqliteGlobalIdentityConflicts(QSqlDatabase &database,
                                                            QString *error = nullptr);

} // namespace Pinloom
