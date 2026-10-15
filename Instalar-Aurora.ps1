# Instale a partir da pasta extraída do pacote Windows.
$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$target = Join-Path $env:ProgramFiles 'Common Files\VST3'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Abra o PowerShell como administrador e execute este arquivo novamente.'
}
$names = @('Aurora.vst3', 'Aurora FX.vst3')
foreach ($name in $names) {
    $binary = Join-Path $source "$name\Contents\x86_64-win\$name"
    if (-not (Test-Path -LiteralPath $binary)) { throw "Pacote incompleto. Extraia o ZIP Windows antes de instalar: $binary" }
    $existing = Join-Path $target $name
    if ((Test-Path -LiteralPath $existing) -and -not (Test-Path -LiteralPath $existing -PathType Container)) {
        throw "Já existe um arquivo com o nome $name. Preserve-o antes de instalar este pacote em formato de pasta."
    }
}
$backup = Join-Path $source ('Backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($name in $names) {
    $existing = Join-Path $target $name
    if (Test-Path -LiteralPath $existing) {
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        Copy-Item -LiteralPath $existing -Destination $backup -Recurse
        $relative = "$name\Contents\x86_64-win\$name"
        if ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $backup $relative)).Hash) {
            throw "Backup não pôde ser verificado: $name. Atualização cancelada."
        }
    }
}
foreach ($name in $names) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $target -Recurse -Force
    $relative = "$name\Contents\x86_64-win\$name"
    if ((Get-FileHash -LiteralPath (Join-Path $source $relative)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash) {
        throw "Falha de integridade: $name. O backup, quando existente, está em $backup."
    }
}
"Aurora 0.2.3 e Aurora FX instalados em $target. Backup anterior: $backup" | Set-Content -LiteralPath (Join-Path $source 'installation-result.txt')
Write-Host 'No FL Studio: Manage plugins > Verify plugins > Rescan previously verified plugins > Find installed plugins.'
