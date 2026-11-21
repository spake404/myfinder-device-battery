[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReleaseDirectory,[Parameter(Mandatory=$true)][string]$PrivateKeyPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Manifest.Common.ps1')
$directory=[IO.Path]::GetFullPath($ReleaseDirectory)
$sums=Join-Path $directory 'SHA256SUMS.txt'
if(-not (Test-Path -LiteralPath $sums -PathType Leaf) -or (Get-Item -LiteralPath $sums).Length -gt 262144){throw 'Release checksums are missing or too large.'}
if(-not (Test-Path -LiteralPath $PrivateKeyPath -PathType Leaf) -or (Get-Item -LiteralPath $PrivateKeyPath).Length -gt 65536){throw 'A protected release signing key or private key XML file is required.'}
$source=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'UpdateValidation.cs'))
$match=[regex]::Match($source,'internal const string PublicKeyXml = @"([^"]+)";')
if(-not $match.Success){throw 'The application public key constant could not be read.'}
$private=$null;$public=$null;$temporary=$null;$keyBytes=$null
try {
    $private=New-Object Security.Cryptography.RSACryptoServiceProvider
    $private.PersistKeyInCsp=$false
    try{
        if([IO.Path]::GetExtension($PrivateKeyPath) -ieq '.dpapi'){
            Add-Type -AssemblyName System.Security
            $keyBytes=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($PrivateKeyPath),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
            $private.FromXmlString([Text.Encoding]::UTF8.GetString($keyBytes))
        }else{$private.FromXmlString([IO.File]::ReadAllText($PrivateKeyPath))}
    }catch{throw 'Unable to load the release signing key with the current Windows account.'}
    if($private.PublicOnly){throw 'The supplied XML has no private key.'}
    $bytes=[IO.File]::ReadAllBytes($sums)
    $signature=$private.SignData($bytes,[Security.Cryptography.CryptoConfig]::MapNameToOID('SHA256'))
    $public=New-Object Security.Cryptography.RSACryptoServiceProvider
    $public.PersistKeyInCsp=$false;$public.FromXmlString($match.Groups[1].Value)
    if(-not $public.VerifyData($bytes,[Security.Cryptography.CryptoConfig]::MapNameToOID('SHA256'),$signature)){throw 'The signing key does not match the application public key; no signature was written.'}
    $release=Read-DeviceBatteryRelease (Split-Path -Parent $PSScriptRoot)
    $name='myfinder-device-battery-v'+$release.version+'-windows-x64'
    $lines=[Text.Encoding]::UTF8.GetString($bytes).TrimEnd("`r","`n") -split '\r?\n'
    if(@($lines).Count -ne 2){throw 'Expected one ZIP checksum and one Setup checksum.'}
    foreach($artifact in @($name+'.zip';$name+'-Setup.exe')){
        $hash=Get-DeviceBatteryFileHash (Join-Path $directory $artifact)
        if(($hash.ToLowerInvariant()+'  '+$artifact) -cnotin $lines){throw ('Release artifact checksum mismatch: '+$artifact)}
    }
    $destination=$sums+'.sig';$temporary=$destination+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllBytes($temporary,$signature)
    if(Test-Path -LiteralPath $destination){[IO.File]::Replace($temporary,$destination,[NullString]::Value)}else{[IO.File]::Move($temporary,$destination)}
    Write-Output 'SHA256SUMS.txt.sig created and verified with the built-in public key.'
}finally{
    if($keyBytes){[Array]::Clear($keyBytes,0,$keyBytes.Length)}
    if($private){$private.Dispose()};if($public){$public.Dispose()}
    if($temporary -and (Test-Path -LiteralPath $temporary)){Remove-Item -LiteralPath $temporary -Force}
}
