#include "pinloom/clip/ClipRepository.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace Pinloom {

namespace {

QDateTime effectiveUtcNow(const QDateTime &now)
{
    return now.isValid() ? now.toUTC() : QDateTime::currentDateTimeUtc();
}

QString makeClipId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString contentHashForBytes(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString previewForText(const QString &text)
{
    const QString preview = text.simplified();
    constexpr qsizetype maxPreviewLength = 80;
    if (preview.size() <= maxPreviewLength) {
        return preview;
    }
    return preview.left(maxPreviewLength - 3) + QStringLiteral("...");
}

QStringList cleanStringList(const QStringList &values)
{
    QStringList cleaned;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            cleaned.append(trimmed);
        }
    }
    cleaned.removeDuplicates();
    return cleaned;
}

bool containsExcludedSourceApp(const QStringList &excludedSourceApps, const QString &sourceApp)
{
    const QString normalizedSourceApp = sourceApp.trimmed();
    if (normalizedSourceApp.isEmpty()) {
        return false;
    }

    for (const QString &excluded : excludedSourceApps) {
        if (excluded.trimmed().compare(normalizedSourceApp, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

int clipIndexById(const QList<Clip> &clips, const QString &id)
{
    for (int index = 0; index < clips.size(); ++index) {
        if (clips.at(index).id == id) {
            return index;
        }
    }
    return -1;
}

QString insertModeToString(ClipInsertMode mode)
{
    switch (mode) {
    case ClipInsertMode::Paste:
        return QStringLiteral("paste");
    }
    return QStringLiteral("paste");
}

} // namespace

bool ClipCaptureResult::captured() const
{
    return status == ClipCaptureStatus::Captured && clip.has_value();
}

ClipCaptureResult InMemoryClipRepository::captureText(const QString &text,
                                                      const ClipCapturePolicy &policy,
                                                      const QString &sourceApp,
                                                      const QDateTime &now)
{
    const QDateTime capturedAt = effectiveUtcNow(now);
    pruneTemporaryHistory(policy, capturedAt);

    if (policy.capturePaused) {
        return {ClipCaptureStatus::IgnoredPaused, std::nullopt};
    }

    if (containsExcludedSourceApp(policy.excludedSourceApps, sourceApp)) {
        return {ClipCaptureStatus::IgnoredExcludedSource, std::nullopt};
    }

    if (text.trimmed().isEmpty()) {
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }

    const QByteArray bytes = text.toUtf8();
    if (policy.maxTextBytes >= 0 && bytes.size() > policy.maxTextBytes) {
        return {ClipCaptureStatus::IgnoredTooLarge, std::nullopt};
    }

    const QString contentHash = contentHashForBytes(bytes);
    const bool duplicate = std::any_of(clips_.cbegin(), clips_.cend(), [&](const Clip &clip) {
        return clip.contentHash == contentHash;
    });
    if (duplicate) {
        return {ClipCaptureStatus::IgnoredDuplicate, std::nullopt};
    }

    Clip clip;
    clip.id = makeClipId();
    clip.kind = ClipKind::Text;
    clip.state = ClipState::Temporary;
    clip.text = text;
    clip.preview = previewForText(text);
    clip.contentHash = contentHash;
    clip.createdAt = capturedAt;
    clip.updatedAt = capturedAt;
    clip.usedAt = capturedAt;
    clip.sourceApp = sourceApp.trimmed();
    clip.sizeBytes = bytes.size();
    if (policy.temporaryTtlSeconds > 0) {
        clip.expiresAt = capturedAt.addSecs(policy.temporaryTtlSeconds);
    }

    clips_.append(clip);
    pruneTemporaryHistory(policy, capturedAt);

    return {ClipCaptureStatus::Captured, clip};
}

bool InMemoryClipRepository::saveClip(const QString &id,
                                      const QString &name,
                                      const QStringList &aliases,
                                      const QStringList &tags,
                                      bool pinned,
                                      const QDateTime &now)
{
    const int index = clipIndexById(clips_, id);
    if (index < 0) {
        return false;
    }

    Clip &clip = clips_[index];
    const QString trimmedName = name.trimmed();
    clip.state = ClipState::Saved;
    clip.name = trimmedName.isEmpty() ? clip.preview : trimmedName;
    clip.aliases = cleanStringList(aliases);
    clip.tags = cleanStringList(tags);
    clip.pinned = pinned;
    clip.updatedAt = effectiveUtcNow(now);
    clip.expiresAt = {};
    return true;
}

bool InMemoryClipRepository::markClipUsed(const QString &id, const QDateTime &now)
{
    const int index = clipIndexById(clips_, id);
    if (index < 0) {
        return false;
    }

    clips_[index].usedAt = effectiveUtcNow(now);
    return true;
}

void InMemoryClipRepository::pruneTemporaryHistory(const ClipCapturePolicy &policy, const QDateTime &now)
{
    const QDateTime pruneAt = effectiveUtcNow(now);
    QSet<QString> removeIds;
    QList<Clip> retainedTemporaryClips;

    for (const Clip &clip : clips_) {
        if (clip.state != ClipState::Temporary) {
            continue;
        }
        if (clip.expiresAt.isValid() && clip.expiresAt.toUTC() <= pruneAt) {
            removeIds.insert(clip.id);
            continue;
        }
        retainedTemporaryClips.append(clip);
    }

    if (policy.maxTemporaryClips >= 0 && retainedTemporaryClips.size() > policy.maxTemporaryClips) {
        std::sort(retainedTemporaryClips.begin(), retainedTemporaryClips.end(), [](const Clip &left, const Clip &right) {
            if (left.createdAt == right.createdAt) {
                return left.id > right.id;
            }
            return left.createdAt > right.createdAt;
        });

        for (int index = policy.maxTemporaryClips; index < retainedTemporaryClips.size(); ++index) {
            removeIds.insert(retainedTemporaryClips.at(index).id);
        }
    }

    if (removeIds.isEmpty()) {
        return;
    }

    for (int index = clips_.size() - 1; index >= 0; --index) {
        if (removeIds.contains(clips_.at(index).id)) {
            clips_.removeAt(index);
        }
    }
}

QList<Clip> InMemoryClipRepository::clips() const
{
    return clips_;
}

QList<Clip> InMemoryClipRepository::temporaryClips() const
{
    QList<Clip> temporary;
    for (const Clip &clip : clips_) {
        if (clip.state == ClipState::Temporary) {
            temporary.append(clip);
        }
    }
    return temporary;
}

QList<Clip> InMemoryClipRepository::savedClips() const
{
    QList<Clip> saved;
    for (const Clip &clip : clips_) {
        if (clip.state == ClipState::Saved) {
            saved.append(clip);
        }
    }
    return saved;
}

std::optional<Clip> InMemoryClipRepository::findClip(const QString &id) const
{
    const int index = clipIndexById(clips_, id);
    if (index < 0) {
        return std::nullopt;
    }
    return clips_.at(index);
}

QString clipTargetApp()
{
    return QStringLiteral("pinloom.clip");
}

QString clipLocatorType()
{
    return QStringLiteral("clip.insert");
}

QString clipTargetUri(const QString &clipId)
{
    return QStringLiteral("clip://%1").arg(clipId);
}

QString clipLocatorJson(const QString &clipId, ClipInsertMode mode)
{
    QJsonObject locator;
    locator.insert(QStringLiteral("clip_id"), clipId);
    locator.insert(QStringLiteral("mode"), insertModeToString(mode));
    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

std::optional<Anchor> savedClipAnchor(const Clip &clip, ClipInsertMode mode)
{
    if (clip.state != ClipState::Saved || clip.id.trimmed().isEmpty()) {
        return std::nullopt;
    }

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.id = QStringLiteral("clip:%1").arg(clip.id);
    anchor.name = clip.name.trimmed().isEmpty() ? clip.preview : clip.name.trimmed();
    anchor.target = anchor.name;
    anchor.targetApp = clipTargetApp();
    anchor.targetUri = clipTargetUri(clip.id);
    anchor.locatorType = clipLocatorType();
    anchor.locatorJson = clipLocatorJson(clip.id, mode);
    anchor.aliases = clip.aliases;
    anchor.tags = clip.tags;
    anchor.pinned = clip.pinned;
    anchor.createdAt = clip.createdAt;
    anchor.updatedAt = clip.updatedAt;
    anchor.usedAt = clip.usedAt;
    return anchor;
}

} // namespace Pinloom
