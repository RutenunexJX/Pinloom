#include "pinloom/core/AnchorLocator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <cmath>

namespace Pinloom {

namespace {

std::optional<double> numberValue(const QJsonValue &value)
{
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double number = value.toString().trimmed().toDouble(&ok);
        if (ok) {
            return number;
        }
    }
    return std::nullopt;
}

std::optional<QList<double>> fourNumbers(const QJsonValue &value)
{
    if (!value.isArray() || value.toArray().size() != 4) {
        return std::nullopt;
    }

    QList<double> numbers;
    for (const QJsonValue &item : value.toArray()) {
        const std::optional<double> number = numberValue(item);
        if (!number.has_value()) {
            return std::nullopt;
        }
        numbers.append(number.value());
    }
    return numbers;
}

int positiveInteger(const QJsonObject &locator, const QString &key)
{
    const std::optional<double> value = numberValue(locator.value(key));
    if (!value.has_value()) {
        return -1;
    }
    const int integer = static_cast<int>(std::round(value.value()));
    return integer > 0 ? integer : -1;
}

} // namespace

QJsonObject anchorLocatorObject(const Anchor &anchor, QString *error)
{
    if (error) {
        error->clear();
    }

    const QString json = anchor.locatorJson.trimmed();
    if (json.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Anchor locator JSON is invalid");
        }
        return {};
    }
    return document.object();
}

QString anchorLocatorType(const Anchor &anchor)
{
    QString type = anchor.locatorType.trimmed();
    if (type.isEmpty()) {
        type = anchorLocatorObject(anchor).value(QStringLiteral("type")).toString().trimmed();
    }
    return type.toLower();
}

int anchorLocatorLine(const Anchor &anchor)
{
    return positiveInteger(anchorLocatorObject(anchor), QStringLiteral("line"));
}

int anchorLocatorPage(const Anchor &anchor)
{
    return positiveInteger(anchorLocatorObject(anchor), QStringLiteral("page"));
}

std::optional<QRectF> anchorLocatorRegion(const Anchor &anchor)
{
    const QJsonObject locator = anchorLocatorObject(anchor);

    if (const std::optional<QList<double>> rect = fourNumbers(locator.value(QStringLiteral("rect")));
        rect.has_value()) {
        const QRectF region(QPointF(rect->at(0), rect->at(1)),
                            QPointF(rect->at(2), rect->at(3)));
        if (region.isValid()) {
            return region;
        }
    }

    if (const std::optional<QList<double>> highlight = fourNumbers(locator.value(QStringLiteral("highlight")));
        highlight.has_value()) {
        const QRectF region(QPointF(highlight->at(0), highlight->at(2)),
                            QPointF(highlight->at(1), highlight->at(3)));
        if (region.isValid()) {
            return region;
        }
    }

    for (const QString &key : {QStringLiteral("viewrect"), QStringLiteral("region")}) {
        const std::optional<QList<double>> values = fourNumbers(locator.value(key));
        if (!values.has_value()) {
            continue;
        }
        const QRectF region(values->at(0), values->at(1), values->at(2), values->at(3));
        if (region.isValid()) {
            return region;
        }
    }
    return std::nullopt;
}

QString anchorLocatorFragment(const Anchor &anchor)
{
    const QJsonObject locator = anchorLocatorObject(anchor);
    QString fragment = locator.value(QStringLiteral("fragment")).toString().trimmed();
    if (fragment.isEmpty()) {
        fragment = locator.value(QStringLiteral("id")).toString().trimmed();
    }
    return fragment;
}

QString anchorIdentityKey(const Anchor &anchor)
{
    if (!anchor.id.trimmed().isEmpty()) {
        return QStringLiteral("id|%1").arg(anchor.id.trimmed());
    }
    return QStringLiteral("locator|%1|%2|%3|%4|%5")
        .arg(anchor.targetApp.trimmed().toLower(),
             anchor.targetFile.trimmed(),
             anchor.targetUri.trimmed(),
             anchorLocatorType(anchor),
             anchor.locatorJson.trimmed());
}

bool sameAnchorIdentity(const Anchor &left, const Anchor &right)
{
    if (!left.id.trimmed().isEmpty() && !right.id.trimmed().isEmpty()) {
        return left.id.trimmed() == right.id.trimmed();
    }
    return left.name.trimmed() == right.name.trimmed()
        && left.targetApp.trimmed().compare(right.targetApp.trimmed(), Qt::CaseInsensitive) == 0
        && left.targetFile.trimmed() == right.targetFile.trimmed()
        && left.targetUri.trimmed() == right.targetUri.trimmed()
        && anchorLocatorType(left) == anchorLocatorType(right)
        && left.locatorJson.trimmed() == right.locatorJson.trimmed();
}

bool hasAnchorIdentity(const Anchor &anchor)
{
    return !anchor.id.trimmed().isEmpty()
        || !anchorLocatorType(anchor).isEmpty()
        || !anchor.targetFile.trimmed().isEmpty()
        || !anchor.targetUri.trimmed().isEmpty();
}

} // namespace Pinloom
