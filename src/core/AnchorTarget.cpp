#include "pinloom/core/AnchorTarget.h"

#include <QDir>
#include <QUrl>

namespace Pinloom {

namespace {

QString comparableTarget(QString target)
{
    target = target.trimmed();
    const QUrl url(target);
    if (url.isValid() && url.isLocalFile()) target = url.toLocalFile();
    if (!target.contains(QStringLiteral("://"))) {
        target = QDir::cleanPath(QDir::fromNativeSeparators(target));
#ifdef Q_OS_WIN
        target = target.toLower();
#endif
    }
    return target;
}

} // namespace

bool ResolvedAnchorTarget::hasValue() const
{
    return !value.trimmed().isEmpty();
}

bool ResolvedAnchorTarget::isOverride() const
{
    return source == AnchorTargetSource::AnchorFile
        || source == AnchorTargetSource::AnchorUri
        || source == AnchorTargetSource::Locator;
}

QString localPathFromAnchorTarget(const QString &target)
{
    const QString trimmed = target.trimmed();
    const QUrl url(trimmed);
    return url.isValid() && url.isLocalFile() ? url.toLocalFile() : trimmed;
}

ResolvedAnchorTarget resolveAnchorTarget(const Anchor &anchor,
                                         const QStringList &locatorCandidates,
                                         const QString &resourceLocation)
{
    ResolvedAnchorTarget result;
    const QString targetFile = anchor.targetFile.trimmed();
    const QString targetUri = anchor.targetUri.trimmed();
    result.conflictingExplicitTargets = !targetFile.isEmpty()
        && !targetUri.isEmpty()
        && comparableTarget(targetFile) != comparableTarget(targetUri);

    if (!targetFile.isEmpty()) {
        result.value = targetFile;
        result.source = AnchorTargetSource::AnchorFile;
        return result;
    }
    if (!targetUri.isEmpty()) {
        result.value = localPathFromAnchorTarget(targetUri);
        result.source = AnchorTargetSource::AnchorUri;
        return result;
    }
    for (const QString &candidate : locatorCandidates) {
        if (!candidate.trimmed().isEmpty()) {
            result.value = localPathFromAnchorTarget(candidate);
            result.source = AnchorTargetSource::Locator;
            return result;
        }
    }
    if (!resourceLocation.trimmed().isEmpty()) {
        result.value = localPathFromAnchorTarget(resourceLocation);
        result.source = AnchorTargetSource::Resource;
    }
    return result;
}

} // namespace Pinloom
