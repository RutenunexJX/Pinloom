#include "pinloom/core/GlobalIdentity.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <algorithm>

namespace Pinloom {

namespace {

constexpr auto RegistrySchema = "identity_registry";

QString objectTypeToStorage(GlobalIdentityObjectType type)
{
    switch (type) {
    case GlobalIdentityObjectType::File:
        return QStringLiteral("file");
    case GlobalIdentityObjectType::Clip:
        return QStringLiteral("clip");
    case GlobalIdentityObjectType::Anchor:
        return QStringLiteral("anchor");
    }
    return QStringLiteral("file");
}

GlobalIdentityObjectType objectTypeFromStorage(const QString &value)
{
    if (value == QLatin1String("clip")) {
        return GlobalIdentityObjectType::Clip;
    }
    if (value == QLatin1String("anchor")) {
        return GlobalIdentityObjectType::Anchor;
    }
    return GlobalIdentityObjectType::File;
}

QString fieldKindToStorage(GlobalIdentityFieldKind kind)
{
    return kind == GlobalIdentityFieldKind::Alias
        ? QStringLiteral("alias")
        : QStringLiteral("name");
}

GlobalIdentityFieldKind fieldKindFromStorage(const QString &value)
{
    return value == QLatin1String("alias")
        ? GlobalIdentityFieldKind::Alias
        : GlobalIdentityFieldKind::Name;
}

bool sameOwner(const GlobalIdentityOwner &left, const GlobalIdentityOwner &right)
{
    return left.objectType == right.objectType
        && left.objectId == right.objectId
        && left.parentId == right.parentId;
}

QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

void appendFullCaseFold(QString *output, uint codePoint)
{
    const auto append = [output](const char16_t *value) {
        output->append(QStringView(value));
    };

    if (codePoint >= 0x1F80 && codePoint <= 0x1F8F) {
        output->append(QChar(static_cast<ushort>(0x1F00 + ((codePoint - 0x1F80) & 0x7))));
        output->append(QChar(0x03B9));
        return;
    }
    if (codePoint >= 0x1F90 && codePoint <= 0x1F9F) {
        output->append(QChar(static_cast<ushort>(0x1F20 + ((codePoint - 0x1F90) & 0x7))));
        output->append(QChar(0x03B9));
        return;
    }
    if (codePoint >= 0x1FA0 && codePoint <= 0x1FAF) {
        output->append(QChar(static_cast<ushort>(0x1F60 + ((codePoint - 0x1FA0) & 0x7))));
        output->append(QChar(0x03B9));
        return;
    }

    // QString::toCaseFolded() implements simple folds. These are the full-fold
    // expansions that can remain after NFKC normalization (Unicode CaseFolding.txt).
    switch (codePoint) {
    case 0x00DF:
    case 0x1E9E: append(u"ss"); return;
    case 0x0130: append(u"i\u0307"); return;
    case 0x01F0: append(u"j\u030C"); return;
    case 0x0390: append(u"\u03B9\u0308\u0301"); return;
    case 0x03B0: append(u"\u03C5\u0308\u0301"); return;
    case 0x1E96: append(u"h\u0331"); return;
    case 0x1E97: append(u"t\u0308"); return;
    case 0x1E98: append(u"w\u030A"); return;
    case 0x1E99: append(u"y\u030A"); return;
    case 0x1F50: append(u"\u03C5\u0313"); return;
    case 0x1F52: append(u"\u03C5\u0313\u0300"); return;
    case 0x1F54: append(u"\u03C5\u0313\u0301"); return;
    case 0x1F56: append(u"\u03C5\u0313\u0342"); return;
    case 0x1FB2: append(u"\u1F70\u03B9"); return;
    case 0x1FB3:
    case 0x1FBC: append(u"\u03B1\u03B9"); return;
    case 0x1FB4: append(u"\u03AC\u03B9"); return;
    case 0x1FB6: append(u"\u03B1\u0342"); return;
    case 0x1FB7: append(u"\u03B1\u0342\u03B9"); return;
    case 0x1FC2: append(u"\u1F74\u03B9"); return;
    case 0x1FC3:
    case 0x1FCC: append(u"\u03B7\u03B9"); return;
    case 0x1FC4: append(u"\u03AE\u03B9"); return;
    case 0x1FC6: append(u"\u03B7\u0342"); return;
    case 0x1FC7: append(u"\u03B7\u0342\u03B9"); return;
    case 0x1FD2: append(u"\u03B9\u0308\u0300"); return;
    case 0x1FD6: append(u"\u03B9\u0342"); return;
    case 0x1FD7: append(u"\u03B9\u0308\u0342"); return;
    case 0x1FE2: append(u"\u03C5\u0308\u0300"); return;
    case 0x1FE4: append(u"\u03C1\u0313"); return;
    case 0x1FE6: append(u"\u03C5\u0342"); return;
    case 0x1FE7: append(u"\u03C5\u0308\u0342"); return;
    case 0x1FF2: append(u"\u1F7C\u03B9"); return;
    case 0x1FF3:
    case 0x1FFC: append(u"\u03C9\u03B9"); return;
    case 0x1FF4: append(u"\u03CE\u03B9"); return;
    case 0x1FF6: append(u"\u03C9\u0342"); return;
    case 0x1FF7: append(u"\u03C9\u0342\u03B9"); return;
    default:
        const char32_t scalar = static_cast<char32_t>(codePoint);
        output->append(QString::fromUcs4(&scalar, 1));
        return;
    }
}

QString fullCaseFold(const QString &value)
{
    QString folded;
    folded.reserve(value.size());
    for (uint codePoint : value.toUcs4()) {
        appendFullCaseFold(&folded, codePoint);
    }
    return folded.toCaseFolded();
}

QList<GlobalIdentityClaim> claimsForObjects(
    const QList<GlobalIdentityObject> &objects,
    bool rejectDuplicates,
    std::optional<GlobalIdentityConflict> *conflict)
{
    if (conflict) {
        conflict->reset();
    }
    QList<GlobalIdentityClaim> claims;
    QHash<QString, GlobalIdentityClaim> firstByValue;
    for (const GlobalIdentityObject &object : objects) {
        if (!object.active) {
            continue;
        }
        for (int index = 0; index < object.values.size(); ++index) {
            const GlobalIdentityValue &value = object.values.at(index);
            const QString normalized = normalizedGlobalIdentity(value.displayValue);
            if (normalized.isEmpty()) {
                continue;
            }
            GlobalIdentityClaim claim;
            claim.normalizedValue = normalized;
            claim.owner = object.owner;
            claim.fieldKind = value.fieldKind;
            claim.displayValue = value.displayValue;
            claim.fieldIndex = index;
            if (rejectDuplicates && firstByValue.contains(normalized)) {
                if (conflict) {
                    GlobalIdentityConflict duplicate;
                    duplicate.normalizedValue = normalized;
                    duplicate.attemptedClaim = claim;
                    duplicate.conflictingClaims = {firstByValue.value(normalized)};
                    *conflict = duplicate;
                }
                return {};
            }
            firstByValue.insert(normalized, claim);
            claims.append(claim);
        }
    }
    return claims;
}

QString claimDescription(const GlobalIdentityClaim &claim)
{
    const QString parent = claim.owner.parentId.trimmed().isEmpty()
        ? QString()
        : QStringLiteral(", parent %1").arg(claim.owner.parentId);
    const QString locator = claim.owner.locator.trimmed().isEmpty()
        ? QString()
        : QStringLiteral(", %1").arg(claim.owner.locator);
    return QStringLiteral("%1 %2 \"%3\" on \"%4\" (id %5%6%7)")
        .arg(globalIdentityObjectTypeName(claim.owner.objectType),
             globalIdentityFieldKindName(claim.fieldKind),
             claim.displayValue,
             claim.owner.displayName,
             claim.owner.objectId,
             parent,
             locator);
}

bool execSql(QSqlDatabase &database, const QString &sql, QString *error)
{
    QSqlQuery query(database);
    if (query.exec(sql)) {
        return true;
    }
    if (error) {
        *error = query.lastError().text();
    }
    return false;
}

GlobalIdentityClaim claimFromQuery(QSqlQuery &query, int offset = 0)
{
    GlobalIdentityClaim claim;
    claim.normalizedValue = query.value(offset).toString();
    claim.owner.objectType = objectTypeFromStorage(query.value(offset + 1).toString());
    claim.owner.objectId = query.value(offset + 2).toString();
    claim.owner.parentId = query.value(offset + 3).toString();
    claim.fieldKind = fieldKindFromStorage(query.value(offset + 4).toString());
    claim.displayValue = query.value(offset + 5).toString();
    claim.owner.displayName = query.value(offset + 6).toString();
    claim.owner.locator = query.value(offset + 7).toString();
    claim.fieldIndex = query.value(offset + 8).toInt();
    return claim;
}

bool insertConflictMember(QSqlDatabase &database,
                          const GlobalIdentityClaim &claim,
                          QString *error)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO identity_registry.identity_conflict_members("
        "normalized_value, object_type, object_id, parent_id, field_kind, display_value, "
        "display_name, locator, field_index) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(claim.normalizedValue);
    query.addBindValue(objectTypeToStorage(claim.owner.objectType));
    query.addBindValue(nonNullText(claim.owner.objectId));
    query.addBindValue(nonNullText(claim.owner.parentId));
    query.addBindValue(fieldKindToStorage(claim.fieldKind));
    query.addBindValue(nonNullText(claim.displayValue));
    query.addBindValue(nonNullText(claim.owner.displayName));
    query.addBindValue(nonNullText(claim.owner.locator));
    query.addBindValue(claim.fieldIndex);
    if (query.exec()) {
        return true;
    }
    if (error) {
        *error = query.lastError().text();
    }
    return false;
}

QList<GlobalIdentityClaim> conflictMembers(QSqlDatabase &database,
                                           const QString &normalizedValue,
                                           QString *error)
{
    QList<GlobalIdentityClaim> claims;
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT normalized_value, object_type, object_id, parent_id, field_kind, "
        "display_value, display_name, locator, field_index "
        "FROM identity_registry.identity_conflict_members "
        "WHERE normalized_value = ? "
        "ORDER BY object_type, parent_id, object_id, field_kind, field_index"));
    query.addBindValue(normalizedValue);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return {};
    }
    while (query.next()) {
        claims.append(claimFromQuery(query));
    }
    return claims;
}

std::optional<GlobalIdentityClaim> activeClaim(QSqlDatabase &database,
                                               const QString &normalizedValue,
                                               QString *state,
                                               QString *error)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT state, normalized_value, object_type, object_id, parent_id, field_kind, "
        "display_value, display_name, locator, field_index "
        "FROM identity_registry.identity_claims WHERE normalized_value = ?"));
    query.addBindValue(normalizedValue);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        if (state) {
            state->clear();
        }
        return std::nullopt;
    }
    if (state) {
        *state = query.value(0).toString();
    }
    if (query.value(0).toString() != QLatin1String("active")) {
        return std::nullopt;
    }
    return claimFromQuery(query, 1);
}

bool insertActiveClaim(QSqlDatabase &database,
                       const GlobalIdentityClaim &claim,
                       QString *error)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO identity_registry.identity_claims("
        "normalized_value, state, object_type, object_id, parent_id, field_kind, "
        "display_value, display_name, locator, field_index, updated_at) "
        "VALUES (?, 'active', ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(claim.normalizedValue);
    query.addBindValue(objectTypeToStorage(claim.owner.objectType));
    query.addBindValue(nonNullText(claim.owner.objectId));
    query.addBindValue(nonNullText(claim.owner.parentId));
    query.addBindValue(fieldKindToStorage(claim.fieldKind));
    query.addBindValue(nonNullText(claim.displayValue));
    query.addBindValue(nonNullText(claim.owner.displayName));
    query.addBindValue(nonNullText(claim.owner.locator));
    query.addBindValue(claim.fieldIndex);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (query.exec()) {
        return true;
    }
    if (error) {
        *error = query.lastError().text();
    }
    return false;
}

bool resolveHistoricalConflict(QSqlDatabase &database,
                               const QString &normalizedValue,
                               QString *error)
{
    QString state;
    activeClaim(database, normalizedValue, &state, error);
    if (state.isEmpty() || state == QLatin1String("active")) {
        return error == nullptr || error->isEmpty();
    }
    const QList<GlobalIdentityClaim> members = conflictMembers(database, normalizedValue, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    if (members.size() > 1) {
        return true;
    }
    if (members.isEmpty()) {
        QSqlQuery remove(database);
        remove.prepare(QStringLiteral(
            "DELETE FROM identity_registry.identity_claims WHERE normalized_value = ?"));
        remove.addBindValue(normalizedValue);
        if (!remove.exec()) {
            if (error) *error = remove.lastError().text();
            return false;
        }
        return true;
    }

    const GlobalIdentityClaim claim = members.first();
    QSqlQuery promote(database);
    promote.prepare(QStringLiteral(
        "UPDATE identity_registry.identity_claims SET state = 'active', object_type = ?, "
        "object_id = ?, parent_id = ?, field_kind = ?, display_value = ?, display_name = ?, "
        "locator = ?, field_index = ?, updated_at = ? WHERE normalized_value = ?"));
    promote.addBindValue(objectTypeToStorage(claim.owner.objectType));
    promote.addBindValue(nonNullText(claim.owner.objectId));
    promote.addBindValue(nonNullText(claim.owner.parentId));
    promote.addBindValue(fieldKindToStorage(claim.fieldKind));
    promote.addBindValue(nonNullText(claim.displayValue));
    promote.addBindValue(nonNullText(claim.owner.displayName));
    promote.addBindValue(nonNullText(claim.owner.locator));
    promote.addBindValue(claim.fieldIndex);
    promote.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    promote.addBindValue(normalizedValue);
    if (!promote.exec()) {
        if (error) *error = promote.lastError().text();
        return false;
    }
    QSqlQuery removeMember(database);
    removeMember.prepare(QStringLiteral(
        "DELETE FROM identity_registry.identity_conflict_members WHERE normalized_value = ?"));
    removeMember.addBindValue(normalizedValue);
    if (!removeMember.exec()) {
        if (error) *error = removeMember.lastError().text();
        return false;
    }
    return true;
}

bool releaseOwner(QSqlDatabase &database,
                  const GlobalIdentityOwner &owner,
                  QString *error)
{
    const QString type = objectTypeToStorage(owner.objectType);
    QSet<QString> affectedValues;
    for (const QString &sql : {
             QStringLiteral("SELECT normalized_value FROM identity_registry.identity_claims "
                            "WHERE state = 'active' AND object_type = ? AND object_id = ? AND parent_id = ?"),
             QStringLiteral("SELECT normalized_value FROM identity_registry.identity_conflict_members "
                            "WHERE object_type = ? AND object_id = ? AND parent_id = ?")}) {
        QSqlQuery query(database);
        query.prepare(sql);
        query.addBindValue(type);
        query.addBindValue(nonNullText(owner.objectId));
        query.addBindValue(nonNullText(owner.parentId));
        if (!query.exec()) {
            if (error) *error = query.lastError().text();
            return false;
        }
        while (query.next()) {
            affectedValues.insert(query.value(0).toString());
        }
    }

    QSqlQuery removeActive(database);
    removeActive.prepare(QStringLiteral(
        "DELETE FROM identity_registry.identity_claims "
        "WHERE state = 'active' AND object_type = ? AND object_id = ? AND parent_id = ?"));
    removeActive.addBindValue(type);
    removeActive.addBindValue(nonNullText(owner.objectId));
    removeActive.addBindValue(nonNullText(owner.parentId));
    if (!removeActive.exec()) {
        if (error) *error = removeActive.lastError().text();
        return false;
    }

    QSqlQuery removeMembers(database);
    removeMembers.prepare(QStringLiteral(
        "DELETE FROM identity_registry.identity_conflict_members "
        "WHERE object_type = ? AND object_id = ? AND parent_id = ?"));
    removeMembers.addBindValue(type);
    removeMembers.addBindValue(nonNullText(owner.objectId));
    removeMembers.addBindValue(nonNullText(owner.parentId));
    if (!removeMembers.exec()) {
        if (error) *error = removeMembers.lastError().text();
        return false;
    }
    for (const QString &value : std::as_const(affectedValues)) {
        if (!resolveHistoricalConflict(database, value, error)) {
            return false;
        }
    }
    return true;
}

bool claimHistoricalValue(QSqlDatabase &database,
                          const GlobalIdentityClaim &claim,
                          QString *error)
{
    QString state;
    const std::optional<GlobalIdentityClaim> existing = activeClaim(
        database, claim.normalizedValue, &state, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    if (state.isEmpty()) {
        return insertActiveClaim(database, claim, error);
    }
    if (state == QLatin1String("active")) {
        if (!existing.has_value()) {
            if (error) *error = QStringLiteral("Identity registry active claim is incomplete");
            return false;
        }
        if (!insertConflictMember(database, existing.value(), error)
            || !insertConflictMember(database, claim, error)) {
            return false;
        }
        QSqlQuery markConflict(database);
        markConflict.prepare(QStringLiteral(
            "UPDATE identity_registry.identity_claims SET state = 'conflict', "
            "object_type = '', object_id = '', parent_id = '', field_kind = '', "
            "display_value = '', display_name = '', locator = '', field_index = 0, "
            "updated_at = ? WHERE normalized_value = ?"));
        markConflict.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        markConflict.addBindValue(claim.normalizedValue);
        if (!markConflict.exec()) {
            if (error) *error = markConflict.lastError().text();
            return false;
        }
        return true;
    }
    return insertConflictMember(database, claim, error);
}

} // namespace

QString GlobalIdentityConflict::message() const
{
    QStringList descriptions;
    for (const GlobalIdentityClaim &claim : conflictingClaims) {
        descriptions.append(claimDescription(claim));
    }
    if (attemptedClaim.has_value() && !conflictingClaims.isEmpty()) {
        return QStringLiteral("%1 conflicts with %2 (normalized value \"%3\"). "
                              "Open the listed object and rename its name or alias before retrying.")
            .arg(claimDescription(attemptedClaim.value()),
                 descriptions.join(QStringLiteral("; ")),
                 normalizedValue);
    }
    return QStringLiteral("Historical identity conflict for normalized value \"%1\": %2. "
                          "Open each listed object and rename one name or alias.")
        .arg(normalizedValue, descriptions.join(QStringLiteral("; ")));
}

QString normalizedGlobalIdentity(const QString &value)
{
    return fullCaseFold(value.trimmed().normalized(QString::NormalizationForm_KC));
}

QString globalIdentityOwnerKey(const GlobalIdentityOwner &owner)
{
    return QStringLiteral("%1\x1f%2\x1f%3")
        .arg(objectTypeToStorage(owner.objectType), owner.parentId, owner.objectId);
}

QString globalIdentityObjectTypeName(GlobalIdentityObjectType type)
{
    switch (type) {
    case GlobalIdentityObjectType::File:
        return QStringLiteral("File");
    case GlobalIdentityObjectType::Clip:
        return QStringLiteral("Clip");
    case GlobalIdentityObjectType::Anchor:
        return QStringLiteral("Anchor");
    }
    return QStringLiteral("File");
}

QString globalIdentityFieldKindName(GlobalIdentityFieldKind kind)
{
    return kind == GlobalIdentityFieldKind::Alias
        ? QStringLiteral("alias")
        : QStringLiteral("name");
}

QString globalIdentityLocator(GlobalIdentityObjectType type,
                              const QString &objectId,
                              const QString &parentId)
{
    Q_UNUSED(parentId);
    QString host;
    QString pathId;
    QUrlQuery query;
    switch (type) {
    case GlobalIdentityObjectType::File:
        host = QStringLiteral("entry");
        pathId = QStringLiteral("resource:%1").arg(objectId);
        query.addQueryItem(QStringLiteral("resource"), objectId);
        break;
    case GlobalIdentityObjectType::Clip:
        host = QStringLiteral("clip");
        pathId = objectId;
        break;
    case GlobalIdentityObjectType::Anchor:
        host = QStringLiteral("anchor");
        pathId = objectId;
        break;
    }
    QUrl locator;
    locator.setScheme(QStringLiteral("pinloom"));
    locator.setHost(host);
    locator.setPath(QStringLiteral("/") + pathId);
    locator.setQuery(query);
    return locator.toString(QUrl::FullyEncoded);
}

QString defaultGlobalIdentityRegistryPath(const QString &databasePath)
{
    if (databasePath == QLatin1String(":memory:")) {
        return QStringLiteral(":memory:");
    }
    return QFileInfo(databasePath).absoluteDir().filePath(
        QStringLiteral("pinloom_identity.sqlite3"));
}

bool InMemoryGlobalIdentityRegistry::replaceObjects(
    const QList<GlobalIdentityObject> &objects,
    std::optional<GlobalIdentityConflict> *conflict)
{
    std::optional<GlobalIdentityConflict> candidateConflict;
    const QList<GlobalIdentityClaim> candidateClaims = claimsForObjects(
        objects, true, &candidateConflict);
    if (candidateConflict.has_value()) {
        if (conflict) *conflict = candidateConflict;
        return false;
    }

    QMutexLocker locker(&mutex_);
    QHash<QString, QList<GlobalIdentityClaim>> proposed = claims_;
    QSet<QString> replacedOwners;
    for (const GlobalIdentityObject &object : objects) {
        replacedOwners.insert(globalIdentityOwnerKey(object.owner));
    }
    for (auto it = proposed.begin(); it != proposed.end();) {
        QList<GlobalIdentityClaim> retained;
        for (const GlobalIdentityClaim &claim : std::as_const(it.value())) {
            if (!replacedOwners.contains(globalIdentityOwnerKey(claim.owner))) {
                retained.append(claim);
            }
        }
        if (retained.isEmpty()) {
            it = proposed.erase(it);
        } else {
            it.value() = retained;
            ++it;
        }
    }
    for (const GlobalIdentityClaim &claim : candidateClaims) {
        const QList<GlobalIdentityClaim> existing = proposed.value(claim.normalizedValue);
        if (!existing.isEmpty()) {
            GlobalIdentityConflict found;
            found.normalizedValue = claim.normalizedValue;
            found.attemptedClaim = claim;
            found.conflictingClaims = existing;
            if (conflict) *conflict = found;
            return false;
        }
        proposed[claim.normalizedValue].append(claim);
    }
    claims_ = std::move(proposed);
    if (conflict) conflict->reset();
    return true;
}

void InMemoryGlobalIdentityRegistry::rebuildHistoricalObjects(
    const QList<GlobalIdentityObjectType> &objectTypes,
    const QList<GlobalIdentityObject> &objects)
{
    QMutexLocker locker(&mutex_);
    for (auto it = claims_.begin(); it != claims_.end();) {
        QList<GlobalIdentityClaim> retained;
        for (const GlobalIdentityClaim &claim : std::as_const(it.value())) {
            if (!objectTypes.contains(claim.owner.objectType)) {
                retained.append(claim);
            }
        }
        if (retained.isEmpty()) {
            it = claims_.erase(it);
        } else {
            it.value() = retained;
            ++it;
        }
    }
    const QList<GlobalIdentityClaim> historical = claimsForObjects(objects, false, nullptr);
    for (const GlobalIdentityClaim &claim : historical) {
        claims_[claim.normalizedValue].append(claim);
    }
}

QList<GlobalIdentityConflict> InMemoryGlobalIdentityRegistry::conflicts() const
{
    QMutexLocker locker(&mutex_);
    QList<GlobalIdentityConflict> result;
    for (auto it = claims_.cbegin(); it != claims_.cend(); ++it) {
        if (it.value().size() < 2) {
            continue;
        }
        GlobalIdentityConflict conflict;
        conflict.normalizedValue = it.key();
        conflict.conflictingClaims = it.value();
        result.append(conflict);
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.normalizedValue < right.normalizedValue;
    });
    return result;
}

SharedInMemoryGlobalIdentityRegistry createInMemoryGlobalIdentityRegistry()
{
    return std::make_shared<InMemoryGlobalIdentityRegistry>();
}

bool attachSqliteGlobalIdentityRegistry(QSqlDatabase &database,
                                        const QString &registryPath,
                                        QString *error)
{
    if (error) error->clear();
    const QString effectivePath = registryPath.trimmed().isEmpty()
        ? defaultGlobalIdentityRegistryPath(database.databaseName())
        : registryPath;
    if (effectivePath != QLatin1String(":memory:")) {
        const QFileInfo file(effectivePath);
        if (!QDir(file.absolutePath()).mkpath(QStringLiteral("."))) {
            if (error) *error = QStringLiteral("Unable to create identity registry directory");
            return false;
        }
    }
    QSqlQuery databases(database);
    if (!databases.exec(QStringLiteral("PRAGMA database_list"))) {
        if (error) *error = databases.lastError().text();
        return false;
    }
    while (databases.next()) {
        if (databases.value(1).toString() == QLatin1String(RegistrySchema)) {
            return true;
        }
    }
    QString escapedPath = effectivePath;
    escapedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    return execSql(database,
                   QStringLiteral("ATTACH DATABASE '%1' AS identity_registry").arg(escapedPath),
                   error);
}

bool initializeSqliteGlobalIdentityRegistry(QSqlDatabase &database, QString *error)
{
    if (error) error->clear();
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS identity_registry.identity_schema_migrations("
                       "version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS identity_registry.identity_write_guard("
                       "singleton INTEGER PRIMARY KEY CHECK(singleton = 1), revision INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("INSERT OR IGNORE INTO identity_registry.identity_write_guard(singleton, revision) VALUES (1, 0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS identity_registry.identity_claims("
                       "normalized_value TEXT PRIMARY KEY, state TEXT NOT NULL, object_type TEXT NOT NULL DEFAULT '', "
                       "object_id TEXT NOT NULL DEFAULT '', parent_id TEXT NOT NULL DEFAULT '', "
                       "field_kind TEXT NOT NULL DEFAULT '', display_value TEXT NOT NULL DEFAULT '', "
                       "display_name TEXT NOT NULL DEFAULT '', locator TEXT NOT NULL DEFAULT '', "
                       "field_index INTEGER NOT NULL DEFAULT 0, updated_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS identity_registry.idx_identity_claim_owner "
                       "ON identity_claims(object_type, parent_id, object_id)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS identity_registry.identity_conflict_members("
                       "normalized_value TEXT NOT NULL, object_type TEXT NOT NULL, object_id TEXT NOT NULL, "
                       "parent_id TEXT NOT NULL DEFAULT '', field_kind TEXT NOT NULL, display_value TEXT NOT NULL, "
                       "display_name TEXT NOT NULL DEFAULT '', locator TEXT NOT NULL DEFAULT '', "
                       "field_index INTEGER NOT NULL DEFAULT 0, "
                       "PRIMARY KEY(normalized_value, object_type, object_id, parent_id, field_kind, field_index), "
                       "FOREIGN KEY(normalized_value) REFERENCES identity_claims(normalized_value) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS identity_registry.idx_identity_conflict_owner "
                       "ON identity_conflict_members(object_type, parent_id, object_id)"),
        QStringLiteral("INSERT OR IGNORE INTO identity_registry.identity_schema_migrations(version, name, applied_at) "
                       "VALUES (1, 'global_name_alias_registry', datetime('now'))")
    };
    for (const QString &statement : statements) {
        if (!execSql(database, statement, error)) {
            return false;
        }
    }
    return true;
}

bool replaceSqliteGlobalIdentityObjects(
    QSqlDatabase &database,
    const QList<GlobalIdentityObject> &objects,
    std::optional<GlobalIdentityConflict> *conflict,
    QString *error)
{
    if (error) error->clear();
    std::optional<GlobalIdentityConflict> candidateConflict;
    const QList<GlobalIdentityClaim> candidateClaims = claimsForObjects(
        objects, true, &candidateConflict);
    if (candidateConflict.has_value()) {
        if (conflict) *conflict = candidateConflict;
        if (error) *error = candidateConflict->message();
        return false;
    }
    if (!execSql(database,
                 QStringLiteral("UPDATE identity_registry.identity_write_guard "
                                "SET revision = revision + 1 WHERE singleton = 1"),
                 error)) {
        return false;
    }
    QSet<QString> releasedOwners;
    for (const GlobalIdentityObject &object : objects) {
        const QString key = globalIdentityOwnerKey(object.owner);
        if (releasedOwners.contains(key)) {
            continue;
        }
        releasedOwners.insert(key);
        if (!releaseOwner(database, object.owner, error)) {
            return false;
        }
    }
    for (const GlobalIdentityClaim &claim : candidateClaims) {
        QString state;
        const std::optional<GlobalIdentityClaim> existing = activeClaim(
            database, claim.normalizedValue, &state, error);
        if (error && !error->isEmpty()) {
            return false;
        }
        if (!state.isEmpty()) {
            GlobalIdentityConflict found;
            found.normalizedValue = claim.normalizedValue;
            found.attemptedClaim = claim;
            found.conflictingClaims = state == QLatin1String("active") && existing.has_value()
                ? QList<GlobalIdentityClaim>{existing.value()}
                : conflictMembers(database, claim.normalizedValue, error);
            if (conflict) *conflict = found;
            if (error) *error = found.message();
            return false;
        }
        if (!insertActiveClaim(database, claim, error)) {
            QString ignoredState;
            const std::optional<GlobalIdentityClaim> raced = activeClaim(
                database, claim.normalizedValue, &ignoredState, nullptr);
            if (!ignoredState.isEmpty()) {
                GlobalIdentityConflict found;
                found.normalizedValue = claim.normalizedValue;
                found.attemptedClaim = claim;
                found.conflictingClaims = raced.has_value()
                    ? QList<GlobalIdentityClaim>{raced.value()}
                    : conflictMembers(database, claim.normalizedValue, nullptr);
                if (conflict) *conflict = found;
                if (error) *error = found.message();
            }
            return false;
        }
    }
    if (conflict) conflict->reset();
    return true;
}

bool rebuildSqliteHistoricalIdentityObjects(
    QSqlDatabase &database,
    const QList<GlobalIdentityObjectType> &objectTypes,
    const QList<GlobalIdentityObject> &objects,
    QString *error)
{
    if (error) error->clear();
    if (!execSql(database,
                 QStringLiteral("UPDATE identity_registry.identity_write_guard "
                                "SET revision = revision + 1 WHERE singleton = 1"),
                 error)) {
        return false;
    }
    for (GlobalIdentityObjectType objectType : objectTypes) {
        const QString storedType = objectTypeToStorage(objectType);
        QSet<QString> affectedValues;
        QSqlQuery keys(database);
        keys.prepare(QStringLiteral(
            "SELECT normalized_value FROM identity_registry.identity_conflict_members "
            "WHERE object_type = ?"));
        keys.addBindValue(storedType);
        if (!keys.exec()) {
            if (error) *error = keys.lastError().text();
            return false;
        }
        while (keys.next()) {
            affectedValues.insert(keys.value(0).toString());
        }
        QSqlQuery removeActive(database);
        removeActive.prepare(QStringLiteral(
            "DELETE FROM identity_registry.identity_claims "
            "WHERE state = 'active' AND object_type = ?"));
        removeActive.addBindValue(storedType);
        if (!removeActive.exec()) {
            if (error) *error = removeActive.lastError().text();
            return false;
        }
        QSqlQuery removeMembers(database);
        removeMembers.prepare(QStringLiteral(
            "DELETE FROM identity_registry.identity_conflict_members WHERE object_type = ?"));
        removeMembers.addBindValue(storedType);
        if (!removeMembers.exec()) {
            if (error) *error = removeMembers.lastError().text();
            return false;
        }
        for (const QString &value : std::as_const(affectedValues)) {
            if (!resolveHistoricalConflict(database, value, error)) {
                return false;
            }
        }
    }
    const QList<GlobalIdentityClaim> historical = claimsForObjects(objects, false, nullptr);
    for (const GlobalIdentityClaim &claim : historical) {
        if (!claimHistoricalValue(database, claim, error)) {
            return false;
        }
    }
    return true;
}

QList<GlobalIdentityConflict> sqliteGlobalIdentityConflicts(QSqlDatabase &database,
                                                            QString *error)
{
    if (error) error->clear();
    QList<GlobalIdentityConflict> result;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT normalized_value FROM identity_registry.identity_claims "
            "WHERE state = 'conflict' ORDER BY normalized_value"))) {
        if (error) *error = query.lastError().text();
        return {};
    }
    while (query.next()) {
        GlobalIdentityConflict conflict;
        conflict.normalizedValue = query.value(0).toString();
        conflict.conflictingClaims = conflictMembers(database, conflict.normalizedValue, error);
        if (error && !error->isEmpty()) {
            return {};
        }
        result.append(conflict);
    }
    return result;
}

} // namespace Pinloom
