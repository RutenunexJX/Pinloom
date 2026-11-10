#pragma once

#include <QString>

namespace Pinloom {

inline QString officeApplicationScript(const QString &variable, const QString &progId)
{
    return QStringLiteral("try { %1 = [Runtime.InteropServices.Marshal]::GetActiveObject('%2') } "
                          "catch { %1 = New-Object -ComObject %2 }").arg(variable, progId);
}

inline QString officeDocumentScript(const QString &variable, const QString &collection,
                                   const QString &quotedPath)
{
    return QStringLiteral("%1 = $null; foreach ($openDocument in %2) { "
                          "if ([string]::Equals([IO.Path]::GetFullPath([string]$openDocument.FullName), "
                          "[IO.Path]::GetFullPath(%3), [StringComparison]::OrdinalIgnoreCase)) "
                          "{ %1 = $openDocument; break } }; "
                          "if ($null -eq %1) { %1 = %2.Open(%3) }").arg(variable, collection, quotedPath);
}

// A zero exit code is accepted only with this marker, after selection verification.
inline QString completedOfficeJumpScript(const QString &body)
{
    return QStringLiteral("$ErrorActionPreference = 'Stop'; "
                          "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); try { %1; "
                          "[Console]::Out.WriteLine('PinloomOfficeLocated'); exit 0 "
                          "} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }").arg(body);
}

} // namespace Pinloom
