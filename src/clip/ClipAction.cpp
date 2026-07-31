#include "pinloom/clip/ClipAction.h"

#include <algorithm>

namespace Pinloom {

QString webBrowserClipTag()
{
    return QStringLiteral("wb");
}

ClipActionType taggedActionForTags(const QStringList &tags)
{
    const QString actionTag = webBrowserClipTag();
    const bool opensUrl = std::any_of(tags.cbegin(), tags.cend(), [&actionTag](const QString &tag) {
        return tag.trimmed().compare(actionTag, Qt::CaseInsensitive) == 0;
    });
    return opensUrl ? ClipActionType::OpenWebUrl : ClipActionType::InsertText;
}

ClipActionType actionForClip(const Clip &clip)
{
    return clip.actionType;
}

std::optional<QUrl> webUrlForClip(const Clip &clip, QString *error)
{
    const QString candidate = clip.text.trimmed();
    if (candidate.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The wb Clip is empty");
        }
        return std::nullopt;
    }

    const bool containsWhitespace = std::any_of(candidate.cbegin(), candidate.cend(), [](QChar character) {
        return character.isSpace();
    });
    if (containsWhitespace) {
        if (error) {
            *error = QStringLiteral("The wb Clip must contain one URL only");
        }
        return std::nullopt;
    }

    const QUrl url = QUrl::fromUserInput(candidate);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || url.host().isEmpty()) {
        if (error) {
            *error = QStringLiteral("The wb Clip does not contain a valid HTTP or HTTPS URL");
        }
        return std::nullopt;
    }

    if (error) {
        error->clear();
    }
    return url;
}

} // namespace Pinloom
