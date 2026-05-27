# ============================================================================
#  build.ps1 - Empaqueta, firma y despliega BrigadaZ_Transport
# ============================================================================
#  Uso:
#    .\build.ps1                  # build + deploy al server
#    .\build.ps1 -NoDeploy        # build, no copia al server
#    .\build.ps1 -Clean           # borra temp/ antes de empezar
#    .\build.ps1 -DeployClient    # tambien copia al DayZ del cliente (test local)
# ============================================================================

[CmdletBinding()]
param(
    [switch]$NoDeploy,
    [switch]$Clean,
    [switch]$DeployClient,
    [switch]$Yes
)

# ---- GUARDIA DE ENTORNO --------------------------------------------------------
$_homeMarker = "E:\BRIGADA Z PVE SERVER\MODS"
if (-not (Test-Path $_homeMarker)) {
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Red
    Write-Host "  BUILD DESHABILITADO EN ESTA PC" -ForegroundColor Red
    Write-Host "==========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Este script requiere las rutas de la PC de casa:" -ForegroundColor Yellow
    Write-Host "    Mods:   E:\BRIGADA Z PVE SERVER\MODS\BrigadaZ_Transport\" -ForegroundColor Yellow
    Write-Host "    Server: C:\DayZServer\@BrigadaZ_Transport\" -ForegroundColor Yellow
    Write-Host "    Key:    E:\BRIGADA Z PVE SERVER\KEYS\HercoKey.biprivatekey" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Fuente detectada (backup): $PSScriptRoot" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  Trabajá los scripts directamente acá." -ForegroundColor Green
    Write-Host "  El empaquetado y deploy lo retomas desde casa." -ForegroundColor Green
    Write-Host ""
    exit 0
}

# ---- RECORDATORIO POST-BACKUP --------------------------------------------------
Write-Host ""
Write-Host "==========================================" -ForegroundColor Magenta
Write-Host "  Venias editando desde el backup?" -ForegroundColor Magenta
Write-Host "==========================================" -ForegroundColor Magenta
Write-Host ""
Write-Host "  Antes de buildear, verifica:" -ForegroundColor Yellow
Write-Host "    1. Copiaste todos los cambios de scripts\ desde el backup" -ForegroundColor Yellow
Write-Host "    2. build.ps1 sigue en sync (rutas, parametros)" -ForegroundColor Yellow
Write-Host ""
Write-Host "  Ref backup: E:\BACKUP\mod\MOD-SCIPTS\BrigadaZ_Transport\" -ForegroundColor Cyan
Write-Host ""
if ($Yes) {
    Write-Host "  -Yes: skip confirmacion." -ForegroundColor DarkYellow
} else {
    $_confirm = Read-Host "  Continuar con el build? (s/n)"
    if ($_confirm -notmatch '^[sS]$') {
        Write-Host "Build cancelado. Revisa los cambios primero." -ForegroundColor DarkYellow
        exit 0
    }
}
Write-Host ""

$ErrorActionPreference = "Stop"

# ---- Paths -----------------------------------------------------------------
$ModSource    = $PSScriptRoot
$ModName      = "BrigadaZ_Transport"
$AddonBuilder = "C:\Program Files (x86)\Steam\steamapps\common\DayZ Tools\Bin\AddonBuilder\AddonBuilder.exe"
$PrivateKey   = "E:\BRIGADA Z PVE SERVER\KEYS\HercoKey.biprivatekey"
$IncludeList  = Join-Path $ModSource "build_include.lst"

$ModsRoot     = "E:\BRIGADA Z PVE SERVER\MODS\$ModName"
$ModsAddons   = Join-Path $ModsRoot "addons"

$TempRoot     = Join-Path (Split-Path -Parent $ModSource) ".build_$ModName"
$TempDir      = Join-Path $TempRoot "temp"

$ServerMod    = "C:\DayZServer\@$ModName"
$ClientMod    = "C:\Program Files (x86)\Steam\steamapps\common\DayZ\!Workshop\@$ModName"

# ---- Banner ----------------------------------------------------------------
Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  Build $ModName" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Source     : $ModSource"
Write-Host "Mods (out) : $ModsRoot"
Write-Host "Server     : $ServerMod"
Write-Host ""

# ---- Validaciones ----------------------------------------------------------
if (-not (Test-Path $AddonBuilder)) { Write-Host "ERROR: AddonBuilder no encontrado" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $PrivateKey))   { Write-Host "ERROR: Llave privada no encontrada en $PrivateKey" -ForegroundColor Red; exit 1 }
if (-not (Test-Path (Join-Path $ModSource "config.cpp"))) { Write-Host "ERROR: config.cpp no encontrado" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $IncludeList))  { Write-Host "ERROR: build_include.lst no encontrado" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $ModsRoot)) {
    Write-Host "ERROR: $ModsRoot no existe." -ForegroundColor Red
    Write-Host "  Crear con keys\HercoKey.bikey antes de buildear." -ForegroundColor Red
    exit 1
}

# ---- Clean opcional --------------------------------------------------------
if ($Clean -and (Test-Path $TempRoot)) {
    Write-Host "[clean] Eliminando $TempRoot ..." -ForegroundColor Yellow
    Remove-Item $TempRoot -Recurse -Force
}

if (Test-Path $ModsAddons) {
    Get-ChildItem $ModsAddons -Filter "$ModName.pbo*" -ErrorAction SilentlyContinue | Remove-Item -Force
} else {
    New-Item -ItemType Directory -Path $ModsAddons -Force | Out-Null
}

if (-not (Test-Path $TempDir)) { New-Item -ItemType Directory -Path $TempDir -Force | Out-Null }

# ---- AddonBuilder ----------------------------------------------------------
Write-Host "[1/3] Empaquetando..." -ForegroundColor Yellow

$abArgs = @(
    "`"$ModSource`"",
    "`"$ModsAddons`"",
    "-prefix=$ModName",
    "-sign=`"$PrivateKey`"",
    "-include=`"$IncludeList`"",
    "-temp=`"$TempDir`""
)

$startTime = Get-Date
& $AddonBuilder @abArgs
$exit = $LASTEXITCODE
$elapsed = ((Get-Date) - $startTime).TotalSeconds

if ($exit -ne 0) { Write-Host "ERROR: AddonBuilder fallo (exit=$exit)" -ForegroundColor Red; exit $exit }

$pboPath = Join-Path $ModsAddons "$ModName.pbo"
if (-not (Test-Path $pboPath)) { Write-Host "ERROR: PBO no generado" -ForegroundColor Red; exit 1 }

$pboSize = [math]::Round((Get-Item $pboPath).Length / 1MB, 2)
Write-Host "      OK: $ModName.pbo ($pboSize MB) en $([math]::Round($elapsed,1))s" -ForegroundColor Green

# ---- Verificar firma -------------------------------------------------------
Write-Host "[2/3] Verificando firma..." -ForegroundColor Yellow
$bisign = Get-ChildItem $ModsAddons -Filter "$ModName.pbo.*.bisign" | Select-Object -First 1
if (-not $bisign) { Write-Host "ERROR: No se genero .bisign" -ForegroundColor Red; exit 1 }
Write-Host "      OK: $($bisign.Name)" -ForegroundColor Green

# ---- Deploy al server ------------------------------------------------------
if ($NoDeploy) {
    Write-Host "[3/3] -NoDeploy: skip." -ForegroundColor DarkYellow
    Write-Host "Build OK en: $ModsRoot" -ForegroundColor Cyan
    exit 0
}

Write-Host "[3/3] Mirror -> $ServerMod ..." -ForegroundColor Yellow
if (-not (Test-Path $ServerMod)) { New-Item -ItemType Directory -Path $ServerMod -Force | Out-Null }

& robocopy $ModsRoot $ServerMod /MIR /NFL /NDL /NJH /NJS /NP /R:10 /W:1 | Out-Null
$rc = $LASTEXITCODE
if ($rc -ge 8) { Write-Host "ERROR: robocopy fallo (exit=$rc)" -ForegroundColor Red; exit $rc }
Write-Host "      OK: server sincronizado" -ForegroundColor Green

# ---- Deploy cliente --------------------------------------------------------
if ($DeployClient) {
    Write-Host "[client] Mirror -> $ClientMod ..." -ForegroundColor Yellow
    if (-not (Test-Path $ClientMod)) { New-Item -ItemType Directory -Path $ClientMod -Force | Out-Null }
    & robocopy $ModsRoot $ClientMod /MIR /NFL /NDL /NJH /NJS /NP /R:10 /W:1 | Out-Null
    $rcc = $LASTEXITCODE
    if ($rcc -ge 8) { Write-Host "ERROR: robocopy cliente fallo (exit=$rcc)" -ForegroundColor Red; exit $rcc }
    Write-Host "      OK: cliente sincronizado" -ForegroundColor Green
}

Write-Host ""
Write-Host "==========================================" -ForegroundColor Green
Write-Host "  Build & Deploy COMPLETO" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
Write-Host "  PBO:    $pboSize MB"
Write-Host "  Tiempo: $([math]::Round($elapsed,1))s"
Write-Host "  Server: $ServerMod"
Write-Host ""
exit 0
