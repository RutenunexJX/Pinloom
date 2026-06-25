#include "pinloom/core/DirectoryLibrarySource.h"

#include "pinloom/core/LibraryRoot.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <optional>
#include <utility>

namespace Pinloom {

namespace {

struct PdfObject {
    int number = -1;
    QString body;
};

struct HtmlLink {
    QUrl url;
    QString title;
};

struct TextUrlLink {
    QUrl url;
    QString title;
};

struct TabularUrlLink {
    QUrl url;
    QString title;
    QStringList aliases;
    QStringList tags;
};

struct BrowserBookmarkLink {
    QUrl url;
    QString title;
    QStringList folderPath;
};

struct OpmlLink {
    QUrl url;
    QString title;
    QUrl feedUrl;
    QStringList folderPath;
};

struct FeedEntryLink {
    QUrl url;
    QString title;
    QString feedTitle;
    QStringList categories;
};

struct SitemapLink {
    QUrl url;
    bool sitemapIndexEntry = false;
};

QString markdownPlainTextFromLine(QString line);
void appendFileLineAnchor(Resource &resource, const QString &target, int line);

QString normalizedPath(const QFileInfo &fileInfo)
{
    return QDir::cleanPath(fileInfo.absoluteFilePath());
}

ResourceKind kindForFileInfo(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return ResourceKind::Folder;
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
        return ResourceKind::Markdown;
    }
    if (suffix == QLatin1String("pdf")) {
        return ResourceKind::Pdf;
    }
    if (suffix == QLatin1String("url")
        || suffix == QLatin1String("webloc")
        || suffix == QLatin1String("website")
        || suffix == QLatin1String("html")
        || suffix == QLatin1String("htm")) {
        return ResourceKind::Url;
    }
    if (QStringList{
            QStringLiteral("c"),
            QStringLiteral("cc"),
            QStringLiteral("cpp"),
            QStringLiteral("cxx"),
            QStringLiteral("h"),
            QStringLiteral("hh"),
            QStringLiteral("hpp"),
            QStringLiteral("hxx"),
            QStringLiteral("py"),
            QStringLiteral("js"),
            QStringLiteral("jsx"),
            QStringLiteral("ts"),
            QStringLiteral("tsx"),
            QStringLiteral("rs"),
            QStringLiteral("go"),
            QStringLiteral("java"),
            QStringLiteral("cs"),
            QStringLiteral("qml"),
            QStringLiteral("sv"),
            QStringLiteral("svh"),
            QStringLiteral("v"),
            QStringLiteral("vh"),
            QStringLiteral("tcl")
        }.contains(suffix)) {
        return ResourceKind::CodeSnippet;
    }
    return ResourceKind::File;
}

bool isPlainTextContentFile(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 512 * 1024) {
        return false;
    }

    if (fileInfo.fileName().compare(QStringLiteral("go.mod"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return QStringList{
        QStringLiteral("txt"),
        QStringLiteral("text"),
        QStringLiteral("log"),
        QStringLiteral("list"),
        QStringLiteral("links"),
        QStringLiteral("urls"),
        QStringLiteral("csv"),
        QStringLiteral("tsv"),
        QStringLiteral("json"),
        QStringLiteral("jsonl"),
        QStringLiteral("yaml"),
        QStringLiteral("yml"),
        QStringLiteral("toml"),
        QStringLiteral("ini"),
        QStringLiteral("cfg"),
        QStringLiteral("conf")
    }.contains(suffix);
}

bool isStructuredPlainTextFile(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return QStringList{
        QStringLiteral("json"),
        QStringLiteral("jsonl"),
        QStringLiteral("yaml"),
        QStringLiteral("yml"),
        QStringLiteral("toml"),
        QStringLiteral("ini"),
        QStringLiteral("cfg"),
        QStringLiteral("conf")
    }.contains(suffix);
}

std::optional<QChar> tabularDelimiterForFile(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    if (suffix == QLatin1String("csv")) {
        return QLatin1Char(',');
    }
    if (suffix == QLatin1String("tsv")) {
        return QLatin1Char('\t');
    }
    return std::nullopt;
}

bool isPlainTextUrlListCandidate(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 512 * 1024) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return QStringList{
        QStringLiteral("txt"),
        QStringLiteral("text"),
        QStringLiteral("log"),
        QStringLiteral("list"),
        QStringLiteral("links"),
        QStringLiteral("urls")
    }.contains(suffix);
}

bool isFeedXmlCandidate(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 4 * 1024 * 1024) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QLatin1String("rss")
        || suffix == QLatin1String("atom")
        || suffix == QLatin1String("xml");
}

QString stripYamlQuotes(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            value = value.mid(1, value.size() - 2);
        }
    }
    return value.trimmed();
}

void appendUnique(QStringList &values, const QString &value)
{
    const QString normalized = stripYamlQuotes(value);
    if (!normalized.isEmpty() && !values.contains(normalized, Qt::CaseInsensitive)) {
        values.append(normalized);
    }
}

QStringList splitYamlInlineList(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('[')) && value.endsWith(QLatin1Char(']'))) {
        value = value.mid(1, value.size() - 2);
    }

    QStringList values;
    for (const QString &part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        appendUnique(values, part);
    }
    if (values.isEmpty()) {
        appendUnique(values, value);
    }
    return values;
}

void appendFrontmatterValue(QStringList &values, const QString &value)
{
    for (const QString &entry : splitYamlInlineList(value)) {
        appendUnique(values, entry);
    }
}

void appendFrontmatterListItem(QStringList &values, QString line)
{
    line = line.trimmed();
    if (!line.startsWith(QLatin1Char('-'))) {
        return;
    }
    appendUnique(values, line.mid(1));
}

void appendInlineTags(QStringList &tags, const QString &line)
{
    static const QRegularExpression tagPattern(QStringLiteral("(^|[^A-Za-z0-9_/-])#([A-Za-z0-9_/-]+)"));
    QRegularExpressionMatchIterator matches = tagPattern.globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        appendUnique(tags, match.captured(2));
    }
}

void appendWikilinksAsAliases(QStringList &aliases, const QString &line)
{
    static const QRegularExpression wikilinkPattern(QStringLiteral("\\[\\[([^\\]]+)\\]\\]"));
    QRegularExpressionMatchIterator matches = wikilinkPattern.globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString linkWithoutDisplayText = match.captured(1).section(QLatin1Char('|'), 0, 0).trimmed();
        const QString linkTarget = linkWithoutDisplayText.section(QLatin1Char('#'), 0, 0).trimmed();
        const QString linkFragment = linkWithoutDisplayText.section(QLatin1Char('#'), 1).trimmed();
        appendUnique(aliases, linkTarget);
        if (!linkFragment.isEmpty()) {
            appendUnique(aliases, linkFragment.startsWith(QLatin1Char('^')) ? linkFragment.mid(1) : linkFragment);
        }
    }
}

bool isLocalMarkdownLinkTarget(const QString &target)
{
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return false;
    }

    static const QRegularExpression schemePattern(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*):"));
    const QRegularExpressionMatch schemeMatch = schemePattern.match(trimmed);
    if (schemeMatch.hasMatch()) {
        return schemeMatch.captured(1).size() == 1
            && trimmed.size() >= 2
            && trimmed.at(1) == QLatin1Char(':');
    }
    return true;
}

QString markdownLinkPathWithoutFragment(QString target)
{
    target = target.trimmed();
    const int fragmentIndex = target.indexOf(QLatin1Char('#'));
    if (fragmentIndex >= 0) {
        target = target.left(fragmentIndex);
    }
    return QUrl::fromPercentEncoding(target.trimmed().toUtf8()).trimmed();
}

QString markdownLinkFragment(QString target)
{
    target = target.trimmed();
    const int fragmentIndex = target.indexOf(QLatin1Char('#'));
    if (fragmentIndex < 0 || fragmentIndex + 1 >= target.size()) {
        return {};
    }
    return QUrl::fromPercentEncoding(target.mid(fragmentIndex + 1).trimmed().toUtf8()).trimmed();
}

QString markdownNotePathForWikilink(QString targetPath)
{
    targetPath = targetPath.trimmed();
    if (targetPath.isEmpty() || QFileInfo(targetPath).suffix().isEmpty()) {
        targetPath.append(QStringLiteral(".md"));
    }
    return targetPath;
}

QString resourceIdForLinkedPath(const QFileInfo &sourceFileInfo, const QString &targetPath)
{
    QFileInfo targetInfo;
    if (QDir::isAbsolutePath(targetPath)) {
        targetInfo = QFileInfo(targetPath);
    } else {
        targetInfo = QFileInfo(sourceFileInfo.dir(), targetPath);
    }
    return QStringLiteral("file:%1").arg(normalizedPath(targetInfo));
}

void appendLocalMarkdownLinks(Resource &resource, const QFileInfo &fileInfo, const QString &line, int lineNumber)
{
    static const QRegularExpression linkPattern(QStringLiteral("(?<!!)\\[([^\\]]+)\\]\\(([^\\)]+)\\)"));
    QRegularExpressionMatchIterator matches = linkPattern.globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString rawTarget = match.captured(2).trimmed();
        if (!isLocalMarkdownLinkTarget(rawTarget)) {
            continue;
        }

        const QString targetPath = markdownLinkPathWithoutFragment(rawTarget);
        const QString label = markdownPlainTextFromLine(match.captured(1));
        appendUnique(resource.aliases, label);
        appendUnique(resource.aliases, QFileInfo(targetPath).fileName());
        appendUnique(resource.aliases, targetPath);

        const QString anchorTarget = label.isEmpty()
            ? QStringLiteral("link: %1").arg(targetPath)
            : QStringLiteral("link: %1 -> %2").arg(label, targetPath);
        appendFileLineAnchor(resource, anchorTarget, lineNumber);

        const QString targetResourceId = resourceIdForLinkedPath(fileInfo, targetPath);
        if (!targetResourceId.isEmpty() && targetResourceId != resource.id) {
            ResourceRelation relation;
            relation.sourceResourceId = resource.id;
            relation.targetResourceId = targetResourceId;
            relation.label = QStringLiteral("links-to");
            relation.note = anchorTarget;
            resource.relations.append(relation);
        }
    }
}

void appendLocalWikilinks(Resource &resource, const QFileInfo &fileInfo, const QString &line, int lineNumber)
{
    static const QRegularExpression wikilinkPattern(QStringLiteral("\\[\\[([^\\]]+)\\]\\]"));
    QRegularExpressionMatchIterator matches = wikilinkPattern.globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString rawLink = match.captured(1).trimmed();
        const QString rawTarget = rawLink.section(QLatin1Char('|'), 0, 0).trimmed();
        const QString displayText = rawLink.section(QLatin1Char('|'), 1).trimmed();
        const QString rawTargetPath = markdownLinkPathWithoutFragment(rawTarget);
        if (rawTargetPath.isEmpty()) {
            continue;
        }

        const QString targetPath = markdownNotePathForWikilink(rawTargetPath);
        const QString fragment = markdownLinkFragment(rawTarget);
        const QString normalizedFragment = fragment.startsWith(QLatin1Char('^')) ? fragment.mid(1) : fragment;
        const QString label = displayText.isEmpty()
            ? QFileInfo(rawTargetPath).fileName()
            : markdownPlainTextFromLine(displayText);

        appendUnique(resource.aliases, label);
        appendUnique(resource.aliases, QFileInfo(targetPath).fileName());
        appendUnique(resource.aliases, rawTargetPath);
        appendUnique(resource.aliases, targetPath);
        appendUnique(resource.aliases, normalizedFragment);

        QString anchorTarget = label.isEmpty()
            ? QStringLiteral("wikilink: %1").arg(targetPath)
            : QStringLiteral("wikilink: %1 -> %2").arg(label, targetPath);
        if (!normalizedFragment.isEmpty()) {
            anchorTarget.append(QStringLiteral("#%1").arg(normalizedFragment));
        }
        appendFileLineAnchor(resource, anchorTarget, lineNumber);

        const QString targetResourceId = resourceIdForLinkedPath(fileInfo, targetPath);
        if (!targetResourceId.isEmpty() && targetResourceId != resource.id) {
            ResourceRelation relation;
            relation.sourceResourceId = resource.id;
            relation.targetResourceId = targetResourceId;
            relation.label = QStringLiteral("links-to");
            relation.note = anchorTarget;
            resource.relations.append(relation);
        }
    }
}

QString markdownPlainTextFromLine(QString line)
{
    line = line.trimmed();
    if (line.isEmpty()) {
        return {};
    }

    static const QRegularExpression blockIdPattern(QStringLiteral("(?:^|\\s)\\^[A-Za-z0-9_-]+\\s*$"));
    line.remove(blockIdPattern);

    static const QRegularExpression headingMarkerPattern(QStringLiteral("^#{1,6}\\s+"));
    line.remove(headingMarkerPattern);

    static const QRegularExpression taskMarkerPattern(QStringLiteral("^[-*+]\\s+\\[[ xX-]\\]\\s+"));
    line.remove(taskMarkerPattern);

    static const QRegularExpression markdownLinkPattern(QStringLiteral("!?\\[([^\\]]+)\\]\\([^\\)]+\\)"));
    line.replace(markdownLinkPattern, QStringLiteral("\\1"));

    static const QRegularExpression wikilinkPattern(QStringLiteral("\\[\\[([^\\]|#]+)(?:#[^\\]|]+)?(?:\\|([^\\]]+))?\\]\\]"));
    QRegularExpressionMatchIterator matches = wikilinkPattern.globalMatch(line);
    QString replaced;
    int cursor = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        replaced.append(line.mid(cursor, match.capturedStart() - cursor));
        replaced.append(match.captured(2).isEmpty() ? match.captured(1).trimmed() : match.captured(2).trimmed());
        cursor = match.capturedEnd();
    }
    replaced.append(line.mid(cursor));
    line = replaced;

    static const QRegularExpression inlineTagPattern(QStringLiteral("(^|[^A-Za-z0-9_/-])#([A-Za-z0-9_/-]+)"));
    line.replace(inlineTagPattern, QStringLiteral("\\1\\2"));

    line.remove(QRegularExpression(QStringLiteral("[*_`~>]+")));
    static const QRegularExpression whitespacePattern(QStringLiteral("\\s+"));
    return line.replace(whitespacePattern, QStringLiteral(" ")).trimmed();
}

QString decodePdfLiteralString(const QString &value)
{
    QString decoded;
    decoded.reserve(value.size());

    int cursor = 0;
    while (cursor < value.size()) {
        const QChar ch = value.at(cursor++);
        if (ch == QLatin1Char('\\') && cursor < value.size()) {
            const QChar escaped = value.at(cursor++);
            if (escaped >= QLatin1Char('0') && escaped <= QLatin1Char('7')) {
                QString octal;
                octal.append(escaped);
                while (cursor < value.size()
                       && octal.size() < 3
                       && value.at(cursor) >= QLatin1Char('0')
                       && value.at(cursor) <= QLatin1Char('7')) {
                    octal.append(value.at(cursor++));
                }

                bool ok = false;
                const int code = octal.toInt(&ok, 8);
                if (ok) {
                    decoded.append(QChar(static_cast<ushort>(code)));
                }
                continue;
            }

            switch (escaped.unicode()) {
            case 'n':
                decoded.append(QLatin1Char('\n'));
                break;
            case 'r':
                decoded.append(QLatin1Char('\r'));
                break;
            case 't':
                decoded.append(QLatin1Char('\t'));
                break;
            case 'b':
                decoded.append(QLatin1Char('\b'));
                break;
            case 'f':
                decoded.append(QLatin1Char('\f'));
                break;
            case '\r':
                if (cursor < value.size() && value.at(cursor) == QLatin1Char('\n')) {
                    ++cursor;
                }
                break;
            case '\n':
                break;
            default:
                decoded.append(escaped);
                break;
            }
            continue;
        }

        decoded.append(ch);
    }

    return decoded.trimmed();
}

QString pdfTitleFromText(const QString &text)
{
    const int titleIndex = text.indexOf(QStringLiteral("/Title"));
    if (titleIndex < 0) {
        return {};
    }

    int cursor = titleIndex + 6;
    while (cursor < text.size() && text.at(cursor).isSpace()) {
        ++cursor;
    }
    if (cursor >= text.size()) {
        return {};
    }

    if (text.at(cursor) == QLatin1Char('(')) {
        ++cursor;
        QString literal;
        int depth = 1;
        bool escaped = false;
        while (cursor < text.size() && depth > 0) {
            const QChar ch = text.at(cursor++);
            if (escaped) {
                literal.append(QLatin1Char('\\'));
                literal.append(ch);
                escaped = false;
                continue;
            }
            if (ch == QLatin1Char('\\')) {
                escaped = true;
                continue;
            }
            if (ch == QLatin1Char('(')) {
                ++depth;
            } else if (ch == QLatin1Char(')')) {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            literal.append(ch);
        }
        return decodePdfLiteralString(literal);
    }

    if (text.at(cursor) == QLatin1Char('<')) {
        const int end = text.indexOf(QLatin1Char('>'), cursor + 1);
        if (end > cursor) {
            const QByteArray bytes = QByteArray::fromHex(text.mid(cursor + 1, end - cursor - 1).toLatin1());
            return QString::fromUtf8(bytes).trimmed();
        }
    }

    return {};
}

QList<PdfObject> pdfObjectsFromText(const QString &text)
{
    static const QRegularExpression objectPattern(
        QStringLiteral("(\\d+)\\s+\\d+\\s+obj\\b(.*?)\\bendobj"),
        QRegularExpression::DotMatchesEverythingOption);

    QList<PdfObject> objects;
    QRegularExpressionMatchIterator matches = objectPattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        PdfObject object;
        object.number = match.captured(1).toInt();
        object.body = match.captured(2);
        objects.append(object);
    }
    return objects;
}

QString pdfLiteralValueAfterKey(const QString &text, const QString &key)
{
    const QString marker = QStringLiteral("/%1").arg(key);
    const int keyIndex = text.indexOf(marker);
    if (keyIndex < 0) {
        return {};
    }

    int cursor = keyIndex + marker.size();
    while (cursor < text.size() && text.at(cursor).isSpace()) {
        ++cursor;
    }
    if (cursor >= text.size()) {
        return {};
    }

    if (text.at(cursor) == QLatin1Char('(')) {
        ++cursor;
        QString literal;
        int depth = 1;
        bool escaped = false;
        while (cursor < text.size() && depth > 0) {
            const QChar ch = text.at(cursor++);
            if (escaped) {
                literal.append(QLatin1Char('\\'));
                literal.append(ch);
                escaped = false;
                continue;
            }
            if (ch == QLatin1Char('\\')) {
                escaped = true;
                continue;
            }
            if (ch == QLatin1Char('(')) {
                ++depth;
            } else if (ch == QLatin1Char(')')) {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            literal.append(ch);
        }
        return decodePdfLiteralString(literal);
    }

    if (text.at(cursor) == QLatin1Char('<') && cursor + 1 < text.size() && text.at(cursor + 1) != QLatin1Char('<')) {
        const int end = text.indexOf(QLatin1Char('>'), cursor + 1);
        if (end > cursor) {
            const QByteArray bytes = QByteArray::fromHex(text.mid(cursor + 1, end - cursor - 1).toLatin1());
            return QString::fromUtf8(bytes).trimmed();
        }
    }

    return {};
}

QString pdfNameValueAfterKey(const QString &text, const QString &key)
{
    static const QRegularExpression delimiterPattern(QStringLiteral("[\\s/<>\\[\\]()]"));
    const QString marker = QStringLiteral("/%1").arg(key);
    int cursor = text.indexOf(marker);
    if (cursor < 0) {
        return {};
    }

    cursor += marker.size();
    while (cursor < text.size() && text.at(cursor).isSpace()) {
        ++cursor;
    }
    if (cursor >= text.size() || text.at(cursor) != QLatin1Char('/')) {
        return {};
    }
    ++cursor;

    int end = cursor;
    while (end < text.size()) {
        const QString ch(text.at(end));
        if (delimiterPattern.match(ch).hasMatch()) {
            break;
        }
        ++end;
    }

    return text.mid(cursor, end - cursor).trimmed();
}

int pdfIntegerValueAfterKey(const QString &text, const QString &key)
{
    const QRegularExpression pattern(QStringLiteral("/%1\\s+(\\d+)").arg(key));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return -1;
    }
    bool ok = false;
    const int value = match.captured(1).toInt(&ok);
    return ok ? value : -1;
}

QList<int> pdfAnnotationRefsFromPage(const QString &pageBody)
{
    static const QRegularExpression annotsPattern(
        QStringLiteral("/Annots\\s*\\[(.*?)\\]"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression refPattern(QStringLiteral("(\\d+)\\s+\\d+\\s+R"));

    QList<int> refs;
    const QRegularExpressionMatch annotsMatch = annotsPattern.match(pageBody);
    if (!annotsMatch.hasMatch()) {
        return refs;
    }

    QRegularExpressionMatchIterator matches = refPattern.globalMatch(annotsMatch.captured(1));
    while (matches.hasNext()) {
        refs.append(matches.next().captured(1).toInt());
    }
    return refs;
}

std::optional<QRectF> pdfRectFromAnnotation(const QString &annotationBody)
{
    static const QRegularExpression rectPattern(
        QStringLiteral("/Rect\\s*\\[\\s*"
                       "([+-]?\\d+(?:\\.\\d+)?)\\s+"
                       "([+-]?\\d+(?:\\.\\d+)?)\\s+"
                       "([+-]?\\d+(?:\\.\\d+)?)\\s+"
                       "([+-]?\\d+(?:\\.\\d+)?)\\s*\\]"));
    const QRegularExpressionMatch match = rectPattern.match(annotationBody);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    const double x1 = match.captured(1).toDouble();
    const double y1 = match.captured(2).toDouble();
    const double x2 = match.captured(3).toDouble();
    const double y2 = match.captured(4).toDouble();
    const double x = std::min(x1, x2);
    const double y = std::min(y1, y2);
    const double width = std::abs(x2 - x1);
    const double height = std::abs(y2 - y1);
    if (width <= 0.0 || height <= 0.0) {
        return std::nullopt;
    }

    return QRectF(x, y, width, height);
}

QString pdfAnnotationTarget(const QString &annotationBody, int page)
{
    QString target = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("Contents"));
    if (target.isEmpty()) {
        target = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("T"));
    }
    if (target.isEmpty()) {
        target = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("Subject"));
    }
    if (target.isEmpty()) {
        target = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("NM"));
    }
    if (target.isEmpty()) {
        target = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("URI"));
    }
    if (target.isEmpty()) {
        target = pdfNameValueAfterKey(annotationBody, QStringLiteral("Dest"));
    }
    if (target.isEmpty()) {
        target = QStringLiteral("Region page %1").arg(page);
    }
    return target.trimmed();
}

void appendPdfRegionAnchor(Resource &resource, const QString &annotationBody, int page)
{
    if (!annotationBody.contains(QStringLiteral("/Subtype"))) {
        return;
    }

    const std::optional<QRectF> region = pdfRectFromAnnotation(annotationBody);
    if (!region.has_value()) {
        return;
    }

    Anchor anchor;
    anchor.type = AnchorType::PdfRegion;
    anchor.target = pdfAnnotationTarget(annotationBody, page);
    anchor.page = page;
    anchor.region = region.value();
    resource.anchors.append(anchor);
}

QString shortcutUrlFromText(const QString &text)
{
    static const QRegularExpression internetShortcutPattern(
        QStringLiteral("(?im)^\\s*URL\\s*=\\s*(\\S.*)$"));
    const QRegularExpressionMatch internetShortcutMatch = internetShortcutPattern.match(text);
    if (internetShortcutMatch.hasMatch()) {
        return internetShortcutMatch.captured(1).trimmed();
    }

    static const QRegularExpression xmlUrlPattern(
        QStringLiteral("<key>\\s*URL\\s*</key>\\s*<string>\\s*([^<]+?)\\s*</string>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch xmlUrlMatch = xmlUrlPattern.match(text);
    if (xmlUrlMatch.hasMatch()) {
        return xmlUrlMatch.captured(1).trimmed();
    }

    static const QRegularExpression firstWebUrlPattern(QStringLiteral("(https?://[^\\s<>\"]+)"));
    const QRegularExpressionMatch firstWebUrlMatch = firstWebUrlPattern.match(text);
    if (firstWebUrlMatch.hasMatch()) {
        return firstWebUrlMatch.captured(1).trimmed();
    }

    return {};
}

QString shortcutTitleFromText(const QString &text)
{
    static const QRegularExpression titlePattern(
        QStringLiteral("(?im)^\\s*(?:Name|Title)\\s*=\\s*(.+)$"));
    const QRegularExpressionMatch titleMatch = titlePattern.match(text);
    if (titleMatch.hasMatch()) {
        return titleMatch.captured(1).trimmed();
    }
    return {};
}

bool isIndexableWebUrl(const QUrl &url)
{
    return url.isValid()
        && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
        && !url.host().isEmpty();
}

void appendUrlFragmentAnchor(Resource &resource, const QString &fragment)
{
    const QString target = fragment.trimmed();
    if (target.isEmpty()) {
        return;
    }

    const auto duplicate = std::find_if(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment
            && anchor.target.compare(target, Qt::CaseInsensitive) == 0;
    });
    if (duplicate != resource.anchors.cend()) {
        return;
    }

    Anchor anchor;
    anchor.type = AnchorType::UrlFragment;
    anchor.target = target;
    resource.anchors.append(anchor);
}

void appendWebUrlMetadata(Resource &resource, const QUrl &url)
{
    if (!isIndexableWebUrl(url)) {
        return;
    }

    appendUnique(resource.tags, QStringLiteral("web"));
    appendUnique(resource.aliases, url.host());
    if (url.host().startsWith(QLatin1String("www."), Qt::CaseInsensitive)) {
        appendUnique(resource.aliases, url.host().mid(4));
    }
    appendUnique(resource.aliases, url.toDisplayString());
    appendUrlFragmentAnchor(resource, url.fragment());
}

std::optional<DirectoryLibrarySource::WebPageFetchResult> defaultWebPageFetcher(const QUrl &url, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!isIndexableWebUrl(url)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("URL is not indexable: %1").arg(url.toDisplayString());
        }
        return std::nullopt;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Pinloom/0.1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QTimer timeout;
    timeout.setSingleShot(true);
    QEventLoop loop;
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Timed out fetching: %1").arg(url.toDisplayString());
        }
        reply->deleteLater();
        return std::nullopt;
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = reply->errorString();
        }
        reply->deleteLater();
        return std::nullopt;
    }

    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (!contentType.isEmpty() && !contentType.contains(QStringLiteral("html"), Qt::CaseInsensitive)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("URL did not return HTML: %1").arg(contentType);
        }
        reply->deleteLater();
        return std::nullopt;
    }

    DirectoryLibrarySource::WebPageFetchResult result;
    result.finalUrl = reply->url();
    result.body = reply->readAll();
    result.contentType = contentType;
    reply->deleteLater();
    return result;
}

QString decodedHtmlEntities(QString text)
{
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""), Qt::CaseInsensitive);
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"), Qt::CaseInsensitive);

    static const QRegularExpression numericEntityPattern(QStringLiteral("&#(x?[0-9A-Fa-f]+);"));
    QRegularExpressionMatchIterator matches = numericEntityPattern.globalMatch(text);
    QString decoded;
    int cursor = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        decoded.append(text.mid(cursor, match.capturedStart() - cursor));
        bool ok = false;
        const QString value = match.captured(1);
        const uint codePoint = value.startsWith(QLatin1Char('x'), Qt::CaseInsensitive)
            ? value.mid(1).toUInt(&ok, 16)
            : value.toUInt(&ok, 10);
        const char32_t character = static_cast<char32_t>(codePoint);
        decoded.append(ok ? QString::fromUcs4(&character, 1) : match.captured(0));
        cursor = match.capturedEnd();
    }
    decoded.append(text.mid(cursor));
    return decoded;
}

QString collapsedWhitespace(QString text)
{
    static const QRegularExpression whitespacePattern(QStringLiteral("\\s+"));
    return text.replace(whitespacePattern, QStringLiteral(" ")).trimmed();
}

bool readPdfLiteralStringAt(const QString &text, int start, QString *value, int *end)
{
    if (start < 0 || start >= text.size() || text.at(start) != QLatin1Char('(')) {
        return false;
    }

    QString literal;
    int cursor = start + 1;
    int depth = 1;
    bool escaped = false;
    while (cursor < text.size() && depth > 0) {
        const QChar ch = text.at(cursor++);
        if (escaped) {
            literal.append(QLatin1Char('\\'));
            literal.append(ch);
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char('(')) {
            ++depth;
        } else if (ch == QLatin1Char(')')) {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        literal.append(ch);
    }

    if (depth != 0) {
        return false;
    }
    if (value) {
        *value = decodePdfLiteralString(literal);
    }
    if (end) {
        *end = cursor;
    }
    return true;
}

QString decodePdfHexString(QString hex)
{
    hex.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (hex.isEmpty()) {
        return {};
    }
    if (hex.size() % 2 != 0) {
        hex.append(QLatin1Char('0'));
    }

    const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
    if (bytes.size() >= 2
        && static_cast<unsigned char>(bytes.at(0)) == 0xfe
        && static_cast<unsigned char>(bytes.at(1)) == 0xff) {
        QString decoded;
        for (int i = 2; i + 1 < bytes.size(); i += 2) {
            const ushort codeUnit = static_cast<ushort>(
                (static_cast<unsigned char>(bytes.at(i)) << 8)
                | static_cast<unsigned char>(bytes.at(i + 1)));
            decoded.append(QChar(codeUnit));
        }
        return decoded.trimmed();
    }

    return QString::fromUtf8(bytes).trimmed();
}

bool readPdfHexStringAt(const QString &text, int start, QString *value, int *end)
{
    if (start < 0
        || start >= text.size()
        || text.at(start) != QLatin1Char('<')
        || (start + 1 < text.size() && text.at(start + 1) == QLatin1Char('<'))) {
        return false;
    }

    const int close = text.indexOf(QLatin1Char('>'), start + 1);
    if (close <= start) {
        return false;
    }
    if (value) {
        *value = decodePdfHexString(text.mid(start + 1, close - start - 1));
    }
    if (end) {
        *end = close + 1;
    }
    return true;
}

QString pdfTextFromTextSection(const QString &section)
{
    QStringList chunks;
    int cursor = 0;
    while (cursor < section.size()) {
        QString value;
        int next = cursor + 1;
        if (readPdfLiteralStringAt(section, cursor, &value, &next)
            || readPdfHexStringAt(section, cursor, &value, &next)) {
            if (!value.trimmed().isEmpty()) {
                chunks.append(value);
            }
            cursor = next;
            continue;
        }
        ++cursor;
    }
    return chunks.join(QLatin1Char(' '));
}

QString pdfTextFromContentStream(QString stream)
{
    QStringList chunks;
    static const QRegularExpression textSectionPattern(
        QStringLiteral("\\bBT\\b(.*?)\\bET\\b"),
        QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator matches = textSectionPattern.globalMatch(stream);
    while (matches.hasNext()) {
        const QString text = pdfTextFromTextSection(matches.next().captured(1));
        if (!text.isEmpty()) {
            chunks.append(text);
        }
    }
    return collapsedWhitespace(chunks.join(QLatin1Char(' ')));
}

QByteArray pdfStreamBodyBytes(const QString &objectBody, QString *dictionary)
{
    const int streamStart = objectBody.indexOf(QStringLiteral("stream"));
    if (streamStart < 0) {
        return {};
    }
    if (dictionary) {
        *dictionary = objectBody.left(streamStart);
    }

    int bodyStart = streamStart + 6;
    if (bodyStart + 1 < objectBody.size()
        && objectBody.at(bodyStart) == QLatin1Char('\r')
        && objectBody.at(bodyStart + 1) == QLatin1Char('\n')) {
        bodyStart += 2;
    } else if (bodyStart < objectBody.size()
               && (objectBody.at(bodyStart) == QLatin1Char('\n') || objectBody.at(bodyStart) == QLatin1Char('\r'))) {
        ++bodyStart;
    }

    const int streamEnd = objectBody.indexOf(QStringLiteral("endstream"), bodyStart);
    if (streamEnd <= bodyStart) {
        return {};
    }

    int bodyEnd = streamEnd;
    while (bodyEnd > bodyStart
           && (objectBody.at(bodyEnd - 1) == QLatin1Char('\n') || objectBody.at(bodyEnd - 1) == QLatin1Char('\r'))) {
        --bodyEnd;
    }
    return objectBody.mid(bodyStart, bodyEnd - bodyStart).toLatin1();
}

QByteArray qUncompressPdfFlateData(const QByteArray &compressed, int decodedLengthHint)
{
    if (compressed.isEmpty()) {
        return {};
    }

    QList<int> sizeHints;
    if (decodedLengthHint > 0) {
        sizeHints.append(decodedLengthHint);
    }
    const int scaledHint = std::max(4096, static_cast<int>(compressed.size() * 8));
    for (int hint : {scaledHint, 64 * 1024, 256 * 1024, 1024 * 1024, 4 * 1024 * 1024}) {
        if (!sizeHints.contains(hint)) {
            sizeHints.append(hint);
        }
    }

    for (const int hint : sizeHints) {
        QByteArray prefixed;
        prefixed.reserve(compressed.size() + 4);
        prefixed.append(char((hint >> 24) & 0xff));
        prefixed.append(char((hint >> 16) & 0xff));
        prefixed.append(char((hint >> 8) & 0xff));
        prefixed.append(char(hint & 0xff));
        prefixed.append(compressed);

        const QByteArray decoded = qUncompress(prefixed);
        if (!decoded.isEmpty()) {
            return decoded;
        }
    }

    return {};
}

QByteArray decodePdfAsciiHexData(const QByteArray &encoded)
{
    QByteArray hex;
    hex.reserve(encoded.size());
    for (const char raw : encoded) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isspace(ch)) {
            continue;
        }
        if (ch == '>') {
            break;
        }
        if (std::isxdigit(ch)) {
            hex.append(static_cast<char>(ch));
        }
    }

    if (hex.isEmpty()) {
        return {};
    }
    if (hex.size() % 2 != 0) {
        hex.append('0');
    }
    return QByteArray::fromHex(hex);
}

QByteArray decodePdfAscii85Data(const QByteArray &encoded)
{
    QByteArray decoded;
    QByteArray group;
    group.reserve(5);

    auto appendDecodedGroup = [&](QByteArray tuple, int outputBytes) {
        while (tuple.size() < 5) {
            tuple.append('u');
        }

        quint64 value = 0;
        for (const char raw : tuple) {
            value = value * 85 + static_cast<unsigned char>(raw) - 33;
        }

        for (int shift = 24; shift >= 0 && outputBytes > 0; shift -= 8, --outputBytes) {
            decoded.append(static_cast<char>((value >> shift) & 0xff));
        }
    };

    for (const char raw : encoded) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isspace(ch)) {
            continue;
        }
        if (ch == '~') {
            break;
        }
        if (ch == 'z' && group.isEmpty()) {
            decoded.append(QByteArray(4, '\0'));
            continue;
        }
        if (ch < '!' || ch > 'u') {
            continue;
        }

        group.append(static_cast<char>(ch));
        if (group.size() == 5) {
            appendDecodedGroup(group, 4);
            group.clear();
        }
    }

    if (!group.isEmpty()) {
        appendDecodedGroup(group, group.size() - 1);
    }
    return decoded;
}

QByteArray decodePdfRunLengthData(const QByteArray &encoded)
{
    QByteArray decoded;
    int cursor = 0;
    while (cursor < encoded.size()) {
        const int lengthByte = static_cast<unsigned char>(encoded.at(cursor++));
        if (lengthByte == 128) {
            break;
        }

        if (lengthByte <= 127) {
            const int count = lengthByte + 1;
            if (cursor + count > encoded.size()) {
                return {};
            }
            decoded.append(encoded.constData() + cursor, count);
            cursor += count;
            continue;
        }

        if (cursor >= encoded.size()) {
            return {};
        }
        decoded.append(QByteArray(257 - lengthByte, encoded.at(cursor++)));
    }
    return decoded;
}

QByteArray decodePdfLzwData(const QByteArray &encoded, int earlyChange = 1)
{
    if (encoded.isEmpty()) {
        return {};
    }

    QVector<QByteArray> table(4096);
    auto resetTable = [&]() {
        std::fill(table.begin(), table.end(), QByteArray{});
        for (int i = 0; i < 256; ++i) {
            table[i] = QByteArray(1, static_cast<char>(i));
        }
    };

    int byteIndex = 0;
    int bitIndex = 0;
    auto readCode = [&](int bitWidth, int *code) {
        if (!code || bitWidth <= 0) {
            return false;
        }

        int value = 0;
        for (int i = 0; i < bitWidth; ++i) {
            if (byteIndex >= encoded.size()) {
                return false;
            }
            const int byte = static_cast<unsigned char>(encoded.at(byteIndex));
            value = (value << 1) | ((byte >> (7 - bitIndex)) & 0x01);
            ++bitIndex;
            if (bitIndex == 8) {
                bitIndex = 0;
                ++byteIndex;
            }
        }
        *code = value;
        return true;
    };

    resetTable();
    QByteArray decoded;
    QByteArray previous;
    int codeSize = 9;
    int nextCode = 258;

    int code = -1;
    while (readCode(codeSize, &code)) {
        if (code == 256) {
            resetTable();
            previous.clear();
            codeSize = 9;
            nextCode = 258;
            continue;
        }
        if (code == 257) {
            return decoded;
        }

        QByteArray entry;
        if (code >= 0 && code < table.size() && !table.at(code).isEmpty()) {
            entry = table.at(code);
        } else if (code == nextCode && !previous.isEmpty()) {
            entry = previous + previous.left(1);
        } else {
            return {};
        }

        decoded.append(entry);

        if (!previous.isEmpty() && nextCode < table.size()) {
            table[nextCode++] = previous + entry.left(1);
            if (codeSize < 12 && nextCode + earlyChange == (1 << codeSize)) {
                ++codeSize;
            }
        }

        previous = entry;
    }

    return {};
}

QStringList pdfFilterNames(const QString &dictionary)
{
    const int filterIndex = dictionary.indexOf(QStringLiteral("/Filter"));
    if (filterIndex < 0) {
        return {};
    }

    int cursor = filterIndex + 7;
    while (cursor < dictionary.size() && dictionary.at(cursor).isSpace()) {
        ++cursor;
    }
    if (cursor >= dictionary.size()) {
        return {};
    }

    auto readNameAt = [&](int *position) {
        if (!position || *position >= dictionary.size() || dictionary.at(*position) != QLatin1Char('/')) {
            return QString();
        }

        ++(*position);
        const int start = *position;
        while (*position < dictionary.size()) {
            const QChar ch = dictionary.at(*position);
            if (ch.isSpace()
                || ch == QLatin1Char('/')
                || ch == QLatin1Char('[')
                || ch == QLatin1Char(']')
                || ch == QLatin1Char('<')
                || ch == QLatin1Char('>')
                || ch == QLatin1Char('(')
                || ch == QLatin1Char(')')) {
                break;
            }
            ++(*position);
        }
        return dictionary.mid(start, *position - start);
    };

    QStringList filters;
    if (dictionary.at(cursor) == QLatin1Char('/')) {
        const QString name = readNameAt(&cursor);
        if (!name.isEmpty()) {
            filters.append(name);
        }
        return filters;
    }

    if (dictionary.at(cursor) != QLatin1Char('[')) {
        return {};
    }

    ++cursor;
    while (cursor < dictionary.size() && dictionary.at(cursor) != QLatin1Char(']')) {
        if (dictionary.at(cursor) == QLatin1Char('/')) {
            const QString name = readNameAt(&cursor);
            if (!name.isEmpty()) {
                filters.append(name);
            }
            continue;
        }
        ++cursor;
    }
    return filters;
}

QByteArray decodePdfStreamFilter(const QByteArray &input,
                                 const QString &filterName,
                                 int decodedLengthHint,
                                 int lzwEarlyChange)
{
    if (filterName == QLatin1String("ASCIIHexDecode") || filterName == QLatin1String("AHx")) {
        return decodePdfAsciiHexData(input);
    }
    if (filterName == QLatin1String("ASCII85Decode") || filterName == QLatin1String("A85")) {
        return decodePdfAscii85Data(input);
    }
    if (filterName == QLatin1String("FlateDecode") || filterName == QLatin1String("Fl")) {
        return qUncompressPdfFlateData(input, decodedLengthHint);
    }
    if (filterName == QLatin1String("RunLengthDecode") || filterName == QLatin1String("RL")) {
        return decodePdfRunLengthData(input);
    }
    if (filterName == QLatin1String("LZWDecode") || filterName == QLatin1String("LZW")) {
        return decodePdfLzwData(input, lzwEarlyChange);
    }
    return {};
}

QString pdfDecodedStreamBody(const QString &objectBody)
{
    QString dictionary;
    const QByteArray body = pdfStreamBodyBytes(objectBody, &dictionary);
    if (body.isEmpty()) {
        return {};
    }

    if (!dictionary.contains(QStringLiteral("/Filter"))) {
        return QString::fromLatin1(body);
    }

    const QStringList filters = pdfFilterNames(dictionary);
    if (filters.isEmpty()) {
        return {};
    }

    QByteArray decoded = body;
    const int decodedLengthHint = pdfIntegerValueAfterKey(dictionary, QStringLiteral("DL"));
    const int parsedLzwEarlyChange = pdfIntegerValueAfterKey(dictionary, QStringLiteral("EarlyChange"));
    const int lzwEarlyChange = parsedLzwEarlyChange == 0 ? 0 : 1;
    for (const QString &filter : filters) {
        decoded = decodePdfStreamFilter(decoded, filter, decodedLengthHint, lzwEarlyChange);
        if (decoded.isEmpty()) {
            return {};
        }
    }
    return QString::fromLatin1(decoded);
}

QString pdfContentTextFromObjects(const QList<PdfObject> &objects)
{
    QStringList chunks;
    for (const PdfObject &object : objects) {
        const QString stream = pdfDecodedStreamBody(object.body);
        if (stream.isEmpty()) {
            continue;
        }
        const QString text = pdfTextFromContentStream(stream);
        if (!text.isEmpty()) {
            chunks.append(text);
        }
    }
    return collapsedWhitespace(chunks.join(QLatin1Char(' ')));
}

QString plainTextFromHtml(QString html)
{
    html.remove(QRegularExpression(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption));
    html.remove(QRegularExpression(QStringLiteral("<script\\b[^>]*>.*?</script>"),
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    html.remove(QRegularExpression(QStringLiteral("<style\\b[^>]*>.*?</style>"),
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    html.replace(QRegularExpression(QStringLiteral("</(?:p|div|section|article|li|h[1-6]|br)\\s*>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    html.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    return collapsedWhitespace(decodedHtmlEntities(html));
}

QString htmlElementText(const QString &html, const QString &tagName)
{
    const QRegularExpression pattern(QStringLiteral("<%1\\b[^>]*>(.*?)</%1>").arg(tagName),
                                     QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = pattern.match(html);
    if (!match.hasMatch()) {
        return {};
    }
    return plainTextFromHtml(match.captured(1));
}

QString htmlAttributeValue(const QString &attributes, const QString &name)
{
    const QRegularExpression pattern(QStringLiteral("\\b%1\\s*=\\s*([\"'])(.*?)\\1").arg(name),
                                     QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = pattern.match(attributes);
    if (!match.hasMatch()) {
        return {};
    }
    return decodedHtmlEntities(match.captured(2).trimmed());
}

QString canonicalUrlFromHtml(const QString &html)
{
    static const QRegularExpression linkPattern(QStringLiteral("<link\\b([^>]*)>"),
                                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator matches = linkPattern.globalMatch(html);
    while (matches.hasNext()) {
        const QString attributes = matches.next().captured(1);
        if (htmlAttributeValue(attributes, QStringLiteral("rel")).compare(QStringLiteral("canonical"), Qt::CaseInsensitive) == 0) {
            return htmlAttributeValue(attributes, QStringLiteral("href"));
        }
    }
    return {};
}

void appendHtmlHeadingAnchors(Resource &resource, const QString &html)
{
    static const QRegularExpression headingPattern(QStringLiteral("<h[1-6]\\b([^>]*)>(.*?)</h[1-6]>"),
                                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator matches = headingPattern.globalMatch(html);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString id = htmlAttributeValue(match.captured(1), QStringLiteral("id")).trimmed();
        if (id.isEmpty()) {
            continue;
        }

        appendUrlFragmentAnchor(resource, id);
        appendUnique(resource.aliases, plainTextFromHtml(match.captured(2)));
    }
}

void applyHtmlDocumentMetadata(Resource &resource,
                               const QString &html,
                               const QString &fallbackTitle,
                               const std::optional<QUrl> &sourceUrl)
{
    const QString title = htmlElementText(html, QStringLiteral("title"));
    resource.title = title.isEmpty() ? fallbackTitle : title;
    if (resource.title.isEmpty()) {
        resource.title = resource.location;
    }
    resource.content = plainTextFromHtml(html);
    appendUnique(resource.tags, QStringLiteral("web"));

    if (sourceUrl.has_value()) {
        appendWebUrlMetadata(resource, sourceUrl.value());
    }

    const QUrl canonicalUrl = QUrl::fromUserInput(canonicalUrlFromHtml(html));
    appendWebUrlMetadata(resource, canonicalUrl);
    appendHtmlHeadingAnchors(resource, html);
}

QList<HtmlLink> htmlLinksFromDocument(const QString &html)
{
    QList<HtmlLink> links;
    static const QRegularExpression anchorPattern(QStringLiteral("<a\\b([^>]*)>(.*?)</a>"),
                                                  QRegularExpression::CaseInsensitiveOption
                                                      | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator matches = anchorPattern.globalMatch(html);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QUrl url = QUrl::fromUserInput(htmlAttributeValue(match.captured(1), QStringLiteral("href")));
        if (!isIndexableWebUrl(url)) {
            continue;
        }

        HtmlLink link;
        link.url = url;
        link.title = plainTextFromHtml(match.captured(2));
        if (link.title.isEmpty()) {
            link.title = url.toDisplayString();
        }
        links.append(link);
    }
    return links;
}

QString trimmedPlainTextUrl(QString rawUrl)
{
    rawUrl = rawUrl.trimmed();
    static const QString trailingPunctuation = QStringLiteral(".,;:!?)]}\"'");
    while (!rawUrl.isEmpty() && trailingPunctuation.contains(rawUrl.back())) {
        rawUrl.chop(1);
    }
    return rawUrl;
}

QString titleFromTextBeforeUrl(QString linePrefix)
{
    linePrefix = collapsedWhitespace(linePrefix);
    while (linePrefix.startsWith(QLatin1Char('-'))
           || linePrefix.startsWith(QLatin1Char('*'))
           || linePrefix.startsWith(QLatin1Char('>'))) {
        linePrefix = linePrefix.mid(1).trimmed();
    }

    static const QRegularExpression trailingSeparatorPattern(QStringLiteral("[\\s:=-]+$"));
    linePrefix.remove(trailingSeparatorPattern);
    return collapsedWhitespace(linePrefix);
}

QList<TextUrlLink> textUrlLinksFromDocument(const QString &text)
{
    QList<TextUrlLink> links;
    QStringList seenUrls;
    static const QRegularExpression urlPattern(QStringLiteral("https?://[^\\s<>\"]+"));

    for (const QString &line : text.split(QLatin1Char('\n'))) {
        QRegularExpressionMatchIterator matches = urlPattern.globalMatch(line);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const QUrl url = QUrl::fromUserInput(trimmedPlainTextUrl(match.captured(0)));
            if (!isIndexableWebUrl(url)) {
                continue;
            }

            const QString urlKey = url.toString(QUrl::FullyEncoded);
            if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
                continue;
            }
            seenUrls.append(urlKey);

            TextUrlLink link;
            link.url = url;
            link.title = titleFromTextBeforeUrl(line.left(match.capturedStart()));
            if (link.title.isEmpty()) {
                link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
            }
            links.append(link);
        }
    }
    return links;
}

bool looksLikeBookmarkExport(const QString &html)
{
    return html.contains(QStringLiteral("NETSCAPE-Bookmark-file-1"), Qt::CaseInsensitive)
        || (html.contains(QStringLiteral("<dt><a"), Qt::CaseInsensitive)
            && html.contains(QStringLiteral("<h1"), Qt::CaseInsensitive)
            && html.contains(QStringLiteral("bookmark"), Qt::CaseInsensitive));
}

bool isBrowserBookmarkJsonCandidate(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 4 * 1024 * 1024) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QLatin1String("json")
        || fileInfo.fileName().compare(QStringLiteral("Bookmarks"), Qt::CaseInsensitive) == 0;
}

void appendBrowserBookmarkLinksFromNode(const QJsonObject &node,
                                        const QStringList &folderPath,
                                        QList<BrowserBookmarkLink> &links)
{
    const QString type = node.value(QStringLiteral("type")).toString();
    if (type.compare(QStringLiteral("url"), Qt::CaseInsensitive) == 0) {
        const QUrl url = QUrl::fromUserInput(node.value(QStringLiteral("url")).toString().trimmed());
        if (!isIndexableWebUrl(url)) {
            return;
        }

        BrowserBookmarkLink link;
        link.url = url;
        link.title = node.value(QStringLiteral("name")).toString().trimmed();
        if (link.title.isEmpty()) {
            link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
        }
        link.folderPath = folderPath;
        links.append(link);
        return;
    }

    QStringList childFolderPath = folderPath;
    const QString folderName = node.value(QStringLiteral("name")).toString().trimmed();
    if (!folderName.isEmpty()
        && (childFolderPath.isEmpty()
            || childFolderPath.constLast().compare(folderName, Qt::CaseInsensitive) != 0)) {
        childFolderPath.append(folderName);
    }

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children) {
        if (child.isObject()) {
            appendBrowserBookmarkLinksFromNode(child.toObject(), childFolderPath, links);
        }
    }
}

QList<BrowserBookmarkLink> browserBookmarkLinksFromJsonDocument(const QJsonDocument &document)
{
    if (!document.isObject()) {
        return {};
    }

    const QJsonObject root = document.object();
    const QJsonObject roots = root.value(QStringLiteral("roots")).toObject();
    if (roots.isEmpty()) {
        return {};
    }

    QList<BrowserBookmarkLink> links;
    for (auto it = roots.constBegin(); it != roots.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }

        QStringList folderPath;
        if (it.key() == QLatin1String("bookmark_bar")) {
            folderPath.append(QStringLiteral("Bookmarks Bar"));
        } else if (it.key() == QLatin1String("other")) {
            folderPath.append(QStringLiteral("Other Bookmarks"));
        } else if (it.key() == QLatin1String("synced")) {
            folderPath.append(QStringLiteral("Mobile Bookmarks"));
        }

        appendBrowserBookmarkLinksFromNode(it.value().toObject(), folderPath, links);
    }
    return links;
}

QString xmlAttributeValue(const QXmlStreamAttributes &attributes, const QString &name)
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.name().toString().compare(name, Qt::CaseInsensitive) == 0) {
            return attribute.value().toString().trimmed();
        }
    }
    return {};
}

QList<OpmlLink> opmlLinksFromDocument(const QByteArray &content)
{
    QXmlStreamReader reader(content);
    QList<OpmlLink> links;
    QStringList folderPath;
    QList<bool> outlinePathPushed;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement() && reader.name().toString().compare(QStringLiteral("outline"), Qt::CaseInsensitive) == 0) {
            const QXmlStreamAttributes attributes = reader.attributes();
            QString title = xmlAttributeValue(attributes, QStringLiteral("text"));
            if (title.isEmpty()) {
                title = xmlAttributeValue(attributes, QStringLiteral("title"));
            }

            const QUrl pageUrl = QUrl::fromUserInput(xmlAttributeValue(attributes, QStringLiteral("htmlUrl")));
            const QUrl directUrl = QUrl::fromUserInput(xmlAttributeValue(attributes, QStringLiteral("url")));
            const QUrl feedUrl = QUrl::fromUserInput(xmlAttributeValue(attributes, QStringLiteral("xmlUrl")));
            const QUrl resourceUrl = isIndexableWebUrl(pageUrl)
                ? pageUrl
                : (isIndexableWebUrl(directUrl) ? directUrl : feedUrl);

            if (isIndexableWebUrl(resourceUrl)) {
                OpmlLink link;
                link.url = resourceUrl;
                link.title = title.isEmpty()
                    ? (resourceUrl.host().isEmpty() ? resourceUrl.toDisplayString() : resourceUrl.host())
                    : title;
                if (isIndexableWebUrl(feedUrl)) {
                    link.feedUrl = feedUrl;
                }
                link.folderPath = folderPath;
                links.append(link);
            }

            const bool pushPath = !title.isEmpty();
            if (pushPath) {
                folderPath.append(title);
            }
            outlinePathPushed.append(pushPath);
        } else if (reader.isEndElement()
                   && reader.name().toString().compare(QStringLiteral("outline"), Qt::CaseInsensitive) == 0
                   && !outlinePathPushed.isEmpty()) {
            if (outlinePathPushed.takeLast() && !folderPath.isEmpty()) {
                folderPath.removeLast();
            }
        }
    }

    if (reader.hasError()) {
        return {};
    }
    return links;
}

QList<FeedEntryLink> feedLinksFromXmlDocument(const QByteArray &content)
{
    QXmlStreamReader reader(content);
    QList<FeedEntryLink> links;
    QString feedTitle;
    bool rootSeen = false;
    bool rssFeed = false;
    bool atomFeed = false;
    bool inChannel = false;
    bool inItem = false;
    bool inEntry = false;
    FeedEntryLink current;

    auto finishCurrent = [&]() {
        if (isIndexableWebUrl(current.url)) {
            if (current.title.isEmpty()) {
                current.title = current.url.host().isEmpty() ? current.url.toDisplayString() : current.url.host();
            }
            current.feedTitle = feedTitle;
            links.append(current);
        }
        current = {};
    };

    while (!reader.atEnd()) {
        reader.readNext();
        const QString name = reader.name().toString().toLower();

        if (reader.isStartElement()) {
            if (!rootSeen) {
                rootSeen = true;
                rssFeed = name == QLatin1String("rss") || name == QLatin1String("rdf");
                atomFeed = name == QLatin1String("feed");
                if (!rssFeed && !atomFeed) {
                    return {};
                }
            }

            if (rssFeed) {
                if (name == QLatin1String("channel")) {
                    inChannel = true;
                } else if (inChannel && name == QLatin1String("item")) {
                    inItem = true;
                    current = {};
                } else if (inChannel && !inItem && name == QLatin1String("title")) {
                    feedTitle = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                } else if (inItem && name == QLatin1String("title")) {
                    current.title = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                } else if (inItem && name == QLatin1String("link")) {
                    const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                    if (isIndexableWebUrl(url)) {
                        current.url = url;
                    }
                } else if (inItem && name == QLatin1String("guid") && !isIndexableWebUrl(current.url)) {
                    const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                    if (isIndexableWebUrl(url)) {
                        current.url = url;
                    }
                } else if (inItem && name == QLatin1String("category")) {
                    appendUnique(current.categories, reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                }
            } else if (atomFeed) {
                if (!inEntry && name == QLatin1String("title")) {
                    feedTitle = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                } else if (name == QLatin1String("entry")) {
                    inEntry = true;
                    current = {};
                } else if (inEntry && name == QLatin1String("title")) {
                    current.title = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                } else if (inEntry && name == QLatin1String("link")) {
                    const QString rel = xmlAttributeValue(reader.attributes(), QStringLiteral("rel"));
                    const QUrl url = QUrl::fromUserInput(xmlAttributeValue(reader.attributes(), QStringLiteral("href")));
                    if ((rel.isEmpty() || rel.compare(QStringLiteral("alternate"), Qt::CaseInsensitive) == 0)
                        && isIndexableWebUrl(url)) {
                        current.url = url;
                    }
                } else if (inEntry && name == QLatin1String("id") && !isIndexableWebUrl(current.url)) {
                    const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                    if (isIndexableWebUrl(url)) {
                        current.url = url;
                    }
                } else if (inEntry && name == QLatin1String("category")) {
                    QString category = xmlAttributeValue(reader.attributes(), QStringLiteral("term"));
                    if (category.isEmpty()) {
                        category = xmlAttributeValue(reader.attributes(), QStringLiteral("label"));
                    }
                    appendUnique(current.categories, category);
                }
            }
        } else if (reader.isEndElement()) {
            if (rssFeed && name == QLatin1String("item")) {
                inItem = false;
                finishCurrent();
            } else if (rssFeed && name == QLatin1String("channel")) {
                inChannel = false;
            } else if (atomFeed && name == QLatin1String("entry")) {
                inEntry = false;
                finishCurrent();
            }
        }
    }

    if (reader.hasError()) {
        return {};
    }
    return links;
}

QList<SitemapLink> sitemapLinksFromXmlDocument(const QByteArray &content)
{
    QXmlStreamReader reader(content);
    QList<SitemapLink> links;
    bool rootSeen = false;
    bool urlset = false;
    bool sitemapIndex = false;
    bool inUrl = false;
    bool inSitemap = false;

    while (!reader.atEnd()) {
        reader.readNext();
        const QString name = reader.name().toString().toLower();

        if (reader.isStartElement()) {
            if (!rootSeen) {
                rootSeen = true;
                urlset = name == QLatin1String("urlset");
                sitemapIndex = name == QLatin1String("sitemapindex");
                if (!urlset && !sitemapIndex) {
                    return {};
                }
            }

            if (urlset && name == QLatin1String("url")) {
                inUrl = true;
            } else if (sitemapIndex && name == QLatin1String("sitemap")) {
                inSitemap = true;
            } else if ((inUrl || inSitemap) && name == QLatin1String("loc")) {
                const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                if (isIndexableWebUrl(url)) {
                    SitemapLink link;
                    link.url = url;
                    link.sitemapIndexEntry = inSitemap;
                    links.append(link);
                }
            }
        } else if (reader.isEndElement()) {
            if (urlset && name == QLatin1String("url")) {
                inUrl = false;
            } else if (sitemapIndex && name == QLatin1String("sitemap")) {
                inSitemap = false;
            }
        }
    }

    if (reader.hasError()) {
        return {};
    }
    return links;
}

int pdfPageCountFromText(const QString &text)
{
    static const QRegularExpression pagePattern(QStringLiteral("/Type\\s*/Page\\b"));
    int count = 0;
    QRegularExpressionMatchIterator matches = pagePattern.globalMatch(text);
    while (matches.hasNext()) {
        matches.next();
        ++count;
    }
    return count;
}

void appendCodeSymbol(Resource &resource, const QString &target, int line)
{
    const QString normalized = target.trimmed();
    if (normalized.isEmpty()) {
        return;
    }
    const auto duplicate = std::find_if(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
        return anchor.type == AnchorType::CodeSymbol && anchor.target == normalized && anchor.line == line;
    });
    if (duplicate != resource.anchors.cend()) {
        return;
    }

    Anchor anchor;
    anchor.type = AnchorType::CodeSymbol;
    anchor.target = normalized;
    anchor.line = line;
    resource.anchors.append(anchor);
}

void appendFileLineAnchor(Resource &resource, const QString &target, int line)
{
    const QString normalized = collapsedWhitespace(target);
    if (normalized.isEmpty()) {
        return;
    }
    const auto duplicate = std::find_if(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target.compare(normalized, Qt::CaseInsensitive) == 0
            && anchor.line == line;
    });
    if (duplicate != resource.anchors.cend()) {
        return;
    }

    Anchor anchor;
    anchor.type = AnchorType::FileLine;
    anchor.target = normalized;
    anchor.line = line;
    resource.anchors.append(anchor);
}

void appendActionLineAnchorsFromLine(Resource &resource, const QString &line, int lineNumber)
{
    static const QRegularExpression markerPattern(
        QStringLiteral("\\b(TODO|FIXME|NOTE)\\b\\s*:?[\\s-]*(.*)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = markerPattern.match(line);
    if (!match.hasMatch()) {
        return;
    }

    const QString kind = match.captured(1).toUpper();
    const QString detail = match.captured(2).trimmed();
    appendFileLineAnchor(resource,
                         detail.isEmpty() ? kind : QStringLiteral("%1: %2").arg(kind, detail),
                         lineNumber);
}

void appendCodeDependencyAnchorsFromLine(Resource &resource, const QString &line, int lineNumber)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    static const QRegularExpression cppIncludePattern(
        QStringLiteral("^#\\s*include\\s*[<\"]([^>\"]+)[>\"]"));
    static const QRegularExpression pythonFromPattern(
        QStringLiteral("^from\\s+([A-Za-z_][A-Za-z0-9_.]*)\\s+import\\s+.+$"));
    static const QRegularExpression pythonImportPattern(
        QStringLiteral("^import\\s+([A-Za-z_][A-Za-z0-9_.]*(?:\\s*,\\s*[A-Za-z_][A-Za-z0-9_.]*)*)\\b"));
    static const QRegularExpression jsImportFromPattern(
        QStringLiteral("^import(?:\\s+type)?\\s+.+\\s+from\\s+[\"']([^\"']+)[\"']"));
    static const QRegularExpression jsSideEffectImportPattern(
        QStringLiteral("^import\\s+[\"']([^\"']+)[\"']"));
    static const QRegularExpression jsRequirePattern(
        QStringLiteral("^(?:const|let|var)\\s+[A-Za-z_$][A-Za-z0-9_$]*\\s*=\\s*require\\s*\\(\\s*[\"']([^\"']+)[\"']\\s*\\)"));
    static const QRegularExpression jsDynamicImportPattern(
        QStringLiteral("\\bimport\\s*\\(\\s*[\"']([^\"']+)[\"']\\s*\\)"));
    static const QRegularExpression rustUsePattern(
        QStringLiteral("^use\\s+([^;]+);"));
    static const QRegularExpression goImportPattern(
        QStringLiteral("^import\\s+\"([^\"]+)\""));
    static const QRegularExpression javaImportPattern(
        QStringLiteral("^import\\s+(?:static\\s+)?([^;]+);"));
    static const QRegularExpression csharpUsingPattern(
        QStringLiteral("^using\\s+([^;=]+);"));

    struct DependencyPattern {
        const QRegularExpression *pattern;
        QString label;
    };

    for (const DependencyPattern &candidate : {
             DependencyPattern{&cppIncludePattern, QStringLiteral("include")},
             DependencyPattern{&pythonFromPattern, QStringLiteral("import")},
             DependencyPattern{&jsImportFromPattern, QStringLiteral("import")},
             DependencyPattern{&jsSideEffectImportPattern, QStringLiteral("import")},
             DependencyPattern{&jsRequirePattern, QStringLiteral("require")},
             DependencyPattern{&jsDynamicImportPattern, QStringLiteral("import")},
             DependencyPattern{&pythonImportPattern, QStringLiteral("import")},
             DependencyPattern{&rustUsePattern, QStringLiteral("use")},
             DependencyPattern{&goImportPattern, QStringLiteral("import")},
             DependencyPattern{&javaImportPattern, QStringLiteral("import")},
             DependencyPattern{&csharpUsingPattern, QStringLiteral("using")}
         }) {
        const QRegularExpressionMatch match = candidate.pattern->match(trimmed);
        if (match.hasMatch()) {
            appendFileLineAnchor(resource,
                                 QStringLiteral("%1: %2").arg(candidate.label, match.captured(1).trimmed()),
                                 lineNumber);
            return;
        }
    }
}

bool isGoImportBlockStart(const QString &line)
{
    static const QRegularExpression pattern(QStringLiteral("^import\\s*\\($"));
    return pattern.match(line.trimmed()).hasMatch();
}

bool isGoImportBlockEnd(const QString &line)
{
    return line.trimmed().startsWith(QLatin1Char(')'));
}

void appendGoImportBlockDependencyAnchorFromLine(Resource &resource, const QString &line, int lineNumber)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:(?:[A-Za-z_][A-Za-z0-9_]*|\\.|_)\\s+)?\"([^\"]+)\""));
    const QRegularExpressionMatch match = pattern.match(line.trimmed());
    if (match.hasMatch()) {
        appendFileLineAnchor(resource, QStringLiteral("import: %1").arg(match.captured(1).trimmed()), lineNumber);
    }
}

struct StructuredPlainTextState {
    QStringList jsonPath;
    QList<int> yamlIndents;
    QStringList yamlPath;
    QString sectionPath;
};

QString joinedConfigPath(QStringList parts)
{
    parts.removeAll(QString());
    return parts.join(QLatin1Char('.')).trimmed();
}

void appendConfigPathAnchor(Resource &resource, const QStringList &pathParts, int lineNumber)
{
    const QString path = joinedConfigPath(pathParts);
    if (!path.isEmpty()) {
        appendFileLineAnchor(resource, QStringLiteral("path: %1").arg(path), lineNumber);
    }
}

void popJsonStructuredPathClosures(StructuredPlainTextState &state, const QString &trimmed)
{
    int cursor = 0;
    while (cursor < trimmed.size()
           && (trimmed.at(cursor) == QLatin1Char('}') || trimmed.at(cursor) == QLatin1Char(']'))) {
        if (!state.jsonPath.isEmpty()) {
            state.jsonPath.removeLast();
        }
        ++cursor;
        while (cursor < trimmed.size() && trimmed.at(cursor).isSpace()) {
            ++cursor;
        }
    }
}

void appendStructuredPlainTextAnchorsFromLine(Resource &resource,
                                             const QFileInfo &fileInfo,
                                             const QString &line,
                                             int lineNumber,
                                             StructuredPlainTextState &state)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()
        || trimmed.startsWith(QLatin1Char('#'))
        || trimmed.startsWith(QLatin1Char(';'))
        || trimmed.startsWith(QLatin1String("//"))) {
        return;
    }

    const QString suffix = fileInfo.suffix().toLower();
    const bool jsonLike = suffix == QLatin1String("json") || suffix == QLatin1String("jsonl");
    const bool yamlLike = suffix == QLatin1String("yaml") || suffix == QLatin1String("yml");

    static const QRegularExpression sectionPattern(QStringLiteral("^\\[([^\\]]+)\\]$"));
    const QRegularExpressionMatch sectionMatch = sectionPattern.match(trimmed);
    if (sectionMatch.hasMatch()) {
        state.sectionPath = sectionMatch.captured(1).trimmed();
        appendFileLineAnchor(resource, QStringLiteral("section: %1").arg(state.sectionPath), lineNumber);
        appendConfigPathAnchor(resource, state.sectionPath.split(QLatin1Char('.'), Qt::SkipEmptyParts), lineNumber);
        return;
    }

    if (jsonLike) {
        popJsonStructuredPathClosures(state, trimmed);
    }

    static const QRegularExpression jsonKeyPattern(QStringLiteral("^\"([^\"]+)\"\\s*:\\s*(.*)$"));
    const QRegularExpressionMatch jsonKeyMatch = jsonKeyPattern.match(trimmed);
    if (jsonKeyMatch.hasMatch()) {
        const QString key = jsonKeyMatch.captured(1).trimmed();
        appendFileLineAnchor(resource, QStringLiteral("key: %1").arg(key), lineNumber);
        if (jsonLike) {
            appendConfigPathAnchor(resource, state.jsonPath + QStringList{key}, lineNumber);

            const QString value = jsonKeyMatch.captured(2).trimmed();
            const bool opensObject = value.startsWith(QLatin1Char('{')) && !value.contains(QLatin1Char('}'));
            const bool opensArray = value.startsWith(QLatin1Char('[')) && !value.contains(QLatin1Char(']'));
            if (opensObject || opensArray) {
                state.jsonPath.append(key);
            }
        }
        return;
    }

    static const QRegularExpression yamlKeyPattern(
        QStringLiteral("^(\\s*)(?:-\\s+)?([A-Za-z0-9_.-]+)\\s*:\\s*(.*)$"));
    const QRegularExpressionMatch yamlKeyMatch = yamlKeyPattern.match(line);
    if (yamlLike && yamlKeyMatch.hasMatch()) {
        const int indent = yamlKeyMatch.captured(1).replace(QLatin1Char('\t'), QStringLiteral("    ")).size();
        while (!state.yamlIndents.isEmpty() && state.yamlIndents.last() >= indent) {
            state.yamlIndents.removeLast();
            state.yamlPath.removeLast();
        }

        const QString key = yamlKeyMatch.captured(2).trimmed();
        const QString value = yamlKeyMatch.captured(3).trimmed();
        appendFileLineAnchor(resource, QStringLiteral("key: %1").arg(key), lineNumber);
        appendConfigPathAnchor(resource, state.yamlPath + QStringList{key}, lineNumber);

        if (value.isEmpty()
            || value == QLatin1String("|")
            || value == QLatin1String(">")
            || (value.startsWith(QLatin1Char('[')) && !value.contains(QLatin1Char(']')))
            || (value.startsWith(QLatin1Char('{')) && !value.contains(QLatin1Char('}')))) {
            state.yamlIndents.append(indent);
            state.yamlPath.append(key);
        }
        return;
    }

    static const QRegularExpression assignmentKeyPattern(
        QStringLiteral("^([A-Za-z0-9_.-]+)\\s*(?::|=)\\s*.+$"));
    const QRegularExpressionMatch assignmentKeyMatch = assignmentKeyPattern.match(trimmed);
    if (assignmentKeyMatch.hasMatch()) {
        const QString key = assignmentKeyMatch.captured(1).trimmed();
        appendFileLineAnchor(resource,
                             QStringLiteral("key: %1").arg(key),
                             lineNumber);
        appendConfigPathAnchor(resource,
                               state.sectionPath.isEmpty()
                                   ? QStringList{key}
                                   : state.sectionPath.split(QLatin1Char('.'), Qt::SkipEmptyParts) + QStringList{key},
                               lineNumber);
    }
}

struct ManifestDependencyState {
    bool inPackageJsonDependencySection = false;
    bool inCargoDependencySection = false;
    bool inGoRequireBlock = false;
};

bool isPackageJsonDependencySection(const QString &name)
{
    return name == QLatin1String("dependencies")
        || name == QLatin1String("devDependencies")
        || name == QLatin1String("peerDependencies")
        || name == QLatin1String("optionalDependencies");
}

bool isCargoDependencySection(const QString &name)
{
    return name == QLatin1String("dependencies")
        || name == QLatin1String("dev-dependencies")
        || name == QLatin1String("build-dependencies")
        || name.endsWith(QLatin1String(".dependencies"))
        || name.endsWith(QLatin1String(".dev-dependencies"))
        || name.endsWith(QLatin1String(".build-dependencies"));
}

bool isRequirementsFile(const QFileInfo &fileInfo)
{
    return fileInfo.suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0
        && fileInfo.completeBaseName().startsWith(QStringLiteral("requirements"), Qt::CaseInsensitive);
}

void appendManifestDependencyAnchorsFromLine(Resource &resource,
                                             const QFileInfo &fileInfo,
                                             const QString &line,
                                             int lineNumber,
                                             ManifestDependencyState &state)
{
    const QString fileName = fileInfo.fileName().toLower();
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    if (fileName == QLatin1String("package.json")) {
        static const QRegularExpression sectionPattern(QStringLiteral("^\"([^\"]+)\"\\s*:\\s*\\{"));
        static const QRegularExpression dependencyPattern(QStringLiteral("^\"([^\"]+)\"\\s*:"));

        const QRegularExpressionMatch sectionMatch = sectionPattern.match(trimmed);
        if (sectionMatch.hasMatch()) {
            state.inPackageJsonDependencySection = isPackageJsonDependencySection(sectionMatch.captured(1));
            return;
        }
        if (state.inPackageJsonDependencySection) {
            if (trimmed.startsWith(QLatin1Char('}'))) {
                state.inPackageJsonDependencySection = false;
                return;
            }
            const QRegularExpressionMatch dependencyMatch = dependencyPattern.match(trimmed);
            if (dependencyMatch.hasMatch()) {
                appendFileLineAnchor(resource,
                                     QStringLiteral("dependency: %1").arg(dependencyMatch.captured(1).trimmed()),
                                     lineNumber);
            }
        }
        return;
    }

    if (fileName == QLatin1String("cargo.toml")) {
        static const QRegularExpression sectionPattern(QStringLiteral("^\\[([^\\]]+)\\]$"));
        static const QRegularExpression dependencyPattern(QStringLiteral("^(?:\"([^\"]+)\"|([A-Za-z0-9_.-]+))\\s*="));

        const QRegularExpressionMatch sectionMatch = sectionPattern.match(trimmed);
        if (sectionMatch.hasMatch()) {
            state.inCargoDependencySection = isCargoDependencySection(sectionMatch.captured(1));
            return;
        }
        if (state.inCargoDependencySection) {
            const QRegularExpressionMatch dependencyMatch = dependencyPattern.match(trimmed);
            if (dependencyMatch.hasMatch()) {
                const QString dependency = dependencyMatch.captured(1).isEmpty()
                    ? dependencyMatch.captured(2)
                    : dependencyMatch.captured(1);
                appendFileLineAnchor(resource,
                                     QStringLiteral("dependency: %1").arg(dependency.trimmed()),
                                     lineNumber);
            }
        }
        return;
    }

    if (fileName == QLatin1String("go.mod")) {
        static const QRegularExpression singleRequirePattern(QStringLiteral("^require\\s+([^\\s]+)\\s+v\\S+"));
        static const QRegularExpression blockDependencyPattern(QStringLiteral("^([^\\s]+)\\s+v\\S+"));

        if (trimmed == QLatin1String("require (")) {
            state.inGoRequireBlock = true;
            return;
        }
        if (state.inGoRequireBlock) {
            if (trimmed.startsWith(QLatin1Char(')'))) {
                state.inGoRequireBlock = false;
                return;
            }
            const QRegularExpressionMatch dependencyMatch = blockDependencyPattern.match(trimmed);
            if (dependencyMatch.hasMatch()) {
                appendFileLineAnchor(resource,
                                     QStringLiteral("dependency: %1").arg(dependencyMatch.captured(1).trimmed()),
                                     lineNumber);
            }
            return;
        }

        const QRegularExpressionMatch requireMatch = singleRequirePattern.match(trimmed);
        if (requireMatch.hasMatch()) {
            appendFileLineAnchor(resource,
                                 QStringLiteral("dependency: %1").arg(requireMatch.captured(1).trimmed()),
                                 lineNumber);
        }
        return;
    }

    if (isRequirementsFile(fileInfo)) {
        static const QRegularExpression requirementPattern(QStringLiteral("^([A-Za-z0-9_.-]+)(?:\\[[^\\]]+\\])?\\s*(?:[<>=!~]=|===|@|;|$)"));
        if (trimmed.startsWith(QLatin1Char('-'))) {
            return;
        }
        const QRegularExpressionMatch requirementMatch = requirementPattern.match(trimmed);
        if (requirementMatch.hasMatch()) {
            appendFileLineAnchor(resource,
                                 QStringLiteral("dependency: %1").arg(requirementMatch.captured(1).trimmed()),
                                 lineNumber);
        }
    }
}

QString normalizedDelimitedCell(QString cell)
{
    cell = cell.trimmed();
    if (cell.size() >= 2
        && cell.startsWith(QLatin1Char('"'))
        && cell.endsWith(QLatin1Char('"'))) {
        cell = cell.mid(1, cell.size() - 2);
        cell.replace(QStringLiteral("\"\""), QStringLiteral("\""));
    }
    return cell.trimmed();
}

QStringList splitDelimitedLine(const QString &line, QChar delimiter)
{
    QStringList cells;
    QString cell;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (inQuotes && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                cell.append(ch);
                ++i;
                continue;
            }
            inQuotes = !inQuotes;
            cell.append(ch);
            continue;
        }
        if (ch == delimiter && !inQuotes) {
            cells.append(normalizedDelimitedCell(cell));
            cell.clear();
            continue;
        }
        cell.append(ch);
    }
    cells.append(normalizedDelimitedCell(cell));
    return cells;
}

QString normalizedTabularHeaderKey(QString header)
{
    header = header.trimmed().toLower();
    header.remove(QRegularExpression(QStringLiteral("[^a-z0-9]+")));
    return header;
}

bool isTabularUrlHeaderKey(const QString &key)
{
    return key == QLatin1String("url")
        || key == QLatin1String("uri")
        || key == QLatin1String("link")
        || key == QLatin1String("href")
        || key == QLatin1String("website")
        || key == QLatin1String("webpage")
        || key == QLatin1String("source")
        || key == QLatin1String("reference")
        || key.endsWith(QLatin1String("url"))
        || key.endsWith(QLatin1String("uri"))
        || key.endsWith(QLatin1String("link"));
}

bool isTabularTitleHeaderKey(const QString &key)
{
    return key == QLatin1String("title")
        || key == QLatin1String("name")
        || key == QLatin1String("label")
        || key == QLatin1String("description")
        || key == QLatin1String("desc")
        || key == QLatin1String("summary");
}

bool isTabularMetadataHeaderKey(const QString &key)
{
    return key == QLatin1String("tag")
        || key == QLatin1String("tags")
        || key == QLatin1String("category")
        || key == QLatin1String("categories")
        || key == QLatin1String("folder")
        || key == QLatin1String("folders")
        || key == QLatin1String("topic")
        || key == QLatin1String("topics")
        || key == QLatin1String("collection")
        || key == QLatin1String("project");
}

QStringList splitTabularMetadataValues(const QString &value)
{
    QStringList values;
    for (const QString &part : value.split(QRegularExpression(QStringLiteral("[;|]")), Qt::SkipEmptyParts)) {
        appendUnique(values, part);
    }
    if (values.isEmpty()) {
        appendUnique(values, value);
    }
    return values;
}

QUrl urlFromTabularCell(const QString &cell)
{
    const QString trimmed = cell.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QUrl url = QUrl::fromUserInput(trimmedPlainTextUrl(trimmed));
    if (isIndexableWebUrl(url)) {
        return url;
    }

    static const QRegularExpression urlPattern(QStringLiteral("https?://[^\\s<>\"]+"));
    const QRegularExpressionMatch match = urlPattern.match(trimmed);
    if (!match.hasMatch()) {
        return {};
    }
    url = QUrl::fromUserInput(trimmedPlainTextUrl(match.captured(0)));
    return isIndexableWebUrl(url) ? url : QUrl();
}

QList<TabularUrlLink> tabularUrlLinksFromDocument(const QString &text, QChar delimiter)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList headers;
    int headerLineIndex = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().isEmpty()) {
            continue;
        }
        headers = splitDelimitedLine(lines.at(i), delimiter);
        headerLineIndex = i;
        break;
    }

    if (headers.size() < 2 || headerLineIndex < 0) {
        return {};
    }

    QVector<int> urlColumns;
    QVector<int> metadataColumns;
    int titleColumn = -1;
    for (int i = 0; i < headers.size(); ++i) {
        const QString key = normalizedTabularHeaderKey(headers.at(i));
        if (isTabularUrlHeaderKey(key)) {
            urlColumns.append(i);
        }
        if (titleColumn < 0 && isTabularTitleHeaderKey(key)) {
            titleColumn = i;
        }
        if (isTabularMetadataHeaderKey(key)) {
            metadataColumns.append(i);
        }
    }

    if (urlColumns.isEmpty()) {
        return {};
    }

    QList<TabularUrlLink> links;
    QStringList seenUrls;
    for (int lineIndex = headerLineIndex + 1; lineIndex < lines.size(); ++lineIndex) {
        const QString line = lines.at(lineIndex);
        if (line.trimmed().isEmpty()) {
            continue;
        }

        const QStringList cells = splitDelimitedLine(line, delimiter);
        for (const int urlColumn : urlColumns) {
            if (urlColumn >= cells.size()) {
                continue;
            }

            const QUrl url = urlFromTabularCell(cells.at(urlColumn));
            if (!isIndexableWebUrl(url)) {
                continue;
            }

            const QString urlKey = url.toString(QUrl::FullyEncoded);
            if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
                continue;
            }
            seenUrls.append(urlKey);

            TabularUrlLink link;
            link.url = url;
            if (titleColumn >= 0 && titleColumn < cells.size() && titleColumn != urlColumn) {
                link.title = collapsedWhitespace(cells.at(titleColumn));
            }
            if (link.title.isEmpty()) {
                link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
            }

            for (const int metadataColumn : metadataColumns) {
                if (metadataColumn >= cells.size() || metadataColumn == urlColumn) {
                    continue;
                }
                for (const QString &value : splitTabularMetadataValues(cells.at(metadataColumn))) {
                    appendUnique(link.aliases, value);
                    appendUnique(link.tags, value);
                }
            }

            links.append(link);
        }
    }

    return links;
}

void appendTabularHeaderAnchorsFromLine(Resource &resource, const QString &line, int lineNumber, QChar delimiter)
{
    const QStringList headers = splitDelimitedLine(line, delimiter);
    if (headers.size() < 2) {
        return;
    }
    for (const QString &header : headers) {
        if (!header.isEmpty()) {
            appendFileLineAnchor(resource, QStringLiteral("column: %1").arg(header), lineNumber);
        }
    }
}

void appendCodeSymbolsFromLine(Resource &resource, const QString &line, int lineNumber)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("//")) || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    static const QRegularExpression cppTestPattern(
        QStringLiteral("^(?:TYPED_TEST|TYPED_TEST_P|TEST|TEST_F|TEST_P)\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_:]*)\\s*,\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)"));
    static const QRegularExpression jsTestPattern(
        QStringLiteral("^(describe|it|test)\\s*\\(\\s*([\"'`])([^\"'`\\r\\n]+)\\2"));
    static const QRegularExpression typePattern(
        QStringLiteral("^(?:template\\s*<[^>]+>\\s*)?(?:class|struct|enum(?:\\s+class)?)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"));
    static const QRegularExpression cppFunctionPattern(
        QStringLiteral("^(?!(?:if|for|while|switch|catch)\\b)(?:template\\s*<[^>]+>\\s*)?[A-Za-z_~][A-Za-z0-9_:<>~*&\\s]*\\s+([A-Za-z_~][A-Za-z0-9_:~]*)\\s*\\([^;{}]*\\)\\s*(?:const\\s*)?(?:noexcept\\s*)?(?:->\\s*[A-Za-z_][A-Za-z0-9_:<>*&\\s]*)?\\s*(?:\\{|$)"));
    static const QRegularExpression pythonPattern(
        QStringLiteral("^(?:async\\s+def|def|class)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"));
    static const QRegularExpression jsFunctionPattern(
        QStringLiteral("^(?:export\\s+)?(?:async\\s+)?function\\s+([A-Za-z_$][A-Za-z0-9_$]*)\\b"));
    static const QRegularExpression jsClassPattern(
        QStringLiteral("^(?:export\\s+)?class\\s+([A-Za-z_$][A-Za-z0-9_$]*)\\b"));
    static const QRegularExpression jsArrowPattern(
        QStringLiteral("^(?:export\\s+)?(?:const|let|var)\\s+([A-Za-z_$][A-Za-z0-9_$]*)\\s*=\\s*(?:async\\s*)?(?:\\([^)]*\\)|[A-Za-z_$][A-Za-z0-9_$]*)\\s*=>"));
    static const QRegularExpression rustTypePattern(
        QStringLiteral("^(?:pub(?:\\([^)]*\\))?\\s+)?(?:struct|enum|trait|type)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"));
    static const QRegularExpression rustFunctionPattern(
        QStringLiteral("^(?:pub(?:\\([^)]*\\))?\\s+)?(?:async\\s+)?fn\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"));
    static const QRegularExpression goTypePattern(
        QStringLiteral("^type\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+(?:struct|interface|func|[A-Za-z_][A-Za-z0-9_]*)\\b"));
    static const QRegularExpression goFunctionPattern(
        QStringLiteral("^func\\s+(?:\\([^)]*\\)\\s*)?([A-Za-z_][A-Za-z0-9_]*)\\s*\\("));
    static const QRegularExpression jvmDotNetTypePattern(
        QStringLiteral("^(?:(?:public|private|protected|internal|static|final|abstract|sealed|partial|readonly)\\s+)*(?:class|interface|enum|record|struct)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"));
    static const QRegularExpression hdlPattern(
        QStringLiteral("^(?:module|interface|package|class|task|function)\\s+(?:automatic\\s+)?([A-Za-z_][A-Za-z0-9_$]*)\\b"));
    static const QRegularExpression tclPattern(QStringLiteral("^proc\\s+([^\\s{]+)\\b"));

    const QRegularExpressionMatch cppTestMatch = cppTestPattern.match(trimmed);
    if (cppTestMatch.hasMatch()) {
        appendCodeSymbol(resource,
                         QStringLiteral("%1.%2").arg(cppTestMatch.captured(1), cppTestMatch.captured(2)),
                         lineNumber);
        return;
    }

    const QRegularExpressionMatch jsTestMatch = jsTestPattern.match(trimmed);
    if (jsTestMatch.hasMatch()) {
        const QString prefix = jsTestMatch.captured(1) == QLatin1String("describe")
            ? QStringLiteral("suite")
            : QStringLiteral("test");
        appendCodeSymbol(resource,
                         QStringLiteral("%1: %2").arg(prefix, jsTestMatch.captured(3).trimmed()),
                         lineNumber);
        return;
    }

    for (const QRegularExpression *pattern : {
             &typePattern,
             &cppFunctionPattern,
             &pythonPattern,
             &jsFunctionPattern,
             &jsClassPattern,
             &jsArrowPattern,
             &rustTypePattern,
             &rustFunctionPattern,
             &goTypePattern,
             &goFunctionPattern,
             &jvmDotNetTypePattern,
             &hdlPattern,
             &tclPattern
         }) {
        const QRegularExpressionMatch match = pattern->match(trimmed);
        if (match.hasMatch()) {
            appendCodeSymbol(resource, match.captured(1), lineNumber);
            return;
        }
    }
}

} // namespace

DirectoryLibrarySource::DirectoryLibrarySource(QString rootPath)
    : rootPath_(normalizedLibraryRootPath(rootPath))
{
}

void DirectoryLibrarySource::setRemoteWebFetchingEnabled(bool enabled)
{
    remoteWebFetchingEnabled_ = enabled;
}

bool DirectoryLibrarySource::remoteWebFetchingEnabled() const
{
    return remoteWebFetchingEnabled_;
}

void DirectoryLibrarySource::setWebPageFetcher(WebPageFetcher fetcher)
{
    webPageFetcher_ = std::move(fetcher);
}

QString DirectoryLibrarySource::rootPath() const
{
    return rootPath_;
}

QString DirectoryLibrarySource::displayName() const
{
    return QStringLiteral("Directory: %1").arg(rootPath_);
}

QList<Resource> DirectoryLibrarySource::scan(QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const QFileInfo rootInfo(rootPath_);
    if (!rootInfo.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Directory does not exist: %1").arg(rootPath_);
        }
        return {};
    }
    if (!rootInfo.isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Library root is not a directory: %1").arg(rootPath_);
        }
        return {};
    }

    QList<Resource> resources;
    resources.append(resourcesFromFileInfo(rootInfo));

    QDirIterator iterator(rootPath_,
                          QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        resources.append(resourcesFromFileInfo(iterator.fileInfo()));
    }

    std::sort(resources.begin(), resources.end(), [](const Resource &left, const Resource &right) {
        return left.location < right.location;
    });

    return resources;
}

QList<Resource> DirectoryLibrarySource::resourcesFromFileInfo(const QFileInfo &fileInfo) const
{
    QList<Resource> resources;
    Resource primary = resourceFromFileInfo(fileInfo);
    resources.append(primary);

    const QString suffix = fileInfo.suffix().toLower();
    if (primary.kind == ResourceKind::Markdown) {
        resources.append(markdownLinkResourcesFromFile(fileInfo));
    } else if (primary.kind == ResourceKind::Url
        && (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))) {
        resources.append(bookmarkResourcesFromHtmlFile(fileInfo));
    } else if (isBrowserBookmarkJsonCandidate(fileInfo)) {
        resources.append(browserBookmarkResourcesFromJsonFile(fileInfo));
    } else if (suffix == QLatin1String("opml")) {
        resources.append(opmlResourcesFromFile(fileInfo));
    } else if (isFeedXmlCandidate(fileInfo)) {
        resources.append(feedResourcesFromXmlFile(fileInfo));
        resources.append(sitemapResourcesFromXmlFile(fileInfo));
    }
    if (primary.kind == ResourceKind::File && isPlainTextUrlListCandidate(fileInfo)) {
        resources.append(plainTextUrlResourcesFromFile(fileInfo));
    }
    if (primary.kind == ResourceKind::File && tabularDelimiterForFile(fileInfo).has_value()) {
        resources.append(tabularUrlResourcesFromFile(fileInfo));
    }

    return resources;
}

Resource DirectoryLibrarySource::resourceFromFileInfo(const QFileInfo &fileInfo) const
{
    Resource resource;
    resource.location = normalizedPath(fileInfo);
    resource.id = QStringLiteral("file:%1").arg(resource.location);
    resource.kind = kindForFileInfo(fileInfo);
    resource.title = fileInfo.fileName();
    if (resource.title.isEmpty()) {
        resource.title = resource.location;
    }
    resource.updatedAt = fileInfo.lastModified().toUTC();
    if (resource.kind == ResourceKind::Markdown) {
        applyMarkdownMetadata(resource, fileInfo);
    } else if (resource.kind == ResourceKind::Pdf) {
        applyPdfMetadata(resource, fileInfo);
    } else if (resource.kind == ResourceKind::CodeSnippet) {
        applyCodeMetadata(resource, fileInfo);
    } else if (resource.kind == ResourceKind::Url) {
        const QString suffix = fileInfo.suffix().toLower();
        if (suffix == QLatin1String("html") || suffix == QLatin1String("htm")) {
            applyHtmlMetadata(resource, fileInfo);
        } else {
            applyUrlMetadata(resource, fileInfo);
        }
    } else if (resource.kind == ResourceKind::File && isPlainTextContentFile(fileInfo)) {
        applyPlainTextMetadata(resource, fileInfo);
    }
    return resource;
}

QList<Resource> DirectoryLibrarySource::markdownLinkResourcesFromFile(const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString markdown = QString::fromUtf8(file.readAll());
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    const QRegularExpression inlineLinkPattern(
        QStringLiteral("(?<!!)\\[([^\\]]+)\\]\\((https?://[^\\s\\)]+)\\)"));
    const QRegularExpression autolinkPattern(QStringLiteral("<(https?://[^\\s<>]+)>"));

    QList<HtmlLink> links;
    QStringList seenUrls;
    auto appendLink = [&](const QString &rawUrl, const QString &rawTitle) {
        const QUrl url = QUrl::fromUserInput(rawUrl.trimmed());
        if (!isIndexableWebUrl(url)) {
            return;
        }

        const QString key = url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(key, Qt::CaseInsensitive)) {
            return;
        }
        seenUrls.append(key);

        HtmlLink link;
        link.url = url;
        link.title = rawTitle.trimmed();
        if (link.title.isEmpty()) {
            link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
        }
        links.append(link);
    };

    bool inFrontmatter = false;
    bool inFence = false;
    int lineNumber = 0;
    for (const QString &line : lines) {
        ++lineNumber;
        const QString trimmed = line.trimmed();

        if (lineNumber == 1 && trimmed == QLatin1String("---")) {
            inFrontmatter = true;
            continue;
        }
        if (inFrontmatter) {
            if (trimmed == QLatin1String("---")) {
                inFrontmatter = false;
            }
            continue;
        }

        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence) {
            continue;
        }

        QRegularExpressionMatchIterator inlineMatches = inlineLinkPattern.globalMatch(line);
        while (inlineMatches.hasNext()) {
            const QRegularExpressionMatch match = inlineMatches.next();
            appendLink(match.captured(2), markdownPlainTextFromLine(match.captured(1)));
        }

        QRegularExpressionMatchIterator autolinkMatches = autolinkPattern.globalMatch(line);
        while (autolinkMatches.hasNext()) {
            const QRegularExpressionMatch match = autolinkMatches.next();
            appendLink(match.captured(1), QString());
        }
    }

    QList<Resource> resources;
    for (const HtmlLink &link : links) {
        Resource resource;
        resource.id = QStringLiteral("markdown-link:%1:%2")
                          .arg(normalizedPath(fileInfo),
                               link.url.toString(QUrl::FullyEncoded));
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = link.url.toString(QUrl::FullyEncoded);
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("markdown-link"));
        resources.append(resource);
    }

    return resources;
}

QList<Resource> DirectoryLibrarySource::plainTextUrlResourcesFromFile(const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString text = QString::fromUtf8(file.readAll());
    QList<Resource> resources;
    for (const TextUrlLink &link : textUrlLinksFromDocument(text)) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("text-url:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("web-link"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        resources.append(resource);
    }
    return resources;
}

QList<Resource> DirectoryLibrarySource::tabularUrlResourcesFromFile(const QFileInfo &fileInfo) const
{
    if (fileInfo.isDir() || fileInfo.size() > 512 * 1024) {
        return {};
    }

    const std::optional<QChar> delimiter = tabularDelimiterForFile(fileInfo);
    if (!delimiter.has_value()) {
        return {};
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0')) {
        return {};
    }

    const QString text = QString::fromUtf8(bytes);
    QList<Resource> resources;
    for (const TabularUrlLink &link : tabularUrlLinksFromDocument(text, delimiter.value())) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("tabular-url:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("tabular-link"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        for (const QString &alias : link.aliases) {
            appendUnique(resource.aliases, alias);
        }
        for (const QString &tag : link.tags) {
            appendUnique(resource.tags, tag);
        }
        resources.append(resource);
    }
    return resources;
}

QList<Resource> DirectoryLibrarySource::bookmarkResourcesFromHtmlFile(const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString html = QString::fromUtf8(file.readAll());
    if (!looksLikeBookmarkExport(html)) {
        return {};
    }

    QList<Resource> resources;
    for (const HtmlLink &link : htmlLinksFromDocument(html)) {
        Resource resource;
        resource.id = QStringLiteral("bookmark:%1:%2")
                          .arg(normalizedPath(fileInfo),
                               link.url.toString(QUrl::FullyEncoded));
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = link.url.toString(QUrl::FullyEncoded);
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("bookmark"));
        resources.append(resource);
    }
    return resources;
}

QList<Resource> DirectoryLibrarySource::browserBookmarkResourcesFromJsonFile(const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    QList<Resource> resources;
    QStringList seenUrls;
    for (const BrowserBookmarkLink &link : browserBookmarkLinksFromJsonDocument(document)) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);

        Resource resource;
        resource.id = QStringLiteral("browser-bookmark:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("bookmark"));
        appendUnique(resource.tags, QStringLiteral("browser-bookmark"));
        for (const QString &folder : link.folderPath) {
            appendUnique(resource.aliases, folder);
        }
        if (!link.folderPath.isEmpty()) {
            appendUnique(resource.aliases, link.folderPath.join(QStringLiteral(" / ")));
        }
        resources.append(resource);
    }
    return resources;
}

QList<Resource> DirectoryLibrarySource::opmlResourcesFromFile(const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QList<Resource> resources;
    QStringList seenUrls;
    for (const OpmlLink &link : opmlLinksFromDocument(file.readAll())) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);

        Resource resource;
        resource.id = QStringLiteral("opml-link:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("opml"));
        if (isIndexableWebUrl(link.feedUrl)) {
            appendUnique(resource.tags, QStringLiteral("feed"));
            appendWebUrlMetadata(resource, link.feedUrl);
        }
        for (const QString &folder : link.folderPath) {
            appendUnique(resource.aliases, folder);
        }
        if (!link.folderPath.isEmpty()) {
            appendUnique(resource.aliases, link.folderPath.join(QStringLiteral(" / ")));
        }
        resources.append(resource);
    }
    return resources;
}

QList<Resource> DirectoryLibrarySource::feedResourcesFromXmlFile(const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QList<Resource> resources;
    QStringList seenUrls;
    for (const FeedEntryLink &link : feedLinksFromXmlDocument(file.readAll())) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);

        Resource resource;
        resource.id = QStringLiteral("feed-entry:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("feed"));
        appendUnique(resource.tags, QStringLiteral("feed-entry"));
        appendUnique(resource.aliases, link.feedTitle);
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        for (const QString &category : link.categories) {
            appendUnique(resource.tags, category);
        }
        resources.append(resource);
    }
    return resources;
}

QList<Resource> DirectoryLibrarySource::sitemapResourcesFromXmlFile(const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QList<Resource> resources;
    QStringList seenUrls;
    for (const SitemapLink &link : sitemapLinksFromXmlDocument(file.readAll())) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);

        Resource resource;
        resource.id = QStringLiteral("sitemap:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.url.path().isEmpty() || link.url.path() == QLatin1String("/")
            ? link.url.host()
            : link.url.path().section(QLatin1Char('/'), -1);
        if (resource.title.isEmpty()) {
            resource.title = link.url.toDisplayString();
        }
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("sitemap"));
        if (link.sitemapIndexEntry) {
            appendUnique(resource.tags, QStringLiteral("sitemap-index"));
        }
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        resources.append(resource);
    }
    return resources;
}

void DirectoryLibrarySource::applyMarkdownMetadata(Resource &resource, const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QRegularExpression headingPattern(QStringLiteral("^(#{1,6})\\s+(.+?)\\s*#*\\s*$"));
    const QRegularExpression blockPattern(QStringLiteral("(?:^|\\s)\\^([A-Za-z0-9_-]+)\\s*$"));
    const QRegularExpression taskPattern(QStringLiteral("^[-*+]\\s+\\[[ xX-]\\]\\s+(.+?)\\s*$"));

    int lineNumber = 0;
    bool inFrontmatter = false;
    QString activeFrontmatterList;
    QStringList contentLines;
    while (!file.atEnd()) {
        ++lineNumber;
        const QString line = QString::fromUtf8(file.readLine()).trimmed();

        if (lineNumber == 1 && line == QLatin1String("---")) {
            inFrontmatter = true;
            continue;
        }
        if (inFrontmatter) {
            if (line == QLatin1String("---")) {
                inFrontmatter = false;
                activeFrontmatterList.clear();
                continue;
            }

            const int separator = line.indexOf(QLatin1Char(':'));
            if (separator > 0) {
                const QString key = line.left(separator).trimmed().toLower();
                const QString value = line.mid(separator + 1).trimmed();
                if (key == QLatin1String("aliases") || key == QLatin1String("alias")) {
                    activeFrontmatterList = QStringLiteral("aliases");
                    if (!value.isEmpty()) {
                        appendFrontmatterValue(resource.aliases, value);
                    }
                    continue;
                }
                if (key == QLatin1String("tags") || key == QLatin1String("tag")) {
                    activeFrontmatterList = QStringLiteral("tags");
                    if (!value.isEmpty()) {
                        appendFrontmatterValue(resource.tags, value);
                    }
                    continue;
                }
                activeFrontmatterList.clear();
                continue;
            }

            if (activeFrontmatterList == QLatin1String("aliases")) {
                appendFrontmatterListItem(resource.aliases, line);
            } else if (activeFrontmatterList == QLatin1String("tags")) {
                appendFrontmatterListItem(resource.tags, line);
            }
            continue;
        }

        const QString contentLine = markdownPlainTextFromLine(line);
        if (!contentLine.isEmpty()) {
            contentLines.append(contentLine);
        }

        appendInlineTags(resource.tags, line);
        appendWikilinksAsAliases(resource.aliases, line);
        appendLocalWikilinks(resource, fileInfo, line, lineNumber);
        appendLocalMarkdownLinks(resource, fileInfo, line, lineNumber);

        const QRegularExpressionMatch headingMatch = headingPattern.match(line);
        if (headingMatch.hasMatch()) {
            Anchor anchor;
            anchor.type = AnchorType::MarkdownHeading;
            anchor.target = headingMatch.captured(2).trimmed();
            anchor.line = lineNumber;
            resource.anchors.append(anchor);
        }

        const QRegularExpressionMatch blockMatch = blockPattern.match(line);
        if (blockMatch.hasMatch()) {
            Anchor anchor;
            anchor.type = AnchorType::MarkdownBlock;
            anchor.target = blockMatch.captured(1).trimmed();
            anchor.line = lineNumber;
            resource.anchors.append(anchor);
        }

        const QRegularExpressionMatch taskMatch = taskPattern.match(line);
        if (taskMatch.hasMatch()) {
            appendFileLineAnchor(resource, markdownPlainTextFromLine(taskMatch.captured(1)), lineNumber);
        }
    }

    resource.content = collapsedWhitespace(contentLines.join(QLatin1Char(' ')));
}

void DirectoryLibrarySource::applyPdfMetadata(Resource &resource, const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QString pdfText = QString::fromLatin1(file.readAll());
    const QString title = pdfTitleFromText(pdfText);
    appendUnique(resource.aliases, title);

    const int pageCount = pdfPageCountFromText(pdfText);
    for (int page = 1; page <= pageCount; ++page) {
        Anchor anchor;
        anchor.type = AnchorType::PdfPage;
        anchor.target = QStringLiteral("Page %1").arg(page);
        anchor.page = page;
        resource.anchors.append(anchor);
    }

    const QList<PdfObject> objects = pdfObjectsFromText(pdfText);
    resource.content = pdfContentTextFromObjects(objects);

    QHash<int, QString> objectsByNumber;
    for (const PdfObject &object : objects) {
        objectsByNumber.insert(object.number, object.body);
    }

    int page = 0;
    static const QRegularExpression pageObjectPattern(QStringLiteral("/Type\\s*/Page\\b"));
    for (const PdfObject &object : objects) {
        if (!pageObjectPattern.match(object.body).hasMatch()) {
            continue;
        }

        ++page;
        const QList<int> annotationRefs = pdfAnnotationRefsFromPage(object.body);
        for (const int ref : annotationRefs) {
            const auto annotationIt = objectsByNumber.constFind(ref);
            if (annotationIt != objectsByNumber.constEnd()) {
                appendPdfRegionAnchor(resource, annotationIt.value(), page);
            }
        }
    }
}

void DirectoryLibrarySource::applyCodeMetadata(Resource &resource, const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    int lineNumber = 0;
    bool inGoImportBlock = false;
    while (!file.atEnd()) {
        ++lineNumber;
        const QString line = QString::fromUtf8(file.readLine());
        appendActionLineAnchorsFromLine(resource, line, lineNumber);

        if (inGoImportBlock) {
            if (isGoImportBlockEnd(line)) {
                inGoImportBlock = false;
            } else {
                appendGoImportBlockDependencyAnchorFromLine(resource, line, lineNumber);
            }
            continue;
        }

        if (isGoImportBlockStart(line)) {
            inGoImportBlock = true;
            continue;
        }

        appendCodeDependencyAnchorsFromLine(resource, line, lineNumber);
        appendCodeSymbolsFromLine(resource, line, lineNumber);
    }
}

void DirectoryLibrarySource::applyUrlMetadata(Resource &resource, const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        resource.kind = ResourceKind::File;
        return;
    }

    const QString text = QString::fromUtf8(file.readAll());
    const QString rawUrl = shortcutUrlFromText(text);
    const QUrl url = QUrl::fromUserInput(rawUrl);
    if (!isIndexableWebUrl(url)) {
        resource.kind = ResourceKind::File;
        return;
    }

    const QString explicitTitle = shortcutTitleFromText(text);
    resource.title = explicitTitle.isEmpty() ? fileInfo.completeBaseName() : explicitTitle;
    if (resource.title.isEmpty()) {
        resource.title = url.toDisplayString();
    }
    resource.location = url.toString(QUrl::FullyEncoded);

    appendWebUrlMetadata(resource, url);

    if (remoteWebFetchingEnabled_) {
        QString fetchError;
        const WebPageFetcher fetcher = webPageFetcher_ ? webPageFetcher_ : defaultWebPageFetcher;
        const std::optional<WebPageFetchResult> webPage = fetcher(url, &fetchError);
        if (webPage.has_value()) {
            const QString html = QString::fromUtf8(webPage->body);
            const QUrl sourceUrl = isIndexableWebUrl(webPage->finalUrl) ? webPage->finalUrl : url;
            applyHtmlDocumentMetadata(resource, html, resource.title, sourceUrl);
        }
    }
}

void DirectoryLibrarySource::applyHtmlMetadata(Resource &resource, const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        resource.kind = ResourceKind::File;
        return;
    }

    const QString html = QString::fromUtf8(file.readAll());
    applyHtmlDocumentMetadata(resource, html, fileInfo.fileName(), std::nullopt);
}

void DirectoryLibrarySource::applyPlainTextMetadata(Resource &resource, const QFileInfo &fileInfo) const
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0')) {
        return;
    }

    const QString text = QString::fromUtf8(bytes);
    const bool structured = isStructuredPlainTextFile(fileInfo);
    const std::optional<QChar> tabularDelimiter = tabularDelimiterForFile(fileInfo);
    bool tabularHeaderAnchorsAdded = false;
    ManifestDependencyState manifestDependencyState;
    StructuredPlainTextState structuredPlainTextState;
    int lineNumber = 0;
    QStringList contentLines;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        ++lineNumber;
        contentLines.append(line);
        appendActionLineAnchorsFromLine(resource, line, lineNumber);
        appendManifestDependencyAnchorsFromLine(resource, fileInfo, line, lineNumber, manifestDependencyState);
        if (structured) {
            appendStructuredPlainTextAnchorsFromLine(resource, fileInfo, line, lineNumber, structuredPlainTextState);
        }
        if (tabularDelimiter.has_value()
            && !tabularHeaderAnchorsAdded
            && !line.trimmed().isEmpty()) {
            appendTabularHeaderAnchorsFromLine(resource, line, lineNumber, tabularDelimiter.value());
            tabularHeaderAnchorsAdded = true;
        }
    }

    resource.content = collapsedWhitespace(contentLines.join(QLatin1Char(' ')));
}

} // namespace Pinloom
