#include "pinloom/core/PdfAnnotatedCopy.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <zlib.h>

namespace Pinloom {

namespace {

constexpr char kPresentationVersion[] = "sumatra-annotated-copy-v1";
constexpr qsizetype kMaximumDecodedPdfStreamBytes = 256 * 1024 * 1024;

enum class PdfValueKind {
    Invalid,
    Null,
    Boolean,
    Number,
    Name,
    String,
    Array,
    Dictionary,
    Reference,
    Keyword,
};

struct PdfValue {
    PdfValueKind kind = PdfValueKind::Invalid;
    QByteArray scalar;
    bool boolean = false;
    QVector<PdfValue> array;
    QMap<QByteArray, PdfValue> dictionary;
    int objectNumber = -1;
    int generation = 0;

    static PdfValue nullValue()
    {
        PdfValue value;
        value.kind = PdfValueKind::Null;
        return value;
    }

    static PdfValue numberValue(const QByteArray &number)
    {
        PdfValue value;
        value.kind = PdfValueKind::Number;
        value.scalar = number;
        return value;
    }

    static PdfValue nameValue(const QByteArray &name)
    {
        PdfValue value;
        value.kind = PdfValueKind::Name;
        value.scalar = name;
        return value;
    }

    static PdfValue stringValue(const QByteArray &string)
    {
        PdfValue value;
        value.kind = PdfValueKind::String;
        value.scalar = string;
        return value;
    }

    static PdfValue arrayValue(QVector<PdfValue> values = {})
    {
        PdfValue value;
        value.kind = PdfValueKind::Array;
        value.array = std::move(values);
        return value;
    }

    static PdfValue dictionaryValue(QMap<QByteArray, PdfValue> values = {})
    {
        PdfValue value;
        value.kind = PdfValueKind::Dictionary;
        value.dictionary = std::move(values);
        return value;
    }

    static PdfValue referenceValue(int number, int referenceGeneration = 0)
    {
        PdfValue value;
        value.kind = PdfValueKind::Reference;
        value.objectNumber = number;
        value.generation = referenceGeneration;
        return value;
    }
};

struct PdfObject {
    int objectNumber = -1;
    int generation = 0;
    PdfValue value;
    QByteArray stream;
    bool hasStream = false;
};

struct XrefEntry {
    int type = 0;
    qint64 offset = -1;
    int generation = 0;
    int objectStreamNumber = -1;
    int objectStreamIndex = -1;
};

struct XrefSection {
    QHash<int, XrefEntry> entries;
    PdfValue trailer;
    qint64 previousOffset = -1;
    qint64 hybridXrefOffset = -1;
};

bool isPdfWhitespace(char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return value == 0 || value == 9 || value == 10 || value == 12
        || value == 13 || value == 32;
}

bool isPdfDelimiter(char ch)
{
    return isPdfWhitespace(ch)
        || ch == '(' || ch == ')' || ch == '<' || ch == '>'
        || ch == '[' || ch == ']' || ch == '{' || ch == '}'
        || ch == '/' || ch == '%';
}

bool isIntegerToken(const QByteArray &token, qint64 *number = nullptr)
{
    if (token.isEmpty()) {
        return false;
    }
    bool ok = false;
    const qint64 parsed = token.toLongLong(&ok);
    if (ok && number) {
        *number = parsed;
    }
    return ok && !token.contains('.') && !token.contains('e') && !token.contains('E');
}

bool isNumberToken(const QByteArray &token)
{
    if (token.isEmpty()) {
        return false;
    }
    bool ok = false;
    token.toDouble(&ok);
    return ok;
}

class PdfLexer {
public:
    explicit PdfLexer(const QByteArray &data,
                      qsizetype position = 0,
                      qsizetype end = -1)
        : data_(data)
        , position_(std::max<qsizetype>(0, position))
        , end_(end < 0 ? data.size() : std::min(end, data.size()))
    {
    }

    qsizetype position() const { return position_; }
    void setPosition(qsizetype position)
    {
        position_ = std::clamp(position, qsizetype(0), end_);
    }

    void skipWhitespaceAndComments()
    {
        while (position_ < end_) {
            if (isPdfWhitespace(data_.at(position_))) {
                ++position_;
                continue;
            }
            if (data_.at(position_) == '%') {
                while (position_ < end_
                       && data_.at(position_) != '\r'
                       && data_.at(position_) != '\n') {
                    ++position_;
                }
                continue;
            }
            break;
        }
    }

    QByteArray readToken()
    {
        skipWhitespaceAndComments();
        if (position_ >= end_) {
            return {};
        }
        if (position_ + 1 < end_) {
            const QByteArray pair = data_.mid(position_, 2);
            if (pair == "<<" || pair == ">>") {
                position_ += 2;
                return pair;
            }
        }
        const char first = data_.at(position_);
        if (first == '[' || first == ']' || first == '<' || first == '>'
            || first == '(' || first == ')' || first == '/') {
            ++position_;
            return QByteArray(1, first);
        }
        const qsizetype start = position_;
        while (position_ < end_ && !isPdfDelimiter(data_.at(position_))) {
            ++position_;
        }
        return data_.mid(start, position_ - start);
    }

    bool parseValue(PdfValue *value, QString *error, int depth = 0)
    {
        if (!value) {
            return false;
        }
        if (depth > 128) {
            if (error) *error = QStringLiteral("PDF object nesting is too deep");
            return false;
        }
        skipWhitespaceAndComments();
        if (position_ >= end_) {
            if (error) *error = QStringLiteral("Unexpected end of PDF object");
            return false;
        }

        if (data_.at(position_) == '[') {
            ++position_;
            QVector<PdfValue> values;
            while (true) {
                skipWhitespaceAndComments();
                if (position_ >= end_) {
                    if (error) *error = QStringLiteral("Unterminated PDF array");
                    return false;
                }
                if (data_.at(position_) == ']') {
                    ++position_;
                    *value = PdfValue::arrayValue(std::move(values));
                    return true;
                }
                PdfValue child;
                if (!parseValue(&child, error, depth + 1)) {
                    return false;
                }
                values.append(std::move(child));
            }
        }

        if (position_ + 1 < end_
            && data_.at(position_) == '<'
            && data_.at(position_ + 1) == '<') {
            position_ += 2;
            QMap<QByteArray, PdfValue> dictionary;
            while (true) {
                skipWhitespaceAndComments();
                if (position_ + 1 < end_
                    && data_.at(position_) == '>'
                    && data_.at(position_ + 1) == '>') {
                    position_ += 2;
                    *value = PdfValue::dictionaryValue(std::move(dictionary));
                    return true;
                }
                QByteArray key;
                if (!parseName(&key, error)) {
                    if (error && error->isEmpty()) {
                        *error = QStringLiteral("PDF dictionary key is not a name");
                    }
                    return false;
                }
                PdfValue child;
                if (!parseValue(&child, error, depth + 1)) {
                    return false;
                }
                dictionary.insert(key, std::move(child));
            }
        }

        if (data_.at(position_) == '/') {
            QByteArray name;
            if (!parseName(&name, error)) {
                return false;
            }
            *value = PdfValue::nameValue(name);
            return true;
        }
        if (data_.at(position_) == '(') {
            QByteArray string;
            if (!parseLiteralString(&string, error)) {
                return false;
            }
            *value = PdfValue::stringValue(string);
            return true;
        }
        if (data_.at(position_) == '<') {
            QByteArray string;
            if (!parseHexString(&string, error)) {
                return false;
            }
            *value = PdfValue::stringValue(string);
            return true;
        }

        const QByteArray token = readToken();
        if (token == "true" || token == "false") {
            value->kind = PdfValueKind::Boolean;
            value->boolean = token == "true";
            return true;
        }
        if (token == "null") {
            *value = PdfValue::nullValue();
            return true;
        }
        if (isNumberToken(token)) {
            const qsizetype afterNumber = position_;
            const QByteArray generationToken = readToken();
            qint64 generation = -1;
            if (isIntegerToken(generationToken, &generation)) {
                const QByteArray referenceToken = readToken();
                qint64 objectNumber = -1;
                if (referenceToken == "R"
                    && isIntegerToken(token, &objectNumber)
                    && objectNumber >= 0
                    && objectNumber <= std::numeric_limits<int>::max()
                    && generation >= 0
                    && generation <= std::numeric_limits<int>::max()) {
                    *value = PdfValue::referenceValue(
                        static_cast<int>(objectNumber), static_cast<int>(generation));
                    return true;
                }
            }
            position_ = afterNumber;
            *value = PdfValue::numberValue(token);
            return true;
        }
        if (token.isEmpty()) {
            if (error) *error = QStringLiteral("Unable to read PDF token");
            return false;
        }
        value->kind = PdfValueKind::Keyword;
        value->scalar = token;
        return true;
    }

private:
    bool parseName(QByteArray *name, QString *error)
    {
        skipWhitespaceAndComments();
        if (position_ >= end_ || data_.at(position_) != '/') {
            if (error) *error = QStringLiteral("Expected PDF name");
            return false;
        }
        ++position_;
        QByteArray decoded;
        while (position_ < end_ && !isPdfDelimiter(data_.at(position_))) {
            const char ch = data_.at(position_++);
            if (ch == '#' && position_ + 1 < end_) {
                bool ok = false;
                const int byte = data_.mid(position_, 2).toInt(&ok, 16);
                if (ok) {
                    decoded.append(static_cast<char>(byte));
                    position_ += 2;
                    continue;
                }
            }
            decoded.append(ch);
        }
        *name = decoded;
        return true;
    }

    bool parseLiteralString(QByteArray *string, QString *error)
    {
        if (position_ >= end_ || data_.at(position_) != '(') {
            return false;
        }
        ++position_;
        int nesting = 1;
        QByteArray decoded;
        while (position_ < end_) {
            char ch = data_.at(position_++);
            if (ch == '\\') {
                if (position_ >= end_) break;
                ch = data_.at(position_++);
                switch (ch) {
                case 'n': decoded.append('\n'); break;
                case 'r': decoded.append('\r'); break;
                case 't': decoded.append('\t'); break;
                case 'b': decoded.append('\b'); break;
                case 'f': decoded.append('\f'); break;
                case '\r':
                    if (position_ < end_ && data_.at(position_) == '\n') ++position_;
                    break;
                case '\n': break;
                default:
                    if (ch >= '0' && ch <= '7') {
                        int number = ch - '0';
                        int digits = 1;
                        while (digits < 3 && position_ < end_
                               && data_.at(position_) >= '0'
                               && data_.at(position_) <= '7') {
                            number = number * 8 + (data_.at(position_) - '0');
                            ++position_;
                            ++digits;
                        }
                        decoded.append(static_cast<char>(number & 0xff));
                    } else {
                        decoded.append(ch);
                    }
                    break;
                }
                continue;
            }
            if (ch == '(') {
                ++nesting;
                decoded.append(ch);
                continue;
            }
            if (ch == ')') {
                --nesting;
                if (nesting == 0) {
                    *string = decoded;
                    return true;
                }
                decoded.append(ch);
                continue;
            }
            decoded.append(ch);
        }
        if (error) *error = QStringLiteral("Unterminated PDF literal string");
        return false;
    }

    bool parseHexString(QByteArray *string, QString *error)
    {
        if (position_ >= end_ || data_.at(position_) != '<') {
            return false;
        }
        ++position_;
        QByteArray digits;
        while (position_ < end_ && data_.at(position_) != '>') {
            const char ch = data_.at(position_++);
            if (!isPdfWhitespace(ch)) digits.append(ch);
        }
        if (position_ >= end_) {
            if (error) *error = QStringLiteral("Unterminated PDF hexadecimal string");
            return false;
        }
        ++position_;
        if (digits.size() % 2 != 0) digits.append('0');
        QByteArray decoded;
        decoded.reserve(digits.size() / 2);
        for (qsizetype index = 0; index < digits.size(); index += 2) {
            bool ok = false;
            const int byte = digits.mid(index, 2).toInt(&ok, 16);
            if (!ok) {
                if (error) *error = QStringLiteral("Invalid PDF hexadecimal string");
                return false;
            }
            decoded.append(static_cast<char>(byte));
        }
        *string = decoded;
        return true;
    }

    const QByteArray &data_;
    qsizetype position_ = 0;
    qsizetype end_ = 0;
};

const PdfValue *dictionaryValue(const PdfValue &dictionary, const QByteArray &key)
{
    if (dictionary.kind != PdfValueKind::Dictionary) return nullptr;
    const auto found = dictionary.dictionary.constFind(key);
    return found == dictionary.dictionary.cend() ? nullptr : &found.value();
}

std::optional<qint64> integerValue(const PdfValue &value)
{
    if (value.kind != PdfValueKind::Number) return std::nullopt;
    qint64 number = 0;
    return isIntegerToken(value.scalar, &number)
        ? std::optional<qint64>(number)
        : std::nullopt;
}

std::optional<double> realValue(const PdfValue &value)
{
    if (value.kind != PdfValueKind::Number) return std::nullopt;
    bool ok = false;
    const double number = value.scalar.toDouble(&ok);
    return ok && std::isfinite(number)
        ? std::optional<double>(number)
        : std::nullopt;
}

std::optional<qint64> dictionaryInteger(const PdfValue &dictionary,
                                        const QByteArray &key)
{
    const PdfValue *value = dictionaryValue(dictionary, key);
    return value ? integerValue(*value) : std::nullopt;
}

QByteArray dictionaryName(const PdfValue &dictionary, const QByteArray &key)
{
    const PdfValue *value = dictionaryValue(dictionary, key);
    return value && value->kind == PdfValueKind::Name ? value->scalar : QByteArray{};
}

bool inflatePdfBytes(const QByteArray &compressed, QByteArray *decoded, QString *error)
{
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        if (error) *error = QStringLiteral("Unable to initialize PDF Flate decoder");
        return false;
    }

    QByteArray output;
    constexpr int ChunkSize = 64 * 1024;
    char chunk[ChunkSize];
    qsizetype inputOffset = 0;
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        if (stream.avail_in == 0 && inputOffset < compressed.size()) {
            const qsizetype available = std::min<qsizetype>(
                compressed.size() - inputOffset,
                std::numeric_limits<uInt>::max());
            stream.next_in = reinterpret_cast<Bytef *>(
                const_cast<char *>(compressed.constData() + inputOffset));
            stream.avail_in = static_cast<uInt>(available);
            inputOffset += available;
        }
        stream.next_out = reinterpret_cast<Bytef *>(chunk);
        stream.avail_out = ChunkSize;
        result = inflate(&stream, Z_NO_FLUSH);
        const int produced = ChunkSize - static_cast<int>(stream.avail_out);
        if (produced > 0) {
            if (output.size() + produced > kMaximumDecodedPdfStreamBytes) {
                inflateEnd(&stream);
                if (error) *error = QStringLiteral("Decoded PDF stream exceeds the safety limit");
                return false;
            }
            output.append(chunk, produced);
        }
        if (result != Z_OK && result != Z_STREAM_END) {
            const QString detail = stream.msg
                ? QString::fromLatin1(stream.msg)
                : QStringLiteral("zlib error %1").arg(result);
            inflateEnd(&stream);
            if (error) *error = QStringLiteral("Unable to decode PDF Flate stream: %1").arg(detail);
            return false;
        }
        if (stream.avail_in == 0 && inputOffset >= compressed.size()
            && produced == 0 && result != Z_STREAM_END) {
            inflateEnd(&stream);
            if (error) *error = QStringLiteral("PDF Flate stream ended prematurely");
            return false;
        }
    }
    inflateEnd(&stream);
    *decoded = output;
    return true;
}

int paethPredictor(int left, int above, int upperLeft)
{
    const int estimate = left + above - upperLeft;
    const int leftDistance = std::abs(estimate - left);
    const int aboveDistance = std::abs(estimate - above);
    const int upperLeftDistance = std::abs(estimate - upperLeft);
    if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance) return left;
    if (aboveDistance <= upperLeftDistance) return above;
    return upperLeft;
}

bool applyPdfPredictor(const QByteArray &input,
                       const PdfValue *decodeParameters,
                       QByteArray *output,
                       QString *error)
{
    if (!decodeParameters || decodeParameters->kind == PdfValueKind::Null) {
        *output = input;
        return true;
    }
    if (decodeParameters->kind != PdfValueKind::Dictionary) {
        if (error) *error = QStringLiteral("Unsupported PDF DecodeParms value");
        return false;
    }
    const int predictor = static_cast<int>(
        dictionaryInteger(*decodeParameters, "Predictor").value_or(1));
    if (predictor <= 1) {
        *output = input;
        return true;
    }
    const int colors = static_cast<int>(
        dictionaryInteger(*decodeParameters, "Colors").value_or(1));
    const int bitsPerComponent = static_cast<int>(
        dictionaryInteger(*decodeParameters, "BitsPerComponent").value_or(8));
    const int columns = static_cast<int>(
        dictionaryInteger(*decodeParameters, "Columns").value_or(1));
    if (colors <= 0 || columns <= 0 || bitsPerComponent <= 0
        || bitsPerComponent > 16) {
        if (error) *error = QStringLiteral("Invalid PDF predictor parameters");
        return false;
    }
    const qint64 rowBytes64 =
        (static_cast<qint64>(colors) * columns * bitsPerComponent + 7) / 8;
    const qint64 bytesPerPixel64 =
        std::max<qint64>(1, (static_cast<qint64>(colors) * bitsPerComponent + 7) / 8);
    if (rowBytes64 <= 0 || rowBytes64 > std::numeric_limits<int>::max()
        || bytesPerPixel64 > std::numeric_limits<int>::max()) {
        if (error) *error = QStringLiteral("PDF predictor row is too large");
        return false;
    }
    const int rowBytes = static_cast<int>(rowBytes64);
    const int bytesPerPixel = static_cast<int>(bytesPerPixel64);

    if (predictor == 2) {
        if (bitsPerComponent != 8 || input.size() % rowBytes != 0) {
            if (error) *error = QStringLiteral("Unsupported TIFF PDF predictor layout");
            return false;
        }
        QByteArray decoded = input;
        for (qsizetype row = 0; row < decoded.size(); row += rowBytes) {
            for (int column = bytesPerPixel; column < rowBytes; ++column) {
                const int current = static_cast<unsigned char>(decoded.at(row + column));
                const int left = static_cast<unsigned char>(
                    decoded.at(row + column - bytesPerPixel));
                decoded[row + column] = static_cast<char>((current + left) & 0xff);
            }
        }
        *output = decoded;
        return true;
    }

    if (predictor < 10 || predictor > 15
        || input.size() % (rowBytes + 1) != 0) {
        if (error) *error = QStringLiteral("Unsupported PNG PDF predictor layout");
        return false;
    }
    QByteArray decoded;
    decoded.resize(input.size() / (rowBytes + 1) * rowBytes);
    QByteArray previous(rowBytes, '\0');
    qsizetype inputOffset = 0;
    qsizetype outputOffset = 0;
    while (inputOffset < input.size()) {
        const int filter = static_cast<unsigned char>(input.at(inputOffset++));
        if (filter < 0 || filter > 4) {
            if (error) *error = QStringLiteral("Invalid PNG predictor filter in PDF stream");
            return false;
        }
        for (int column = 0; column < rowBytes; ++column) {
            const int encoded = static_cast<unsigned char>(input.at(inputOffset++));
            const int left = column >= bytesPerPixel
                ? static_cast<unsigned char>(decoded.at(outputOffset + column - bytesPerPixel))
                : 0;
            const int above = static_cast<unsigned char>(previous.at(column));
            const int upperLeft = column >= bytesPerPixel
                ? static_cast<unsigned char>(previous.at(column - bytesPerPixel))
                : 0;
            int value = encoded;
            if (filter == 1) value += left;
            else if (filter == 2) value += above;
            else if (filter == 3) value += (left + above) / 2;
            else if (filter == 4) value += paethPredictor(left, above, upperLeft);
            decoded[outputOffset + column] = static_cast<char>(value & 0xff);
        }
        previous = decoded.mid(outputOffset, rowBytes);
        outputOffset += rowBytes;
    }
    *output = decoded;
    return true;
}

bool decodePdfStream(const PdfObject &object, QByteArray *decoded, QString *error)
{
    const PdfValue *filterValue = dictionaryValue(object.value, "Filter");
    if (!filterValue) {
        *decoded = object.stream;
        return true;
    }

    QVector<QByteArray> filters;
    if (filterValue->kind == PdfValueKind::Name) {
        filters.append(filterValue->scalar);
    } else if (filterValue->kind == PdfValueKind::Array) {
        for (const PdfValue &filter : filterValue->array) {
            if (filter.kind != PdfValueKind::Name) {
                if (error) *error = QStringLiteral("Unsupported PDF stream filter value");
                return false;
            }
            filters.append(filter.scalar);
        }
    } else {
        if (error) *error = QStringLiteral("Unsupported PDF stream filter value");
        return false;
    }

    const PdfValue *decodeParameters = dictionaryValue(object.value, "DecodeParms");
    QByteArray current = object.stream;
    for (int index = 0; index < filters.size(); ++index) {
        const QByteArray filter = filters.at(index);
        if (filter != "FlateDecode" && filter != "Fl") {
            if (error) {
                *error = QStringLiteral("Unsupported PDF stream filter: %1")
                             .arg(QString::fromLatin1(filter));
            }
            return false;
        }
        QByteArray inflated;
        if (!inflatePdfBytes(current, &inflated, error)) {
            return false;
        }
        const PdfValue *parametersForFilter = decodeParameters;
        if (decodeParameters && decodeParameters->kind == PdfValueKind::Array) {
            parametersForFilter = index < decodeParameters->array.size()
                ? &decodeParameters->array.at(index)
                : nullptr;
        }
        QByteArray predicted;
        if (!applyPdfPredictor(inflated, parametersForFilter, &predicted, error)) {
            return false;
        }
        current = predicted;
    }
    *decoded = current;
    return true;
}

bool parseIndirectObjectAt(const QByteArray &data,
                           qint64 offset,
                           PdfObject *object,
                           QString *error)
{
    if (offset < 0 || offset >= data.size()) {
        if (error) *error = QStringLiteral("PDF object offset is outside the file");
        return false;
    }
    PdfLexer lexer(data, static_cast<qsizetype>(offset));
    qint64 objectNumber = -1;
    qint64 generation = -1;
    const QByteArray objectToken = lexer.readToken();
    const QByteArray generationToken = lexer.readToken();
    const QByteArray marker = lexer.readToken();
    if (!isIntegerToken(objectToken, &objectNumber)
        || !isIntegerToken(generationToken, &generation)
        || marker != "obj"
        || objectNumber < 0 || objectNumber > std::numeric_limits<int>::max()
        || generation < 0 || generation > std::numeric_limits<int>::max()) {
        if (error) *error = QStringLiteral("Invalid indirect PDF object header");
        return false;
    }
    PdfValue value;
    if (!lexer.parseValue(&value, error)) {
        return false;
    }

    PdfObject parsed;
    parsed.objectNumber = static_cast<int>(objectNumber);
    parsed.generation = static_cast<int>(generation);
    parsed.value = std::move(value);

    const qsizetype afterValue = lexer.position();
    const QByteArray nextToken = lexer.readToken();
    if (nextToken == "stream") {
        qsizetype streamStart = lexer.position();
        if (streamStart < data.size() && data.at(streamStart) == '\r') {
            ++streamStart;
            if (streamStart < data.size() && data.at(streamStart) == '\n') ++streamStart;
        } else if (streamStart < data.size() && data.at(streamStart) == '\n') {
            ++streamStart;
        } else {
            if (error) *error = QStringLiteral("PDF stream marker is not followed by an end-of-line");
            return false;
        }
        qint64 streamLength = -1;
        if (const PdfValue *length = dictionaryValue(parsed.value, "Length")) {
            streamLength = integerValue(*length).value_or(-1);
        }
        qsizetype streamEnd = -1;
        if (streamLength >= 0
            && streamLength <= data.size() - streamStart) {
            streamEnd = streamStart + static_cast<qsizetype>(streamLength);
        } else {
            qsizetype markerOffset = data.indexOf("\nendstream", streamStart);
            if (markerOffset < 0) markerOffset = data.indexOf("\rendstream", streamStart);
            if (markerOffset < 0) markerOffset = data.indexOf("endstream", streamStart);
            streamEnd = markerOffset;
        }
        if (streamEnd < streamStart || streamEnd > data.size()) {
            if (error) *error = QStringLiteral("Unable to locate the end of a PDF stream");
            return false;
        }
        parsed.stream = data.mid(streamStart, streamEnd - streamStart);
        parsed.hasStream = true;
    } else {
        lexer.setPosition(afterValue);
    }
    *object = std::move(parsed);
    return true;
}

quint64 unsignedBigEndian(const QByteArray &bytes, qsizetype offset, int width)
{
    quint64 value = 0;
    for (int index = 0; index < width; ++index) {
        value = (value << 8)
            | static_cast<unsigned char>(bytes.at(offset + index));
    }
    return value;
}

bool parseClassicXref(const QByteArray &data,
                      qint64 offset,
                      XrefSection *section,
                      QString *error)
{
    PdfLexer lexer(data, static_cast<qsizetype>(offset));
    if (lexer.readToken() != "xref") {
        return false;
    }
    while (true) {
        const QByteArray firstToken = lexer.readToken();
        if (firstToken == "trailer") {
            PdfValue trailer;
            if (!lexer.parseValue(&trailer, error)
                || trailer.kind != PdfValueKind::Dictionary) {
                if (error && error->isEmpty()) *error = QStringLiteral("Invalid PDF trailer");
                return false;
            }
            section->trailer = trailer;
            section->previousOffset = dictionaryInteger(trailer, "Prev").value_or(-1);
            section->hybridXrefOffset = dictionaryInteger(trailer, "XRefStm").value_or(-1);
            return true;
        }
        qint64 firstObject = -1;
        qint64 count = -1;
        if (!isIntegerToken(firstToken, &firstObject)
            || !isIntegerToken(lexer.readToken(), &count)
            || firstObject < 0 || count < 0
            || firstObject + count > std::numeric_limits<int>::max()) {
            if (error) *error = QStringLiteral("Invalid PDF xref subsection");
            return false;
        }
        for (qint64 index = 0; index < count; ++index) {
            qint64 entryOffset = -1;
            qint64 generation = -1;
            const QByteArray offsetToken = lexer.readToken();
            const QByteArray generationToken = lexer.readToken();
            const QByteArray stateToken = lexer.readToken();
            if (!isIntegerToken(offsetToken, &entryOffset)
                || !isIntegerToken(generationToken, &generation)
                || (stateToken != "n" && stateToken != "f")) {
                if (error) *error = QStringLiteral("Invalid PDF xref entry");
                return false;
            }
            XrefEntry entry;
            if (stateToken == "n") {
                entry.type = 1;
                entry.offset = entryOffset;
                entry.generation = static_cast<int>(generation);
            } else {
                entry.type = 0;
                entry.generation = static_cast<int>(generation);
            }
            section->entries.insert(static_cast<int>(firstObject + index), entry);
        }
    }
}

bool parseXrefStream(const QByteArray &data,
                     qint64 offset,
                     XrefSection *section,
                     QString *error)
{
    PdfObject object;
    if (!parseIndirectObjectAt(data, offset, &object, error)
        || object.value.kind != PdfValueKind::Dictionary
        || !object.hasStream) {
        if (error && error->isEmpty()) *error = QStringLiteral("Invalid PDF xref stream");
        return false;
    }
    if (dictionaryName(object.value, "Type") != "XRef") {
        if (error) *error = QStringLiteral("PDF startxref does not reference an xref section");
        return false;
    }
    const PdfValue *widthsValue = dictionaryValue(object.value, "W");
    if (!widthsValue || widthsValue->kind != PdfValueKind::Array
        || widthsValue->array.size() != 3) {
        if (error) *error = QStringLiteral("PDF xref stream is missing W");
        return false;
    }
    int widths[3] = {};
    int rowWidth = 0;
    for (int index = 0; index < 3; ++index) {
        const qint64 width = integerValue(widthsValue->array.at(index)).value_or(-1);
        if (width < 0 || width > 8) {
            if (error) *error = QStringLiteral("Invalid PDF xref field width");
            return false;
        }
        widths[index] = static_cast<int>(width);
        rowWidth += widths[index];
    }
    if (rowWidth <= 0) {
        if (error) *error = QStringLiteral("Invalid empty PDF xref row");
        return false;
    }

    QVector<qint64> indexValues;
    if (const PdfValue *indexValue = dictionaryValue(object.value, "Index")) {
        if (indexValue->kind != PdfValueKind::Array
            || indexValue->array.size() % 2 != 0) {
            if (error) *error = QStringLiteral("Invalid PDF xref Index");
            return false;
        }
        for (const PdfValue &value : indexValue->array) {
            const qint64 number = integerValue(value).value_or(-1);
            if (number < 0) {
                if (error) *error = QStringLiteral("Invalid PDF xref Index value");
                return false;
            }
            indexValues.append(number);
        }
    } else {
        const qint64 size = dictionaryInteger(object.value, "Size").value_or(-1);
        if (size <= 0) {
            if (error) *error = QStringLiteral("PDF xref stream is missing Size");
            return false;
        }
        indexValues = {0, size};
    }

    QByteArray decoded;
    if (!decodePdfStream(object, &decoded, error)) {
        return false;
    }
    qsizetype cursor = 0;
    for (int range = 0; range < indexValues.size(); range += 2) {
        const qint64 firstObject = indexValues.at(range);
        const qint64 count = indexValues.at(range + 1);
        if (firstObject < 0 || count < 0
            || firstObject + count > std::numeric_limits<int>::max()
            || count > (decoded.size() - cursor) / rowWidth) {
            if (error) *error = QStringLiteral("PDF xref stream data is truncated");
            return false;
        }
        for (qint64 index = 0; index < count; ++index) {
            const quint64 field0 = widths[0] == 0
                ? 1
                : unsignedBigEndian(decoded, cursor, widths[0]);
            cursor += widths[0];
            const quint64 field1 = unsignedBigEndian(decoded, cursor, widths[1]);
            cursor += widths[1];
            const quint64 field2 = unsignedBigEndian(decoded, cursor, widths[2]);
            cursor += widths[2];
            XrefEntry entry;
            if (field0 == 1 && field1 <= static_cast<quint64>(std::numeric_limits<qint64>::max())
                && field2 <= static_cast<quint64>(std::numeric_limits<int>::max())) {
                entry.type = 1;
                entry.offset = static_cast<qint64>(field1);
                entry.generation = static_cast<int>(field2);
            } else if (field0 == 2
                       && field1 <= static_cast<quint64>(std::numeric_limits<int>::max())
                       && field2 <= static_cast<quint64>(std::numeric_limits<int>::max())) {
                entry.type = 2;
                entry.objectStreamNumber = static_cast<int>(field1);
                entry.objectStreamIndex = static_cast<int>(field2);
            }
            if (field0 == 0
                && field2 <= static_cast<quint64>(std::numeric_limits<int>::max())) {
                entry.type = 0;
                entry.generation = static_cast<int>(field2);
            }
            section->entries.insert(static_cast<int>(firstObject + index), entry);
        }
    }
    section->trailer = object.value;
    section->previousOffset = dictionaryInteger(object.value, "Prev").value_or(-1);
    return true;
}

QByteArray decimalPdfNumber(double value)
{
    if (std::abs(value) < 0.0000005) value = 0.0;
    QByteArray number = QByteArray::number(value, 'f', 6);
    while (number.contains('.') && number.endsWith('0')) number.chop(1);
    if (number.endsWith('.')) number.chop(1);
    return number.isEmpty() || number == "-0" ? QByteArray("0") : number;
}

QByteArray escapedPdfName(const QByteArray &name)
{
    static const QByteArray hex = "0123456789ABCDEF";
    QByteArray escaped("/");
    for (const unsigned char byte : name) {
        if (byte >= 33 && byte <= 126 && !isPdfDelimiter(static_cast<char>(byte))
            && byte != '#') {
            escaped.append(static_cast<char>(byte));
        } else {
            escaped.append('#');
            escaped.append(hex.at((byte >> 4) & 0x0f));
            escaped.append(hex.at(byte & 0x0f));
        }
    }
    return escaped;
}

QByteArray serializePdfValue(const PdfValue &value, QString *error, int depth = 0)
{
    if (depth > 128) {
        if (error) *error = QStringLiteral("PDF object nesting is too deep to serialize");
        return {};
    }
    switch (value.kind) {
    case PdfValueKind::Null:
        return "null";
    case PdfValueKind::Boolean:
        return value.boolean ? QByteArray("true") : QByteArray("false");
    case PdfValueKind::Number:
        return value.scalar;
    case PdfValueKind::Name:
        return escapedPdfName(value.scalar);
    case PdfValueKind::String:
        return '<' + value.scalar.toHex().toUpper() + '>';
    case PdfValueKind::Reference:
        return QByteArray::number(value.objectNumber) + ' '
            + QByteArray::number(value.generation) + " R";
    case PdfValueKind::Keyword:
        return value.scalar;
    case PdfValueKind::Array: {
        QByteArray serialized("[");
        bool first = true;
        for (const PdfValue &child : value.array) {
            const QByteArray childBytes = serializePdfValue(child, error, depth + 1);
            if (childBytes.isEmpty() && child.kind != PdfValueKind::String) return {};
            if (!first) serialized.append(' ');
            serialized.append(childBytes);
            first = false;
        }
        serialized.append(']');
        return serialized;
    }
    case PdfValueKind::Dictionary: {
        QByteArray serialized("<<");
        for (auto iterator = value.dictionary.cbegin();
             iterator != value.dictionary.cend(); ++iterator) {
            const QByteArray childBytes = serializePdfValue(iterator.value(), error, depth + 1);
            if (childBytes.isEmpty() && iterator.value().kind != PdfValueKind::String) return {};
            serialized.append('\n');
            serialized.append(escapedPdfName(iterator.key()));
            serialized.append(' ');
            serialized.append(childBytes);
        }
        serialized.append("\n>>");
        return serialized;
    }
    case PdfValueKind::Invalid:
        break;
    }
    if (error) *error = QStringLiteral("Unsupported PDF value while serializing");
    return {};
}

class PdfDocument {
public:
    ~PdfDocument()
    {
        data_.clear();
        if (mapped_) source_.unmap(mapped_);
    }

    bool open(const QString &path, QString *error)
    {
        source_.setFileName(path);
        if (!source_.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Unable to read source PDF: %1").arg(source_.errorString());
            return false;
        }
        if (source_.size() <= 0
            || source_.size() > std::numeric_limits<qsizetype>::max()) {
            if (error) *error = QStringLiteral("Source PDF has an unsupported size");
            return false;
        }
        mapped_ = source_.map(0, source_.size());
        if (mapped_) {
            data_ = QByteArray::fromRawData(
                reinterpret_cast<const char *>(mapped_),
                static_cast<qsizetype>(source_.size()));
        } else {
            data_ = source_.readAll();
        }
        if (!data_.startsWith("%PDF-")) {
            if (error) *error = QStringLiteral("Source file is not a PDF document");
            return false;
        }
        const qsizetype marker = data_.lastIndexOf("startxref");
        if (marker < 0) {
            if (error) *error = QStringLiteral("PDF startxref marker is missing");
            return false;
        }
        PdfLexer lexer(data_, marker + qsizetype(sizeof("startxref") - 1));
        qint64 start = -1;
        if (!isIntegerToken(lexer.readToken(), &start)
            || start < 0 || start >= data_.size()) {
            if (error) *error = QStringLiteral("PDF startxref offset is invalid");
            return false;
        }
        startXref_ = start;
        if (!loadXrefChain(start, error)) {
            return false;
        }
        if (dictionaryValue(effectiveTrailer_, "Encrypt")) {
            if (error) *error = QStringLiteral(
                "Encrypted PDF documents cannot be used for Pinloom Preview");
            return false;
        }
        const PdfValue *root = dictionaryValue(effectiveTrailer_, "Root");
        if (!root || root->kind != PdfValueKind::Reference) {
            if (error) *error = QStringLiteral("PDF trailer is missing the document catalog");
            return false;
        }
        rootReference_ = *root;
        return true;
    }

    const QByteArray &data() const { return data_; }
    qint64 startXref() const { return startXref_; }
    const PdfValue &trailer() const { return effectiveTrailer_; }
    const PdfValue &rootReference() const { return rootReference_; }

    int nextObjectNumber() const
    {
        int next = 1;
        for (auto iterator = xref_.cbegin(); iterator != xref_.cend(); ++iterator) {
            next = std::max(next, iterator.key() + 1);
        }
        const qint64 declaredSize = dictionaryInteger(effectiveTrailer_, "Size").value_or(0);
        if (declaredSize > 0 && declaredSize <= std::numeric_limits<int>::max()) {
            next = std::max(next, static_cast<int>(declaredSize));
        }
        return next;
    }

    bool loadObject(const PdfValue &reference, PdfObject *object, QString *error)
    {
        if (reference.kind != PdfValueKind::Reference) {
            if (error) *error = QStringLiteral("Expected an indirect PDF object reference");
            return false;
        }
        return loadObject(reference.objectNumber, object, error);
    }

    bool loadObject(int objectNumber, PdfObject *object, QString *error)
    {
        const auto cached = objectCache_.constFind(objectNumber);
        if (cached != objectCache_.cend()) {
            *object = cached.value();
            return true;
        }
        if (loadingObjects_.contains(objectNumber)) {
            if (error) *error = QStringLiteral("Cyclic PDF object reference");
            return false;
        }
        const auto xref = xref_.constFind(objectNumber);
        if (xref == xref_.cend()) {
            if (error) *error = QStringLiteral("PDF object %1 is missing from xref").arg(objectNumber);
            return false;
        }
        loadingObjects_.insert(objectNumber);
        PdfObject loaded;
        bool success = false;
        if (xref->type == 1) {
            success = parseIndirectObjectAt(data_, xref->offset, &loaded, error);
            if (success && loaded.objectNumber != objectNumber) {
                if (error) *error = QStringLiteral("PDF xref points to the wrong object");
                success = false;
            }
        } else if (xref->type == 2) {
            PdfObject objectStream;
            success = loadObject(xref->objectStreamNumber, &objectStream, error);
            if (success) {
                success = loadCompressedObject(objectNumber,
                                               xref->objectStreamIndex,
                                               objectStream,
                                               &loaded,
                                               error);
            }
        } else if (error) {
            *error = QStringLiteral("PDF object %1 is marked free").arg(objectNumber);
        }
        loadingObjects_.remove(objectNumber);
        if (!success) return false;
        objectCache_.insert(objectNumber, loaded);
        *object = loaded;
        return true;
    }

    bool findPage(int pageNumber,
                  PdfValue *pageReference,
                  PdfObject *pageObject,
                  PdfPageGeometry *geometry,
                  PdfObject *catalogObject,
                  QString *error)
    {
        if (pageNumber <= 0) {
            if (error) *error = QStringLiteral("PDF page number must be positive");
            return false;
        }
        PdfObject catalog;
        if (!loadObject(rootReference_, &catalog, error)
            || catalog.value.kind != PdfValueKind::Dictionary
            || dictionaryName(catalog.value, "Type") != "Catalog") {
            if (error && error->isEmpty()) *error = QStringLiteral("Invalid PDF document catalog");
            return false;
        }
        const PdfValue *pages = dictionaryValue(catalog.value, "Pages");
        if (!pages || pages->kind != PdfValueKind::Reference) {
            if (error) *error = QStringLiteral("PDF catalog is missing its page tree");
            return false;
        }
        InheritedPageGeometry inherited;
        int pageIndex = 0;
        QSet<int> ancestors;
        if (!findPageInTree(*pages,
                            pageNumber,
                            &pageIndex,
                            inherited,
                            &ancestors,
                            pageReference,
                            pageObject,
                            geometry,
                            error)) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("PDF page %1 does not exist").arg(pageNumber);
            }
            return false;
        }
        if (catalogObject) *catalogObject = catalog;
        return true;
    }

private:
    struct InheritedPageGeometry {
        std::optional<QRectF> mediaBox;
        std::optional<QRectF> cropBox;
        int rotation = 0;
    };

    bool parseXrefSection(qint64 offset, XrefSection *section, QString *error)
    {
        qsizetype cursor = static_cast<qsizetype>(offset);
        while (cursor < data_.size() && isPdfWhitespace(data_.at(cursor))) ++cursor;
        if (data_.mid(cursor, 4) == "xref") {
            return parseClassicXref(data_, cursor, section, error);
        }
        return parseXrefStream(data_, cursor, section, error);
    }

    void mergeTrailer(const PdfValue &trailer)
    {
        if (trailer.kind != PdfValueKind::Dictionary) return;
        if (effectiveTrailer_.kind != PdfValueKind::Dictionary) {
            effectiveTrailer_ = PdfValue::dictionaryValue();
        }
        for (auto iterator = trailer.dictionary.cbegin();
             iterator != trailer.dictionary.cend(); ++iterator) {
            if (!effectiveTrailer_.dictionary.contains(iterator.key())) {
                effectiveTrailer_.dictionary.insert(iterator.key(), iterator.value());
            }
        }
    }

    void mergeXrefEntries(const QHash<int, XrefEntry> &entries)
    {
        for (auto iterator = entries.cbegin(); iterator != entries.cend(); ++iterator) {
            if (!xref_.contains(iterator.key())) {
                xref_.insert(iterator.key(), iterator.value());
            }
        }
    }

    bool loadXrefChain(qint64 offset, QString *error)
    {
        if (visitedXrefOffsets_.contains(offset)) {
            if (error) *error = QStringLiteral("Cyclic PDF xref chain");
            return false;
        }
        visitedXrefOffsets_.insert(offset);
        XrefSection section;
        if (!parseXrefSection(offset, &section, error)) {
            return false;
        }
        mergeTrailer(section.trailer);
        QHash<int, XrefEntry> revisionEntries = section.entries;
        if (section.hybridXrefOffset >= 0
            && !visitedXrefOffsets_.contains(section.hybridXrefOffset)) {
            visitedXrefOffsets_.insert(section.hybridXrefOffset);
            XrefSection hybrid;
            if (!parseXrefSection(section.hybridXrefOffset, &hybrid, error)) {
                return false;
            }
            mergeTrailer(hybrid.trailer);
            // Hybrid-reference files use the stream to supplement or replace
            // same-revision table entries, especially compressed objects.
            for (auto iterator = hybrid.entries.cbegin();
                 iterator != hybrid.entries.cend(); ++iterator) {
                revisionEntries.insert(iterator.key(), iterator.value());
            }
        }
        mergeXrefEntries(revisionEntries);
        if (section.previousOffset >= 0) {
            return loadXrefChain(section.previousOffset, error);
        }
        return true;
    }

    bool loadCompressedObject(int objectNumber,
                              int objectIndex,
                              const PdfObject &objectStream,
                              PdfObject *object,
                              QString *error)
    {
        if (!objectStream.hasStream
            || dictionaryName(objectStream.value, "Type") != "ObjStm") {
            if (error) *error = QStringLiteral("PDF compressed object has an invalid object stream");
            return false;
        }
        const qint64 count = dictionaryInteger(objectStream.value, "N").value_or(-1);
        const qint64 first = dictionaryInteger(objectStream.value, "First").value_or(-1);
        if (count <= 0 || first < 0 || count > std::numeric_limits<int>::max()) {
            if (error) *error = QStringLiteral("Invalid PDF object stream header");
            return false;
        }
        QByteArray decoded;
        if (!decodePdfStream(objectStream, &decoded, error)
            || first > decoded.size()) {
            if (error && error->isEmpty()) *error = QStringLiteral("Invalid PDF object stream offset");
            return false;
        }
        QVector<int> numbers;
        QVector<qsizetype> offsets;
        PdfLexer header(decoded, 0, static_cast<qsizetype>(first));
        for (qint64 index = 0; index < count; ++index) {
            qint64 number = -1;
            qint64 relativeOffset = -1;
            if (!isIntegerToken(header.readToken(), &number)
                || !isIntegerToken(header.readToken(), &relativeOffset)
                || number < 0 || number > std::numeric_limits<int>::max()
                || relativeOffset < 0 || relativeOffset > decoded.size() - first) {
                if (error) *error = QStringLiteral("Invalid PDF object stream index");
                return false;
            }
            numbers.append(static_cast<int>(number));
            offsets.append(static_cast<qsizetype>(relativeOffset));
        }
        int selected = objectIndex;
        if (selected < 0 || selected >= numbers.size()
            || numbers.at(selected) != objectNumber) {
            selected = numbers.indexOf(objectNumber);
        }
        if (selected < 0) {
            if (error) *error = QStringLiteral("PDF object is missing from its object stream");
            return false;
        }
        const qsizetype start = static_cast<qsizetype>(first) + offsets.at(selected);
        const qsizetype end = selected + 1 < offsets.size()
            ? static_cast<qsizetype>(first) + offsets.at(selected + 1)
            : decoded.size();
        if (start < first || end < start || end > decoded.size()) {
            if (error) *error = QStringLiteral("Invalid PDF compressed object bounds");
            return false;
        }
        PdfLexer lexer(decoded, start, end);
        PdfValue value;
        if (!lexer.parseValue(&value, error)) {
            return false;
        }
        object->objectNumber = objectNumber;
        object->generation = 0;
        object->value = std::move(value);
        return true;
    }

    bool resolveValue(const PdfValue &value, PdfValue *resolved, QString *error)
    {
        if (value.kind != PdfValueKind::Reference) {
            *resolved = value;
            return true;
        }
        PdfObject object;
        if (!loadObject(value, &object, error)) return false;
        *resolved = object.value;
        return true;
    }

    bool readBox(const PdfValue &value, QRectF *box, QString *error)
    {
        PdfValue resolved;
        if (!resolveValue(value, &resolved, error)) return false;
        if (resolved.kind != PdfValueKind::Array || resolved.array.size() != 4) {
            if (error) *error = QStringLiteral("Invalid PDF page box");
            return false;
        }
        double coordinates[4] = {};
        for (int index = 0; index < 4; ++index) {
            const std::optional<double> number = realValue(resolved.array.at(index));
            if (!number.has_value()) {
                if (error) *error = QStringLiteral("Invalid PDF page box coordinate");
                return false;
            }
            coordinates[index] = number.value();
        }
        const double left = std::min(coordinates[0], coordinates[2]);
        const double right = std::max(coordinates[0], coordinates[2]);
        const double bottom = std::min(coordinates[1], coordinates[3]);
        const double top = std::max(coordinates[1], coordinates[3]);
        *box = QRectF(left, bottom, right - left, top - bottom);
        if (!box->isValid()) {
            if (error) *error = QStringLiteral("PDF page box is empty");
            return false;
        }
        return true;
    }

    bool findPageInTree(const PdfValue &nodeReference,
                        int targetPage,
                        int *pageIndex,
                        InheritedPageGeometry inherited,
                        QSet<int> *ancestors,
                        PdfValue *pageReference,
                        PdfObject *pageObject,
                        PdfPageGeometry *geometry,
                        QString *error)
    {
        if (nodeReference.kind != PdfValueKind::Reference) {
            if (error) *error = QStringLiteral("PDF page tree contains a direct child");
            return false;
        }
        if (ancestors->contains(nodeReference.objectNumber)) {
            if (error) *error = QStringLiteral("Cyclic PDF page tree");
            return false;
        }
        ancestors->insert(nodeReference.objectNumber);
        PdfObject node;
        if (!loadObject(nodeReference, &node, error)
            || node.value.kind != PdfValueKind::Dictionary) {
            ancestors->remove(nodeReference.objectNumber);
            if (error && error->isEmpty()) *error = QStringLiteral("Invalid PDF page tree node");
            return false;
        }

        if (const PdfValue *mediaBox = dictionaryValue(node.value, "MediaBox")) {
            QRectF box;
            if (!readBox(*mediaBox, &box, error)) {
                ancestors->remove(nodeReference.objectNumber);
                return false;
            }
            inherited.mediaBox = box;
        }
        if (const PdfValue *cropBox = dictionaryValue(node.value, "CropBox")) {
            QRectF box;
            if (!readBox(*cropBox, &box, error)) {
                ancestors->remove(nodeReference.objectNumber);
                return false;
            }
            inherited.cropBox = box;
        }
        if (const PdfValue *rotation = dictionaryValue(node.value, "Rotate")) {
            PdfValue resolvedRotation;
            if (!resolveValue(*rotation, &resolvedRotation, error)) {
                ancestors->remove(nodeReference.objectNumber);
                return false;
            }
            const qint64 value = integerValue(resolvedRotation).value_or(
                std::numeric_limits<qint64>::min());
            if (value == std::numeric_limits<qint64>::min()) {
                ancestors->remove(nodeReference.objectNumber);
                if (error) *error = QStringLiteral("Invalid PDF page rotation");
                return false;
            }
            inherited.rotation = static_cast<int>((value % 360 + 360) % 360);
        }

        const QByteArray type = dictionaryName(node.value, "Type");
        const PdfValue *kidsValue = dictionaryValue(node.value, "Kids");
        if (type == "Page" || !kidsValue) {
            ++(*pageIndex);
            ancestors->remove(nodeReference.objectNumber);
            if (*pageIndex != targetPage) return false;
            if (!inherited.mediaBox.has_value()) {
                if (error) *error = QStringLiteral("PDF page has no MediaBox");
                return false;
            }
            PdfPageGeometry pageGeometry;
            pageGeometry.mediaBox = inherited.mediaBox.value();
            pageGeometry.cropBox = inherited.cropBox.value_or(pageGeometry.mediaBox)
                                       .intersected(pageGeometry.mediaBox);
            pageGeometry.rotation = inherited.rotation;
            if (const PdfValue *userUnit = dictionaryValue(node.value, "UserUnit")) {
                PdfValue resolvedUserUnit;
                if (!resolveValue(*userUnit, &resolvedUserUnit, error)) return false;
                const std::optional<double> value = realValue(resolvedUserUnit);
                if (!value.has_value() || !std::isfinite(*value) || *value <= 0.0) {
                    if (error) *error = QStringLiteral("Invalid PDF page UserUnit");
                    return false;
                }
                pageGeometry.userUnit = *value;
            }
            if (!pageGeometry.isValid()) {
                if (error) *error = QStringLiteral("PDF page geometry is invalid");
                return false;
            }
            *pageReference = nodeReference;
            *pageObject = node;
            *geometry = pageGeometry;
            return true;
        }

        PdfValue kids;
        if (!resolveValue(*kidsValue, &kids, error)
            || kids.kind != PdfValueKind::Array) {
            ancestors->remove(nodeReference.objectNumber);
            if (error && error->isEmpty()) *error = QStringLiteral("Invalid PDF page tree Kids array");
            return false;
        }
        for (const PdfValue &child : kids.array) {
            QString childError;
            if (findPageInTree(child,
                               targetPage,
                               pageIndex,
                               inherited,
                               ancestors,
                               pageReference,
                               pageObject,
                               geometry,
                               &childError)) {
                ancestors->remove(nodeReference.objectNumber);
                return true;
            }
            if (!childError.isEmpty()) {
                ancestors->remove(nodeReference.objectNumber);
                if (error) *error = childError;
                return false;
            }
        }
        ancestors->remove(nodeReference.objectNumber);
        return false;
    }

    QFile source_;
    uchar *mapped_ = nullptr;
    QByteArray data_;
    qint64 startXref_ = -1;
    QHash<int, XrefEntry> xref_;
    PdfValue effectiveTrailer_;
    PdfValue rootReference_;
    QSet<qint64> visitedXrefOffsets_;
    QHash<int, PdfObject> objectCache_;
    QSet<int> loadingObjects_;
};

QRectF annotationRectForPage(const QRectF &anchorRect,
                             PdfAnchorCoordinateSpace coordinateSpace,
                             const PdfPageGeometry &geometry,
                             QString *error)
{
    if (!anchorRect.isValid()
        || !std::isfinite(anchorRect.left())
        || !std::isfinite(anchorRect.top())
        || !std::isfinite(anchorRect.right())
        || !std::isfinite(anchorRect.bottom())) {
        if (error) *error = QStringLiteral("PDF anchor rectangle is invalid");
        return {};
    }
    QRectF result;
    if (coordinateSpace == PdfAnchorCoordinateSpace::PdfUserSpace) {
        result = anchorRect.normalized();
    } else {
        const QRectF crop = geometry.cropBox;
        const double width = crop.width();
        const double height = crop.height();
        const double coordinateScale = 1.0 / geometry.userUnit;
        const int rotation = (geometry.rotation % 360 + 360) % 360;
        if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
            if (error) {
                *error = QStringLiteral("Unsupported PDF page rotation: %1 degrees")
                             .arg(rotation);
            }
            return {};
        }
        const auto toUserSpace = [&](double horizontal, double vertical) {
            horizontal *= coordinateScale;
            vertical *= coordinateScale;
            QPointF point;
            if (rotation == 0) point = QPointF(horizontal, height - vertical);
            else if (rotation == 90) point = QPointF(vertical, horizontal);
            else if (rotation == 180) point = QPointF(width - horizontal, vertical);
            else point = QPointF(width - vertical, height - horizontal);
            return point + crop.topLeft();
        };
        const QPointF points[] = {
            toUserSpace(anchorRect.left(), anchorRect.top()),
            toUserSpace(anchorRect.right(), anchorRect.top()),
            toUserSpace(anchorRect.left(), anchorRect.bottom()),
            toUserSpace(anchorRect.right(), anchorRect.bottom()),
        };
        double left = points[0].x();
        double right = points[0].x();
        double bottom = points[0].y();
        double top = points[0].y();
        for (const QPointF &point : points) {
            left = std::min(left, point.x());
            right = std::max(right, point.x());
            bottom = std::min(bottom, point.y());
            top = std::max(top, point.y());
        }
        result = QRectF(left, bottom, right - left, top - bottom);
    }
    result = result.intersected(geometry.cropBox);
    if (!result.isValid() || result.width() < 0.01 || result.height() < 0.01) {
        if (error) *error = QStringLiteral("PDF anchor rectangle is outside the page CropBox");
        return {};
    }
    return result;
}

PdfValue numericValue(double value)
{
    return PdfValue::numberValue(decimalPdfNumber(value));
}

PdfValue rectangleValue(const QRectF &rect)
{
    return PdfValue::arrayValue({numericValue(rect.left()),
                                 numericValue(rect.top()),
                                 numericValue(rect.right()),
                                 numericValue(rect.bottom())});
}

QByteArray appearanceStreamForRect(const QRectF &rect)
{
    const double inset = std::min(1.5, std::min(rect.width(), rect.height()) / 4.0);
    const double width = std::max(0.01, rect.width() - inset * 2.0);
    const double height = std::max(0.01, rect.height() - inset * 2.0);
    return QByteArray("q\n/PinloomGS gs\n1 0.82 0.18 rg\n1 0.56 0 RG\n2 w\n")
        + decimalPdfNumber(inset) + ' ' + decimalPdfNumber(inset) + ' '
        + decimalPdfNumber(width) + ' ' + decimalPdfNumber(height)
        + " re B\nQ\n";
}

bool writeAll(QIODevice *device, const QByteArray &data, QString *error)
{
    qsizetype offset = 0;
    while (offset < data.size()) {
        const qint64 written = device->write(data.constData() + offset, data.size() - offset);
        if (written <= 0) {
            if (error) *error = QStringLiteral("Unable to write Pinloom Preview PDF");
            return false;
        }
        offset += static_cast<qsizetype>(written);
    }
    return true;
}

struct WrittenXrefEntry {
    int objectNumber = -1;
    int generation = 0;
    qint64 offset = -1;
};

bool writeIndirectObject(QIODevice *device,
                         int objectNumber,
                         int generation,
                         const PdfValue &value,
                         const QByteArray &stream,
                         QVector<WrittenXrefEntry> *xrefEntries,
                         QString *error)
{
    const qint64 offset = device->pos();
    QByteArray serialized = QByteArray::number(objectNumber) + ' '
        + QByteArray::number(generation) + " obj\n"
        + serializePdfValue(value, error);
    if (serialized.endsWith(" obj\n")) return false;
    serialized.append('\n');
    if (!stream.isNull()) {
        serialized.append("stream\n");
        serialized.append(stream);
        if (!stream.endsWith('\n')) serialized.append('\n');
        serialized.append("endstream\n");
    }
    serialized.append("endobj\n");
    if (!writeAll(device, serialized, error)) return false;
    xrefEntries->append({objectNumber, generation, offset});
    return true;
}

bool sameFingerprint(const PdfSourceFingerprint &left,
                     const PdfSourceFingerprint &right)
{
    return left.success() && right.success()
        && left.normalizedPath == right.normalizedPath
        && left.size == right.size
        && left.modifiedMilliseconds == right.modifiedMilliseconds
        && left.sha256 == right.sha256;
}

bool cachedPreviewLooksComplete(const QString &path,
                                qint64 sourceSize,
                                const QByteArray &cacheKey)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= sourceSize) return false;
    if (file.read(8).startsWith("%PDF-") == false) return false;
    const qint64 tailSize = std::min<qint64>(file.size(), 1024 * 1024);
    if (!file.seek(file.size() - tailSize)) return false;
    const QByteArray tail = file.read(tailSize);
    const QByteArray marker = QByteArray("% Pinloom Preview ")
        + kPresentationVersion + ' ' + cacheKey;
    return tail.contains(marker) && tail.contains("%%EOF");
}

QString normalizedSourcePath(const QFileInfo &fileInfo)
{
    QString path = fileInfo.canonicalFilePath();
    if (path.isEmpty()) path = fileInfo.absoluteFilePath();
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

QString safePreviewBaseName(QString name)
{
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (QChar &character : name) {
        if (character.unicode() < 32 || invalid.contains(character)) {
            character = QLatin1Char('_');
        }
    }
    name = name.simplified();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) name.chop(1);
    if (name.isEmpty()) name = QStringLiteral("document");
    if (name.size() > 48) name.truncate(48);
    return name;
}

bool appendIncrementalAnnotation(const PdfAnnotatedCopyRequest &request,
                                 const PdfSourceFingerprint &fingerprint,
                                 const QString &outputPath,
                                 PdfAnnotatedCopyResult *result)
{
    QString error;
    PdfDocument document;
    if (!document.open(request.sourceFilePath, &error)) {
        result->error = error;
        return false;
    }

    PdfValue pageReference;
    PdfObject pageObject;
    PdfPageGeometry geometry;
    PdfObject catalogObject;
    if (!document.findPage(request.pageNumber,
                           &pageReference,
                           &pageObject,
                           &geometry,
                           &catalogObject,
                           &error)) {
        result->error = error;
        return false;
    }
    const QRectF annotationRect = annotationRectForPage(
        request.anchorRect, request.coordinateSpace, geometry, &error);
    if (!annotationRect.isValid()) {
        result->error = error;
        return false;
    }

    const int annotationObjectNumber = document.nextObjectNumber();
    if (annotationObjectNumber >= std::numeric_limits<int>::max() - 1) {
        result->error = QStringLiteral("PDF object number limit was reached");
        return false;
    }
    const int appearanceObjectNumber = annotationObjectNumber + 1;
    const PdfValue annotationReference = PdfValue::referenceValue(annotationObjectNumber);
    const PdfValue appearanceReference = PdfValue::referenceValue(appearanceObjectNumber);

    PdfValue updatedPage = pageObject.value;
    QVector<PdfValue> annotations;
    if (const PdfValue *existingAnnotations = dictionaryValue(updatedPage, "Annots")) {
        PdfValue resolvedAnnotations = *existingAnnotations;
        if (resolvedAnnotations.kind == PdfValueKind::Reference) {
            PdfObject annotationsObject;
            if (!document.loadObject(resolvedAnnotations, &annotationsObject, &error)) {
                result->error = QStringLiteral("Unable to read existing PDF annotations: %1").arg(error);
                return false;
            }
            resolvedAnnotations = annotationsObject.value;
        }
        if (resolvedAnnotations.kind != PdfValueKind::Null
            && resolvedAnnotations.kind != PdfValueKind::Array) {
            result->error = QStringLiteral("PDF page has an invalid Annots value");
            return false;
        }
        if (resolvedAnnotations.kind == PdfValueKind::Array) {
            annotations = resolvedAnnotations.array;
        }
    }
    annotations.append(annotationReference);
    updatedPage.dictionary.insert("Annots", PdfValue::arrayValue(annotations));

    PdfValue updatedCatalog = catalogObject.value;
    updatedCatalog.dictionary.insert(
        "OpenAction",
        PdfValue::arrayValue({pageReference,
                              PdfValue::nameValue("XYZ"),
                              numericValue(annotationRect.left()),
                              numericValue(annotationRect.bottom()),
                              PdfValue::nullValue()}));

    const QByteArray appearanceStream = appearanceStreamForRect(annotationRect);
    PdfValue graphicState = PdfValue::dictionaryValue({
        {"Type", PdfValue::nameValue("ExtGState")},
        {"CA", numericValue(0.92)},
        {"ca", numericValue(0.22)},
        {"BM", PdfValue::nameValue("Multiply")},
    });
    PdfValue appearanceResources = PdfValue::dictionaryValue({
        {"ExtGState", PdfValue::dictionaryValue({
             {"PinloomGS", graphicState},
         })},
    });
    PdfValue appearanceDictionary = PdfValue::dictionaryValue({
        {"Type", PdfValue::nameValue("XObject")},
        {"Subtype", PdfValue::nameValue("Form")},
        {"FormType", PdfValue::numberValue("1")},
        {"BBox", rectangleValue(QRectF(0, 0, annotationRect.width(), annotationRect.height()))},
        {"Resources", appearanceResources},
        {"Length", PdfValue::numberValue(QByteArray::number(appearanceStream.size()))},
    });
    QByteArray annotationName = request.anchorId.trimmed().toUtf8();
    if (annotationName.isEmpty()) annotationName = result->cacheKey.left(24);
    PdfValue annotationDictionary = PdfValue::dictionaryValue({
        {"Type", PdfValue::nameValue("Annot")},
        {"Subtype", PdfValue::nameValue("Square")},
        {"Rect", rectangleValue(annotationRect)},
        {"P", pageReference},
        {"F", PdfValue::numberValue("4")},
        {"NM", PdfValue::stringValue(QByteArray("Pinloom Preview:") + annotationName)},
        {"Contents", PdfValue::stringValue("Pinloom rectangle anchor preview")},
        {"C", PdfValue::arrayValue({numericValue(1.0), numericValue(0.56), numericValue(0.0)})},
        {"IC", PdfValue::arrayValue({numericValue(1.0), numericValue(0.82), numericValue(0.18)})},
        {"CA", numericValue(0.92)},
        {"BS", PdfValue::dictionaryValue({
             {"Type", PdfValue::nameValue("Border")},
             {"W", numericValue(2.0)},
             {"S", PdfValue::nameValue("S")},
         })},
        {"AP", PdfValue::dictionaryValue({{"N", appearanceReference}})},
    });

    const QFileInfo outputInfo(outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        result->error = QStringLiteral("Unable to create Pinloom Preview cache directory");
        return false;
    }
    if (normalizedSourcePath(QFileInfo(request.sourceFilePath))
        == normalizedSourcePath(outputInfo)) {
        result->error = QStringLiteral("Pinloom Preview output must not replace the source PDF");
        return false;
    }
    QSaveFile output(outputPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        result->error = QStringLiteral("Unable to create Pinloom Preview PDF: %1")
                            .arg(output.errorString());
        return false;
    }
    QFile source(request.sourceFilePath);
    if (!source.open(QIODevice::ReadOnly)) {
        output.cancelWriting();
        result->error = QStringLiteral("Unable to reopen source PDF: %1").arg(source.errorString());
        return false;
    }
    QByteArray buffer(1024 * 1024, '\0');
    while (true) {
        const qint64 read = source.read(buffer.data(), buffer.size());
        if (read < 0) {
            output.cancelWriting();
            result->error = QStringLiteral("Unable to copy source PDF: %1").arg(source.errorString());
            return false;
        }
        if (read == 0) break;
        if (!writeAll(&output, QByteArray::fromRawData(buffer.constData(), read), &error)) {
            output.cancelWriting();
            result->error = error;
            return false;
        }
    }
    if (!document.data().endsWith('\n') && !writeAll(&output, "\n", &error)) {
        output.cancelWriting();
        result->error = error;
        return false;
    }
    const QByteArray previewMarker = QByteArray("% Pinloom Preview ")
        + kPresentationVersion + ' ' + result->cacheKey + "\n";
    if (!writeAll(&output, previewMarker, &error)) {
        output.cancelWriting();
        result->error = error;
        return false;
    }

    QVector<WrittenXrefEntry> xrefEntries;
    if (!writeIndirectObject(&output,
                             appearanceObjectNumber,
                             0,
                             appearanceDictionary,
                             appearanceStream,
                             &xrefEntries,
                             &error)
        || !writeIndirectObject(&output,
                                annotationObjectNumber,
                                0,
                                annotationDictionary,
                                QByteArray(),
                                &xrefEntries,
                                &error)
        || !writeIndirectObject(&output,
                                pageObject.objectNumber,
                                pageObject.generation,
                                updatedPage,
                                QByteArray(),
                                &xrefEntries,
                                &error)
        || !writeIndirectObject(&output,
                                catalogObject.objectNumber,
                                catalogObject.generation,
                                updatedCatalog,
                                QByteArray(),
                                &xrefEntries,
                                &error)) {
        output.cancelWriting();
        result->error = error;
        return false;
    }

    std::sort(xrefEntries.begin(), xrefEntries.end(),
              [](const WrittenXrefEntry &left, const WrittenXrefEntry &right) {
        return left.objectNumber < right.objectNumber;
    });
    const qint64 newXrefOffset = output.pos();
    if (!writeAll(&output, "xref\n", &error)) {
        output.cancelWriting();
        result->error = error;
        return false;
    }
    int index = 0;
    while (index < xrefEntries.size()) {
        int end = index + 1;
        while (end < xrefEntries.size()
               && xrefEntries.at(end).objectNumber
                      == xrefEntries.at(end - 1).objectNumber + 1) {
            ++end;
        }
        QByteArray subsection = QByteArray::number(xrefEntries.at(index).objectNumber)
            + ' ' + QByteArray::number(end - index) + '\n';
        for (int entryIndex = index; entryIndex < end; ++entryIndex) {
            const WrittenXrefEntry &entry = xrefEntries.at(entryIndex);
            if (entry.offset < 0 || entry.offset > 9999999999LL
                || entry.generation < 0 || entry.generation > 99999) {
                output.cancelWriting();
                result->error = QStringLiteral("Pinloom Preview PDF exceeds classic xref limits");
                return false;
            }
            subsection.append(QByteArray::number(entry.offset).rightJustified(10, '0'));
            subsection.append(' ');
            subsection.append(QByteArray::number(entry.generation).rightJustified(5, '0'));
            subsection.append(" n \n");
        }
        if (!writeAll(&output, subsection, &error)) {
            output.cancelWriting();
            result->error = error;
            return false;
        }
        index = end;
    }

    PdfValue trailer = document.trailer();
    const QList<QByteArray> xrefStreamOnlyKeys = {
        "Type", "W", "Index", "Length", "Filter", "DecodeParms", "XRefStm"
    };
    for (const QByteArray &key : xrefStreamOnlyKeys) trailer.dictionary.remove(key);
    trailer.dictionary.remove("Encrypt");
    trailer.dictionary.insert("Size", PdfValue::numberValue(
        QByteArray::number(appearanceObjectNumber + 1)));
    trailer.dictionary.insert("Root", document.rootReference());
    trailer.dictionary.insert("Prev", PdfValue::numberValue(
        QByteArray::number(document.startXref())));
    const QByteArray trailerBytes = QByteArray("trailer\n")
        + serializePdfValue(trailer, &error)
        + "\nstartxref\n" + QByteArray::number(newXrefOffset)
        + "\n%%EOF\n";
    if (!error.isEmpty() || !writeAll(&output, trailerBytes, &error)) {
        output.cancelWriting();
        result->error = error.isEmpty()
            ? QStringLiteral("Unable to serialize Pinloom Preview trailer")
            : error;
        return false;
    }

    const PdfSourceFingerprint afterWrite = fingerprintPdfSource(request.sourceFilePath);
    if (!sameFingerprint(fingerprint, afterWrite)) {
        output.cancelWriting();
        result->error = QStringLiteral(
            "Source PDF changed while Pinloom Preview was being generated");
        return false;
    }
    if (!output.commit()) {
        result->error = QStringLiteral("Unable to atomically publish Pinloom Preview PDF: %1")
                            .arg(output.errorString());
        return false;
    }
    result->pageGeometry = geometry;
    result->annotationRect = annotationRect;
    return true;
}

} // namespace

bool PdfSourceFingerprint::success() const
{
    return error.isEmpty() && !normalizedPath.isEmpty()
        && size >= 0 && modifiedMilliseconds >= 0 && sha256.size() == 32;
}

bool PdfPageGeometry::isValid() const
{
    return mediaBox.isValid() && cropBox.isValid()
        && mediaBox.contains(cropBox)
        && rotation >= 0 && rotation < 360
        && std::isfinite(userUnit) && userUnit > 0.0;
}

bool PdfAnnotatedCopyResult::success() const
{
    return error.isEmpty() && !outputFilePath.trimmed().isEmpty();
}

QString defaultPdfAnchorPresentationCacheDirectory()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.isEmpty()) {
        root = QDir::tempPath() + QStringLiteral("/Pinloom");
    }
    return QDir(root).filePath(QStringLiteral("pdf-anchor-presentations"));
}

PdfSourceFingerprint fingerprintPdfSource(const QString &sourceFilePath)
{
    PdfSourceFingerprint result;
    const QFileInfo fileInfo(sourceFilePath.trimmed());
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.error = QStringLiteral("Source PDF does not exist: %1").arg(sourceFilePath);
        return result;
    }
    result.normalizedPath = normalizedSourcePath(fileInfo);
    result.size = fileInfo.size();
    result.modifiedMilliseconds = fileInfo.lastModified().toMSecsSinceEpoch();
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Unable to read source PDF: %1").arg(file.errorString());
        return result;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, '\0');
    while (true) {
        const qint64 read = file.read(buffer.data(), buffer.size());
        if (read < 0) {
            result.error = QStringLiteral("Unable to fingerprint source PDF: %1")
                               .arg(file.errorString());
            return result;
        }
        if (read == 0) break;
        hash.addData(QByteArrayView(buffer.constData(), read));
    }
    result.sha256 = hash.result();
    return result;
}

QByteArray pdfAnchorPresentationCacheKey(const PdfAnnotatedCopyRequest &request,
                                         const PdfSourceFingerprint &fingerprint)
{
    if (!fingerprint.success()) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto add = [&hash](const QByteArray &value) {
        hash.addData(QByteArray::number(value.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(value);
        hash.addData(QByteArrayView("\n", 1));
    };
    add(QByteArray(kPresentationVersion));
    add(fingerprint.normalizedPath.toUtf8());
    add(QByteArray::number(fingerprint.size));
    add(QByteArray::number(fingerprint.modifiedMilliseconds));
    add(fingerprint.sha256.toHex());
    add(request.anchorId.trimmed().toUtf8());
    add(request.locatorJson.trimmed().toUtf8());
    add(QByteArray::number(request.pageNumber));
    add(decimalPdfNumber(request.anchorRect.left()));
    add(decimalPdfNumber(request.anchorRect.top()));
    add(decimalPdfNumber(request.anchorRect.right()));
    add(decimalPdfNumber(request.anchorRect.bottom()));
    add(request.coordinateSpace == PdfAnchorCoordinateSpace::PdfUserSpace
            ? QByteArray("pdf-user-space")
            : QByteArray("page-top-left"));
    return hash.result().toHex();
}

QString pdfAnchorPresentationCacheFilePath(const PdfAnnotatedCopyRequest &request,
                                           const PdfSourceFingerprint &fingerprint)
{
    const QByteArray key = pdfAnchorPresentationCacheKey(request, fingerprint);
    if (key.isEmpty()) return {};
    const QString cacheDirectory = request.cacheDirectory.trimmed().isEmpty()
        ? defaultPdfAnchorPresentationCacheDirectory()
        : QDir::cleanPath(request.cacheDirectory.trimmed());
    const QString baseName = safePreviewBaseName(
        QFileInfo(request.sourceFilePath).completeBaseName());
    const QString fileName = QStringLiteral("%1 - Pinloom Preview - %2.pinloom-preview.pdf")
                                 .arg(baseName, QString::fromLatin1(key));
    return QDir(cacheDirectory).filePath(fileName);
}

PdfAnnotatedCopyResult preparePdfAnnotatedCopy(const PdfAnnotatedCopyRequest &request)
{
    PdfAnnotatedCopyResult result;
    result.sourceFilePath = QFileInfo(request.sourceFilePath.trimmed()).absoluteFilePath();
    if (request.pageNumber <= 0) {
        result.error = QStringLiteral("PDF anchor page is missing");
        return result;
    }
    if (!request.anchorRect.isValid()) {
        result.error = QStringLiteral("PDF anchor rectangle is missing");
        return result;
    }
    result.sourceFingerprint = fingerprintPdfSource(result.sourceFilePath);
    if (!result.sourceFingerprint.success()) {
        result.error = result.sourceFingerprint.error;
        return result;
    }
    result.cacheKey = pdfAnchorPresentationCacheKey(request, result.sourceFingerprint);
    result.outputFilePath = pdfAnchorPresentationCacheFilePath(request, result.sourceFingerprint);
    if (result.outputFilePath.isEmpty()) {
        result.error = QStringLiteral("Unable to resolve Pinloom Preview cache path");
        return result;
    }
    if (cachedPreviewLooksComplete(result.outputFilePath,
                                   result.sourceFingerprint.size,
                                   result.cacheKey)) {
        result.cacheHit = true;
        return result;
    }
    if (!appendIncrementalAnnotation(request,
                                     result.sourceFingerprint,
                                     result.outputFilePath,
                                     &result)) {
        result.outputFilePath.clear();
    }
    return result;
}

} // namespace Pinloom
