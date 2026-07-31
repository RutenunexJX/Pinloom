#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QUrl>
#include <optional>

namespace Pinloom {

QString webBrowserClipTag();
ClipActionType taggedActionForTags(const QStringList &tags);
ClipActionType actionForClip(const Clip &clip);
std::optional<QUrl> webUrlForClip(const Clip &clip, QString *error = nullptr);

} // namespace Pinloom
