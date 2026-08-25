#include "pinloom/core/NativeAnchorCapture.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace Pinloom {

namespace {

QString captureScript(NativeAnchorApplication application)
{
    switch (application) {
    case NativeAnchorApplication::Word:
        return QString::fromUtf8(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
  $app = [Runtime.InteropServices.Marshal]::GetActiveObject('Word.Application')
  $doc = $app.ActiveDocument
  if ($null -eq $doc) { throw 'Word has no active document' }
  if ([string]::IsNullOrWhiteSpace($doc.Path)) { throw 'The Word document must be saved before capture' }
  $selection = $app.Selection
  if ($null -eq $selection) { throw 'Word has no active selection or caret' }
  $bookmarkName = ''
  foreach ($bookmark in @($doc.Bookmarks)) {
    if ($bookmark.Range.Start -le $selection.Start -and $bookmark.Range.End -ge $selection.End) {
      $bookmarkName = $bookmark.Name
      break
    }
  }
  $requiresMutation = [string]::IsNullOrWhiteSpace($bookmarkName)
  if ($requiresMutation) { $bookmarkName = '_Pinloom_' + [Guid]::NewGuid().ToString('N').Substring(0,12) }
  $selectedText = ([string]$selection.Text).Trim()
  $suggested = if ($selectedText) { $selectedText.Substring(0, [Math]::Min(64, $selectedText.Length)) } else { $doc.Name + ' position' }
  $locator = [ordered]@{ type='word.bookmark'; bookmark=$bookmarkName }
  $mutation = [ordered]@{ documentPath=$doc.FullName; start=[int]$selection.Start; end=[int]$selection.End; bookmark=$bookmarkName }
  [ordered]@{
    ok=$true; targetApp='Word'; targetFile=$doc.FullName; locatorType='word.bookmark'; locator=$locator;
    suggestedName=$suggested; provenance='word-com'; mutationRequired=$requiresMutation; mutationOptional=$false;
    mutationKind=($(if ($requiresMutation) { 'word.bookmark' } else { '' }));
    mutationLabel=($(if ($requiresMutation) { 'Allow Pinloom to create and save a Word bookmark' } else { '' }));
    mutationPayload=$mutation
  } | ConvertTo-Json -Compress -Depth 8
} catch {
  [ordered]@{ ok=$false; error=$_.Exception.Message } | ConvertTo-Json -Compress
}
)PS");
    case NativeAnchorApplication::Visio:
        return QString::fromUtf8(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
  $app = [Runtime.InteropServices.Marshal]::GetActiveObject('Visio.Application')
  $doc = $app.ActiveDocument
  if ($null -eq $doc) { throw 'Visio has no active document' }
  if ([string]::IsNullOrWhiteSpace($doc.Path)) { throw 'The Visio document must be saved before capture' }
  $window = $app.ActiveWindow
  if ($null -eq $window -or $window.Selection.Count -ne 1) { throw 'Select exactly one Visio shape before capture' }
  $shape = $window.Selection.Item(1)
  $uid = ''
  try { $uid = [string]$shape.UniqueID(0) } catch { $uid = '' }
  $requiresMutation = [string]::IsNullOrWhiteSpace($uid)
  $locator = [ordered]@{ type='visio.shape'; pageNameU=$shape.ContainingPage.NameU; shapeUniqueId=$uid; shapeId=[int]$shape.ID }
  $mutation = [ordered]@{ documentPath=$doc.FullName; pageNameU=$shape.ContainingPage.NameU; shapeId=[int]$shape.ID }
  [ordered]@{
    ok=$true; targetApp='Visio'; targetFile=$doc.FullName; locatorType='visio.shape'; locator=$locator;
    suggestedName=([string]$shape.NameU); provenance='visio-com'; mutationRequired=$requiresMutation; mutationOptional=$false;
    mutationKind=($(if ($requiresMutation) { 'visio.uniqueId' } else { '' }));
    mutationLabel=($(if ($requiresMutation) { 'Allow Pinloom to create and save a persistent Visio Shape UniqueID' } else { '' }));
    mutationPayload=$mutation
  } | ConvertTo-Json -Compress -Depth 8
} catch {
  [ordered]@{ ok=$false; error=$_.Exception.Message } | ConvertTo-Json -Compress
}
)PS");
    case NativeAnchorApplication::Excel:
        return QString::fromUtf8(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
  $app = [Runtime.InteropServices.Marshal]::GetActiveObject('Excel.Application')
  $workbook = $app.ActiveWorkbook
  $sheet = $app.ActiveSheet
  $selection = $app.Selection
  if ($null -eq $workbook -or $null -eq $sheet -or $null -eq $selection) { throw 'Excel has no active workbook range' }
  if ([string]::IsNullOrWhiteSpace($workbook.Path)) { throw 'The Excel workbook must be saved before capture' }
  $range = [string]$selection.Address($true, $true, 1)
  if ([string]::IsNullOrWhiteSpace($range)) { throw 'Select one Excel range before capture' }
  $definedName = ''
  try {
    $candidate = $selection.Name
    if ($null -ne $candidate) { $definedName = [string]$candidate.Name }
  } catch { $definedName = '' }
  if ($definedName) {
    $locatorType = 'excel.name'
    $locator = [ordered]@{ type=$locatorType; namedRange=$definedName; sheet=$sheet.Name; range=$range }
    $optionalMutation = $false
  } else {
    $locatorType = 'excel.range'
    $locator = [ordered]@{ type=$locatorType; sheet=$sheet.Name; range=$range }
    $optionalMutation = $true
  }
  $proposed = '_Pinloom_' + [Guid]::NewGuid().ToString('N').Substring(0,12)
  $mutation = [ordered]@{ workbookPath=$workbook.FullName; sheet=$sheet.Name; range=$range; definedName=$proposed }
  [ordered]@{
    ok=$true; targetApp='Excel'; targetFile=$workbook.FullName; locatorType=$locatorType; locator=$locator;
    suggestedName=($sheet.Name + ' ' + $range); provenance='excel-com'; mutationRequired=$false; mutationOptional=$optionalMutation;
    mutationKind=($(if ($optionalMutation) { 'excel.definedName' } else { '' }));
    mutationLabel=($(if ($optionalMutation) { 'Create and save a Pinloom-owned Excel defined name for relocation stability' } else { '' }));
    mutationPayload=$mutation
  } | ConvertTo-Json -Compress -Depth 8
} catch {
  [ordered]@{ ok=$false; error=$_.Exception.Message } | ConvertTo-Json -Compress
}
)PS");
    case NativeAnchorApplication::Unknown:
        break;
    }
    return {};
}

QString powerShellLiteral(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QJsonObject payloadObject(const AnchorCaptureDraft &draft)
{
    return QJsonDocument::fromJson(draft.mutationPayloadJson.toUtf8()).object();
}

QString finalizationScript(const AnchorCaptureDraft &draft)
{
    const QJsonObject payload = payloadObject(draft);
    if (draft.mutationKind == QLatin1String("word.bookmark")) {
        return QString::fromUtf8(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
  $app = [Runtime.InteropServices.Marshal]::GetActiveObject('Word.Application')
  $doc = $app.ActiveDocument
  if ($null -eq $doc -or $doc.FullName -ne %1) { throw 'The captured Word document is no longer active' }
  if ($doc.ReadOnly) { throw 'The Word document is read-only' }
  $range = $doc.Range(%2, %3)
  [void]$doc.Bookmarks.Add(%4, $range)
  $doc.Save()
  [ordered]@{ ok=$true; locatorType='word.bookmark'; locator=[ordered]@{ type='word.bookmark'; bookmark=%4 } } | ConvertTo-Json -Compress -Depth 5
} catch { [ordered]@{ ok=$false; error=$_.Exception.Message } | ConvertTo-Json -Compress }
)PS")
            .arg(powerShellLiteral(payload.value(QStringLiteral("documentPath")).toString()),
                 QString::number(payload.value(QStringLiteral("start")).toInt()),
                 QString::number(payload.value(QStringLiteral("end")).toInt()),
                 powerShellLiteral(payload.value(QStringLiteral("bookmark")).toString()));
    }
    if (draft.mutationKind == QLatin1String("visio.uniqueId")) {
        return QString::fromUtf8(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
  $app = [Runtime.InteropServices.Marshal]::GetActiveObject('Visio.Application')
  $doc = $app.ActiveDocument
  if ($null -eq $doc -or $doc.FullName -ne %1) { throw 'The captured Visio document is no longer active' }
  if ($doc.ReadOnly) { throw 'The Visio document is read-only' }
  $page = $doc.Pages.ItemU(%2)
  $shape = $page.Shapes.ItemFromID(%3)
  $uid = [string]$shape.UniqueID(1)
  $doc.Save()
  [ordered]@{ ok=$true; locatorType='visio.shape'; locator=[ordered]@{ type='visio.shape'; pageNameU=%2; shapeUniqueId=$uid } } | ConvertTo-Json -Compress -Depth 5
} catch { [ordered]@{ ok=$false; error=$_.Exception.Message } | ConvertTo-Json -Compress }
)PS")
            .arg(powerShellLiteral(payload.value(QStringLiteral("documentPath")).toString()),
                 powerShellLiteral(payload.value(QStringLiteral("pageNameU")).toString()),
                 QString::number(payload.value(QStringLiteral("shapeId")).toInt()));
    }
    if (draft.mutationKind == QLatin1String("excel.definedName")) {
        return QString::fromUtf8(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
  $app = [Runtime.InteropServices.Marshal]::GetActiveObject('Excel.Application')
  $workbook = $app.ActiveWorkbook
  if ($null -eq $workbook -or $workbook.FullName -ne %1) { throw 'The captured Excel workbook is no longer active' }
  if ($workbook.ReadOnly) { throw 'The Excel workbook is read-only' }
  $sheet = $workbook.Worksheets.Item(%2)
  $range = $sheet.Range(%3)
  [void]$workbook.Names.Add(%4, $range)
  $workbook.Save()
  [ordered]@{ ok=$true; locatorType='excel.name'; locator=[ordered]@{ type='excel.name'; namedRange=%4; sheet=%2; range=%3 } } | ConvertTo-Json -Compress -Depth 5
} catch { [ordered]@{ ok=$false; error=$_.Exception.Message } | ConvertTo-Json -Compress }
)PS")
            .arg(powerShellLiteral(payload.value(QStringLiteral("workbookPath")).toString()),
                 powerShellLiteral(payload.value(QStringLiteral("sheet")).toString()),
                 powerShellLiteral(payload.value(QStringLiteral("range")).toString()),
                 powerShellLiteral(payload.value(QStringLiteral("definedName")).toString()));
    }
    return {};
}

QJsonObject resultObject(const NativeCaptureScriptResult &scriptResult,
                         QString *error)
{
    if (error) error->clear();
    if (!scriptResult.success()) {
        if (error) {
            *error = scriptResult.error.trimmed().isEmpty()
                ? scriptResult.standardError.trimmed()
                : scriptResult.error.trimmed();
        }
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        scriptResult.standardOutput.trimmed().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Native capture returned invalid JSON");
        return {};
    }
    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("ok")).toBool()) {
        if (error) {
            *error = object.value(QStringLiteral("error")).toString(
                QStringLiteral("Native capture failed"));
        }
        return {};
    }
    return object;
}

NativeAnchorCaptureResult captureResultFromObject(const QJsonObject &object)
{
    NativeAnchorCaptureResult result;
    AnchorCaptureDraft &draft = result.draft;
    draft.targetApp = object.value(QStringLiteral("targetApp")).toString();
    draft.targetFile = object.value(QStringLiteral("targetFile")).toString();
    draft.locatorType = object.value(QStringLiteral("locatorType")).toString();
    draft.locatorJson = QString::fromUtf8(QJsonDocument(
        object.value(QStringLiteral("locator")).toObject())
                                                 .toJson(QJsonDocument::Compact));
    draft.suggestedName = object.value(QStringLiteral("suggestedName")).toString();
    draft.provenance = object.value(QStringLiteral("provenance")).toString();
    draft.mutationRequired = object.value(QStringLiteral("mutationRequired")).toBool();
    draft.mutationOptional = object.value(QStringLiteral("mutationOptional")).toBool();
    draft.mutationKind = object.value(QStringLiteral("mutationKind")).toString();
    draft.mutationLabel = object.value(QStringLiteral("mutationLabel")).toString();
    draft.mutationPayloadJson = QString::fromUtf8(QJsonDocument(
        object.value(QStringLiteral("mutationPayload")).toObject())
                                                       .toJson(QJsonDocument::Compact));
    QString validationError;
    AnchorCaptureDraft validationDraft = draft;
    validationDraft.mutationAuthorized = validationDraft.mutationRequired;
    if (!validationDraft.isValid(&validationError)) {
        result.error = validationError;
    }
    return result;
}

} // namespace

bool NativeCaptureScriptResult::success() const
{
    return error.isEmpty();
}

bool NativeAnchorCaptureResult::success() const
{
    return error.isEmpty();
}

NativeAnchorApplication nativeAnchorApplicationForProcess(
    const QString &processName)
{
    const QString folded = processName.trimmed().toCaseFolded();
    if (folded.contains(QStringLiteral("winword"))
        || folded == QLatin1String("word.exe")) {
        return NativeAnchorApplication::Word;
    }
    if (folded.contains(QStringLiteral("visio"))) {
        return NativeAnchorApplication::Visio;
    }
    if (folded.contains(QStringLiteral("excel"))) {
        return NativeAnchorApplication::Excel;
    }
    return NativeAnchorApplication::Unknown;
}

NativeCaptureScriptResult runNativeCapturePowerShell(
    const QString &script,
    int timeoutMilliseconds)
{
    NativeCaptureScriptResult result;
#ifdef Q_OS_WIN
    QProcess process;
    process.setProgram(QStringLiteral("powershell.exe"));
    process.setArguments({QStringLiteral("-NoLogo"),
                          QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"),
                          QStringLiteral("-ExecutionPolicy"),
                          QStringLiteral("Bypass"),
                          QStringLiteral("-Command"),
                          script});
    process.start();
    if (!process.waitForStarted(1500)) {
        result.error = QStringLiteral("Unable to start PowerShell for native capture");
        return result;
    }
    if (!process.waitForFinished(qMax(1000, timeoutMilliseconds))) {
        process.kill();
        process.waitForFinished(1000);
        result.error = QStringLiteral("Native capture timed out");
        return result;
    }
    result.standardOutput = QString::fromUtf8(process.readAllStandardOutput());
    result.standardError = QString::fromUtf8(process.readAllStandardError());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = result.standardError.trimmed().isEmpty()
            ? QStringLiteral("Native capture process failed")
            : result.standardError.trimmed();
    }
#else
    Q_UNUSED(script)
    Q_UNUSED(timeoutMilliseconds)
    result.error = QStringLiteral("Native Office capture is available only on Windows");
#endif
    return result;
}

NativeAnchorCaptureAdapter::NativeAnchorCaptureAdapter(
    NativeCaptureScriptRunner runner)
    : runner_(std::move(runner))
{
}

NativeAnchorCaptureResult NativeAnchorCaptureAdapter::capture(
    NativeAnchorApplication application) const
{
    NativeAnchorCaptureResult result;
    const QString script = captureScript(application);
    if (script.isEmpty()) {
        result.error = QStringLiteral("The remembered application does not support native Anchor capture");
        return result;
    }
    if (!runner_) {
        result.error = QStringLiteral("Native capture runner is unavailable");
        return result;
    }
    QString error;
    const QJsonObject object = resultObject(runner_(script, 8000), &error);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }
    return captureResultFromObject(object);
}

NativeAnchorCaptureResult NativeAnchorCaptureAdapter::captureForProcess(
    const QString &processName) const
{
    return capture(nativeAnchorApplicationForProcess(processName));
}

NativeAnchorCaptureResult NativeAnchorCaptureAdapter::finalize(
    const AnchorCaptureDraft &draft) const
{
    NativeAnchorCaptureResult result;
    result.draft = draft;
    if (!draft.mutationRequired && !draft.mutationOptional) {
        return result;
    }
    if (!draft.mutationAuthorized) {
        if (draft.mutationRequired) {
            result.error = QStringLiteral("Document mutation authorization is required");
        } else {
            result.draft.mutationOptional = false;
            result.draft.mutationKind.clear();
            result.draft.mutationLabel.clear();
            result.draft.mutationPayloadJson.clear();
        }
        return result;
    }

    const QString script = finalizationScript(draft);
    if (script.isEmpty() || !runner_) {
        result.error = QStringLiteral("Native locator finalization is unavailable");
        return result;
    }
    QString error;
    const QJsonObject object = resultObject(runner_(script, 12000), &error);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }
    result.draft.locatorType = object.value(QStringLiteral("locatorType")).toString();
    result.draft.locatorJson = QString::fromUtf8(QJsonDocument(
        object.value(QStringLiteral("locator")).toObject())
                                                     .toJson(QJsonDocument::Compact));
    result.draft.mutationRequired = false;
    result.draft.mutationOptional = false;
    result.draft.mutationKind.clear();
    result.draft.mutationLabel.clear();
    result.draft.mutationPayloadJson.clear();
    result.draft.mutationAuthorized = false;
    return result;
}

} // namespace Pinloom
