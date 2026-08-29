#include "pinloom/core/ResourceNormalization.h"

#include "pinloom/core/AnchorLocator.h"

#include <QCryptographicHash>
#include <QSet>
#include <algorithm>

namespace Pinloom {

Resource normalizedResource(Resource resource)
{
    QSet<QString> anchorIds;
    for (int i = 0; i < resource.anchors.size(); ++i) {
        Anchor &anchor = resource.anchors[i];
        anchor.locatorType = anchorLocatorType(anchor);
        QString anchorId = anchor.id.trimmed();
        if ((anchorId.isEmpty() || anchorIds.contains(anchorId))
            && !resource.id.trimmed().isEmpty()) {
            QByteArray identity;
            for (const QString &value : {anchor.name,
                                         anchor.targetApp,
                                         anchor.targetFile,
                                         anchor.targetUri,
                                         anchor.locatorType,
                                         anchor.locatorJson}) {
                identity.append(value.trimmed().toUtf8());
                identity.append('\0');
            }
            const QString digest = QString::fromLatin1(
                QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(16));
            const QString base = QStringLiteral("%1#anchor-%2").arg(resource.id, digest);
            anchorId = base;
            int suffix = 2;
            while (anchorIds.contains(anchorId)) {
                anchorId = QStringLiteral("%1-%2").arg(base, QString::number(suffix++));
            }
        }
        anchor.id = anchorId;
        if (!anchorId.isEmpty()) anchorIds.insert(anchorId);
    }
    return resource;
}

bool resourceKindMatchesFilter(ResourceKind kind, const QList<ResourceKind> &requiredKinds)
{
    return requiredKinds.isEmpty() || requiredKinds.contains(kind);
}

} // namespace Pinloom
