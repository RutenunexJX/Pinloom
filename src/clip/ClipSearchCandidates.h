#pragma once

#include "pinloom/clip/ClipRepository.h"
#include <functional>

namespace Pinloom {

// Select from SQL-filtered rows using the final search's matching and tie rules.
// A positive limit bounds retained candidates, not the number of rows examined.
QList<Clip> selectAllFieldClipCandidates(const QString &query,
                                       int limit,
                                       const std::function<std::optional<Clip>()> &nextClip);

} // namespace Pinloom
