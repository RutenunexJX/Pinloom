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
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QTimeZone>
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

struct PdfTextDecodeContext {
    QHash<QByteArray, QString> toUnicode;
    int maxCodeLength = 0;
};

struct HtmlLink {
    QUrl url;
    QString title;
    int lineNumber = -1;
};

struct ReferenceStyleTextLinkDefinition {
    QUrl url;
    int lineNumber = -1;
};

struct ReferenceStyleTextLinkUse {
    QString id;
    QString title;
    int lineNumber = -1;
};

struct TextUrlLink {
    QUrl url;
    QString title;
    int lineNumber = -1;
};

struct EmailUrlLink {
    QUrl url;
    QString title;
    int lineNumber = -1;
};

struct EmailMessageMetadata {
    QString subject;
    QString from;
    QString to;
    QString cc;
    QString date;
    int subjectLineNumber = -1;
    int fromLineNumber = -1;
    int toLineNumber = -1;
    int ccLineNumber = -1;
    int dateLineNumber = -1;
    QString content;
    QList<EmailUrlLink> links;
};

struct TabularUrlLink {
    QUrl url;
    QString title;
    QStringList aliases;
    QStringList tags;
    int lineNumber = -1;
};

struct JsonUrlLink {
    QUrl url;
    QString title;
    QString path;
    int lineNumber = -1;
};

struct CalendarUrlReference {
    QUrl url;
    int lineNumber = -1;
};

struct CalendarEvent {
    QString summary;
    QString location;
    QString startsAt;
    QString endsAt;
    int eventLineNumber = -1;
    int summaryLineNumber = -1;
    int startLineNumber = -1;
    int endLineNumber = -1;
    int locationLineNumber = -1;
    QList<CalendarUrlReference> urls;
};

struct HarEntryUrlTarget {
    QUrl url;
    QString title;
    QString pageRef;
    QString pageTitle;
    QString method;
    QString mimeType;
    int status = -1;
    int lineNumber = -1;
    QDateTime startedAt;
};

struct WarcResponseUrlTarget {
    QUrl url;
    QString html;
    int recordIndex = -1;
    int lineNumber = -1;
};

struct PdfUriLink {
    QUrl url;
    QString title;
    int page = -1;
};

struct FileReferenceEntry {
    QString referencedPath;
    QString displayPath;
    QString detail;
    int lineNumber = -1;
};

struct BrowserBookmarkLink {
    QUrl url;
    QString rawUrl;
    QString title;
    QStringList folderPath;
    int lineNumber = -1;
};

struct XbelBookmarkLink {
    QUrl url;
    QString title;
    QStringList folderPath;
    int lineNumber = -1;
};

struct XbelParseContext {
    enum class Type {
        Folder,
        Bookmark
    };

    Type type = Type::Folder;
    QUrl url;
    QString title;
    int lineNumber = -1;
    bool folderPathPushed = false;
};

struct BrowserHistoryLink {
    QUrl url;
    QString title;
    QStringList aliases;
    QStringList tags;
    int visitCount = 0;
    QDateTime lastVisitedAt;
};

struct GenericSqliteColumn {
    QString tableName;
    QString name;
    QString type;
};

struct FirefoxBookmarkNode {
    int id = 0;
    int type = 0;
    int fk = 0;
    int parent = 0;
    QString title;
};

struct FirefoxBookmarkMetadata {
    QString title;
    QStringList folderPath;
};

struct OpmlLink {
    QUrl url;
    QString title;
    QUrl feedUrl;
    QStringList folderPath;
    int lineNumber = -1;
};

struct FeedEntryLink {
    QUrl url;
    QString title;
    QString feedTitle;
    QStringList categories;
    int lineNumber = -1;
};

QString xmlAttributeValue(const QXmlStreamAttributes &attributes, const QString &name);

struct SitemapLink {
    QUrl url;
    bool sitemapIndexEntry = false;
    int lineNumber = -1;
};

QString normalizedPlainTextFromLine(QString line);
void appendFileLineAnchor(Resource &resource, const QString &target, int line);

QString normalizedReferenceStyleTextLinkId(QString id)
{
    id = normalizedPlainTextFromLine(id).toLower().trimmed();
    id.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return id;
}

QString decodePdfUtf16Bytes(const QByteArray &bytes, bool bigEndian, int offset, bool trimText = true)
{
    QString decoded;
    for (int i = offset; i + 1 < bytes.size(); i += 2) {
        const ushort high = static_cast<unsigned char>(bytes.at(i));
        const ushort low = static_cast<unsigned char>(bytes.at(i + 1));
        const ushort codeUnit = bigEndian
            ? static_cast<ushort>((high << 8) | low)
            : static_cast<ushort>((low << 8) | high);
        decoded.append(QChar(codeUnit));
    }
    return trimText ? decoded.trimmed() : decoded;
}

QString decodePdfTextBytes(const QByteArray &bytes, bool trimText = true)
{
    if (bytes.isEmpty()) {
        return {};
    }
    if (bytes.size() >= 2
        && static_cast<unsigned char>(bytes.at(0)) == 0xfe
        && static_cast<unsigned char>(bytes.at(1)) == 0xff) {
        return decodePdfUtf16Bytes(bytes, true, 2, trimText);
    }
    if (bytes.size() >= 2
        && static_cast<unsigned char>(bytes.at(0)) == 0xff
        && static_cast<unsigned char>(bytes.at(1)) == 0xfe) {
        return decodePdfUtf16Bytes(bytes, false, 2, trimText);
    }

    const QString utf8 = QString::fromUtf8(bytes);
    const QString decoded = utf8.contains(QChar::ReplacementCharacter)
        ? QString::fromLatin1(bytes)
        : utf8;
    return trimText ? decoded.trimmed() : decoded;
}

QByteArray pdfBytesFromHex(QString hex)
{
    hex.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (hex.isEmpty()) {
        return {};
    }
    if (hex.size() % 2 != 0) {
        hex.append(QLatin1Char('0'));
    }
    return QByteArray::fromHex(hex.toLatin1());
}

QString decodePdfUnicodeHexString(const QString &hex)
{
    const QByteArray bytes = pdfBytesFromHex(hex);
    if (bytes.isEmpty()) {
        return {};
    }
    if (bytes.size() >= 2
        && ((static_cast<unsigned char>(bytes.at(0)) == 0xfe
             && static_cast<unsigned char>(bytes.at(1)) == 0xff)
            || (static_cast<unsigned char>(bytes.at(0)) == 0xff
                && static_cast<unsigned char>(bytes.at(1)) == 0xfe))) {
        return decodePdfTextBytes(bytes);
    }
    if (bytes.size() % 2 == 0) {
        return decodePdfUtf16Bytes(bytes, true, 0);
    }
    return decodePdfTextBytes(bytes);
}

quint32 integerFromBigEndianBytes(const QByteArray &bytes)
{
    quint32 value = 0;
    for (const char raw : bytes) {
        value = (value << 8) | static_cast<unsigned char>(raw);
    }
    return value;
}

QByteArray bigEndianBytesFromInteger(quint32 value, int length)
{
    QByteArray bytes(length, '\0');
    for (int i = length - 1; i >= 0; --i) {
        bytes[i] = static_cast<char>(value & 0xff);
        value >>= 8;
    }
    return bytes;
}

QString normalizedPath(const QFileInfo &fileInfo)
{
    return QDir::cleanPath(fileInfo.absoluteFilePath());
}

bool hasConfigStyleTextBeaconFormat(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && (fileInfo.fileName().compare(QStringLiteral("CMakeLists.txt"), Qt::CaseInsensitive) == 0
            || fileInfo.suffix().compare(QStringLiteral("cmake"), Qt::CaseInsensitive) == 0);
}

bool hasRuleEntryTextBeaconFormat(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return false;
    }

    const QString fileName = fileInfo.fileName();
    if (fileName.compare(QStringLiteral("Makefile"), Qt::CaseInsensitive) == 0
        || fileName.compare(QStringLiteral("GNUmakefile"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QLatin1String("mk") || suffix == QLatin1String("mak");
}

bool hasContainerTextBeaconFormat(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return false;
    }

    const QString fileName = fileInfo.fileName();
    if (fileName.compare(QStringLiteral("Dockerfile"), Qt::CaseInsensitive) == 0
        || fileName.startsWith(QStringLiteral("Dockerfile."), Qt::CaseInsensitive)) {
        return true;
    }

    return fileInfo.suffix().compare(QStringLiteral("dockerfile"), Qt::CaseInsensitive) == 0;
}

bool hasWorkflowConfigTextBeaconFormat(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (suffix != QLatin1String("yml") && suffix != QLatin1String("yaml")) {
        return false;
    }

    const QString path = QDir::fromNativeSeparators(fileInfo.absoluteFilePath());
    return path.contains(QStringLiteral("/.github/workflows/"), Qt::CaseInsensitive);
}

bool hasPipelineConfigTextBeaconFormat(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return false;
    }

    const QString fileName = fileInfo.fileName();
    return fileName.compare(QStringLiteral(".gitlab-ci.yml"), Qt::CaseInsensitive) == 0
        || fileName.compare(QStringLiteral(".gitlab-ci.yaml"), Qt::CaseInsensitive) == 0;
}

bool isArchiveSplitPackageContainerSuffix(const QString &suffix, const QString &completeSuffix)
{
    static const QRegularExpression zipOrRarVolumeSuffix(QStringLiteral("^[zr]\\d{2}$"));
    if (zipOrRarVolumeSuffix.match(suffix).hasMatch()) {
        return true;
    }

    static const QRegularExpression numberedPartPattern(QStringLiteral("\\.\\d{2,4}$"));
    const QRegularExpressionMatch numberedPartMatch = numberedPartPattern.match(completeSuffix);
    if (!numberedPartMatch.hasMatch()) {
        return false;
    }

    const QString baseSuffix = completeSuffix.left(numberedPartMatch.capturedStart());
    return QStringList{
        QStringLiteral("7z"),
        QStringLiteral("rar"),
        QStringLiteral("tar"),
        QStringLiteral("tar.br"),
        QStringLiteral("tar.bz2"),
        QStringLiteral("tar.gz"),
        QStringLiteral("tar.lz"),
        QStringLiteral("tar.lz4"),
        QStringLiteral("tar.lzma"),
        QStringLiteral("tar.lzo"),
        QStringLiteral("tar.xz"),
        QStringLiteral("tar.z"),
        QStringLiteral("tar.zst"),
        QStringLiteral("tar.zstd"),
        QStringLiteral("zip"),
        QStringLiteral("zipx")
    }.contains(baseSuffix);
}

bool isArchiveLikePackageContainerFile(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    const QString completeSuffix = fileInfo.completeSuffix().toLower();
    return QStringList{
        QStringLiteral("7z"),
        QStringLiteral("aab"),
        QStringLiteral("aar"),
        QStringLiteral("ace"),
        QStringLiteral("alz"),
        QStringLiteral("apk"),
        QStringLiteral("apks"),
        QStringLiteral("appimage"),
        QStringLiteral("appx"),
        QStringLiteral("appxbundle"),
        QStringLiteral("ar"),
        QStringLiteral("arc"),
        QStringLiteral("arj"),
        QStringLiteral("asar"),
        QStringLiteral("br"),
        QStringLiteral("bz2"),
        QStringLiteral("cab"),
        QStringLiteral("cb7"),
        QStringLiteral("cbr"),
        QStringLiteral("cbz"),
        QStringLiteral("cfs"),
        QStringLiteral("cpio"),
        QStringLiteral("crate"),
        QStringLiteral("crx"),
        QStringLiteral("deb"),
        QStringLiteral("dmg"),
        QStringLiteral("egg"),
        QStringLiteral("ear"),
        QStringLiteral("flatpak"),
        QStringLiteral("esd"),
        QStringLiteral("gem"),
        QStringLiteral("gz"),
        QStringLiteral("hpi"),
        QStringLiteral("ipa"),
        QStringLiteral("ipk"),
        QStringLiteral("iso"),
        QStringLiteral("jar"),
        QStringLiteral("jmod"),
        QStringLiteral("jpi"),
        QStringLiteral("lz"),
        QStringLiteral("lz4"),
        QStringLiteral("lzma"),
        QStringLiteral("lzh"),
        QStringLiteral("lha"),
        QStringLiteral("lzo"),
        QStringLiteral("maff"),
        QStringLiteral("mpkg"),
        QStringLiteral("msi"),
        QStringLiteral("msix"),
        QStringLiteral("msixbundle"),
        QStringLiteral("msm"),
        QStringLiteral("msp"),
        QStringLiteral("msu"),
        QStringLiteral("nupkg"),
        QStringLiteral("opk"),
        QStringLiteral("oxt"),
        QStringLiteral("pak"),
        QStringLiteral("pea"),
        QStringLiteral("pkg"),
        QStringLiteral("rpm"),
        QStringLiteral("rar"),
        QStringLiteral("sit"),
        QStringLiteral("sitx"),
        QStringLiteral("snap"),
        QStringLiteral("svgz"),
        QStringLiteral("swc"),
        QStringLiteral("swm"),
        QStringLiteral("tar"),
        QStringLiteral("taz"),
        QStringLiteral("tbr"),
        QStringLiteral("tbz"),
        QStringLiteral("tbz2"),
        QStringLiteral("tgz"),
        QStringLiteral("tlz"),
        QStringLiteral("tlz4"),
        QStringLiteral("tlzma"),
        QStringLiteral("txz"),
        QStringLiteral("tzst"),
        QStringLiteral("tzstd"),
        QStringLiteral("udeb"),
        QStringLiteral("vsix"),
        QStringLiteral("war"),
        QStringLiteral("whl"),
        QStringLiteral("wim"),
        QStringLiteral("xapk"),
        QStringLiteral("xar"),
        QStringLiteral("xip"),
        QStringLiteral("xpi"),
        QStringLiteral("xz"),
        QStringLiteral("z"),
        QStringLiteral("zip"),
        QStringLiteral("zipx"),
        QStringLiteral("zoo"),
        QStringLiteral("zpaq"),
        QStringLiteral("zst"),
        QStringLiteral("zstd")
    }.contains(suffix)
        || isArchiveSplitPackageContainerSuffix(suffix, completeSuffix)
        || completeSuffix.endsWith(QLatin1String(".tar.br"))
        || completeSuffix.endsWith(QLatin1String(".tar.bz2"))
        || completeSuffix.endsWith(QLatin1String(".tar.gz"))
        || completeSuffix.endsWith(QLatin1String(".tar.lz"))
        || completeSuffix.endsWith(QLatin1String(".tar.lz4"))
        || completeSuffix.endsWith(QLatin1String(".tar.lzma"))
        || completeSuffix.endsWith(QLatin1String(".tar.lzo"))
        || completeSuffix.endsWith(QLatin1String(".tar.z"))
        || completeSuffix.endsWith(QLatin1String(".tar.xz"))
        || completeSuffix.endsWith(QLatin1String(".tar.zst"))
        || completeSuffix.endsWith(QLatin1String(".tar.zstd"));
}

bool isDocumentDesignPackageContainerFile(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return QStringList{
        QStringLiteral("doc"),
        QStringLiteral("docb"),
        QStringLiteral("docm"),
        QStringLiteral("docx"),
        QStringLiteral("dot"),
        QStringLiteral("dotm"),
        QStringLiteral("dotx"),
        QStringLiteral("epub"),
        QStringLiteral("odb"),
        QStringLiteral("odf"),
        QStringLiteral("odg"),
        QStringLiteral("odm"),
        QStringLiteral("odp"),
        QStringLiteral("ods"),
        QStringLiteral("odt"),
        QStringLiteral("otg"),
        QStringLiteral("otp"),
        QStringLiteral("ots"),
        QStringLiteral("ott"),
        QStringLiteral("oxps"),
        QStringLiteral("pot"),
        QStringLiteral("potm"),
        QStringLiteral("potx"),
        QStringLiteral("ppa"),
        QStringLiteral("ppam"),
        QStringLiteral("pps"),
        QStringLiteral("ppsm"),
        QStringLiteral("ppsx"),
        QStringLiteral("ppt"),
        QStringLiteral("pptm"),
        QStringLiteral("pptx"),
        QStringLiteral("vsd"),
        QStringLiteral("vsdm"),
        QStringLiteral("vsdx"),
        QStringLiteral("vss"),
        QStringLiteral("vssm"),
        QStringLiteral("vssx"),
        QStringLiteral("vst"),
        QStringLiteral("vstm"),
        QStringLiteral("vstx"),
        QStringLiteral("xla"),
        QStringLiteral("xlam"),
        QStringLiteral("xls"),
        QStringLiteral("xlsb"),
        QStringLiteral("xlsm"),
        QStringLiteral("xlsx"),
        QStringLiteral("xlt"),
        QStringLiteral("xltm"),
        QStringLiteral("xltx"),
        QStringLiteral("xps")
    }.contains(suffix);
}

bool isPathOnlyPackageContainerFile(const QFileInfo &fileInfo)
{
    return isArchiveLikePackageContainerFile(fileInfo) || isDocumentDesignPackageContainerFile(fileInfo);
}

bool isMhtmlFile(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QLatin1String("mhtml") || suffix == QLatin1String("mht");
}

ResourceKind kindForFileInfo(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return ResourceKind::Folder;
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (suffix == QLatin1String("pdf")) {
        return ResourceKind::Pdf;
    }
    if (suffix == QLatin1String("url")
        || suffix == QLatin1String("webloc")
        || suffix == QLatin1String("website")
        || suffix == QLatin1String("desktop")
        || suffix == QLatin1String("html")
        || suffix == QLatin1String("htm")
        || suffix == QLatin1String("mhtml")
        || suffix == QLatin1String("mht")) {
        return ResourceKind::Url;
    }
    return ResourceKind::File;
}

bool isPlainTextContentFile(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 512 * 1024) {
        return false;
    }
    if (isPathOnlyPackageContainerFile(fileInfo)) {
        return false;
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray sample = file.read(8192);
    if (sample.contains('\0')) {
        return false;
    }

    int controlBytes = 0;
    for (const char byte : sample) {
        const uchar value = static_cast<uchar>(byte);
        if (value < 0x20
            && value != '\n'
            && value != '\r'
            && value != '\t'
            && value != '\f') {
            ++controlBytes;
        }
    }

    return sample.isEmpty() || controlBytes * 20 <= sample.size();
}

bool hasTextStructureBeaconFormat(const QFileInfo &fileInfo)
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

bool isTextUrlResourceCandidate(const QFileInfo &fileInfo)
{
    return isPlainTextContentFile(fileInfo);
}

bool isRobotsTxtCandidate(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && fileInfo.size() <= 512 * 1024
        && fileInfo.fileName().compare(QStringLiteral("robots.txt"), Qt::CaseInsensitive) == 0;
}

bool isEmailFileCandidate(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && fileInfo.size() <= 4 * 1024 * 1024
        && fileInfo.suffix().compare(QStringLiteral("eml"), Qt::CaseInsensitive) == 0;
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

struct TextFrontmatterState {
    bool active = false;
    QString activeList;
};

bool consumeTextFrontmatterLine(Resource &resource, const QString &line, int lineNumber, TextFrontmatterState &state)
{
    const QString trimmed = line.trimmed();

    if (lineNumber == 1 && trimmed == QLatin1String("---")) {
        state.active = true;
        return true;
    }
    if (!state.active) {
        return false;
    }

    if (trimmed == QLatin1String("---")) {
        state.active = false;
        state.activeList.clear();
        return true;
    }

    const int separator = line.indexOf(QLatin1Char(':'));
    if (separator > 0) {
        const QString key = line.left(separator).trimmed().toLower();
        const QString value = line.mid(separator + 1).trimmed();
        if (key == QLatin1String("aliases") || key == QLatin1String("alias")) {
            state.activeList = QStringLiteral("aliases");
            if (!value.isEmpty()) {
                appendFrontmatterValue(resource.aliases, value);
            }
            return true;
        }
        if (key == QLatin1String("tags") || key == QLatin1String("tag")) {
            state.activeList = QStringLiteral("tags");
            if (!value.isEmpty()) {
                appendFrontmatterValue(resource.tags, value);
            }
            return true;
        }
        state.activeList.clear();
        return true;
    }

    if (state.activeList == QLatin1String("aliases")) {
        appendFrontmatterListItem(resource.aliases, line);
    } else if (state.activeList == QLatin1String("tags")) {
        appendFrontmatterListItem(resource.tags, line);
    }
    return true;
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

void appendBracketedTextLinksAsAliases(QStringList &aliases, const QString &line)
{
    static const QRegularExpression bracketedTextLinkPattern(QStringLiteral("\\[\\[([^\\]]+)\\]\\]"));
    QRegularExpressionMatchIterator matches = bracketedTextLinkPattern.globalMatch(line);
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

bool isLocalTextLinkTarget(const QString &target)
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

QString textLinkPathWithoutFragment(QString target)
{
    target = target.trimmed();
    const int fragmentIndex = target.indexOf(QLatin1Char('#'));
    if (fragmentIndex >= 0) {
        target = target.left(fragmentIndex);
    }
    return QUrl::fromPercentEncoding(target.trimmed().toUtf8()).trimmed();
}

QString textLinkFragment(QString target)
{
    target = target.trimmed();
    const int fragmentIndex = target.indexOf(QLatin1Char('#'));
    if (fragmentIndex < 0 || fragmentIndex + 1 >= target.size()) {
        return {};
    }
    return QUrl::fromPercentEncoding(target.mid(fragmentIndex + 1).trimmed().toUtf8()).trimmed();
}

QString notePathForBracketedTextLink(QString targetPath)
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

void appendLocalTextLinkAnchors(Resource &resource, const QFileInfo &fileInfo, const QString &line, int lineNumber)
{
    static const QRegularExpression linkPattern(QStringLiteral("(?<!!)\\[([^\\]]+)\\]\\(([^\\)]+)\\)"));
    QRegularExpressionMatchIterator matches = linkPattern.globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString rawTarget = match.captured(2).trimmed();
        if (!isLocalTextLinkTarget(rawTarget)) {
            continue;
        }

        const QString targetPath = textLinkPathWithoutFragment(rawTarget);
        const QString label = normalizedPlainTextFromLine(match.captured(1));
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

void appendLocalBracketedTextLinkAnchors(Resource &resource, const QFileInfo &fileInfo, const QString &line, int lineNumber)
{
    static const QRegularExpression bracketedTextLinkPattern(QStringLiteral("\\[\\[([^\\]]+)\\]\\]"));
    QRegularExpressionMatchIterator matches = bracketedTextLinkPattern.globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString rawLink = match.captured(1).trimmed();
        const QString rawTarget = rawLink.section(QLatin1Char('|'), 0, 0).trimmed();
        const QString displayText = rawLink.section(QLatin1Char('|'), 1).trimmed();
        const QString rawTargetPath = textLinkPathWithoutFragment(rawTarget);
        if (rawTargetPath.isEmpty()) {
            continue;
        }

        const QString targetPath = notePathForBracketedTextLink(rawTargetPath);
        const QString fragment = textLinkFragment(rawTarget);
        const QString normalizedFragment = fragment.startsWith(QLatin1Char('^')) ? fragment.mid(1) : fragment;
        const QString label = displayText.isEmpty()
            ? QFileInfo(rawTargetPath).fileName()
            : normalizedPlainTextFromLine(displayText);

        appendUnique(resource.aliases, label);
        appendUnique(resource.aliases, QFileInfo(targetPath).fileName());
        appendUnique(resource.aliases, rawTargetPath);
        appendUnique(resource.aliases, targetPath);
        appendUnique(resource.aliases, normalizedFragment);

        QString anchorTarget = label.isEmpty()
            ? QStringLiteral("bracket link: %1").arg(targetPath)
            : QStringLiteral("bracket link: %1 -> %2").arg(label, targetPath);
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

void appendTextConventionLineMetadata(Resource &resource, const QFileInfo &fileInfo, const QString &line, int lineNumber)
{
    appendInlineTags(resource.tags, line);
    appendBracketedTextLinksAsAliases(resource.aliases, line);
    appendLocalBracketedTextLinkAnchors(resource, fileInfo, line, lineNumber);
    appendLocalTextLinkAnchors(resource, fileInfo, line, lineNumber);

    static const QRegularExpression headingPattern(QStringLiteral("^(#{1,6})\\s+(.+?)\\s*#*\\s*$"));
    const QRegularExpressionMatch headingMatch = headingPattern.match(line);
    if (headingMatch.hasMatch()) {
        Anchor anchor;
        anchor.type = AnchorType::TextHeading;
        anchor.target = headingMatch.captured(2).trimmed();
        anchor.line = lineNumber;
        resource.anchors.append(anchor);
    }

    static const QRegularExpression blockPattern(QStringLiteral("(?:^|\\s)\\^([A-Za-z0-9_-]+)\\s*$"));
    const QRegularExpressionMatch blockMatch = blockPattern.match(line);
    if (blockMatch.hasMatch()) {
        Anchor anchor;
        anchor.type = AnchorType::TextBlock;
        anchor.target = blockMatch.captured(1).trimmed();
        anchor.line = lineNumber;
        resource.anchors.append(anchor);
    }

    static const QRegularExpression checkboxLinePattern(QStringLiteral("^[-*+]\\s+\\[[ xX-]\\]\\s+(.+?)\\s*$"));
    const QRegularExpressionMatch checkboxLineMatch = checkboxLinePattern.match(line);
    if (checkboxLineMatch.hasMatch()) {
        appendFileLineAnchor(resource, normalizedPlainTextFromLine(checkboxLineMatch.captured(1)), lineNumber);
    }
}

QString normalizedPlainTextFromLine(QString line)
{
    line = line.trimmed();
    if (line.isEmpty()) {
        return {};
    }

    static const QRegularExpression blockIdPattern(QStringLiteral("(?:^|\\s)\\^[A-Za-z0-9_-]+\\s*$"));
    line.remove(blockIdPattern);

    static const QRegularExpression headingMarkerPattern(QStringLiteral("^#{1,6}\\s+"));
    line.remove(headingMarkerPattern);

    static const QRegularExpression checkboxMarkerPattern(QStringLiteral("^[-*+]\\s+\\[[ xX-]\\]\\s+"));
    line.remove(checkboxMarkerPattern);

    static const QRegularExpression inlineTextLinkPattern(QStringLiteral("!?\\[([^\\]]+)\\]\\([^\\)]+\\)"));
    line.replace(inlineTextLinkPattern, QStringLiteral("\\1"));

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

QString decodePdfLiteralString(const QString &value, bool trimText = true)
{
    QByteArray bytes;
    bytes.reserve(value.size());

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
                    bytes.append(static_cast<char>(code & 0xff));
                }
                continue;
            }

            switch (escaped.unicode()) {
            case 'n':
                bytes.append('\n');
                break;
            case 'r':
                bytes.append('\r');
                break;
            case 't':
                bytes.append('\t');
                break;
            case 'b':
                bytes.append('\b');
                break;
            case 'f':
                bytes.append('\f');
                break;
            case '\r':
                if (cursor < value.size() && value.at(cursor) == QLatin1Char('\n')) {
                    ++cursor;
                }
                break;
            case '\n':
                break;
            default:
                bytes.append(static_cast<char>(escaped.unicode() & 0xff));
                break;
            }
            continue;
        }

        bytes.append(static_cast<char>(ch.unicode() & 0xff));
    }

    return decodePdfTextBytes(bytes, trimText);
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
            return decodePdfTextBytes(bytes);
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
            return decodePdfTextBytes(bytes);
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

int pdfDestinationPageObjectNumber(const QString &objectBody)
{
    static const QRegularExpression destinationPattern(
        QStringLiteral("/(?:Dest|D)\\b\\s*\\[\\s*(\\d+)\\s+\\d+\\s+R\\b"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = destinationPattern.match(objectBody);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }

    static const QRegularExpression standaloneDestinationPattern(
        QStringLiteral("^\\s*\\[\\s*(\\d+)\\s+\\d+\\s+R\\b"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch standaloneMatch = standaloneDestinationPattern.match(objectBody);
    return standaloneMatch.hasMatch() ? standaloneMatch.captured(1).toInt() : -1;
}

QString pdfNamedDestinationAfterKey(const QString &objectBody, const QString &key)
{
    QString name = pdfNameValueAfterKey(objectBody, key);
    if (!name.isEmpty()) {
        return name;
    }
    return pdfLiteralValueAfterKey(objectBody, key);
}

QHash<QString, int> pdfNamedDestinationPageObjectNumbers(const QList<PdfObject> &objects)
{
    QHash<QString, int> destinations;
    QHash<int, QString> objectsByNumber;
    for (const PdfObject &object : objects) {
        objectsByNumber.insert(object.number, object.body);
    }

    static const QRegularExpression namedArrayPattern(
        QStringLiteral("(?:/([A-Za-z0-9_.:-]+)|\\(([^()]*)\\))\\s*\\[\\s*(\\d+)\\s+\\d+\\s+R\\b"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression namedReferencePattern(
        QStringLiteral("(?:/([A-Za-z0-9_.:-]+)|\\(([^()]*)\\))\\s+(\\d+)\\s+\\d+\\s+R\\b"),
        QRegularExpression::DotMatchesEverythingOption);

    for (const PdfObject &object : objects) {
        if (!object.body.contains(QStringLiteral("/Dests"))
            && !object.body.contains(QStringLiteral("/Names"))) {
            continue;
        }

        QRegularExpressionMatchIterator matches = namedArrayPattern.globalMatch(object.body);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const QString name = match.captured(1).isEmpty()
                ? decodePdfLiteralString(match.captured(2))
                : match.captured(1).trimmed();
            const int pageObjectNumber = match.captured(3).toInt();
            if (!name.isEmpty() && pageObjectNumber > 0) {
                destinations.insert(name, pageObjectNumber);
            }
        }

        QRegularExpressionMatchIterator referenceMatches = namedReferencePattern.globalMatch(object.body);
        while (referenceMatches.hasNext()) {
            const QRegularExpressionMatch match = referenceMatches.next();
            const QString name = match.captured(1).isEmpty()
                ? decodePdfLiteralString(match.captured(2))
                : match.captured(1).trimmed();
            const int destinationObjectNumber = match.captured(3).toInt();
            const int pageObjectNumber = pdfDestinationPageObjectNumber(objectsByNumber.value(destinationObjectNumber));
            if (!name.isEmpty() && pageObjectNumber > 0) {
                destinations.insert(name, pageObjectNumber);
            }
        }
    }

    return destinations;
}

QHash<int, int> pdfPageNumbersByObjectNumber(const QList<PdfObject> &objects)
{
    QHash<int, int> pageNumbers;
    int page = 0;
    static const QRegularExpression pageObjectPattern(QStringLiteral("/Type\\s*/Page\\b"));
    for (const PdfObject &object : objects) {
        if (!pageObjectPattern.match(object.body).hasMatch()) {
            continue;
        }
        ++page;
        pageNumbers.insert(object.number, page);
    }
    return pageNumbers;
}

void appendPdfPageAnchor(Resource &resource, const QString &target, int page)
{
    const QString normalizedTarget = target.trimmed();
    if (normalizedTarget.isEmpty() || page <= 0) {
        return;
    }

    const auto duplicate = std::find_if(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
        return anchor.type == AnchorType::PdfPage
            && anchor.page == page
            && anchor.target.compare(normalizedTarget, Qt::CaseInsensitive) == 0;
    });
    if (duplicate != resource.anchors.cend()) {
        return;
    }

    Anchor anchor;
    anchor.type = AnchorType::PdfPage;
    anchor.target = normalizedTarget;
    anchor.page = page;
    resource.anchors.append(anchor);
}

void appendPdfOutlineAnchors(Resource &resource,
                             const QList<PdfObject> &objects,
                             const QHash<int, int> &pageNumbers,
                             const QHash<QString, int> &namedDestinations)
{
    for (const PdfObject &object : objects) {
        if (!object.body.contains(QStringLiteral("/Title"))) {
            continue;
        }

        const QString title = pdfLiteralValueAfterKey(object.body, QStringLiteral("Title"));
        int pageObjectNumber = pdfDestinationPageObjectNumber(object.body);
        if (pageObjectNumber <= 0) {
            const QString namedDestination = pdfNamedDestinationAfterKey(object.body, QStringLiteral("Dest"));
            pageObjectNumber = namedDestinations.value(namedDestination, -1);
        }
        if (pageObjectNumber <= 0) {
            const QString namedDestination = pdfNamedDestinationAfterKey(object.body, QStringLiteral("D"));
            pageObjectNumber = namedDestinations.value(namedDestination, -1);
        }
        if (title.isEmpty() || !pageNumbers.contains(pageObjectNumber)) {
            continue;
        }

        appendPdfPageAnchor(resource, title, pageNumbers.value(pageObjectNumber));
    }
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

int shortcutUrlLineNumberFromText(const QString &text)
{
    static const QRegularExpression internetShortcutPattern(
        QStringLiteral("(?im)^\\s*URL\\s*=\\s*(\\S.*)$"));
    const QRegularExpressionMatch internetShortcutMatch = internetShortcutPattern.match(text);
    if (internetShortcutMatch.hasMatch()) {
        return text.left(internetShortcutMatch.capturedStart(1)).count(QLatin1Char('\n')) + 1;
    }

    static const QRegularExpression xmlUrlPattern(
        QStringLiteral("<key>\\s*URL\\s*</key>\\s*<string>\\s*([^<]+?)\\s*</string>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch xmlUrlMatch = xmlUrlPattern.match(text);
    if (xmlUrlMatch.hasMatch()) {
        return text.left(xmlUrlMatch.capturedStart(1)).count(QLatin1Char('\n')) + 1;
    }

    static const QRegularExpression firstWebUrlPattern(QStringLiteral("(https?://[^\\s<>\"]+)"));
    const QRegularExpressionMatch firstWebUrlMatch = firstWebUrlPattern.match(text);
    if (firstWebUrlMatch.hasMatch()) {
        return text.left(firstWebUrlMatch.capturedStart(1)).count(QLatin1Char('\n')) + 1;
    }

    return -1;
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

int shortcutTitleLineNumberFromText(const QString &text)
{
    static const QRegularExpression titlePattern(
        QStringLiteral("(?im)^\\s*(?:Name|Title)\\s*=\\s*(.+)$"));
    const QRegularExpressionMatch titleMatch = titlePattern.match(text);
    if (titleMatch.hasMatch()) {
        return text.left(titleMatch.capturedStart(1)).count(QLatin1Char('\n')) + 1;
    }
    return -1;
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

QString pdfUriValueFromAnnotation(const QString &annotationBody)
{
    const QString rawUri = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("URI"));
    if (!rawUri.isEmpty()) {
        return rawUri;
    }

    static const QRegularExpression uriLiteralPattern(
        QStringLiteral("/URI\\s*\\(([^()]*)\\)"),
        QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator matches = uriLiteralPattern.globalMatch(annotationBody);
    while (matches.hasNext()) {
        const QString uri = decodePdfLiteralString(matches.next().captured(1));
        if (!uri.isEmpty()) {
            return uri;
        }
    }

    return {};
}

std::optional<PdfUriLink> pdfUriLinkFromAnnotation(const QString &annotationBody, int page)
{
    const QString rawUri = pdfUriValueFromAnnotation(annotationBody);
    const QUrl url = QUrl::fromUserInput(rawUri);
    if (!isIndexableWebUrl(url)) {
        return std::nullopt;
    }

    QString title = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("Contents"));
    if (title.isEmpty()) {
        title = pdfLiteralValueAfterKey(annotationBody, QStringLiteral("T"));
    }
    if (title.isEmpty()) {
        title = url.host().isEmpty() ? url.toDisplayString() : url.host();
    }

    return PdfUriLink{url, title, page};
}

QList<Resource> pdfUriResourcesFromLinks(const QFileInfo &fileInfo, const QList<PdfUriLink> &links)
{
    QList<Resource> resources;
    for (const PdfUriLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("pdf-url:%1:%2").arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title.trimmed().isEmpty() ? link.url.toDisplayString() : link.title.trimmed();
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("pdf-link"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        if (link.page > 0) {
            appendUnique(resource.aliases, QStringLiteral("page %1").arg(link.page));
        }
        resources.append(resource);
    }
    return resources;
}

QString pdfUriAnchorTarget(const PdfUriLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

void appendPdfUriSourceMetadata(Resource &sourceResource,
                                const QList<PdfUriLink> &links,
                                const QList<Resource> &urlResources)
{
    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const PdfUriLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = pdfUriAnchorTarget(link);

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.page > 0
            ? QStringLiteral("pdf page %1 link: %2").arg(link.page).arg(anchorTarget)
            : QStringLiteral("pdf link: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QList<PdfUriLink> pdfUriLinksFromFile(const QFileInfo &fileInfo)
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QString pdfText = QString::fromLatin1(file.readAll());
    const QList<PdfObject> objects = pdfObjectsFromText(pdfText);
    const QHash<int, int> pageNumbers = pdfPageNumbersByObjectNumber(objects);

    QHash<int, QString> objectsByNumber;
    for (const PdfObject &object : objects) {
        objectsByNumber.insert(object.number, object.body);
    }

    QList<PdfUriLink> links;
    QStringList seenUrls;
    for (const PdfObject &object : objects) {
        const int page = pageNumbers.value(object.number, -1);
        if (page <= 0) {
            continue;
        }

        const QList<int> annotationRefs = pdfAnnotationRefsFromPage(object.body);
        for (const int ref : annotationRefs) {
            const auto annotationIt = objectsByNumber.constFind(ref);
            if (annotationIt == objectsByNumber.constEnd()) {
                continue;
            }

            const std::optional<PdfUriLink> link = pdfUriLinkFromAnnotation(annotationIt.value(), page);
            if (!link.has_value()) {
                continue;
            }

            const QString urlKey = link->url.toString(QUrl::FullyEncoded);
            if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
                continue;
            }

            seenUrls.append(urlKey);
            links.append(link.value());
        }
    }

    return links;
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

bool readPdfLiteralStringAt(const QString &text, int start, QString *value, int *end, bool trimText = true)
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
        *value = decodePdfLiteralString(literal, trimText);
    }
    if (end) {
        *end = cursor;
    }
    return true;
}

QString decodePdfHexString(QString hex, const PdfTextDecodeContext *context = nullptr, bool trimText = true)
{
    const QByteArray bytes = pdfBytesFromHex(hex);
    if (bytes.isEmpty()) {
        return {};
    }

    if (context && !context->toUnicode.isEmpty() && context->maxCodeLength > 0) {
        QString decoded;
        bool matchedAny = false;
        int cursor = 0;
        while (cursor < bytes.size()) {
            bool matched = false;
            const int maxLength = std::min(context->maxCodeLength,
                                           static_cast<int>(bytes.size() - cursor));
            for (int length = maxLength; length > 0; --length) {
                const QByteArray key = bytes.mid(cursor, length);
                const auto it = context->toUnicode.constFind(key);
                if (it != context->toUnicode.cend()) {
                    decoded.append(it.value());
                    cursor += length;
                    matched = true;
                    matchedAny = true;
                    break;
                }
            }
            if (!matched) {
                decoded.append(decodePdfTextBytes(bytes.mid(cursor, 1)));
                ++cursor;
            }
        }
        if (matchedAny) {
            return trimText ? decoded.trimmed() : decoded;
        }
    }

    return decodePdfTextBytes(bytes, trimText);
}

bool readPdfHexStringAt(const QString &text,
                        int start,
                        const PdfTextDecodeContext *context,
                        QString *value,
                        int *end,
                        bool trimText = true)
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
        *value = decodePdfHexString(text.mid(start + 1, close - start - 1), context, trimText);
    }
    if (end) {
        *end = close + 1;
    }
    return true;
}

bool readPdfTextArrayAt(const QString &text,
                        int start,
                        const PdfTextDecodeContext *context,
                        QString *value,
                        int *end)
{
    if (start < 0 || start >= text.size() || text.at(start) != QLatin1Char('[')) {
        return false;
    }

    QString arrayText;
    int cursor = start + 1;
    bool pendingSpacing = false;
    while (cursor < text.size()) {
        if (text.at(cursor) == QLatin1Char(']')) {
            if (value) {
                *value = arrayText;
            }
            if (end) {
                *end = cursor + 1;
            }
            return true;
        }

        QString chunk;
        int next = cursor + 1;
        if (readPdfLiteralStringAt(text, cursor, &chunk, &next, false)
            || readPdfHexStringAt(text, cursor, context, &chunk, &next, false)) {
            if (pendingSpacing
                && !arrayText.isEmpty()
                && !arrayText.back().isSpace()
                && !chunk.isEmpty()
                && !chunk.front().isSpace()) {
                arrayText.append(QLatin1Char(' '));
            }
            arrayText.append(chunk);
            pendingSpacing = false;
            cursor = next;
            continue;
        }

        if (!text.at(cursor).isSpace()) {
            int tokenEnd = cursor;
            while (tokenEnd < text.size()
                   && !text.at(tokenEnd).isSpace()
                   && text.at(tokenEnd) != QLatin1Char(']')
                   && text.at(tokenEnd) != QLatin1Char('[')
                   && text.at(tokenEnd) != QLatin1Char('(')
                   && text.at(tokenEnd) != QLatin1Char('<')) {
                ++tokenEnd;
            }
            bool ok = false;
            const double spacing = text.mid(cursor, tokenEnd - cursor).toDouble(&ok);
            if (ok && spacing > 0.0) {
                pendingSpacing = true;
            }
            cursor = std::max(tokenEnd, cursor + 1);
            continue;
        }

        ++cursor;
    }

    return false;
}

bool hasPdfTextOperatorAfter(const QString &text, int cursor, const QString &operatorName)
{
    while (cursor < text.size() && text.at(cursor).isSpace()) {
        ++cursor;
    }
    if (text.mid(cursor, operatorName.size()).compare(operatorName, Qt::CaseSensitive) != 0) {
        return false;
    }
    const int end = cursor + operatorName.size();
    return end >= text.size() || text.at(end).isSpace();
}

QString pdfTextFromTextSection(const QString &section, const PdfTextDecodeContext *context)
{
    QStringList chunks;
    int cursor = 0;
    while (cursor < section.size()) {
        QString value;
        int next = cursor + 1;
        if (readPdfTextArrayAt(section, cursor, context, &value, &next)
            && hasPdfTextOperatorAfter(section, next, QStringLiteral("TJ"))) {
            if (!value.trimmed().isEmpty()) {
                chunks.append(value);
            }
            cursor = next;
            continue;
        }
        if (readPdfLiteralStringAt(section, cursor, &value, &next)
            || readPdfHexStringAt(section, cursor, context, &value, &next)) {
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

QString pdfTextFromContentStream(QString stream, const PdfTextDecodeContext *context)
{
    QStringList chunks;
    static const QRegularExpression textSectionPattern(
        QStringLiteral("\\bBT\\b(.*?)\\bET\\b"),
        QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator matches = textSectionPattern.globalMatch(stream);
    while (matches.hasNext()) {
        const QString text = pdfTextFromTextSection(matches.next().captured(1), context);
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

void appendPdfToUnicodeMapping(PdfTextDecodeContext &context, const QByteArray &sourceCode, const QString &unicode)
{
    if (sourceCode.isEmpty() || unicode.isEmpty()) {
        return;
    }
    context.toUnicode.insert(sourceCode, unicode);
    context.maxCodeLength = std::max(context.maxCodeLength, static_cast<int>(sourceCode.size()));
}

void appendPdfBfcharMappings(PdfTextDecodeContext &context, const QString &cmap)
{
    static const QRegularExpression blockPattern(
        QStringLiteral("\\bbeginbfchar\\b(.*?)\\bendbfchar\\b"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression pairPattern(
        QStringLiteral("<([0-9A-Fa-f\\s]+)>\\s*<([0-9A-Fa-f\\s]+)>"));

    QRegularExpressionMatchIterator blocks = blockPattern.globalMatch(cmap);
    while (blocks.hasNext()) {
        QRegularExpressionMatchIterator pairs = pairPattern.globalMatch(blocks.next().captured(1));
        while (pairs.hasNext()) {
            const QRegularExpressionMatch pair = pairs.next();
            appendPdfToUnicodeMapping(context,
                                      pdfBytesFromHex(pair.captured(1)),
                                      decodePdfUnicodeHexString(pair.captured(2)));
        }
    }
}

void appendPdfBfrangeMappings(PdfTextDecodeContext &context, const QString &cmap)
{
    static const QRegularExpression blockPattern(
        QStringLiteral("\\bbeginbfrange\\b(.*?)\\bendbfrange\\b"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression arrayRangePattern(
        QStringLiteral("<([0-9A-Fa-f\\s]+)>\\s*<([0-9A-Fa-f\\s]+)>\\s*\\[(.*?)\\]"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression simpleRangePattern(
        QStringLiteral("<([0-9A-Fa-f\\s]+)>\\s*<([0-9A-Fa-f\\s]+)>\\s*<([0-9A-Fa-f\\s]+)>"));
    static const QRegularExpression destinationPattern(QStringLiteral("<([0-9A-Fa-f\\s]+)>"));

    QRegularExpressionMatchIterator blocks = blockPattern.globalMatch(cmap);
    while (blocks.hasNext()) {
        const QString block = blocks.next().captured(1);

        QRegularExpressionMatchIterator arrayRanges = arrayRangePattern.globalMatch(block);
        while (arrayRanges.hasNext()) {
            const QRegularExpressionMatch range = arrayRanges.next();
            const QByteArray startBytes = pdfBytesFromHex(range.captured(1));
            const QByteArray endBytes = pdfBytesFromHex(range.captured(2));
            if (startBytes.isEmpty() || endBytes.isEmpty() || startBytes.size() != endBytes.size()) {
                continue;
            }

            quint32 sourceCode = integerFromBigEndianBytes(startBytes);
            const quint32 endCode = integerFromBigEndianBytes(endBytes);
            QRegularExpressionMatchIterator destinations = destinationPattern.globalMatch(range.captured(3));
            while (sourceCode <= endCode && destinations.hasNext()) {
                appendPdfToUnicodeMapping(context,
                                          bigEndianBytesFromInteger(sourceCode, startBytes.size()),
                                          decodePdfUnicodeHexString(destinations.next().captured(1)));
                ++sourceCode;
            }
        }

        QRegularExpressionMatchIterator simpleRanges = simpleRangePattern.globalMatch(block);
        while (simpleRanges.hasNext()) {
            const QRegularExpressionMatch range = simpleRanges.next();
            const QByteArray startBytes = pdfBytesFromHex(range.captured(1));
            const QByteArray endBytes = pdfBytesFromHex(range.captured(2));
            const QByteArray destinationBytes = pdfBytesFromHex(range.captured(3));
            if (startBytes.isEmpty()
                || endBytes.isEmpty()
                || destinationBytes.isEmpty()
                || startBytes.size() != endBytes.size()) {
                continue;
            }

            const quint32 startCode = integerFromBigEndianBytes(startBytes);
            const quint32 endCode = integerFromBigEndianBytes(endBytes);
            const quint32 destinationStart = integerFromBigEndianBytes(destinationBytes);
            for (quint32 sourceCode = startCode; sourceCode <= endCode; ++sourceCode) {
                const quint32 offset = sourceCode - startCode;
                const QByteArray sourceBytes = bigEndianBytesFromInteger(sourceCode, startBytes.size());
                const QByteArray unicodeBytes =
                    bigEndianBytesFromInteger(destinationStart + offset, destinationBytes.size());
                appendPdfToUnicodeMapping(context, sourceBytes, decodePdfUtf16Bytes(unicodeBytes, true, 0));
            }
        }
    }
}

PdfTextDecodeContext pdfTextDecodeContextFromObjects(const QList<PdfObject> &objects)
{
    PdfTextDecodeContext context;
    for (const PdfObject &object : objects) {
        const QString stream = pdfDecodedStreamBody(object.body);
        if (!stream.contains(QStringLiteral("beginbfchar"))
            && !stream.contains(QStringLiteral("beginbfrange"))) {
            continue;
        }
        appendPdfBfcharMappings(context, stream);
        appendPdfBfrangeMappings(context, stream);
    }
    return context;
}

QString pdfContentTextFromObjects(const QList<PdfObject> &objects)
{
    const PdfTextDecodeContext decodeContext = pdfTextDecodeContextFromObjects(objects);
    QStringList chunks;
    for (const PdfObject &object : objects) {
        const QString stream = pdfDecodedStreamBody(object.body);
        if (stream.isEmpty()) {
            continue;
        }
        const QString text = pdfTextFromContentStream(stream, &decodeContext);
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

int mimeHeaderSeparatorIndex(const QString &part, int *separatorLength)
{
    const int crlfIndex = part.indexOf(QStringLiteral("\r\n\r\n"));
    const int lfIndex = part.indexOf(QStringLiteral("\n\n"));
    if (crlfIndex < 0 || (lfIndex >= 0 && lfIndex < crlfIndex)) {
        if (lfIndex >= 0 && separatorLength) {
            *separatorLength = 2;
        }
        return lfIndex;
    }
    if (crlfIndex >= 0 && separatorLength) {
        *separatorLength = 4;
    }
    return crlfIndex;
}

QString unfoldedMimeHeaders(QString headers)
{
    headers.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    static const QRegularExpression foldedLinePattern(QStringLiteral("\n[ \t]+"));
    headers.replace(foldedLinePattern, QStringLiteral(" "));
    return headers;
}

QString mimeHeaderValue(const QString &headers, const QString &name)
{
    const QRegularExpression pattern(
        QStringLiteral("(?:^|\\n)%1\\s*:\\s*([^\\n]*)").arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(unfoldedMimeHeaders(headers));
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString mimeParameterValue(const QString &headerValue, const QString &name)
{
    const QRegularExpression pattern(
        QStringLiteral("\\b%1\\s*=\\s*(?:\"([^\"]*)\"|([^;\\s]+))").arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(headerValue);
    if (!match.hasMatch()) {
        return {};
    }
    return match.captured(1).isEmpty() ? match.captured(2).trimmed() : match.captured(1).trimmed();
}

int hexByteValue(char digit)
{
    const unsigned char value = static_cast<unsigned char>(digit);
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

QByteArray decodedQuotedPrintable(QByteArray input)
{
    input.replace("\r\n", "\n");

    QByteArray output;
    output.reserve(input.size());
    for (int i = 0; i < input.size(); ++i) {
        const char value = input.at(i);
        if (value != '=') {
            output.append(value);
            continue;
        }

        if (i + 1 < input.size() && input.at(i + 1) == '\n') {
            ++i;
            continue;
        }
        if (i + 2 < input.size()) {
            const int high = hexByteValue(input.at(i + 1));
            const int low = hexByteValue(input.at(i + 2));
            if (high >= 0 && low >= 0) {
                output.append(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        output.append(value);
    }
    return output;
}

QString decodedMimeBody(const QString &headers, const QString &body)
{
    QByteArray bodyBytes = body.toLatin1();
    const QString encoding = mimeHeaderValue(headers, QStringLiteral("Content-Transfer-Encoding")).toLower();
    if (encoding == QLatin1String("base64")) {
        QByteArray compact;
        compact.reserve(bodyBytes.size());
        for (const char value : bodyBytes) {
            if (!std::isspace(static_cast<unsigned char>(value))) {
                compact.append(value);
            }
        }
        bodyBytes = QByteArray::fromBase64(compact);
    } else if (encoding == QLatin1String("quoted-printable")) {
        bodyBytes = decodedQuotedPrintable(bodyBytes);
    }

    const QString contentType = mimeHeaderValue(headers, QStringLiteral("Content-Type"));
    const QString charset = mimeParameterValue(contentType, QStringLiteral("charset")).toLower();
    if (charset == QLatin1String("iso-8859-1") || charset == QLatin1String("latin1")) {
        return QString::fromLatin1(bodyBytes);
    }
    return QString::fromUtf8(bodyBytes);
}

QString htmlFromMhtmlCapture(const QByteArray &bytes)
{
    const QString document = QString::fromLatin1(bytes);
    int rootSeparatorLength = 0;
    const int rootHeaderEnd = mimeHeaderSeparatorIndex(document, &rootSeparatorLength);
    if (rootHeaderEnd < 0) {
        return document.contains(QStringLiteral("<html"), Qt::CaseInsensitive) ? QString::fromUtf8(bytes) : QString();
    }

    const QString rootHeaders = document.left(rootHeaderEnd);
    const QString boundary = mimeParameterValue(mimeHeaderValue(rootHeaders, QStringLiteral("Content-Type")),
                                                QStringLiteral("boundary"));
    if (boundary.isEmpty()) {
        const QString body = document.mid(rootHeaderEnd + rootSeparatorLength);
        return body.contains(QStringLiteral("<html"), Qt::CaseInsensitive) ? QString::fromUtf8(bytes) : QString();
    }

    const QString delimiter = QStringLiteral("--%1").arg(boundary);
    for (QString part : document.split(delimiter)) {
        part = part.trimmed();
        if (part.isEmpty() || part.startsWith(QLatin1String("--"))) {
            continue;
        }

        int partSeparatorLength = 0;
        const int partHeaderEnd = mimeHeaderSeparatorIndex(part, &partSeparatorLength);
        if (partHeaderEnd < 0) {
            continue;
        }

        const QString partHeaders = part.left(partHeaderEnd);
        const QString contentType = mimeHeaderValue(partHeaders, QStringLiteral("Content-Type"));
        if (!contentType.startsWith(QLatin1String("text/html"), Qt::CaseInsensitive)) {
            continue;
        }

        return decodedMimeBody(partHeaders, part.mid(partHeaderEnd + partSeparatorLength));
    }
    return {};
}

std::optional<QString> htmlDocumentFromFile(const QFileInfo &fileInfo)
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    const QByteArray bytes = file.readAll();
    const QString html = isMhtmlFile(fileInfo)
        ? htmlFromMhtmlCapture(bytes)
        : QString::fromUtf8(bytes);
    if (html.isEmpty()) {
        return std::nullopt;
    }
    return html;
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
        link.lineNumber = html.left(match.capturedStart()).count(QLatin1Char('\n')) + 1;
        links.append(link);
    }
    return links;
}

QList<HtmlLink> htmlLinksFromFile(const QFileInfo &fileInfo)
{
    const std::optional<QString> html = htmlDocumentFromFile(fileInfo);
    return html.has_value() ? htmlLinksFromDocument(html.value()) : QList<HtmlLink>{};
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

    static const QRegularExpression trailingSeparatorPattern(QStringLiteral("(?:[\\s:=-]+|[\"'<]+)+$"));
    linePrefix.remove(trailingSeparatorPattern);
    return collapsedWhitespace(linePrefix);
}

QList<TextUrlLink> textUrlLinksFromDocument(const QString &text)
{
    QList<TextUrlLink> links;
    QStringList seenUrls;
    static const QRegularExpression urlPattern(QStringLiteral("https?://[^\\s<>\"]+"));
    static const QRegularExpression referenceDefinitionPattern(
        QStringLiteral("^\\s{0,3}\\[[^\\]]+\\]:\\s*(?:<[^>]+>|[^\\s]+)(?:\\s+.*)?$"));

    int lineNumber = 0;
    bool inFrontmatter = false;
    bool inFence = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
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
        if (referenceDefinitionPattern.match(line).hasMatch()) {
            continue;
        }

        QRegularExpressionMatchIterator matches = urlPattern.globalMatch(line);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            if (match.capturedStart() >= 2
                && line.mid(match.capturedStart() - 2, 2) == QLatin1String("](")) {
                continue;
            }
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
            link.lineNumber = lineNumber;
            if (link.title.isEmpty()) {
                link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
            }
            links.append(link);
        }
    }
    return links;
}

QList<TextUrlLink> textUrlLinksFromFile(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 512 * 1024) {
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

    return textUrlLinksFromDocument(QString::fromUtf8(bytes));
}

void applyEmailHeader(EmailMessageMetadata &metadata,
                      const QString &name,
                      const QString &value,
                      int lineNumber)
{
    const QString normalizedName = name.trimmed().toLower();
    const QString normalizedValue = collapsedWhitespace(value);
    if (normalizedValue.isEmpty()) {
        return;
    }

    if (normalizedName == QLatin1String("subject") && metadata.subject.isEmpty()) {
        metadata.subject = normalizedValue;
        metadata.subjectLineNumber = lineNumber;
    } else if (normalizedName == QLatin1String("from") && metadata.from.isEmpty()) {
        metadata.from = normalizedValue;
        metadata.fromLineNumber = lineNumber;
    } else if (normalizedName == QLatin1String("to") && metadata.to.isEmpty()) {
        metadata.to = normalizedValue;
        metadata.toLineNumber = lineNumber;
    } else if (normalizedName == QLatin1String("cc") && metadata.cc.isEmpty()) {
        metadata.cc = normalizedValue;
        metadata.ccLineNumber = lineNumber;
    } else if (normalizedName == QLatin1String("date") && metadata.date.isEmpty()) {
        metadata.date = normalizedValue;
        metadata.dateLineNumber = lineNumber;
    }
}

EmailMessageMetadata emailMessageMetadataFromText(const QString &text)
{
    EmailMessageMetadata metadata;
    QStringList seenUrls;
    QStringList bodyLines;
    QString currentHeaderName;
    QString currentHeaderValue;
    int currentHeaderLineNumber = -1;
    bool inHeaders = true;
    int lineNumber = 0;

    auto flushHeader = [&]() {
        if (!currentHeaderName.isEmpty()) {
            applyEmailHeader(metadata, currentHeaderName, currentHeaderValue, currentHeaderLineNumber);
        }
        currentHeaderName.clear();
        currentHeaderValue.clear();
        currentHeaderLineNumber = -1;
    };

    static const QRegularExpression urlPattern(QStringLiteral("https?://[^\\s<>\"]+"));

    for (QString line : text.split(QLatin1Char('\n'))) {
        ++lineNumber;
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }

        if (inHeaders) {
            if (line.trimmed().isEmpty()) {
                flushHeader();
                inHeaders = false;
                continue;
            }

            if (!currentHeaderName.isEmpty()
                && !line.isEmpty()
                && (line.front() == QLatin1Char(' ') || line.front() == QLatin1Char('\t'))) {
                currentHeaderValue.append(QLatin1Char(' '));
                currentHeaderValue.append(line.trimmed());
                continue;
            }

            flushHeader();
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                currentHeaderName = line.left(colon);
                currentHeaderValue = line.mid(colon + 1).trimmed();
                currentHeaderLineNumber = lineNumber;
            }
            continue;
        }

        bodyLines.append(line);
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

            EmailUrlLink link;
            link.url = url;
            link.title = titleFromTextBeforeUrl(line.left(match.capturedStart()));
            if (link.title.isEmpty()) {
                link.title = metadata.subject;
            }
            if (link.title.isEmpty()) {
                link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
            }
            link.lineNumber = lineNumber;
            metadata.links.append(link);
        }
    }

    if (inHeaders) {
        flushHeader();
    }

    metadata.content = collapsedWhitespace(bodyLines.join(QLatin1Char(' ')));
    return metadata;
}

EmailMessageMetadata emailMessageMetadataFromFile(const QFileInfo &fileInfo)
{
    if (!isEmailFileCandidate(fileInfo)) {
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

    return emailMessageMetadataFromText(QString::fromUtf8(bytes));
}

QList<TextUrlLink> robotsSitemapLineLinksFromFile(const QFileInfo &fileInfo)
{
    if (!isRobotsTxtCandidate(fileInfo)) {
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

    QList<TextUrlLink> links;
    QStringList seenUrls;
    static const QRegularExpression sitemapPattern(
        QStringLiteral("^\\s*Sitemap\\s*:\\s*(\\S.*)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression whitespacePattern(QStringLiteral("\\s+"));

    int lineNumber = 0;
    for (const QString &line : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
        ++lineNumber;
        const QRegularExpressionMatch match = sitemapPattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }

        const QStringList urlParts = match.captured(1).trimmed().split(whitespacePattern, Qt::SkipEmptyParts);
        if (urlParts.isEmpty()) {
            continue;
        }

        const QUrl url = QUrl::fromUserInput(trimmedPlainTextUrl(urlParts.first()));
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
        link.title = url.path().isEmpty() || url.path() == QLatin1String("/")
            ? url.host()
            : url.path().section(QLatin1Char('/'), -1);
        if (link.title.isEmpty()) {
            link.title = url.toDisplayString();
        }
        link.lineNumber = lineNumber;
        links.append(link);
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

bool isXbelBookmarkCandidate(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && fileInfo.size() <= 4 * 1024 * 1024
        && fileInfo.suffix().compare(QStringLiteral("xbel"), Qt::CaseInsensitive) == 0;
}

void appendBrowserBookmarkLinksFromNode(const QJsonObject &node,
                                        const QStringList &folderPath,
                                        QList<BrowserBookmarkLink> &links)
{
    const QString type = node.value(QStringLiteral("type")).toString();
    if (type.compare(QStringLiteral("url"), Qt::CaseInsensitive) == 0) {
        const QString rawUrl = node.value(QStringLiteral("url")).toString().trimmed();
        const QUrl url = QUrl::fromUserInput(rawUrl);
        if (!isIndexableWebUrl(url)) {
            return;
        }

        BrowserBookmarkLink link;
        link.url = url;
        link.rawUrl = rawUrl;
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

QList<XbelBookmarkLink> xbelBookmarkLinksFromDocument(const QByteArray &content)
{
    QXmlStreamReader reader(content);
    QList<XbelBookmarkLink> links;
    QList<XbelParseContext> stack;
    QStringList folderPath;
    bool rootSeen = false;

    while (!reader.atEnd()) {
        reader.readNext();
        const QString name = reader.name().toString().toLower();

        if (reader.isStartElement()) {
            if (!rootSeen) {
                rootSeen = true;
                if (name != QLatin1String("xbel")) {
                    return {};
                }
            }

            if (name == QLatin1String("folder")) {
                XbelParseContext context;
                context.type = XbelParseContext::Type::Folder;
                context.lineNumber = static_cast<int>(reader.lineNumber());
                stack.append(context);
            } else if (name == QLatin1String("bookmark")) {
                const QUrl url = QUrl::fromUserInput(xmlAttributeValue(reader.attributes(), QStringLiteral("href")));
                XbelParseContext context;
                context.type = XbelParseContext::Type::Bookmark;
                context.url = url;
                context.lineNumber = static_cast<int>(reader.lineNumber());
                stack.append(context);
            } else if (name == QLatin1String("title") && !stack.isEmpty()) {
                const QString title = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                XbelParseContext &context = stack.last();
                context.title = title;
                if (context.type == XbelParseContext::Type::Folder
                    && !context.folderPathPushed
                    && !title.isEmpty()) {
                    folderPath.append(title);
                    context.folderPathPushed = true;
                }
            }
        } else if (reader.isEndElement()) {
            if (name == QLatin1String("bookmark") && !stack.isEmpty()) {
                const XbelParseContext context = stack.takeLast();
                if (context.type == XbelParseContext::Type::Bookmark && isIndexableWebUrl(context.url)) {
                    XbelBookmarkLink link;
                    link.url = context.url;
                    link.title = context.title.trimmed();
                    if (link.title.isEmpty()) {
                        link.title = context.url.host().isEmpty() ? context.url.toDisplayString() : context.url.host();
                    }
                    link.folderPath = folderPath;
                    link.lineNumber = context.lineNumber;
                    links.append(link);
                }
            } else if (name == QLatin1String("folder") && !stack.isEmpty()) {
                const XbelParseContext context = stack.takeLast();
                if (context.type == XbelParseContext::Type::Folder
                    && context.folderPathPushed
                    && !folderPath.isEmpty()) {
                    folderPath.removeLast();
                }
            }
        }
    }

    if (reader.hasError()) {
        return {};
    }
    return links;
}

QList<XbelBookmarkLink> xbelBookmarkLinksFromFile(const QFileInfo &fileInfo)
{
    if (!isXbelBookmarkCandidate(fileInfo)) {
        return {};
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    return xbelBookmarkLinksFromDocument(file.readAll());
}

QList<XbelBookmarkLink> deduplicatedXbelBookmarkLinks(const QList<XbelBookmarkLink> &links)
{
    QList<XbelBookmarkLink> deduplicated;
    QStringList seenUrls;
    for (const XbelBookmarkLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);
        deduplicated.append(link);
    }
    return deduplicated;
}

bool isBrowserHistorySqliteCandidate(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 128 * 1024 * 1024) {
        return false;
    }

    const QString fileName = fileInfo.fileName().toLower();
    return fileName == QLatin1String("history")
        || fileName == QLatin1String("history.db")
        || fileName == QLatin1String("history.sqlite")
        || fileName == QLatin1String("history.sqlite3");
}

bool isFirefoxPlacesSqliteCandidate(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && fileInfo.size() <= 128 * 1024 * 1024
        && fileInfo.fileName().compare(QStringLiteral("places.sqlite"), Qt::CaseInsensitive) == 0;
}

bool hasSqliteHeader(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() <= 16 || fileInfo.size() > 128 * 1024 * 1024) {
        return false;
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    return file.read(16) == QByteArray("SQLite format 3\000", 16);
}

bool isGenericSqliteCandidate(const QFileInfo &fileInfo)
{
    if (isBrowserHistorySqliteCandidate(fileInfo) || isFirefoxPlacesSqliteCandidate(fileInfo)) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (suffix != QLatin1String("db")
        && suffix != QLatin1String("sqlite")
        && suffix != QLatin1String("sqlite3")) {
        return false;
    }

    return hasSqliteHeader(fileInfo);
}

QString sqliteQuotedIdentifier(QString identifier)
{
    identifier.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(identifier);
}

bool isGenericSqliteTextCandidateColumn(const GenericSqliteColumn &column)
{
    const QString type = column.type.toUpper();
    if (type.contains(QLatin1String("TEXT"))
        || type.contains(QLatin1String("CHAR"))
        || type.contains(QLatin1String("CLOB"))
        || type.contains(QLatin1String("VARCHAR"))) {
        return true;
    }

    const QString name = column.name.toLower();
    return name.contains(QLatin1String("url"))
        || name.contains(QLatin1String("uri"))
        || name.contains(QLatin1String("href"))
        || name.contains(QLatin1String("link"))
        || name.contains(QLatin1String("title"))
        || name.contains(QLatin1String("name"))
        || name.contains(QLatin1String("note"))
        || name.contains(QLatin1String("description"));
}

QString genericSqliteColumnTarget(const GenericSqliteColumn &column)
{
    const QString suffix = column.type.trimmed().isEmpty()
        ? QString()
        : QStringLiteral(" %1").arg(column.type.trimmed().toUpper());
    return QStringLiteral("sqlite column: %1.%2%3").arg(column.tableName, column.name, suffix);
}

void appendGenericSqliteSampleAnchors(Resource &resource,
                                      QSqlDatabase &database,
                                      const QString &tableName,
                                      const QList<GenericSqliteColumn> &columns,
                                      QStringList &contentParts,
                                      int &sampleAnchorCount)
{
    if (columns.isEmpty() || sampleAnchorCount >= 24) {
        return;
    }

    QStringList selectedColumns;
    QList<GenericSqliteColumn> sampledColumns;
    for (const GenericSqliteColumn &column : columns) {
        if (!isGenericSqliteTextCandidateColumn(column)) {
            continue;
        }
        selectedColumns.append(sqliteQuotedIdentifier(column.name));
        sampledColumns.append(column);
        if (selectedColumns.size() >= 6) {
            break;
        }
    }
    if (selectedColumns.isEmpty()) {
        return;
    }

    QSqlQuery sampleQuery(database);
    const QString statement = QStringLiteral("SELECT %1 FROM %2 LIMIT 5")
        .arg(selectedColumns.join(QStringLiteral(", ")), sqliteQuotedIdentifier(tableName));
    if (!sampleQuery.exec(statement)) {
        return;
    }

    QStringList seenSamples;
    while (sampleQuery.next() && sampleAnchorCount < 24) {
        for (int i = 0; i < sampledColumns.size() && sampleAnchorCount < 24; ++i) {
            QString value = collapsedWhitespace(sampleQuery.value(i).toString());
            if (value.isEmpty()) {
                continue;
            }
            if (value.size() > 160) {
                value = value.left(157) + QStringLiteral("...");
            }

            const GenericSqliteColumn column = sampledColumns.at(i);
            const QString sampleKey = QStringLiteral("%1.%2=%3").arg(column.tableName, column.name, value);
            if (seenSamples.contains(sampleKey, Qt::CaseInsensitive)) {
                continue;
            }
            seenSamples.append(sampleKey);
            contentParts.append(value);

            const QUrl url = QUrl::fromUserInput(value);
            if (isIndexableWebUrl(url)) {
                appendFileLineAnchor(resource,
                                     QStringLiteral("sqlite url: %1.%2 -> %3")
                                         .arg(column.tableName,
                                              column.name,
                                              url.toString(QUrl::FullyEncoded)),
                                     1);
                ++sampleAnchorCount;
                continue;
            }

            if (value.size() >= 3) {
                appendFileLineAnchor(resource,
                                     QStringLiteral("sqlite sample: %1.%2 = %3")
                                         .arg(column.tableName, column.name, value),
                                     1);
                ++sampleAnchorCount;
            }
        }
    }
}

void appendGenericSqliteMetadata(Resource &resource, const QFileInfo &fileInfo)
{
    if (!isGenericSqliteCandidate(fileInfo)) {
        return;
    }

    appendUnique(resource.tags, QStringLiteral("sqlite"));
    appendUnique(resource.tags, QStringLiteral("sqlite-database"));
    appendUnique(resource.tags, QStringLiteral("special-reader"));

    QStringList contentParts;
    const QString connectionName =
        QStringLiteral("pinloom_generic_sqlite_%1").arg(qHash(fileInfo.absoluteFilePath()));
    int tableCount = 0;
    int sampleAnchorCount = 0;

    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(fileInfo.absoluteFilePath());
        if (database.open()) {
            QSqlQuery tableQuery(database);
            if (tableQuery.exec(QStringLiteral(
                    "SELECT name FROM sqlite_master "
                    "WHERE type IN ('table', 'view') AND name NOT LIKE 'sqlite_%' "
                    "ORDER BY name LIMIT 32"))) {
                while (tableQuery.next()) {
                    const QString tableName = tableQuery.value(0).toString().trimmed();
                    if (tableName.isEmpty()) {
                        continue;
                    }

                    ++tableCount;
                    appendUnique(resource.aliases, tableName);
                    appendFileLineAnchor(resource, QStringLiteral("sqlite table: %1").arg(tableName), 1);
                    contentParts.append(tableName);

                    QList<GenericSqliteColumn> columns;
                    QSqlQuery columnQuery(database);
                    if (columnQuery.exec(QStringLiteral("PRAGMA table_info(%1)")
                                             .arg(sqliteQuotedIdentifier(tableName)))) {
                        while (columnQuery.next()) {
                            GenericSqliteColumn column;
                            column.tableName = tableName;
                            column.name = columnQuery.value(1).toString().trimmed();
                            column.type = columnQuery.value(2).toString().trimmed();
                            if (column.name.isEmpty()) {
                                continue;
                            }
                            columns.append(column);
                            appendUnique(resource.aliases, column.name);
                            appendFileLineAnchor(resource, genericSqliteColumnTarget(column), 1);
                            contentParts.append(QStringLiteral("%1.%2").arg(tableName, column.name));
                        }
                    }

                    appendGenericSqliteSampleAnchors(resource,
                                                     database,
                                                     tableName,
                                                     columns,
                                                     contentParts,
                                                     sampleAnchorCount);
                }
            }
            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);

    if (tableCount > 0) {
        appendFileLineAnchor(resource, QStringLiteral("sqlite tables: %1").arg(tableCount), 1);
    }
    if (!contentParts.isEmpty()) {
        resource.content = collapsedWhitespace(
            QStringList{resource.content, contentParts.join(QLatin1Char(' '))}.join(QLatin1Char(' ')));
    }
}

quint16 littleEndianUInt16(const QByteArray &bytes, int offset)
{
    if (offset < 0 || offset + 2 > bytes.size()) {
        return 0;
    }

    const auto *data = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return static_cast<quint16>(data[0])
        | static_cast<quint16>(data[1] << 8);
}

quint32 littleEndianUInt32(const QByteArray &bytes, int offset)
{
    if (offset < 0 || offset + 4 > bytes.size()) {
        return 0;
    }

    const auto *data = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return static_cast<quint32>(data[0])
        | (static_cast<quint32>(data[1]) << 8)
        | (static_cast<quint32>(data[2]) << 16)
        | (static_cast<quint32>(data[3]) << 24);
}

QDateTime dateTimeFromChromiumWebTime(qint64 value)
{
    if (value <= 0) {
        return {};
    }

    static const qint64 unixEpochOffsetMicroseconds = 11644473600LL * 1000LL * 1000LL;
    const qint64 unixMicroseconds = value - unixEpochOffsetMicroseconds;
    return unixMicroseconds <= 0
        ? QDateTime()
        : QDateTime::fromMSecsSinceEpoch(unixMicroseconds / 1000, QTimeZone::UTC);
}

QDateTime dateTimeFromUnixMicroseconds(qint64 value)
{
    return value <= 0 ? QDateTime() : QDateTime::fromMSecsSinceEpoch(value / 1000, QTimeZone::UTC);
}

QStringList firefoxBookmarkFolderPath(int bookmarkId, const QHash<int, FirefoxBookmarkNode> &nodes)
{
    QStringList path;
    int currentId = nodes.value(bookmarkId).parent;
    int guard = 0;
    while (currentId > 0 && nodes.contains(currentId) && guard < 64) {
        ++guard;
        const FirefoxBookmarkNode node = nodes.value(currentId);
        if (node.type == 2 && node.parent > 0 && !node.title.trimmed().isEmpty()) {
            path.prepend(node.title.trimmed());
        }
        currentId = node.parent;
    }
    return path;
}

QHash<int, FirefoxBookmarkMetadata> firefoxBookmarkMetadataByPlaceId(QSqlDatabase &database)
{
    QHash<int, FirefoxBookmarkMetadata> metadataByPlaceId;

    QSqlQuery tableQuery(database);
    if (!tableQuery.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='moz_bookmarks'"))
        || !tableQuery.next()) {
        return metadataByPlaceId;
    }

    QHash<int, FirefoxBookmarkNode> nodes;
    QList<FirefoxBookmarkNode> bookmarks;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT id, type, fk, parent, title FROM moz_bookmarks ORDER BY dateAdded DESC, id DESC"))) {
        return metadataByPlaceId;
    }

    while (query.next()) {
        FirefoxBookmarkNode node;
        node.id = query.value(0).toInt();
        node.type = query.value(1).toInt();
        node.fk = query.value(2).toInt();
        node.parent = query.value(3).toInt();
        node.title = query.value(4).toString().trimmed();
        nodes.insert(node.id, node);
        if (node.type == 1 && node.fk > 0) {
            bookmarks.append(node);
        }
    }

    for (const FirefoxBookmarkNode &bookmark : bookmarks) {
        if (metadataByPlaceId.contains(bookmark.fk)) {
            continue;
        }
        FirefoxBookmarkMetadata metadata;
        metadata.title = bookmark.title;
        metadata.folderPath = firefoxBookmarkFolderPath(bookmark.id, nodes);
        metadataByPlaceId.insert(bookmark.fk, metadata);
    }

    return metadataByPlaceId;
}

QList<BrowserHistoryLink> browserHistoryLinksFromSqliteFile(const QFileInfo &fileInfo)
{
    if (!isBrowserHistorySqliteCandidate(fileInfo)) {
        return {};
    }

    QList<BrowserHistoryLink> links;
    QStringList seenUrls;
    const QString connectionName =
        QStringLiteral("pinloom_browser_history_%1").arg(qHash(fileInfo.absoluteFilePath()));

    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(fileInfo.absoluteFilePath());
        if (!database.open()) {
            database.close();
        } else {
            QSqlQuery tableQuery(database);
            if (tableQuery.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='urls'"))
                && tableQuery.next()) {
                QSqlQuery query(database);
                if (query.exec(QStringLiteral(
                        "SELECT url, title, visit_count, last_visit_time FROM urls ORDER BY last_visit_time DESC"))) {
                    while (query.next()) {
                        const QUrl url = QUrl::fromUserInput(query.value(0).toString().trimmed());
                        if (!isIndexableWebUrl(url)) {
                            continue;
                        }

                        const QString urlKey = url.toString(QUrl::FullyEncoded);
                        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
                            continue;
                        }
                        seenUrls.append(urlKey);

                        BrowserHistoryLink link;
                        link.url = url;
                        link.title = query.value(1).toString().trimmed();
                        if (link.title.isEmpty()) {
                            link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
                        }
                        link.visitCount = query.value(2).toInt();
                        link.lastVisitedAt = dateTimeFromChromiumWebTime(query.value(3).toLongLong());
                        links.append(link);
                    }
                }
            }
            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return links;
}

QList<BrowserHistoryLink> firefoxPlacesLinksFromSqliteFile(const QFileInfo &fileInfo)
{
    if (!isFirefoxPlacesSqliteCandidate(fileInfo)) {
        return {};
    }

    QList<BrowserHistoryLink> links;
    QStringList seenUrls;
    const QString connectionName =
        QStringLiteral("pinloom_firefox_places_%1").arg(qHash(fileInfo.absoluteFilePath()));

    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(fileInfo.absoluteFilePath());
        if (!database.open()) {
            database.close();
        } else {
            QSqlQuery tableQuery(database);
            if (tableQuery.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='moz_places'"))
                && tableQuery.next()) {
                const QHash<int, FirefoxBookmarkMetadata> bookmarkMetadata = firefoxBookmarkMetadataByPlaceId(database);
                QSqlQuery query(database);
                if (query.exec(QStringLiteral(
                        "SELECT id, url, title, visit_count, last_visit_date FROM moz_places ORDER BY last_visit_date DESC"))) {
                    while (query.next()) {
                        const int placeId = query.value(0).toInt();
                        const QUrl url = QUrl::fromUserInput(query.value(1).toString().trimmed());
                        if (!isIndexableWebUrl(url)) {
                            continue;
                        }

                        const QString urlKey = url.toString(QUrl::FullyEncoded);
                        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
                            continue;
                        }
                        seenUrls.append(urlKey);

                        BrowserHistoryLink link;
                        link.url = url;
                        const FirefoxBookmarkMetadata metadata = bookmarkMetadata.value(placeId);
                        link.title = metadata.title.trimmed();
                        if (link.title.isEmpty()) {
                            link.title = query.value(2).toString().trimmed();
                        }
                        if (link.title.isEmpty()) {
                            link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
                        }
                        link.visitCount = query.value(3).toInt();
                        link.lastVisitedAt = dateTimeFromUnixMicroseconds(query.value(4).toLongLong());
                        if (!metadata.title.trimmed().isEmpty() || !metadata.folderPath.isEmpty()) {
                            appendUnique(link.tags, QStringLiteral("firefox-bookmark"));
                        }
                        for (const QString &folder : metadata.folderPath) {
                            appendUnique(link.aliases, folder);
                        }
                        if (metadata.folderPath.size() > 1) {
                            appendUnique(link.aliases, metadata.folderPath.join(QStringLiteral(" / ")));
                        }
                        links.append(link);
                    }
                }
            }
            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return links;
}

QList<Resource> browserHistoryResourcesFromLinks(const QFileInfo &fileInfo,
                                                 const QList<BrowserHistoryLink> &links)
{
    QList<Resource> resources;
    for (const BrowserHistoryLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("browser-history:%1:%2").arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = link.lastVisitedAt.isValid() ? link.lastVisitedAt : fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("browser-history"));
        if (isFirefoxPlacesSqliteCandidate(fileInfo)) {
            appendUnique(resource.tags, QStringLiteral("firefox-history"));
        }
        for (const QString &tag : link.tags) {
            appendUnique(resource.tags, tag);
        }
        for (const QString &alias : link.aliases) {
            appendUnique(resource.aliases, alias);
        }
        if (link.visitCount > 0) {
            appendUnique(resource.aliases, QStringLiteral("visited %1 times").arg(link.visitCount));
        }
        resources.append(resource);
    }
    return resources;
}

QString browserHistoryAnchorTarget(const BrowserHistoryLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

void appendBrowserHistorySourceMetadata(Resource &sourceResource,
                                        const QList<BrowserHistoryLink> &links,
                                        const QList<Resource> &urlResources)
{
    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const BrowserHistoryLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = browserHistoryAnchorTarget(link);

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.visitCount > 0
            ? QStringLiteral("browser history visits %1: %2").arg(link.visitCount).arg(anchorTarget)
            : QStringLiteral("browser history: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
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
            const int lineNumber = static_cast<int>(reader.lineNumber());
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
                link.lineNumber = lineNumber;
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
                    const int lineNumber = static_cast<int>(reader.lineNumber());
                    const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                    if (isIndexableWebUrl(url)) {
                        current.url = url;
                        current.lineNumber = lineNumber;
                    }
                } else if (inItem && name == QLatin1String("guid") && !isIndexableWebUrl(current.url)) {
                    const int lineNumber = static_cast<int>(reader.lineNumber());
                    const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                    if (isIndexableWebUrl(url)) {
                        current.url = url;
                        current.lineNumber = lineNumber;
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
                    const int lineNumber = static_cast<int>(reader.lineNumber());
                    const QString rel = xmlAttributeValue(reader.attributes(), QStringLiteral("rel"));
                    const QUrl url = QUrl::fromUserInput(xmlAttributeValue(reader.attributes(), QStringLiteral("href")));
                    if ((rel.isEmpty() || rel.compare(QStringLiteral("alternate"), Qt::CaseInsensitive) == 0)
                        && isIndexableWebUrl(url)) {
                        current.url = url;
                        current.lineNumber = lineNumber;
                    }
                } else if (inEntry && name == QLatin1String("id") && !isIndexableWebUrl(current.url)) {
                    const int lineNumber = static_cast<int>(reader.lineNumber());
                    const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                    if (isIndexableWebUrl(url)) {
                        current.url = url;
                        current.lineNumber = lineNumber;
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
                const int lineNumber = static_cast<int>(reader.lineNumber());
                const QUrl url = QUrl::fromUserInput(reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
                if (isIndexableWebUrl(url)) {
                    SitemapLink link;
                    link.url = url;
                    link.sitemapIndexEntry = inSitemap;
                    link.lineNumber = lineNumber;
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

void appendBeaconLineAnchor(Resource &resource, const QString &target, int line)
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

void appendTodoFixmeNoteTextBeaconAnchorsFromLine(Resource &resource, const QString &line, int lineNumber)
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

void appendGenericTextBeaconAnchorsFromLine(Resource &resource, const QString &line, int lineNumber)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    static const QRegularExpression urlPattern(QStringLiteral("\\bhttps?://[^\\s<>)\"']+"));
    QRegularExpressionMatchIterator urlMatches = urlPattern.globalMatch(trimmed);
    while (urlMatches.hasNext()) {
        const QRegularExpressionMatch match = urlMatches.next();
        appendBeaconLineAnchor(resource, QStringLiteral("url: %1").arg(match.captured(0)), lineNumber);
    }

    static const QRegularExpression errorWarningPattern(
        QStringLiteral("\\b(error|warning|warn)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch errorWarningMatch = errorWarningPattern.match(trimmed);
    if (errorWarningMatch.hasMatch()) {
        const QString kind = errorWarningMatch.captured(1).startsWith(QLatin1String("err"), Qt::CaseInsensitive)
            ? QStringLiteral("error")
            : QStringLiteral("warning");
        appendBeaconLineAnchor(resource, QStringLiteral("%1: %2").arg(kind, trimmed), lineNumber);
    }

    static const QRegularExpression userMarkerPattern(
        QStringLiteral("^\\s*(?://+|#+|;+|--+|/\\*)?\\s*(MARKER|ANCHOR|BOOKMARK)\\b\\s*:?[\\s-]*(.+?)\\s*(?:\\*/)?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch markerMatch = userMarkerPattern.match(line);
    if (markerMatch.hasMatch()) {
        const QString detail = markerMatch.captured(2).trimmed();
        appendBeaconLineAnchor(resource,
                               detail.isEmpty()
                                   ? QStringLiteral("marker")
                                   : QStringLiteral("marker: %1").arg(detail),
                               lineNumber);
    }

    static const QRegularExpression bracketSectionPattern(QStringLiteral("^\\[([^\\]]{1,120})\\]$"));
    static const QRegularExpression colonSectionPattern(QStringLiteral("^([A-Za-z0-9][A-Za-z0-9_. /-]{1,120})\\s*:$"));
    static const QRegularExpression wrappedSectionPattern(
        QStringLiteral("^(?:={2,}|-{3,})\\s*(.+?)\\s*(?:={2,}|-{3,})$"));
    const QRegularExpressionMatch bracketSectionMatch = bracketSectionPattern.match(trimmed);
    const QRegularExpressionMatch colonSectionMatch = colonSectionPattern.match(trimmed);
    const QRegularExpressionMatch wrappedSectionMatch = wrappedSectionPattern.match(trimmed);
    if (bracketSectionMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("section: %1").arg(bracketSectionMatch.captured(1).trimmed()),
                               lineNumber);
    } else if (colonSectionMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("section: %1").arg(colonSectionMatch.captured(1).trimmed()),
                               lineNumber);
    } else if (wrappedSectionMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("section: %1").arg(wrappedSectionMatch.captured(1).trimmed()),
                               lineNumber);
    }

}

void appendConfigStyleTextBeaconsFromLine(Resource &resource, const QString &line, int lineNumber)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    static const QRegularExpression entryPattern(
        QStringLiteral("^(?:qt_)?add_(?:executable|library|custom_target)\\s*\\(\\s*([A-Za-z0-9_.:+-]+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression namedEntryPattern(
        QStringLiteral("^add_test\\s*\\(\\s*(?:NAME\\s+)?([A-Za-z0-9_.:+-]+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression projectEntryPattern(
        QStringLiteral("^project\\s*\\(\\s*([A-Za-z0-9_.:+-]+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression settingPattern(
        QStringLiteral("^option\\s*\\(\\s*([A-Za-z0-9_.:+-]+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression blockPattern(
        QStringLiteral("^(function|macro)\\s*\\(\\s*([A-Za-z0-9_.:+-]+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression referencePattern(
        QStringLiteral("^find_package\\s*\\(\\s*([A-Za-z0-9_.:+-]+)\\b"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch entryMatch = entryPattern.match(trimmed);
    if (entryMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("config entry: %1").arg(entryMatch.captured(1)),
                               lineNumber);
        return;
    }

    const QRegularExpressionMatch namedEntryMatch = namedEntryPattern.match(trimmed);
    if (namedEntryMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("config entry: %1").arg(namedEntryMatch.captured(1)),
                               lineNumber);
        return;
    }

    const QRegularExpressionMatch projectEntryMatch = projectEntryPattern.match(trimmed);
    if (projectEntryMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("config entry: %1").arg(projectEntryMatch.captured(1)),
                               lineNumber);
        return;
    }

    const QRegularExpressionMatch settingMatch = settingPattern.match(trimmed);
    if (settingMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("config setting: %1").arg(settingMatch.captured(1)),
                               lineNumber);
        return;
    }

    const QRegularExpressionMatch blockMatch = blockPattern.match(trimmed);
    if (blockMatch.hasMatch()) {
        appendBeaconLineAnchor(resource,
                               QStringLiteral("config block: %1").arg(blockMatch.captured(2)),
                               lineNumber);
        return;
    }

    const QRegularExpressionMatch referenceMatch = referencePattern.match(trimmed);
    if (referenceMatch.hasMatch()) {
        appendFileLineAnchor(resource,
                             QStringLiteral("config reference: %1").arg(referenceMatch.captured(1)),
                             lineNumber);
    }
}

void appendRuleEntryTextBeaconsFromLine(Resource &resource, const QString &line, int lineNumber)
{
    if (line.startsWith(QLatin1Char('\t'))) {
        return;
    }

    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    static const QRegularExpression entryPattern(
        QStringLiteral("^([^:#=]+?)\\s*:(?![=:])"));
    const QRegularExpressionMatch entryMatch = entryPattern.match(trimmed);
    if (!entryMatch.hasMatch()) {
        return;
    }

    const QStringList entries = entryMatch.captured(1).split(QRegularExpression(QStringLiteral("\\s+")),
                                                             Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const QString normalized = entry.trimmed();
        if (!normalized.isEmpty() && !normalized.startsWith(QLatin1Char('.'))) {
            appendBeaconLineAnchor(resource, QStringLiteral("rule entry: %1").arg(normalized), lineNumber);
        }
    }
}

void appendContainerTextBeaconsFromLine(Resource &resource, const QString &line, int lineNumber)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    static const QRegularExpression fromPattern(
        QStringLiteral("^FROM\\s+([^\\s]+)(?:\\s+AS\\s+([A-Za-z0-9_.-]+))?\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression copyPattern(
        QStringLiteral("^(COPY|ADD)\\s+(?:--[^\\s]+\\s+)*([^\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch fromMatch = fromPattern.match(trimmed);
    if (fromMatch.hasMatch()) {
        const QString stage = fromMatch.captured(2).trimmed();
        if (!stage.isEmpty()) {
            appendBeaconLineAnchor(resource, QStringLiteral("config block: %1").arg(stage), lineNumber);
        }
        appendFileLineAnchor(resource,
                             QStringLiteral("config input: %1").arg(fromMatch.captured(1).trimmed()),
                             lineNumber);
        return;
    }

    const QRegularExpressionMatch copyMatch = copyPattern.match(trimmed);
    if (copyMatch.hasMatch()) {
        appendFileLineAnchor(resource,
                             QStringLiteral("config input: %1").arg(copyMatch.captured(2).trimmed()),
                             lineNumber);
    }
}

struct TextStructureBeaconState {
    QStringList jsonPath;
    QList<int> yamlIndents;
    QStringList yamlPath;
    QString sectionPath;
};

struct WorkflowConfigBeaconState {
    bool inBlocksSection = false;
    QString currentBlock;
    bool inSteps = false;
};

struct PipelineConfigBeaconState {
    QString currentBlock;
    QString currentListKey;
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

QString cleanedYamlScalar(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            value = value.mid(1, value.size() - 2).trimmed();
        }
    }
    return value;
}

int leadingSpaceCount(const QString &line)
{
    int count = 0;
    while (count < line.size() && line.at(count).isSpace() && line.at(count) != QLatin1Char('\n')) {
        count += line.at(count) == QLatin1Char('\t') ? 4 : 1;
    }
    return count;
}

void appendWorkflowConfigBeaconsFromLine(Resource &resource,
                                         const QString &line,
                                         int lineNumber,
                                         WorkflowConfigBeaconState &state)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    const int indent = leadingSpaceCount(line);
    static const QRegularExpression keyPattern(QStringLiteral("^([A-Za-z0-9_.-]+)\\s*:\\s*(.*)$"));
    static const QRegularExpression listKeyPattern(QStringLiteral("^-\\s+([A-Za-z0-9_.-]+)\\s*:\\s*(.*)$"));

    if (indent == 0) {
        const QRegularExpressionMatch keyMatch = keyPattern.match(trimmed);
        if (keyMatch.hasMatch()) {
            const QString key = keyMatch.captured(1);
            if (key == QLatin1String("name")) {
                const QString workflowName = cleanedYamlScalar(keyMatch.captured(2));
                if (!workflowName.isEmpty()) {
                    appendBeaconLineAnchor(resource, QStringLiteral("config workflow: %1").arg(workflowName), lineNumber);
                }
            }
            state.inBlocksSection = key == QLatin1String("jobs");
            state.currentBlock.clear();
            state.inSteps = false;
        }
        return;
    }

    if (!state.inBlocksSection) {
        return;
    }

    if (indent == 2) {
        const QRegularExpressionMatch blockMatch = keyPattern.match(trimmed);
        if (blockMatch.hasMatch()) {
            state.currentBlock = blockMatch.captured(1).trimmed();
            state.inSteps = false;
            if (!state.currentBlock.isEmpty()) {
                appendBeaconLineAnchor(resource,
                                       QStringLiteral("config block: %1").arg(state.currentBlock),
                                       lineNumber);
            }
        }
        return;
    }

    if (state.currentBlock.isEmpty()) {
        return;
    }

    if (indent == 4) {
        const QRegularExpressionMatch keyMatch = keyPattern.match(trimmed);
        if (keyMatch.hasMatch()) {
            const QString key = keyMatch.captured(1);
            if (key == QLatin1String("steps")) {
                state.inSteps = true;
                return;
            }
            if (key == QLatin1String("name")) {
                const QString blockLabel = cleanedYamlScalar(keyMatch.captured(2));
                if (!blockLabel.isEmpty()) {
                    appendFileLineAnchor(resource,
                                         QStringLiteral("config label: %1").arg(blockLabel),
                                         lineNumber);
                }
            }
            state.inSteps = false;
        }
        return;
    }

    if (!state.inSteps || indent < 6) {
        return;
    }

    const QRegularExpressionMatch listMatch = listKeyPattern.match(trimmed);
    const QRegularExpressionMatch keyMatch = keyPattern.match(trimmed);
    const bool listKey = listMatch.hasMatch();
    const QString key = listKey ? listMatch.captured(1) : keyMatch.captured(1);
    const QString value = cleanedYamlScalar(listKey ? listMatch.captured(2) : keyMatch.captured(2));
    if (key.isEmpty() || value.isEmpty() || value == QLatin1String("|") || value == QLatin1String(">")) {
        return;
    }

    if (key == QLatin1String("name")) {
        appendBeaconLineAnchor(resource, QStringLiteral("config step: %1").arg(value), lineNumber);
    } else if (key == QLatin1String("uses")) {
        appendFileLineAnchor(resource, QStringLiteral("config uses: %1").arg(value), lineNumber);
    } else if (key == QLatin1String("run")) {
        appendFileLineAnchor(resource, QStringLiteral("config run: %1").arg(value), lineNumber);
    }
}

bool isPipelineConfigReservedTopLevelKey(const QString &key)
{
    static const QStringList reservedKeys{
        QStringLiteral("stages"),
        QStringLiteral("types"),
        QStringLiteral("variables"),
        QStringLiteral("workflow"),
        QStringLiteral("include"),
        QStringLiteral("default"),
        QStringLiteral("image"),
        QStringLiteral("services"),
        QStringLiteral("cache"),
        QStringLiteral("before_script"),
        QStringLiteral("after_script"),
    };
    return reservedKeys.contains(key, Qt::CaseInsensitive);
}

void appendPipelineConfigBeaconsFromLine(Resource &resource,
                                         const QString &line,
                                         int lineNumber,
                                         PipelineConfigBeaconState &state)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    const int indent = leadingSpaceCount(line);
    static const QRegularExpression keyPattern(QStringLiteral("^([A-Za-z0-9_.-]+)\\s*:\\s*(.*)$"));
    static const QRegularExpression listItemPattern(QStringLiteral("^-\\s+(.+)$"));
    static const QRegularExpression listKeyPattern(QStringLiteral("^-\\s+([A-Za-z0-9_.-]+)\\s*:\\s*(.*)$"));

    if (indent == 0) {
        const QRegularExpressionMatch keyMatch = keyPattern.match(trimmed);
        if (!keyMatch.hasMatch()) {
            state.currentBlock.clear();
            state.currentListKey.clear();
            return;
        }

        const QString key = keyMatch.captured(1).trimmed();
        if (key == QLatin1String("stages")) {
            state.currentBlock.clear();
            state.currentListKey = key;
            return;
        }

        state.currentListKey.clear();
        if (isPipelineConfigReservedTopLevelKey(key)) {
            state.currentBlock.clear();
            return;
        }

        state.currentBlock = key;
        if (!state.currentBlock.isEmpty()) {
            appendBeaconLineAnchor(resource, QStringLiteral("config block: %1").arg(state.currentBlock), lineNumber);
        }
        return;
    }

    if (state.currentListKey == QLatin1String("stages") && indent >= 2) {
        const QRegularExpressionMatch listMatch = listItemPattern.match(trimmed);
        if (listMatch.hasMatch()) {
            const QString stageName = cleanedYamlScalar(listMatch.captured(1));
            if (!stageName.isEmpty()) {
                appendFileLineAnchor(resource, QStringLiteral("config stage: %1").arg(stageName), lineNumber);
            }
        }
        return;
    }

    if (state.currentBlock.isEmpty()) {
        return;
    }

    if (indent == 2) {
        const QRegularExpressionMatch keyMatch = keyPattern.match(trimmed);
        if (!keyMatch.hasMatch()) {
            state.currentListKey.clear();
            return;
        }

        const QString key = keyMatch.captured(1).trimmed();
        const QString value = cleanedYamlScalar(keyMatch.captured(2));
        if (key == QLatin1String("stage") && !value.isEmpty()) {
            appendFileLineAnchor(resource, QStringLiteral("config stage: %1").arg(value), lineNumber);
            state.currentListKey.clear();
            return;
        }
        if (key == QLatin1String("image") && !value.isEmpty()) {
            appendFileLineAnchor(resource, QStringLiteral("config image: %1").arg(value), lineNumber);
            state.currentListKey.clear();
            return;
        }

        if (key == QLatin1String("script")
            || key == QLatin1String("before_script")
            || key == QLatin1String("after_script")
            || key == QLatin1String("needs")) {
            state.currentListKey = key;
            if (!value.isEmpty() && value != QLatin1String("|") && value != QLatin1String(">")) {
                const QString anchorPrefix = key == QLatin1String("needs")
                    ? QStringLiteral("config needs")
                    : QStringLiteral("config %1").arg(key);
                appendFileLineAnchor(resource, QStringLiteral("%1: %2").arg(anchorPrefix, value), lineNumber);
            }
            return;
        }

        state.currentListKey.clear();
        return;
    }

    if (indent < 4 || state.currentListKey.isEmpty()) {
        return;
    }

    const QRegularExpressionMatch listMatch = listItemPattern.match(trimmed);
    if (!listMatch.hasMatch()) {
        return;
    }

    QString value = cleanedYamlScalar(listMatch.captured(1));
    const QRegularExpressionMatch listKeyMatch = listKeyPattern.match(trimmed);
    if (state.currentListKey == QLatin1String("needs")
        && listKeyMatch.hasMatch()
        && listKeyMatch.captured(1) == QLatin1String("job")) {
        value = cleanedYamlScalar(listKeyMatch.captured(2));
    }
    if (value.isEmpty() || value == QLatin1String("|") || value == QLatin1String(">")) {
        return;
    }

    if (state.currentListKey == QLatin1String("needs")) {
        appendFileLineAnchor(resource, QStringLiteral("config needs: %1").arg(value), lineNumber);
        return;
    }

    appendFileLineAnchor(resource,
                         QStringLiteral("config %1: %2").arg(state.currentListKey, value),
                         lineNumber);
}

void popJsonTextStructurePathClosures(TextStructureBeaconState &state, const QString &trimmed)
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

void appendTextStructureAnchorsFromLine(Resource &resource,
                                        const QFileInfo &fileInfo,
                                        const QString &line,
                                        int lineNumber,
                                        TextStructureBeaconState &state)
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
        popJsonTextStructurePathClosures(state, trimmed);
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

struct TextNamedEntryBeaconState {
    bool inJsonNamedEntryMap = false;
    bool inTomlNamedEntrySection = false;
};

bool isJsonNamedEntryMap(const QString &name)
{
    const QString normalized = name.toLower().replace(QLatin1Char('-'), QString())
                                   .replace(QLatin1Char('_'), QString());
    return normalized == QLatin1String("entries")
        || normalized == QLatin1String("items")
        || normalized == QLatin1String("markers")
        || normalized == QLatin1String("beacons")
        || normalized == QLatin1String("anchors")
        || normalized == QLatin1String("namedentries")
        || normalized == QLatin1String("nameditems");
}

bool isTomlNamedEntrySection(const QString &name)
{
    const QString sectionName = name.section(QLatin1Char('.'), -1).toLower().replace(QLatin1Char('-'), QString())
                                    .replace(QLatin1Char('_'), QString());
    return sectionName == QLatin1String("entries")
        || sectionName == QLatin1String("items")
        || sectionName == QLatin1String("markers")
        || sectionName == QLatin1String("beacons")
        || sectionName == QLatin1String("anchors")
        || sectionName == QLatin1String("namedentries")
        || sectionName == QLatin1String("nameditems");
}

void appendTextNamedEntryBeaconsFromLine(Resource &resource,
                                         const QFileInfo &fileInfo,
                                         const QString &line,
                                         int lineNumber,
                                         TextNamedEntryBeaconState &state)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
        return;
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (suffix == QLatin1String("json")) {
        static const QRegularExpression sectionPattern(QStringLiteral("^\"([^\"]+)\"\\s*:\\s*\\{"));
        static const QRegularExpression entryPattern(QStringLiteral("^\"([^\"]+)\"\\s*:"));

        const QRegularExpressionMatch sectionMatch = sectionPattern.match(trimmed);
        if (sectionMatch.hasMatch()) {
            state.inJsonNamedEntryMap = isJsonNamedEntryMap(sectionMatch.captured(1));
            return;
        }
        if (state.inJsonNamedEntryMap) {
            if (trimmed.startsWith(QLatin1Char('}'))) {
                state.inJsonNamedEntryMap = false;
                return;
            }
            const QRegularExpressionMatch entryMatch = entryPattern.match(trimmed);
            if (entryMatch.hasMatch()) {
                appendFileLineAnchor(resource,
                                     QStringLiteral("named entry: %1").arg(entryMatch.captured(1).trimmed()),
                                     lineNumber);
            }
        }
        return;
    }

    if (suffix == QLatin1String("toml")) {
        static const QRegularExpression sectionPattern(QStringLiteral("^\\[([^\\]]+)\\]$"));
        static const QRegularExpression entryPattern(QStringLiteral("^(?:\"([^\"]+)\"|([A-Za-z0-9_.-]+))\\s*="));

        const QRegularExpressionMatch sectionMatch = sectionPattern.match(trimmed);
        if (sectionMatch.hasMatch()) {
            state.inTomlNamedEntrySection = isTomlNamedEntrySection(sectionMatch.captured(1));
            return;
        }
        if (state.inTomlNamedEntrySection) {
            const QRegularExpressionMatch entryMatch = entryPattern.match(trimmed);
            if (entryMatch.hasMatch()) {
                const QString entry = entryMatch.captured(1).isEmpty()
                    ? entryMatch.captured(2)
                    : entryMatch.captured(1);
                appendFileLineAnchor(resource,
                                     QStringLiteral("named entry: %1").arg(entry.trimmed()),
                                     lineNumber);
            }
        }
        return;
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

bool looksLikeExplicitOrBareWebUrl(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        return true;
    }

    QString hostCandidate = trimmed;
    const int pathIndex = hostCandidate.indexOf(QLatin1Char('/'));
    if (pathIndex >= 0) {
        hostCandidate = hostCandidate.left(pathIndex);
    }
    const int queryIndex = hostCandidate.indexOf(QLatin1Char('?'));
    if (queryIndex >= 0) {
        hostCandidate = hostCandidate.left(queryIndex);
    }
    const int fragmentIndex = hostCandidate.indexOf(QLatin1Char('#'));
    if (fragmentIndex >= 0) {
        hostCandidate = hostCandidate.left(fragmentIndex);
    }
    const int portIndex = hostCandidate.indexOf(QLatin1Char(':'));
    if (portIndex >= 0) {
        hostCandidate = hostCandidate.left(portIndex);
    }

    return hostCandidate.startsWith(QLatin1String("www."), Qt::CaseInsensitive)
        || hostCandidate.contains(QLatin1Char('.'));
}

QUrl urlFromTabularCell(const QString &cell)
{
    const QString trimmed = cell.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    if (looksLikeExplicitOrBareWebUrl(trimmed)) {
        QUrl url = QUrl::fromUserInput(trimmedPlainTextUrl(trimmed));
        if (isIndexableWebUrl(url)) {
            return url;
        }
    }

    static const QRegularExpression urlPattern(QStringLiteral("https?://[^\\s<>\"]+"));
    const QRegularExpressionMatch match = urlPattern.match(trimmed);
    if (!match.hasMatch()) {
        return {};
    }
    const QUrl url = QUrl::fromUserInput(trimmedPlainTextUrl(match.captured(0)));
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
            link.lineNumber = lineIndex + 1;
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

QList<TabularUrlLink> tabularUrlLinksFromFile(const QFileInfo &fileInfo)
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

    return tabularUrlLinksFromDocument(QString::fromUtf8(bytes), delimiter.value());
}

QList<Resource> tabularUrlResourcesFromLinks(const QFileInfo &fileInfo, const QList<TabularUrlLink> &links)
{
    QList<Resource> resources;
    for (const TabularUrlLink &link : links) {
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

QString tabularUrlLineAnchorTarget(const TabularUrlLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

void appendTabularUrlSourceMetadata(Resource &sourceResource,
                                    const QList<TabularUrlLink> &links,
                                    const QList<Resource> &urlResources)
{
    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const TabularUrlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = tabularUrlLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("table row %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : anchorTarget;
        sourceResource.relations.append(relation);
    }
}

bool isJsonUrlCandidate(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 512 * 1024) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QLatin1String("json") || suffix == QLatin1String("jsonl");
}

bool isHarFileCandidate(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && fileInfo.size() <= 4 * 1024 * 1024
        && fileInfo.suffix().compare(QStringLiteral("har"), Qt::CaseInsensitive) == 0;
}

bool hasFileReferenceManifestFormat(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && fileInfo.size() <= 16 * 1024 * 1024
        && fileInfo.fileName().compare(QStringLiteral("compile_commands.json"), Qt::CaseInsensitive) == 0;
}

QString jsonPathLabel(QStringList path)
{
    path.removeAll(QString());
    return path.join(QLatin1Char('.')).trimmed();
}

QStringList jsonPathWithArrayIndex(QStringList path, int index)
{
    const QString suffix = QStringLiteral("[%1]").arg(index);
    if (path.isEmpty()) {
        path.append(suffix);
    } else {
        path.last().append(suffix);
    }
    return path;
}

QString jsonTitleFromObject(const QJsonObject &object)
{
    for (const QString &key : {
             QStringLiteral("title"),
             QStringLiteral("name"),
             QStringLiteral("label"),
             QStringLiteral("description"),
             QStringLiteral("summary")
         }) {
        const QString value = object.value(key).toString().trimmed();
        if (!value.isEmpty() && !isIndexableWebUrl(QUrl::fromUserInput(value))) {
            return collapsedWhitespace(value);
        }
    }
    return {};
}

int lineNumberForJsonUrl(const QString &text, const QString &rawUrl)
{
    const int index = text.indexOf(rawUrl);
    if (index < 0) {
        return -1;
    }
    return text.left(index).count(QLatin1Char('\n')) + 1;
}

int lineNumberForTextValue(const QString &text, const QString &value)
{
    int index = text.indexOf(value);
    if (index < 0) {
        QString escaped = value;
        escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        index = text.indexOf(escaped);
    }
    if (index < 0) {
        return -1;
    }
    return text.left(index).count(QLatin1Char('\n')) + 1;
}

int lineNumberForJsonPropertyValue(const QString &text, const QString &propertyName, const QString &value)
{
    const QRegularExpression pattern(
        QStringLiteral("\"%1\"\\s*:\\s*\"([^\"]*)\"").arg(QRegularExpression::escape(propertyName)));
    QRegularExpressionMatchIterator matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QString decoded = match.captured(1);
        decoded.replace(QStringLiteral("\\/"), QStringLiteral("/"));
        decoded.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        decoded.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        if (decoded == value) {
            return text.left(match.capturedStart(1)).count(QLatin1Char('\n')) + 1;
        }
    }
    return lineNumberForTextValue(text, value);
}

QString absoluteReferencedFilePath(const QFileInfo &referenceManifestFile,
                                   const QString &directory,
                                   const QString &path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    const QFileInfo pathInfo(trimmedPath);
    if (pathInfo.isAbsolute()) {
        return QDir::cleanPath(pathInfo.absoluteFilePath());
    }

    const QString basePath = directory.trimmed().isEmpty()
        ? referenceManifestFile.absolutePath()
        : directory.trimmed();
    return QDir::cleanPath(QFileInfo(QDir(basePath).filePath(trimmedPath)).absoluteFilePath());
}

QString displayReferencedFilePath(const QFileInfo &referenceManifestFile, const QString &referencedPath)
{
    const QString relativePath = QDir(referenceManifestFile.absolutePath()).relativeFilePath(referencedPath);
    if (!relativePath.isEmpty()) {
        return QDir::cleanPath(relativePath);
    }
    return QFileInfo(referencedPath).fileName();
}

QList<FileReferenceEntry> fileReferenceEntriesFromManifestFile(const QFileInfo &fileInfo)
{
    if (!hasFileReferenceManifestFormat(fileInfo)) {
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

    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isArray()) {
        return {};
    }

    const QString text = QString::fromUtf8(bytes);
    QList<FileReferenceEntry> entries;
    QStringList seenKeys;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString rawInputPath = object.value(QStringLiteral("file")).toString().trimmed();
        if (rawInputPath.isEmpty()) {
            continue;
        }

        FileReferenceEntry entry;
        entry.referencedPath = absoluteReferencedFilePath(fileInfo,
                                                          object.value(QStringLiteral("directory")).toString(),
                                                          rawInputPath);
        if (entry.referencedPath.isEmpty()) {
            continue;
        }

        entry.detail = object.value(QStringLiteral("output")).toString().trimmed();
        entry.displayPath = displayReferencedFilePath(fileInfo, entry.referencedPath);
        entry.lineNumber = lineNumberForJsonPropertyValue(text, QStringLiteral("file"), rawInputPath);

        const QString key = QStringLiteral("%1|%2").arg(entry.referencedPath, entry.detail);
        if (seenKeys.contains(key, Qt::CaseInsensitive)) {
            continue;
        }
        seenKeys.append(key);
        entries.append(entry);
    }

    return entries;
}

QString fileReferenceAnchorTarget(const FileReferenceEntry &entry)
{
    const QString display = entry.displayPath.trimmed().isEmpty()
        ? QFileInfo(entry.referencedPath).fileName()
        : entry.displayPath.trimmed();
    return entry.detail.trimmed().isEmpty()
        ? QStringLiteral("file reference: %1").arg(display)
        : QStringLiteral("file reference: %1 -> %2").arg(display, entry.detail.trimmed());
}

void appendFileReferenceMetadata(Resource &sourceResource, const QList<FileReferenceEntry> &entries)
{
    for (const FileReferenceEntry &entry : entries) {
        const QString anchorTarget = fileReferenceAnchorTarget(entry);
        if (entry.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, entry.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = QStringLiteral("file:%1").arg(entry.referencedPath);
        relation.label = QStringLiteral("file-reference");
        relation.note = entry.lineNumber > 0
            ? QStringLiteral("file reference line %1: %2").arg(entry.lineNumber).arg(anchorTarget)
            : anchorTarget;
        sourceResource.relations.append(relation);
    }
}

void appendJsonUrlLinkFromString(const QString &text,
                                 const QStringList &path,
                                 const QString &siblingTitle,
                                 int fallbackLineNumber,
                                 const QString &documentText,
                                 QStringList &seenUrls,
                                 QList<JsonUrlLink> &links)
{
    static const QRegularExpression urlPattern(QStringLiteral("https?://[^\\s<>\"]+"));
    QRegularExpressionMatchIterator matches = urlPattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString rawUrl = trimmedPlainTextUrl(match.captured(0));
        const QUrl url = QUrl::fromUserInput(rawUrl);
        if (!isIndexableWebUrl(url)) {
            continue;
        }

        const QString urlKey = url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);

        JsonUrlLink link;
        link.url = url;
        link.path = jsonPathLabel(path);
        link.lineNumber = fallbackLineNumber > 0 ? fallbackLineNumber : lineNumberForJsonUrl(documentText, rawUrl);
        link.title = siblingTitle.trimmed();
        if (link.title.isEmpty() && match.capturedStart() > 0) {
            link.title = titleFromTextBeforeUrl(text.left(match.capturedStart()));
        }
        if (link.title.isEmpty() && !link.path.isEmpty()) {
            link.title = link.path.section(QLatin1Char('.'), -1);
        }
        if (link.title.isEmpty()) {
            link.title = url.host().isEmpty() ? url.toDisplayString() : url.host();
        }
        links.append(link);
    }
}

void appendJsonUrlLinksFromValue(const QJsonValue &value,
                                 const QStringList &path,
                                 const QString &siblingTitle,
                                 int fallbackLineNumber,
                                 const QString &documentText,
                                 QStringList &seenUrls,
                                 QList<JsonUrlLink> &links)
{
    if (value.isString()) {
        appendJsonUrlLinkFromString(value.toString(),
                                    path,
                                    siblingTitle,
                                    fallbackLineNumber,
                                    documentText,
                                    seenUrls,
                                    links);
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            appendJsonUrlLinksFromValue(array.at(i),
                                        jsonPathWithArrayIndex(path, i),
                                        siblingTitle,
                                        fallbackLineNumber,
                                        documentText,
                                        seenUrls,
                                        links);
        }
        return;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QString objectTitle = jsonTitleFromObject(object);
        const QString effectiveTitle = objectTitle.isEmpty() ? siblingTitle : objectTitle;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            QStringList childPath = path;
            childPath.append(it.key());
            appendJsonUrlLinksFromValue(it.value(),
                                        childPath,
                                        effectiveTitle,
                                        fallbackLineNumber,
                                        documentText,
                                        seenUrls,
                                        links);
        }
    }
}

QList<JsonUrlLink> jsonUrlLinksFromFile(const QFileInfo &fileInfo)
{
    if (!isJsonUrlCandidate(fileInfo)) {
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
    QList<JsonUrlLink> links;
    QStringList seenUrls;
    const QString suffix = fileInfo.suffix().toLower();

    if (suffix == QLatin1String("jsonl")) {
        int lineNumber = 0;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            ++lineNumber;
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8());
            if (document.isNull()) {
                appendJsonUrlLinkFromString(trimmed,
                                            QStringList{QStringLiteral("line%1").arg(lineNumber)},
                                            QString(),
                                            lineNumber,
                                            text,
                                            seenUrls,
                                            links);
                continue;
            }
            appendJsonUrlLinksFromValue(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()),
                                        QStringList{QStringLiteral("line%1").arg(lineNumber)},
                                        QString(),
                                        lineNumber,
                                        text,
                                        seenUrls,
                                        links);
        }
        return links;
    }

    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (document.isNull()) {
        return {};
    }
    appendJsonUrlLinksFromValue(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()),
                                {},
                                QString(),
                                -1,
                                text,
                                seenUrls,
                                links);
    return links;
}

QList<Resource> jsonUrlResourcesFromLinks(const QFileInfo &fileInfo, const QList<JsonUrlLink> &links)
{
    QList<Resource> resources;
    for (const JsonUrlLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("json-url:%1:%2").arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("json-link"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        appendUnique(resource.aliases, link.path);
        resources.append(resource);
    }
    return resources;
}

QString jsonUrlLineAnchorTarget(const JsonUrlLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

void appendJsonUrlSourceMetadata(Resource &sourceResource,
                                 const QList<JsonUrlLink> &links,
                                 const QList<Resource> &urlResources)
{
    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const JsonUrlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = jsonUrlLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        if (link.lineNumber > 0 && !link.path.isEmpty()) {
            relation.note = QStringLiteral("json line %1 path %2: %3")
                                .arg(link.lineNumber)
                                .arg(link.path, anchorTarget);
        } else if (link.lineNumber > 0) {
            relation.note = QStringLiteral("json line %1: %2").arg(link.lineNumber).arg(anchorTarget);
        } else if (!link.path.isEmpty()) {
            relation.note = QStringLiteral("json path %1: %2").arg(link.path, anchorTarget);
        } else {
            relation.note = anchorTarget;
        }
        sourceResource.relations.append(relation);
    }
}

QDateTime dateTimeFromHarString(const QString &value)
{
    const QString text = value.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    QDateTime dateTime = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(text, Qt::ISODate);
    }
    return dateTime.isValid() ? dateTime.toUTC() : QDateTime();
}

QString titleForHarEntry(const QUrl &url, const QString &method, const QString &pageTitle)
{
    if (!pageTitle.trimmed().isEmpty()) {
        return collapsedWhitespace(pageTitle);
    }

    QString target = url.path().trimmed();
    if (target.isEmpty() || target == QLatin1String("/")) {
        target = url.host();
    }
    if (target.isEmpty()) {
        target = url.toDisplayString();
    }

    const QString normalizedMethod = method.trimmed().toUpper();
    return normalizedMethod.isEmpty()
        ? target
        : QStringLiteral("%1 %2").arg(normalizedMethod, target);
}

QList<HarEntryUrlTarget> harEntryUrlTargetsFromFile(const QFileInfo &fileInfo)
{
    if (!isHarFileCandidate(fileInfo)) {
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

    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        return {};
    }

    const QString text = QString::fromUtf8(bytes);
    const QJsonObject logObject = document.object().value(QStringLiteral("log")).toObject();
    if (logObject.isEmpty()) {
        return {};
    }

    QHash<QString, QString> pageTitlesById;
    const QJsonArray pages = logObject.value(QStringLiteral("pages")).toArray();
    for (const QJsonValue &pageValue : pages) {
        const QJsonObject page = pageValue.toObject();
        const QString id = page.value(QStringLiteral("id")).toString().trimmed();
        const QString title = page.value(QStringLiteral("title")).toString().trimmed();
        if (!id.isEmpty() && !title.isEmpty()) {
            pageTitlesById.insert(id, collapsedWhitespace(title));
        }
    }

    QList<HarEntryUrlTarget> targets;
    QStringList seenUrls;
    const QJsonArray entries = logObject.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &entryValue : entries) {
        const QJsonObject entry = entryValue.toObject();
        const QJsonObject request = entry.value(QStringLiteral("request")).toObject();
        const QString rawUrl = request.value(QStringLiteral("url")).toString().trimmed();
        const QUrl url = QUrl::fromUserInput(rawUrl);
        if (!isIndexableWebUrl(url)) {
            continue;
        }

        const QString urlKey = url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);

        const QJsonObject response = entry.value(QStringLiteral("response")).toObject();
        const QJsonObject content = response.value(QStringLiteral("content")).toObject();

        HarEntryUrlTarget target;
        target.url = url;
        target.pageRef = entry.value(QStringLiteral("pageref")).toString().trimmed();
        target.pageTitle = pageTitlesById.value(target.pageRef);
        target.method = request.value(QStringLiteral("method")).toString().trimmed().toUpper();
        target.status = response.value(QStringLiteral("status")).toInt(-1);
        target.mimeType = content.value(QStringLiteral("mimeType")).toString().trimmed();
        target.startedAt = dateTimeFromHarString(entry.value(QStringLiteral("startedDateTime")).toString());
        target.lineNumber = lineNumberForJsonUrl(text, rawUrl);
        target.title = titleForHarEntry(url, target.method, target.pageTitle);
        targets.append(target);
    }

    return targets;
}

QList<Resource> harUrlResourcesFromTargets(const QFileInfo &fileInfo, const QList<HarEntryUrlTarget> &targets)
{
    QList<Resource> resources;
    for (const HarEntryUrlTarget &target : targets) {
        const QString urlKey = target.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("har-url:%1:%2").arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = target.title;
        resource.location = urlKey;
        resource.updatedAt = target.startedAt.isValid() ? target.startedAt : fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, target.url);
        appendUnique(resource.tags, QStringLiteral("har"));
        appendUnique(resource.tags, QStringLiteral("web-capture"));
        if (!target.method.isEmpty()) {
            appendUnique(resource.tags, QStringLiteral("http-%1").arg(target.method.toLower()));
            appendUnique(resource.aliases, target.method);
        }
        if (target.status > 0) {
            appendUnique(resource.tags, QStringLiteral("http-%1").arg(target.status));
            appendUnique(resource.aliases, QStringLiteral("status %1").arg(target.status));
        }
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        appendUnique(resource.aliases, target.pageRef);
        appendUnique(resource.aliases, target.pageTitle);
        appendUnique(resource.aliases, target.mimeType);
        resource.content = QStringList{
            target.method,
            target.status > 0 ? QStringLiteral("status %1").arg(target.status) : QString(),
            target.pageRef,
            target.pageTitle,
            target.mimeType,
            target.url.toDisplayString()
        }.join(QLatin1Char(' ')).trimmed();
        resources.append(resource);
    }
    return resources;
}

QString harUrlLineAnchorTarget(const HarEntryUrlTarget &target)
{
    const QString title = target.title.trimmed().isEmpty()
        ? (target.url.host().isEmpty() ? target.url.toDisplayString() : target.url.host())
        : target.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, target.url.toString(QUrl::FullyEncoded));
}

void appendHarSourceMetadata(Resource &sourceResource,
                             const QList<HarEntryUrlTarget> &targets,
                             const QList<Resource> &urlResources)
{
    const int count = std::min(targets.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const HarEntryUrlTarget &target = targets.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = harUrlLineAnchorTarget(target);
        if (target.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, target.lineNumber);
        }

        QStringList details;
        if (!target.method.isEmpty()) {
            details.append(target.method);
        }
        if (target.status > 0) {
            details.append(QStringLiteral("status %1").arg(target.status));
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        if (target.lineNumber > 0 && !details.isEmpty()) {
            relation.note = QStringLiteral("har line %1 %2: %3")
                                .arg(target.lineNumber)
                                .arg(details.join(QLatin1Char(' ')), anchorTarget);
        } else if (target.lineNumber > 0) {
            relation.note = QStringLiteral("har line %1: %2").arg(target.lineNumber).arg(anchorTarget);
        } else if (!details.isEmpty()) {
            relation.note = QStringLiteral("har %1: %2").arg(details.join(QLatin1Char(' ')), anchorTarget);
        } else {
            relation.note = QStringLiteral("har: %1").arg(anchorTarget);
        }
        sourceResource.relations.append(relation);
    }
}

QList<HtmlLink> textLinkUrlBeaconsFromFile(const QFileInfo &fileInfo)
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString text = QString::fromUtf8(file.readAll());
    const QStringList lines = text.split(QLatin1Char('\n'));
    const QRegularExpression inlineLinkPattern(
        QStringLiteral("(?<!!)\\[([^\\]]+)\\]\\((https?://[^\\s\\)]+)\\)"));
    const QRegularExpression autolinkPattern(QStringLiteral("<(https?://[^\\s<>]+)>"));
    const QRegularExpression referenceDefinitionPattern(
        QStringLiteral("^\\s{0,3}\\[([^\\]]+)\\]:\\s*(?:<([^>]+)>|([^\\s]+))(?:\\s+.*)?$"));
    const QRegularExpression referenceUsePattern(QStringLiteral("(?<!!)\\[([^\\]]+)\\]\\[([^\\]]*)\\]"));

    QList<HtmlLink> links;
    QHash<QString, ReferenceStyleTextLinkDefinition> referenceStyleTextLinkDefinitions;
    QList<ReferenceStyleTextLinkUse> referenceStyleTextLinkUses;
    QStringList seenUrls;
    auto appendLink = [&](const QString &rawUrl, const QString &rawTitle, int lineNumber) {
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
        link.lineNumber = lineNumber;
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

        const QRegularExpressionMatch referenceDefinitionMatch = referenceDefinitionPattern.match(line);
        if (referenceDefinitionMatch.hasMatch()) {
            const QString rawUrl = referenceDefinitionMatch.captured(2).isEmpty()
                ? referenceDefinitionMatch.captured(3)
                : referenceDefinitionMatch.captured(2);
            const QUrl url = QUrl::fromUserInput(trimmedPlainTextUrl(rawUrl));
            if (isIndexableWebUrl(url)) {
                ReferenceStyleTextLinkDefinition definition;
                definition.url = url;
                definition.lineNumber = lineNumber;
                referenceStyleTextLinkDefinitions.insert(
                    normalizedReferenceStyleTextLinkId(referenceDefinitionMatch.captured(1)),
                    definition);
            }
            continue;
        }

        QRegularExpressionMatchIterator inlineMatches = inlineLinkPattern.globalMatch(line);
        while (inlineMatches.hasNext()) {
            const QRegularExpressionMatch match = inlineMatches.next();
            appendLink(match.captured(2), normalizedPlainTextFromLine(match.captured(1)), lineNumber);
        }

        QRegularExpressionMatchIterator autolinkMatches = autolinkPattern.globalMatch(line);
        while (autolinkMatches.hasNext()) {
            const QRegularExpressionMatch match = autolinkMatches.next();
            appendLink(match.captured(1), QString(), lineNumber);
        }

        QRegularExpressionMatchIterator referenceMatches = referenceUsePattern.globalMatch(line);
        while (referenceMatches.hasNext()) {
            const QRegularExpressionMatch match = referenceMatches.next();
            const QString title = normalizedPlainTextFromLine(match.captured(1));
            const QString id = match.captured(2).trimmed().isEmpty()
                ? title
                : match.captured(2);
            ReferenceStyleTextLinkUse use;
            use.id = normalizedReferenceStyleTextLinkId(id);
            use.title = title;
            use.lineNumber = lineNumber;
            referenceStyleTextLinkUses.append(use);
        }
    }

    for (const ReferenceStyleTextLinkUse &use : referenceStyleTextLinkUses) {
        const auto definitionIt = referenceStyleTextLinkDefinitions.constFind(use.id);
        if (definitionIt == referenceStyleTextLinkDefinitions.cend()) {
            continue;
        }
        appendLink(definitionIt->url.toString(QUrl::FullyEncoded),
                   use.title,
                   use.lineNumber > 0 ? use.lineNumber : definitionIt->lineNumber);
    }

    return links;
}

QList<Resource> textLinkUrlResourcesFromLinks(const QFileInfo &fileInfo, const QList<HtmlLink> &links)
{
    QList<Resource> resources;
    for (const HtmlLink &link : links) {
        Resource resource;
        resource.id = QStringLiteral("text-link:%1:%2")
                          .arg(normalizedPath(fileInfo),
                               link.url.toString(QUrl::FullyEncoded));
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = link.url.toString(QUrl::FullyEncoded);
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("text-link"));
        resources.append(resource);
    }
    return resources;
}

QStringList urlKeysFromTextLinkUrlBeacons(const QList<HtmlLink> &links)
{
    QStringList keys;
    for (const HtmlLink &link : links) {
        const QString key = link.url.toString(QUrl::FullyEncoded);
        if (!keys.contains(key, Qt::CaseInsensitive)) {
            keys.append(key);
        }
    }
    return keys;
}

QList<TextUrlLink> textUrlLinksExcludingUrls(const QList<TextUrlLink> &links, const QStringList &excludedUrlKeys)
{
    QList<TextUrlLink> filtered;
    for (const TextUrlLink &link : links) {
        if (!excludedUrlKeys.contains(link.url.toString(QUrl::FullyEncoded), Qt::CaseInsensitive)) {
            filtered.append(link);
        }
    }
    return filtered;
}

QString urlLinkAnchorTarget(const HtmlLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

void appendTextLinkUrlSourceMetadata(Resource &sourceResource,
                                     const QList<HtmlLink> &links,
                                     const QList<Resource> &urlResources)
{
    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const HtmlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = urlLinkAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("text line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : anchorTarget;
        sourceResource.relations.append(relation);
    }
}

QList<Resource> htmlLinkResourcesFromLinks(const QFileInfo &fileInfo, const QList<HtmlLink> &links)
{
    QList<Resource> resources;
    for (const HtmlLink &link : links) {
        Resource resource;
        resource.id = QStringLiteral("html-link:%1:%2")
                          .arg(normalizedPath(fileInfo),
                               link.url.toString(QUrl::FullyEncoded));
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = link.url.toString(QUrl::FullyEncoded);
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("html-link"));
        if (isMhtmlFile(fileInfo)) {
            appendUnique(resource.tags, QStringLiteral("web-capture-url"));
        }
        resources.append(resource);
    }
    return resources;
}

void appendHtmlUrlSourceMetadata(Resource &sourceResource,
                                 const QList<HtmlLink> &links,
                                 const QList<Resource> &urlResources,
                                 bool appendSourceLineAnchors)
{
    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const HtmlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = urlLinkAnchorTarget(link);
        if (appendSourceLineAnchors && link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = QStringLiteral("html link: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QList<HtmlLink> bookmarkLinksFromHtmlFile(const QFileInfo &fileInfo)
{
    const std::optional<QString> html = htmlDocumentFromFile(fileInfo);
    if (!html.has_value() || !looksLikeBookmarkExport(html.value())) {
        return {};
    }
    return htmlLinksFromDocument(html.value());
}

QList<Resource> bookmarkResourcesFromLinks(const QFileInfo &fileInfo, const QList<HtmlLink> &links)
{
    QList<Resource> resources;
    for (const HtmlLink &link : links) {
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

void appendBookmarkExportSourceMetadata(Resource &sourceResource,
                                        const QList<HtmlLink> &links,
                                        const QList<Resource> &urlResources)
{
    if (!links.isEmpty()) {
        appendUnique(sourceResource.tags, QStringLiteral("bookmark"));
    }

    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const HtmlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = urlLinkAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("bookmark line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : QStringLiteral("bookmark: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QList<BrowserBookmarkLink> browserBookmarkLinksFromJsonFile(const QFileInfo &fileInfo)
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0')) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    const QString text = QString::fromUtf8(bytes);
    QList<BrowserBookmarkLink> links = browserBookmarkLinksFromJsonDocument(document);
    for (BrowserBookmarkLink &link : links) {
        link.lineNumber = lineNumberForJsonPropertyValue(text, QStringLiteral("url"), link.rawUrl);
    }
    return links;
}

QList<BrowserBookmarkLink> deduplicatedBrowserBookmarkLinks(const QList<BrowserBookmarkLink> &links)
{
    QList<BrowserBookmarkLink> deduplicated;
    QStringList seenUrls;
    for (const BrowserBookmarkLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);
        deduplicated.append(link);
    }
    return deduplicated;
}

QString browserBookmarkLineAnchorTarget(const BrowserBookmarkLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

QList<Resource> browserBookmarkResourcesFromLinks(const QFileInfo &fileInfo,
                                                  const QList<BrowserBookmarkLink> &links)
{
    QList<Resource> resources;
    for (const BrowserBookmarkLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
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

void appendBrowserBookmarkSourceMetadata(Resource &sourceResource,
                                         const QList<BrowserBookmarkLink> &links,
                                         const QList<Resource> &urlResources)
{
    if (!links.isEmpty()) {
        appendUnique(sourceResource.tags, QStringLiteral("bookmark"));
        appendUnique(sourceResource.tags, QStringLiteral("browser-bookmark"));
    }

    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const BrowserBookmarkLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = browserBookmarkLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("browser bookmark line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : QStringLiteral("browser bookmark: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QString xbelBookmarkLineAnchorTarget(const XbelBookmarkLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

QList<Resource> xbelBookmarkResourcesFromLinks(const QFileInfo &fileInfo,
                                               const QList<XbelBookmarkLink> &links)
{
    QList<Resource> resources;
    for (const XbelBookmarkLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("xbel-bookmark:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("bookmark"));
        appendUnique(resource.tags, QStringLiteral("xbel"));
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

void appendXbelBookmarkSourceMetadata(Resource &sourceResource,
                                      const QList<XbelBookmarkLink> &links,
                                      const QList<Resource> &urlResources)
{
    if (!links.isEmpty()) {
        appendUnique(sourceResource.tags, QStringLiteral("bookmark"));
        appendUnique(sourceResource.tags, QStringLiteral("xbel"));
    }

    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const XbelBookmarkLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = xbelBookmarkLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("xbel line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : QStringLiteral("xbel: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

bool isWarcFileCandidate(const QFileInfo &fileInfo)
{
    return !fileInfo.isDir()
        && fileInfo.size() <= 16 * 1024 * 1024
        && fileInfo.suffix().compare(QStringLiteral("warc"), Qt::CaseInsensitive) == 0;
}

QString htmlFromWarcResponsePayload(const QString &payload)
{
    const QString trimmed = payload.trimmed();
    if (!trimmed.startsWith(QLatin1String("HTTP/"), Qt::CaseInsensitive)) {
        return trimmed.contains(QStringLiteral("<html"), Qt::CaseInsensitive) ? payload : QString();
    }

    int separatorLength = 0;
    const int headerEnd = mimeHeaderSeparatorIndex(payload, &separatorLength);
    if (headerEnd < 0) {
        return {};
    }

    const QString httpHeaders = payload.left(headerEnd);
    const QString contentType = mimeHeaderValue(httpHeaders, QStringLiteral("Content-Type"));
    const QString body = payload.mid(headerEnd + separatorLength);
    if (!contentType.startsWith(QLatin1String("text/html"), Qt::CaseInsensitive)
        && !body.contains(QStringLiteral("<html"), Qt::CaseInsensitive)) {
        return {};
    }
    return body;
}

QList<WarcResponseUrlTarget> warcResponseUrlTargetsFromFile(const QFileInfo &fileInfo)
{
    if (!isWarcFileCandidate(fileInfo)) {
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

    const QString text = QString::fromLatin1(bytes);
    QList<WarcResponseUrlTarget> targets;
    QStringList seenUrls;
    int cursor = 0;
    int recordIndex = 0;
    int lineScanCursor = 0;
    int currentLineNumber = 1;
    while (cursor < text.size()) {
        const int recordStart = text.indexOf(QStringLiteral("WARC/1."), cursor);
        if (recordStart < 0) {
            break;
        }
        while (lineScanCursor < recordStart && lineScanCursor < text.size()) {
            if (text.at(lineScanCursor) == QLatin1Char('\n')) {
                ++currentLineNumber;
            }
            ++lineScanCursor;
        }

        int separatorLength = 0;
        const int relativeHeaderEnd = mimeHeaderSeparatorIndex(text.mid(recordStart), &separatorLength);
        if (relativeHeaderEnd < 0) {
            break;
        }

        ++recordIndex;
        const int headerEnd = recordStart + relativeHeaderEnd;
        const QString headers = text.mid(recordStart, relativeHeaderEnd);
        const int bodyStart = headerEnd + separatorLength;
        int nextRecordStart = text.indexOf(QStringLiteral("\nWARC/1."), bodyStart);
        if (nextRecordStart < 0) {
            nextRecordStart = text.size();
        }
        const QString payload = text.mid(bodyStart, nextRecordStart - bodyStart);
        cursor = nextRecordStart + 1;

        if (mimeHeaderValue(headers, QStringLiteral("WARC-Type")).compare(QStringLiteral("response"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QUrl url = QUrl::fromUserInput(mimeHeaderValue(headers, QStringLiteral("WARC-Target-URI")));
        if (!isIndexableWebUrl(url)) {
            continue;
        }

        const QString urlKey = url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }

        const QString html = htmlFromWarcResponsePayload(payload);
        if (html.isEmpty()) {
            continue;
        }

        seenUrls.append(urlKey);
        targets.append(WarcResponseUrlTarget{url, html, recordIndex, currentLineNumber});
    }
    return targets;
}

QList<Resource> warcUrlResourcesFromTargets(const QFileInfo &fileInfo, const QList<WarcResponseUrlTarget> &targets)
{
    QList<Resource> resources;
    for (const WarcResponseUrlTarget &target : targets) {
        const QString urlKey = target.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("warc-url:%1:%2").arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = target.url.host().isEmpty() ? target.url.toDisplayString() : target.url.host();
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        applyHtmlDocumentMetadata(resource, target.html, resource.title, target.url);
        appendUnique(resource.tags, QStringLiteral("warc"));
        appendUnique(resource.tags, QStringLiteral("web-capture"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        resources.append(resource);
    }
    return resources;
}

void appendWarcSourceMetadata(Resource &sourceResource,
                              const QList<WarcResponseUrlTarget> &targets,
                              const QList<Resource> &urlResources)
{
    appendUnique(sourceResource.tags, QStringLiteral("warc"));
    appendUnique(sourceResource.tags, QStringLiteral("web-capture"));

    const int count = std::min(targets.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const WarcResponseUrlTarget &target = targets.at(i);
        const Resource &urlResource = urlResources.at(i);
        HtmlLink htmlLink;
        htmlLink.url = target.url;
        htmlLink.title = urlResource.title;
        const QString anchorTarget = urlLinkAnchorTarget(htmlLink);
        if (target.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, target.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = target.lineNumber > 0
            ? QStringLiteral("warc line %1 record %2: %3").arg(target.lineNumber).arg(target.recordIndex).arg(anchorTarget)
            : QStringLiteral("warc record %1: %2").arg(target.recordIndex).arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QList<Resource> textUrlResourcesFromLinks(const QFileInfo &fileInfo, const QList<TextUrlLink> &links)
{
    QList<Resource> resources;
    for (const TextUrlLink &link : links) {
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

QList<Resource> robotsSitemapLineResourcesFromLinks(const QFileInfo &fileInfo, const QList<TextUrlLink> &links)
{
    QList<Resource> resources;
    for (const TextUrlLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("robots-sitemap:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("robots"));
        appendUnique(resource.tags, QStringLiteral("sitemap"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        appendUnique(resource.aliases, fileInfo.fileName());
        resources.append(resource);
    }
    return resources;
}

QString emailUrlLineAnchorTarget(const EmailUrlLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

QList<Resource> emailUrlResourcesFromLinks(const QFileInfo &fileInfo,
                                           const EmailMessageMetadata &metadata)
{
    QList<Resource> resources;
    for (const EmailUrlLink &link : metadata.links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("email-link:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = link.title;
        resource.location = urlKey;
        resource.updatedAt = fileInfo.lastModified().toUTC();
        appendWebUrlMetadata(resource, link.url);
        appendUnique(resource.tags, QStringLiteral("email-link"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
        appendUnique(resource.aliases, metadata.subject);
        appendUnique(resource.aliases, metadata.from);
        appendUnique(resource.aliases, metadata.to);
        appendUnique(resource.aliases, metadata.cc);
        appendUnique(resource.aliases, metadata.date);
        resources.append(resource);
    }
    return resources;
}

void appendEmailHeaderLineAnchor(Resource &resource, const QString &label, const QString &value, int lineNumber)
{
    if (value.trimmed().isEmpty() || lineNumber <= 0) {
        return;
    }
    appendFileLineAnchor(resource, QStringLiteral("email %1: %2").arg(label, value), lineNumber);
}

void appendEmailSourceMetadata(Resource &sourceResource,
                               const EmailMessageMetadata &metadata,
                               const QList<Resource> &urlResources)
{
    appendUnique(sourceResource.tags, QStringLiteral("email"));
    appendUnique(sourceResource.aliases, metadata.subject);
    appendUnique(sourceResource.aliases, metadata.from);
    appendUnique(sourceResource.aliases, metadata.to);
    appendUnique(sourceResource.aliases, metadata.cc);
    appendUnique(sourceResource.aliases, metadata.date);
    sourceResource.content = metadata.content;

    appendEmailHeaderLineAnchor(sourceResource, QStringLiteral("subject"), metadata.subject, metadata.subjectLineNumber);
    appendEmailHeaderLineAnchor(sourceResource, QStringLiteral("from"), metadata.from, metadata.fromLineNumber);
    appendEmailHeaderLineAnchor(sourceResource, QStringLiteral("to"), metadata.to, metadata.toLineNumber);
    appendEmailHeaderLineAnchor(sourceResource, QStringLiteral("cc"), metadata.cc, metadata.ccLineNumber);
    appendEmailHeaderLineAnchor(sourceResource, QStringLiteral("date"), metadata.date, metadata.dateLineNumber);

    const int count = std::min(metadata.links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const EmailUrlLink &link = metadata.links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = emailUrlLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("email line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : QStringLiteral("email: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QString textUrlLineAnchorTarget(const TextUrlLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

void appendPlainTextUrlSourceMetadata(Resource &sourceResource,
                                      const QList<TextUrlLink> &links,
                                      const QList<Resource> &urlResources)
{
    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const TextUrlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = textUrlLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("text line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : anchorTarget;
        sourceResource.relations.append(relation);
    }
}

void appendRobotsSitemapLineSourceMetadata(Resource &sourceResource,
                                           const QList<TextUrlLink> &links,
                                           const QList<Resource> &urlResources)
{
    if (!links.isEmpty()) {
        appendUnique(sourceResource.tags, QStringLiteral("robots"));
        appendUnique(sourceResource.tags, QStringLiteral("sitemap"));
    }

    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const TextUrlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = textUrlLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("robots line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : QStringLiteral("robots: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QList<OpmlLink> opmlLinksFromFile(const QFileInfo &fileInfo)
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    return opmlLinksFromDocument(file.readAll());
}

QList<OpmlLink> deduplicatedOpmlLinks(const QList<OpmlLink> &links)
{
    QList<OpmlLink> deduplicated;
    QStringList seenUrls;
    for (const OpmlLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);
        deduplicated.append(link);
    }
    return deduplicated;
}

QString opmlLineAnchorTarget(const OpmlLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

QList<Resource> opmlResourcesFromLinks(const QFileInfo &fileInfo, const QList<OpmlLink> &links)
{
    QList<Resource> resources;
    for (const OpmlLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
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

void appendOpmlSourceMetadata(Resource &sourceResource,
                              const QList<OpmlLink> &links,
                              const QList<Resource> &urlResources)
{
    if (!links.isEmpty()) {
        appendUnique(sourceResource.tags, QStringLiteral("opml"));
    }

    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const OpmlLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = opmlLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("opml line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : QStringLiteral("opml: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QList<FeedEntryLink> feedLinksFromXmlFile(const QFileInfo &fileInfo)
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    return feedLinksFromXmlDocument(file.readAll());
}

QList<FeedEntryLink> deduplicatedFeedEntryLinks(const QList<FeedEntryLink> &links)
{
    QList<FeedEntryLink> deduplicated;
    QStringList seenUrls;
    for (const FeedEntryLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);
        deduplicated.append(link);
    }
    return deduplicated;
}

QString feedEntryLineAnchorTarget(const FeedEntryLink &link)
{
    const QString title = link.title.trimmed().isEmpty()
        ? (link.url.host().isEmpty() ? link.url.toDisplayString() : link.url.host())
        : link.title.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, link.url.toString(QUrl::FullyEncoded));
}

QList<Resource> feedResourcesFromLinks(const QFileInfo &fileInfo, const QList<FeedEntryLink> &links)
{
    QList<Resource> resources;
    for (const FeedEntryLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
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

void appendFeedXmlSourceMetadata(Resource &sourceResource,
                                 const QList<FeedEntryLink> &links,
                                 const QList<Resource> &urlResources)
{
    if (!links.isEmpty()) {
        appendUnique(sourceResource.tags, QStringLiteral("feed"));
        for (const FeedEntryLink &link : links) {
            appendUnique(sourceResource.aliases, link.feedTitle);
        }
    }

    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const FeedEntryLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = feedEntryLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("feed line %1: %2").arg(link.lineNumber).arg(anchorTarget)
            : QStringLiteral("feed: %1").arg(anchorTarget);
        sourceResource.relations.append(relation);
    }
}

QList<SitemapLink> sitemapLinksFromXmlFile(const QFileInfo &fileInfo)
{
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    return sitemapLinksFromXmlDocument(file.readAll());
}

QList<SitemapLink> deduplicatedSitemapLinks(const QList<SitemapLink> &links)
{
    QList<SitemapLink> deduplicated;
    QStringList seenUrls;
    for (const SitemapLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
            continue;
        }
        seenUrls.append(urlKey);
        deduplicated.append(link);
    }
    return deduplicated;
}

QString sitemapLinkTitle(const SitemapLink &link)
{
    QString title = link.url.path().isEmpty() || link.url.path() == QLatin1String("/")
        ? link.url.host()
        : link.url.path().section(QLatin1Char('/'), -1);
    if (title.isEmpty()) {
        title = link.url.toDisplayString();
    }
    return title;
}

QString sitemapLineAnchorTarget(const SitemapLink &link)
{
    return QStringLiteral("url: %1 -> %2")
        .arg(sitemapLinkTitle(link), link.url.toString(QUrl::FullyEncoded));
}

QList<Resource> sitemapResourcesFromLinks(const QFileInfo &fileInfo, const QList<SitemapLink> &links)
{
    QList<Resource> resources;
    for (const SitemapLink &link : links) {
        const QString urlKey = link.url.toString(QUrl::FullyEncoded);
        Resource resource;
        resource.id = QStringLiteral("sitemap:%1:%2")
                          .arg(normalizedPath(fileInfo), urlKey);
        resource.kind = ResourceKind::Url;
        resource.title = sitemapLinkTitle(link);
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

void appendSitemapXmlSourceMetadata(Resource &sourceResource,
                                    const QList<SitemapLink> &links,
                                    const QList<Resource> &urlResources)
{
    if (!links.isEmpty()) {
        appendUnique(sourceResource.tags, QStringLiteral("sitemap"));
        if (std::any_of(links.cbegin(), links.cend(), [](const SitemapLink &link) {
                return link.sitemapIndexEntry;
            })) {
            appendUnique(sourceResource.tags, QStringLiteral("sitemap-index"));
        }
    }

    const int count = std::min(links.size(), urlResources.size());
    for (int i = 0; i < count; ++i) {
        const SitemapLink &link = links.at(i);
        const Resource &urlResource = urlResources.at(i);
        const QString anchorTarget = sitemapLineAnchorTarget(link);
        if (link.lineNumber > 0) {
            appendFileLineAnchor(sourceResource, anchorTarget, link.lineNumber);
        }

        ResourceRelation relation;
        relation.sourceResourceId = sourceResource.id;
        relation.targetResourceId = urlResource.id;
        relation.label = QStringLiteral("links-to");
        relation.note = link.lineNumber > 0
            ? QStringLiteral("%1 line %2: %3")
                  .arg(link.sitemapIndexEntry ? QStringLiteral("sitemap-index") : QStringLiteral("sitemap"))
                  .arg(link.lineNumber)
                  .arg(anchorTarget)
            : QStringLiteral("%1: %2")
                  .arg(link.sitemapIndexEntry ? QStringLiteral("sitemap-index") : QStringLiteral("sitemap"),
                       anchorTarget);
        sourceResource.relations.append(relation);
    }
}

bool isIcalendarFileCandidate(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir() || fileInfo.size() > 512 * 1024) {
        return false;
    }

    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QLatin1String("ics") || suffix == QLatin1String("ical");
}

QString unescapeIcalendarText(const QString &value)
{
    QString unescaped;
    unescaped.reserve(value.size());
    bool escaping = false;
    for (const QChar ch : value) {
        if (escaping) {
            if (ch == QLatin1Char('n') || ch == QLatin1Char('N')) {
                unescaped.append(QLatin1Char(' '));
            } else {
                unescaped.append(ch);
            }
            escaping = false;
            continue;
        }

        if (ch == QLatin1Char('\\')) {
            escaping = true;
        } else {
            unescaped.append(ch);
        }
    }
    if (escaping) {
        unescaped.append(QLatin1Char('\\'));
    }
    return collapsedWhitespace(unescaped);
}

QString formatIcalendarDateTime(QString value)
{
    value = value.trimmed();
    if (value.size() < 8) {
        return value;
    }

    const QString compact = value.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive)
        ? value.left(value.size() - 1)
        : value;
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4})(\\d{2})(\\d{2})(?:T(\\d{2})(\\d{2})(\\d{2})?)?$"));
    const QRegularExpressionMatch match = pattern.match(compact);
    if (!match.hasMatch()) {
        return value;
    }

    QString formatted = QStringLiteral("%1-%2-%3")
                            .arg(match.captured(1), match.captured(2), match.captured(3));
    if (!match.captured(4).isEmpty()) {
        formatted.append(QStringLiteral(" %1:%2").arg(match.captured(4), match.captured(5)));
        if (!match.captured(6).isEmpty()) {
            formatted.append(QStringLiteral(":%1").arg(match.captured(6)));
        }
        if (value.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive)) {
            formatted.append(QStringLiteral(" UTC"));
        }
    }
    return formatted;
}

struct CalendarLogicalLine {
    QString text;
    int lineNumber = -1;
};

QList<CalendarLogicalLine> unfoldedIcalendarLines(const QString &text)
{
    QList<CalendarLogicalLine> lines;
    int lineNumber = 0;
    for (QString line : text.split(QLatin1Char('\n'))) {
        ++lineNumber;
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (!lines.isEmpty()
            && !line.isEmpty()
            && (line.front() == QLatin1Char(' ') || line.front() == QLatin1Char('\t'))) {
            lines.last().text.append(line.mid(1));
            continue;
        }
        lines.append(CalendarLogicalLine{line, lineNumber});
    }
    return lines;
}

QString icalendarPropertyName(const QString &line)
{
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        return {};
    }
    QString name = line.left(colon).section(QLatin1Char(';'), 0, 0).trimmed();
    return name.toUpper();
}

QString icalendarPropertyValue(const QString &line)
{
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        return {};
    }
    return unescapeIcalendarText(line.mid(colon + 1).trimmed());
}

QString trimmedIcalendarUrlCandidate(QString value)
{
    value = value.trimmed();
    while (!value.isEmpty()
           && (value.endsWith(QLatin1Char('.'))
               || value.endsWith(QLatin1Char(','))
               || value.endsWith(QLatin1Char(';'))
               || value.endsWith(QLatin1Char(')'))
               || value.endsWith(QLatin1Char(']')))) {
        value.chop(1);
    }
    return value;
}

void appendCalendarUrlReference(CalendarEvent &event,
                                const QString &rawUrl,
                                int lineNumber,
                                QStringList &seenUrls)
{
    const QUrl url = QUrl::fromUserInput(trimmedIcalendarUrlCandidate(rawUrl));
    if (!isIndexableWebUrl(url)) {
        return;
    }

    const QString urlKey = url.toString(QUrl::FullyEncoded);
    if (seenUrls.contains(urlKey, Qt::CaseInsensitive)) {
        return;
    }

    seenUrls.append(urlKey);
    event.urls.append(CalendarUrlReference{url, lineNumber});
}

void appendCalendarUrlsFromValue(CalendarEvent &event,
                                 const QString &value,
                                 int lineNumber,
                                 QStringList &seenUrls)
{
    static const QRegularExpression urlPattern(QStringLiteral("https?://[^\\s<>\"]+"),
                                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator matches = urlPattern.globalMatch(value);
    while (matches.hasNext()) {
        appendCalendarUrlReference(event, matches.next().captured(0), lineNumber, seenUrls);
    }
}

QList<CalendarEvent> calendarEventsFromFile(const QFileInfo &fileInfo)
{
    if (!isIcalendarFileCandidate(fileInfo)) {
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

    QList<CalendarEvent> events;
    CalendarEvent currentEvent;
    QStringList seenUrls;
    bool inEvent = false;

    for (const CalendarLogicalLine &line : unfoldedIcalendarLines(QString::fromUtf8(bytes))) {
        const QString name = icalendarPropertyName(line.text);
        const QString value = icalendarPropertyValue(line.text);
        if (name == QLatin1String("BEGIN") && value.compare(QStringLiteral("VEVENT"), Qt::CaseInsensitive) == 0) {
            currentEvent = CalendarEvent{};
            currentEvent.eventLineNumber = line.lineNumber;
            seenUrls.clear();
            inEvent = true;
            continue;
        }
        if (!inEvent) {
            continue;
        }
        if (name == QLatin1String("END") && value.compare(QStringLiteral("VEVENT"), Qt::CaseInsensitive) == 0) {
            events.append(currentEvent);
            currentEvent = CalendarEvent{};
            seenUrls.clear();
            inEvent = false;
            continue;
        }

        if (name == QLatin1String("SUMMARY")) {
            currentEvent.summary = value;
            currentEvent.summaryLineNumber = line.lineNumber;
        } else if (name == QLatin1String("DTSTART")) {
            currentEvent.startsAt = formatIcalendarDateTime(value);
            currentEvent.startLineNumber = line.lineNumber;
        } else if (name == QLatin1String("DTEND")) {
            currentEvent.endsAt = formatIcalendarDateTime(value);
            currentEvent.endLineNumber = line.lineNumber;
        } else if (name == QLatin1String("LOCATION")) {
            currentEvent.location = value;
            currentEvent.locationLineNumber = line.lineNumber;
            appendCalendarUrlsFromValue(currentEvent, value, line.lineNumber, seenUrls);
        } else if (name == QLatin1String("URL")) {
            appendCalendarUrlReference(currentEvent, value, line.lineNumber, seenUrls);
        } else if (name == QLatin1String("DESCRIPTION")) {
            appendCalendarUrlsFromValue(currentEvent, value, line.lineNumber, seenUrls);
        }
    }

    if (inEvent) {
        events.append(currentEvent);
    }
    return events;
}

QList<Resource> calendarUrlResourcesFromEvents(const QFileInfo &fileInfo, const QList<CalendarEvent> &events)
{
    QList<Resource> resources;
    for (const CalendarEvent &event : events) {
        for (const CalendarUrlReference &reference : event.urls) {
            const QString urlKey = reference.url.toString(QUrl::FullyEncoded);
            Resource resource;
            resource.id = QStringLiteral("calendar-url:%1:%2:%3")
                              .arg(normalizedPath(fileInfo), QString::number(reference.lineNumber), urlKey);
            resource.kind = ResourceKind::Url;
            resource.title = event.summary.trimmed().isEmpty()
                ? (reference.url.host().isEmpty() ? reference.url.toDisplayString() : reference.url.host())
                : event.summary;
            resource.location = urlKey;
            resource.updatedAt = fileInfo.lastModified().toUTC();
            appendWebUrlMetadata(resource, reference.url);
            appendUnique(resource.tags, QStringLiteral("calendar-link"));
            appendUnique(resource.aliases, fileInfo.completeBaseName());
            appendUnique(resource.aliases, event.summary);
            appendUnique(resource.aliases, event.location);
            appendUnique(resource.aliases, event.startsAt);
            resources.append(resource);
        }
    }
    return resources;
}

QString calendarUrlLineAnchorTarget(const CalendarEvent &event, const CalendarUrlReference &reference)
{
    const QString title = event.summary.trimmed().isEmpty()
        ? (reference.url.host().isEmpty() ? reference.url.toDisplayString() : reference.url.host())
        : event.summary.trimmed();
    return QStringLiteral("url: %1 -> %2").arg(title, reference.url.toString(QUrl::FullyEncoded));
}

void appendCalendarSourceMetadata(Resource &sourceResource,
                                  const QList<CalendarEvent> &events,
                                  const QList<Resource> &urlResources)
{
    int urlResourceIndex = 0;
    for (const CalendarEvent &event : events) {
        appendUnique(sourceResource.tags, QStringLiteral("calendar"));
        appendUnique(sourceResource.aliases, event.summary);
        appendUnique(sourceResource.aliases, event.location);

        if (!event.summary.isEmpty()) {
            appendFileLineAnchor(sourceResource,
                                 QStringLiteral("calendar event: %1").arg(event.summary),
                                 event.summaryLineNumber > 0 ? event.summaryLineNumber : event.eventLineNumber);
        }
        if (!event.startsAt.isEmpty()) {
            appendFileLineAnchor(sourceResource,
                                 QStringLiteral("calendar start: %1").arg(event.startsAt),
                                 event.startLineNumber);
        }
        if (!event.endsAt.isEmpty()) {
            appendFileLineAnchor(sourceResource,
                                 QStringLiteral("calendar end: %1").arg(event.endsAt),
                                 event.endLineNumber);
        }
        if (!event.location.isEmpty()) {
            appendFileLineAnchor(sourceResource,
                                 QStringLiteral("calendar location: %1").arg(event.location),
                                 event.locationLineNumber);
        }

        for (const CalendarUrlReference &reference : event.urls) {
            if (urlResourceIndex >= urlResources.size()) {
                break;
            }

            const Resource &urlResource = urlResources.at(urlResourceIndex);
            const QString anchorTarget = calendarUrlLineAnchorTarget(event, reference);
            if (reference.lineNumber > 0) {
                appendFileLineAnchor(sourceResource, anchorTarget, reference.lineNumber);
            }

            ResourceRelation relation;
            relation.sourceResourceId = sourceResource.id;
            relation.targetResourceId = urlResource.id;
            relation.label = QStringLiteral("links-to");
            relation.note = reference.lineNumber > 0
                ? QStringLiteral("calendar line %1: %2").arg(reference.lineNumber).arg(anchorTarget)
                : anchorTarget;
            sourceResource.relations.append(relation);
            ++urlResourceIndex;
        }
    }
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
    QList<Resource> derivedResources;
    QList<Resource> browserBookmarkResources;

    if (isPathOnlyPackageContainerFile(fileInfo)) {
        resources.append(primary);
        return resources;
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (primary.kind == ResourceKind::Url
        && (suffix == QLatin1String("html") || suffix == QLatin1String("htm") || isMhtmlFile(fileInfo))) {
        const QList<HtmlLink> bookmarkLinks = bookmarkLinksFromHtmlFile(fileInfo);
        const QList<Resource> bookmarkResources = bookmarkResourcesFromLinks(fileInfo, bookmarkLinks);
        if (bookmarkResources.isEmpty()) {
            const QList<HtmlLink> htmlLinks = htmlLinksFromFile(fileInfo);
            const QList<Resource> htmlResources = htmlLinkResourcesFromLinks(fileInfo, htmlLinks);
            appendHtmlUrlSourceMetadata(primary, htmlLinks, htmlResources, !isMhtmlFile(fileInfo));
            derivedResources.append(htmlResources);
        } else {
            appendBookmarkExportSourceMetadata(primary, bookmarkLinks, bookmarkResources);
            derivedResources.append(bookmarkResources);
        }
    } else if (isBrowserBookmarkJsonCandidate(fileInfo)) {
        const QList<BrowserBookmarkLink> browserBookmarkLinks =
            deduplicatedBrowserBookmarkLinks(browserBookmarkLinksFromJsonFile(fileInfo));
        browserBookmarkResources = browserBookmarkResourcesFromLinks(fileInfo, browserBookmarkLinks);
        appendBrowserBookmarkSourceMetadata(primary, browserBookmarkLinks, browserBookmarkResources);
        derivedResources.append(browserBookmarkResources);
    } else if (isXbelBookmarkCandidate(fileInfo)) {
        const QList<XbelBookmarkLink> xbelBookmarkLinks =
            deduplicatedXbelBookmarkLinks(xbelBookmarkLinksFromFile(fileInfo));
        const QList<Resource> xbelBookmarkResources =
            xbelBookmarkResourcesFromLinks(fileInfo, xbelBookmarkLinks);
        appendXbelBookmarkSourceMetadata(primary, xbelBookmarkLinks, xbelBookmarkResources);
        derivedResources.append(xbelBookmarkResources);
    } else if (suffix == QLatin1String("opml")) {
        const QList<OpmlLink> opmlLinks = deduplicatedOpmlLinks(opmlLinksFromFile(fileInfo));
        const QList<Resource> opmlResources = opmlResourcesFromLinks(fileInfo, opmlLinks);
        appendOpmlSourceMetadata(primary, opmlLinks, opmlResources);
        derivedResources.append(opmlResources);
    } else if (isFeedXmlCandidate(fileInfo)) {
        const QList<FeedEntryLink> feedLinks = deduplicatedFeedEntryLinks(feedLinksFromXmlFile(fileInfo));
        const QList<Resource> feedResources = feedResourcesFromLinks(fileInfo, feedLinks);
        appendFeedXmlSourceMetadata(primary, feedLinks, feedResources);
        derivedResources.append(feedResources);
        const QList<SitemapLink> sitemapLinks = deduplicatedSitemapLinks(sitemapLinksFromXmlFile(fileInfo));
        const QList<Resource> sitemapResources = sitemapResourcesFromLinks(fileInfo, sitemapLinks);
        appendSitemapXmlSourceMetadata(primary, sitemapLinks, sitemapResources);
        derivedResources.append(sitemapResources);
    }
    const bool emailCandidate = primary.kind == ResourceKind::File && isEmailFileCandidate(fileInfo);
    if (emailCandidate) {
        const EmailMessageMetadata metadata = emailMessageMetadataFromFile(fileInfo);
        const QList<Resource> emailResources = emailUrlResourcesFromLinks(fileInfo, metadata);
        appendEmailSourceMetadata(primary, metadata, emailResources);
        derivedResources.append(emailResources);
    }
    const bool robotsTxtCandidate = primary.kind == ResourceKind::File && isRobotsTxtCandidate(fileInfo);
    if (robotsTxtCandidate) {
        const QList<TextUrlLink> robotsLinks = robotsSitemapLineLinksFromFile(fileInfo);
        const QList<Resource> robotsResources = robotsSitemapLineResourcesFromLinks(fileInfo, robotsLinks);
        appendRobotsSitemapLineSourceMetadata(primary, robotsLinks, robotsResources);
        derivedResources.append(robotsResources);
    }
    const bool tabularCandidate = primary.kind == ResourceKind::File && tabularDelimiterForFile(fileInfo).has_value();
    const bool specializedUrlResourceCandidate =
        robotsTxtCandidate
        || emailCandidate
        || tabularCandidate
        || (primary.kind == ResourceKind::File && isIcalendarFileCandidate(fileInfo))
        || (primary.kind == ResourceKind::File && isBrowserBookmarkJsonCandidate(fileInfo))
        || (primary.kind == ResourceKind::File && isJsonUrlCandidate(fileInfo))
        || (primary.kind == ResourceKind::File && isHarFileCandidate(fileInfo))
        || (primary.kind == ResourceKind::File && isWarcFileCandidate(fileInfo))
        || (primary.kind == ResourceKind::File && isXbelBookmarkCandidate(fileInfo))
        || (primary.kind == ResourceKind::File && isFeedXmlCandidate(fileInfo))
        || (primary.kind == ResourceKind::File && fileInfo.suffix().compare(QStringLiteral("opml"), Qt::CaseInsensitive) == 0);
    if (primary.kind == ResourceKind::File && !specializedUrlResourceCandidate && isTextUrlResourceCandidate(fileInfo)) {
        const QList<HtmlLink> textLinkUrlBeacons = textLinkUrlBeaconsFromFile(fileInfo);
        const QList<Resource> textLinkUrlResources = textLinkUrlResourcesFromLinks(fileInfo, textLinkUrlBeacons);
        appendTextLinkUrlSourceMetadata(primary, textLinkUrlBeacons, textLinkUrlResources);
        derivedResources.append(textLinkUrlResources);

        const QList<TextUrlLink> textLinks = textUrlLinksExcludingUrls(
            textUrlLinksFromFile(fileInfo),
            urlKeysFromTextLinkUrlBeacons(textLinkUrlBeacons));
        const QList<Resource> textResources = textUrlResourcesFromLinks(fileInfo, textLinks);
        appendPlainTextUrlSourceMetadata(primary, textLinks, textResources);
        derivedResources.append(textResources);
    }
    if (tabularCandidate) {
        const QList<TabularUrlLink> tabularLinks = tabularUrlLinksFromFile(fileInfo);
        const QList<Resource> tabularResources = tabularUrlResourcesFromLinks(fileInfo, tabularLinks);
        appendTabularUrlSourceMetadata(primary, tabularLinks, tabularResources);
        derivedResources.append(tabularResources);
    }
    if (primary.kind == ResourceKind::File && hasFileReferenceManifestFormat(fileInfo)) {
        appendFileReferenceMetadata(primary, fileReferenceEntriesFromManifestFile(fileInfo));
    }
    if (primary.kind == ResourceKind::File && isIcalendarFileCandidate(fileInfo)) {
        const QList<CalendarEvent> calendarEvents = calendarEventsFromFile(fileInfo);
        const QList<Resource> calendarResources = calendarUrlResourcesFromEvents(fileInfo, calendarEvents);
        appendCalendarSourceMetadata(primary, calendarEvents, calendarResources);
        derivedResources.append(calendarResources);
    }
    if (primary.kind == ResourceKind::File
        && isJsonUrlCandidate(fileInfo)
        && browserBookmarkResources.isEmpty()) {
        const QList<JsonUrlLink> jsonLinks = jsonUrlLinksFromFile(fileInfo);
        const QList<Resource> jsonResources = jsonUrlResourcesFromLinks(fileInfo, jsonLinks);
        appendJsonUrlSourceMetadata(primary, jsonLinks, jsonResources);
        derivedResources.append(jsonResources);
    }
    if (primary.kind == ResourceKind::File && isHarFileCandidate(fileInfo)) {
        const QList<HarEntryUrlTarget> harTargets = harEntryUrlTargetsFromFile(fileInfo);
        const QList<Resource> harResources = harUrlResourcesFromTargets(fileInfo, harTargets);
        appendHarSourceMetadata(primary, harTargets, harResources);
        derivedResources.append(harResources);
    }
    if (primary.kind == ResourceKind::File && isWarcFileCandidate(fileInfo)) {
        const QList<WarcResponseUrlTarget> warcTargets = warcResponseUrlTargetsFromFile(fileInfo);
        const QList<Resource> warcResources = warcUrlResourcesFromTargets(fileInfo, warcTargets);
        appendWarcSourceMetadata(primary, warcTargets, warcResources);
        derivedResources.append(warcResources);
    }
    if (primary.kind == ResourceKind::Pdf) {
        const QList<PdfUriLink> pdfLinks = pdfUriLinksFromFile(fileInfo);
        const QList<Resource> pdfResources = pdfUriResourcesFromLinks(fileInfo, pdfLinks);
        appendPdfUriSourceMetadata(primary, pdfLinks, pdfResources);
        derivedResources.append(pdfResources);
    }
    if (primary.kind == ResourceKind::File && isBrowserHistorySqliteCandidate(fileInfo)) {
        const QList<BrowserHistoryLink> historyLinks = browserHistoryLinksFromSqliteFile(fileInfo);
        const QList<Resource> historyResources = browserHistoryResourcesFromLinks(fileInfo, historyLinks);
        appendBrowserHistorySourceMetadata(primary, historyLinks, historyResources);
        derivedResources.append(historyResources);
    }
    if (primary.kind == ResourceKind::File && isFirefoxPlacesSqliteCandidate(fileInfo)) {
        const QList<BrowserHistoryLink> placesLinks = firefoxPlacesLinksFromSqliteFile(fileInfo);
        const QList<Resource> placesResources = browserHistoryResourcesFromLinks(fileInfo, placesLinks);
        appendBrowserHistorySourceMetadata(primary, placesLinks, placesResources);
        derivedResources.append(placesResources);
    }
    if (primary.kind == ResourceKind::File && isGenericSqliteCandidate(fileInfo)) {
        appendGenericSqliteMetadata(primary, fileInfo);
    }
    resources.append(primary);
    resources.append(derivedResources);
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
    if (isPathOnlyPackageContainerFile(fileInfo)) {
        appendUnique(resource.tags, QStringLiteral("path-only"));
        appendUnique(resource.tags, QStringLiteral("package-container"));
        return resource;
    }
    if (resource.kind == ResourceKind::Pdf) {
        applyPdfMetadata(resource, fileInfo);
    } else if (resource.kind == ResourceKind::Url) {
        const QString suffix = fileInfo.suffix().toLower();
        if (suffix == QLatin1String("html") || suffix == QLatin1String("htm") || isMhtmlFile(fileInfo)) {
            applyHtmlMetadata(resource, fileInfo);
        } else {
            applyUrlMetadata(resource, fileInfo);
        }
    } else if (resource.kind == ResourceKind::File && isPlainTextContentFile(fileInfo)) {
        applyPlainTextMetadata(resource, fileInfo);
    }
    return resource;
}

QList<Resource> DirectoryLibrarySource::bookmarkResourcesFromHtmlFile(const QFileInfo &fileInfo) const
{
    return bookmarkResourcesFromLinks(fileInfo, bookmarkLinksFromHtmlFile(fileInfo));
}

QList<Resource> DirectoryLibrarySource::browserBookmarkResourcesFromJsonFile(const QFileInfo &fileInfo) const
{
    return browserBookmarkResourcesFromLinks(
        fileInfo,
        deduplicatedBrowserBookmarkLinks(browserBookmarkLinksFromJsonFile(fileInfo)));
}

QList<Resource> DirectoryLibrarySource::opmlResourcesFromFile(const QFileInfo &fileInfo) const
{
    return opmlResourcesFromLinks(fileInfo, deduplicatedOpmlLinks(opmlLinksFromFile(fileInfo)));
}

QList<Resource> DirectoryLibrarySource::feedResourcesFromXmlFile(const QFileInfo &fileInfo) const
{
    return feedResourcesFromLinks(fileInfo, deduplicatedFeedEntryLinks(feedLinksFromXmlFile(fileInfo)));
}

QList<Resource> DirectoryLibrarySource::sitemapResourcesFromXmlFile(const QFileInfo &fileInfo) const
{
    return sitemapResourcesFromLinks(fileInfo, deduplicatedSitemapLinks(sitemapLinksFromXmlFile(fileInfo)));
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
        appendPdfPageAnchor(resource, QStringLiteral("Page %1").arg(page), page);
    }

    const QList<PdfObject> objects = pdfObjectsFromText(pdfText);
    resource.content = pdfContentTextFromObjects(objects);
    const QHash<int, int> pageNumbers = pdfPageNumbersByObjectNumber(objects);
    const QHash<QString, int> namedDestinations = pdfNamedDestinationPageObjectNumbers(objects);
    appendPdfOutlineAnchors(resource, objects, pageNumbers, namedDestinations);

    QHash<int, QString> objectsByNumber;
    for (const PdfObject &object : objects) {
        objectsByNumber.insert(object.number, object.body);
    }

    for (const PdfObject &object : objects) {
        const int page = pageNumbers.value(object.number, -1);
        if (page <= 0) {
            continue;
        }

        const QList<int> annotationRefs = pdfAnnotationRefsFromPage(object.body);
        for (const int ref : annotationRefs) {
            const auto annotationIt = objectsByNumber.constFind(ref);
            if (annotationIt != objectsByNumber.constEnd()) {
                appendPdfRegionAnchor(resource, annotationIt.value(), page);
            }
        }
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
    appendFileLineAnchor(resource,
                         QStringLiteral("url: %1 -> %2").arg(resource.title, resource.location),
                         shortcutUrlLineNumberFromText(text));
    const int titleLineNumber = shortcutTitleLineNumberFromText(text);
    if (titleLineNumber > 0) {
        appendFileLineAnchor(resource,
                             QStringLiteral("shortcut title: %1").arg(resource.title),
                             titleLineNumber);
    }

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
    const std::optional<QString> html = htmlDocumentFromFile(fileInfo);
    if (!html.has_value()) {
        resource.kind = ResourceKind::File;
        return;
    }
    applyHtmlDocumentMetadata(resource, html.value(), fileInfo.fileName(), std::nullopt);
    if (isMhtmlFile(fileInfo)) {
        appendUnique(resource.tags, QStringLiteral("web-capture"));
        appendUnique(resource.aliases, fileInfo.completeBaseName());
    }
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
    const bool textStructureBeacons = hasTextStructureBeaconFormat(fileInfo);
    const bool workflowConfigText = hasWorkflowConfigTextBeaconFormat(fileInfo);
    const bool pipelineConfigText = hasPipelineConfigTextBeaconFormat(fileInfo);
    const bool configStyleText = hasConfigStyleTextBeaconFormat(fileInfo);
    const bool ruleEntryText = hasRuleEntryTextBeaconFormat(fileInfo);
    const bool containerText = hasContainerTextBeaconFormat(fileInfo);
    const std::optional<QChar> tabularDelimiter = tabularDelimiterForFile(fileInfo);
    bool tabularHeaderAnchorsAdded = false;
    TextNamedEntryBeaconState textNamedEntryBeaconState;
    TextStructureBeaconState textStructureState;
    WorkflowConfigBeaconState workflowConfigBeaconState;
    PipelineConfigBeaconState pipelineConfigBeaconState;
    int lineNumber = 0;
    TextFrontmatterState frontmatterState;
    QStringList contentLines;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        ++lineNumber;
        if (consumeTextFrontmatterLine(resource, line, lineNumber, frontmatterState)) {
            continue;
        }

        const QString contentLine = normalizedPlainTextFromLine(line);
        if (!contentLine.isEmpty()) {
            contentLines.append(contentLine);
        }
        appendTextConventionLineMetadata(resource, fileInfo, line, lineNumber);
        appendTodoFixmeNoteTextBeaconAnchorsFromLine(resource, line, lineNumber);
        appendGenericTextBeaconAnchorsFromLine(resource, line, lineNumber);
        appendTextNamedEntryBeaconsFromLine(resource, fileInfo, line, lineNumber, textNamedEntryBeaconState);
        if (configStyleText) {
            appendConfigStyleTextBeaconsFromLine(resource, line, lineNumber);
        }
        if (ruleEntryText) {
            appendRuleEntryTextBeaconsFromLine(resource, line, lineNumber);
        }
        if (containerText) {
            appendContainerTextBeaconsFromLine(resource, line, lineNumber);
        }
        if (workflowConfigText) {
            appendWorkflowConfigBeaconsFromLine(resource, line, lineNumber, workflowConfigBeaconState);
        }
        if (pipelineConfigText) {
            appendPipelineConfigBeaconsFromLine(resource, line, lineNumber, pipelineConfigBeaconState);
        }
        if (textStructureBeacons) {
            appendTextStructureAnchorsFromLine(resource, fileInfo, line, lineNumber, textStructureState);
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
