#include "pinloom/application/NativePdfCapture.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>

namespace Pinloom {
namespace {
PdfCaptureRect rect(const QJsonValue &value)
{
    const auto values = value.toArray();
    if (values.size() != 4) return {};
    for (const auto &number : values)
        if (!number.isDouble() || !std::isfinite(number.toDouble())) return {};
    return {values[0].toDouble(), values[1].toDouble(), values[2].toDouble(), values[3].toDouble()};
}
}
NativePdfCaptureDraftResult nativePdfCaptureDraft(const QJsonObject &p)
{
    const auto rejected = [](const QString &message, const QString &code = QStringLiteral("invalid_capture")) {
        return NativePdfCaptureDraftResult{{}, message, code};
    };
    if (p["captureProtocol"] != "pinloom-pdf-capture/v1" || QUuid(p["captureId"].toString()).isNull())
        return rejected(QStringLiteral("Unsupported capture protocol or invalid capture ID"));
    const QFileInfo file(p["targetFile"].toString());
    if (!file.isAbsolute() || !file.isFile() || !file.isReadable())
        return rejected(QStringLiteral("The selected PDF is missing or not readable"), "target_unavailable");
    bool sizeOk = false, modifiedOk = false;
    const auto size = p["documentSize"].toString().toLongLong(&sizeOk);
    const auto modified = p["documentModifiedMs"].toString().toLongLong(&modifiedOk);
    if (!sizeOk || !modifiedOk || size < 0 || modified < 0)
        return rejected(QStringLiteral("Document size and modification timestamp are required"));
    if (file.size() != size || file.lastModified().toMSecsSinceEpoch() != modified)
        return rejected(QStringLiteral("The PDF changed after selection; select the region again"), "target_changed");
    QFile pdf(file.absoluteFilePath());
    if (!pdf.open(QIODevice::ReadOnly) || !pdf.read(1024).contains("%PDF-"))
        return rejected(QStringLiteral("The selected file is not a readable PDF"), "target_unavailable");
    const auto locator = p["locator"].toObject();
    const auto region = rect(locator["rect"]);
    const auto media = rect(locator["mediaBox"]);
    const auto crop = rect(locator["cropBox"]);
    const auto pageValue = locator["page"].toDouble();
    const auto rotationValue = locator["rotation"].toDouble(-1);
    const auto unit = locator["userUnit"].toDouble(-1);
    if (locator["type"] != "sumatrapdf.rect" || locator["coordinateSpace"] != "page-top-left"
        || locator["unit"] != "pt" || locator["version"].toInt() != 3
        || pageValue < 1 || pageValue > 1000000 || pageValue != std::floor(pageValue)
        || !region.isValid() || !media.isValid() || !crop.isValid()
        || (rotationValue != 0 && rotationValue != 90 && rotationValue != 180 && rotationValue != 270)
        || !std::isfinite(unit) || unit <= 0 || unit > 75000)
        return rejected(QStringLiteral("Invalid native PDF page geometry or rectangle"));
    if (crop.left < media.left || crop.top < media.top || crop.right > media.right || crop.bottom > media.bottom)
        return rejected(QStringLiteral("PDF CropBox must be inside MediaBox"));
    const bool swapped = rotationValue == 90 || rotationValue == 270;
    const auto width = (swapped ? crop.bottom - crop.top : crop.right - crop.left) * unit;
    const auto height = (swapped ? crop.right - crop.left : crop.bottom - crop.top) * unit;
    if (region.left < -0.01 || region.top < -0.01 || region.right > width + 0.01 || region.bottom > height + 0.01
        || region.right - region.left < 0.5 || region.bottom - region.top < 0.5)
        return rejected(QStringLiteral("Select a visible region of at least 0.5 PDF points within one page"));
    const auto name = p["name"].toString().trimmed();
    if (name.isEmpty() || name.size() > 256)
        return rejected(QStringLiteral("Enter a name of 1 to 256 characters"));
    ManualPdfAnchorCreationRequest request;
    request.file = QDir::fromNativeSeparators(file.canonicalFilePath());
    request.name = name;
    request.documentIdentity = request.file;
    request.page = int(pageValue);
    request.rect = region; request.mediaBox = media; request.cropBox = crop;
    request.rotation = int(rotationValue); request.userUnit = unit;
    request.locatorType = QStringLiteral("sumatrapdf.rect");
    request.source = QStringLiteral("sumatra-native-selection");
    request.adapterId = QStringLiteral("sumatra-pinloom/v1");
    for (const auto &tag : p["tags"].toArray()) request.tags.append(tag.toString());
    auto draft = anchorCaptureDraftFromPdfRequest(request);
    auto storedLocator = QJsonDocument::fromJson(draft.locatorJson.toUtf8()).object();
    storedLocator.insert("document", QJsonObject{{"identity", request.file},
        {"size", QString::number(size)}, {"modifiedMilliseconds", QString::number(modified)}});
    draft.locatorJson = QString::fromUtf8(QJsonDocument(storedLocator).toJson(QJsonDocument::Compact));
    return {draft, {}, {}};
}
}
