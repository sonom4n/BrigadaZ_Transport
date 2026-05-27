# ============================================================================
#  csv_to_route.ps1 — convierte un CSV del PathLogger en BZBusRoute.json
#
#  Uso:
#    .\csv_to_route.ps1                            # toma el CSV mas reciente
#    .\csv_to_route.ps1 -CsvPath "C:\...\path.csv" # CSV especifico
#    .\csv_to_route.ps1 -DryRun                    # no escribe nada, solo muestra
#
#  Lee:
#    - $CsvPath (default: ultimo CSV en %LOCALAPPDATA%\DayZ\BrigadaZ_Transport_PathLogger\)
#    - bus_stops.json (para mapear coordenadas de paradas a nombres reales)
#
#  Escribe:
#    - C:\DayZServer\profiles\BrigadaZ_Transport\BZBusRoute.json
#    - Backup automatico de la version anterior (.bak_HHMMSS)
# ============================================================================

param(
    [string]$CsvPath        = "",
    [string]$OutputPath     = "C:\DayZServer\profiles\BrigadaZ_Transport\BZBusRoute.json",
    [string]$StopsJsonPath  = "E:\BRIGADA Z PVE SERVER\MOD-SCIPTS\BrigadaZ_Transport\data\bus_stops.json",
    [string]$VehicleClass   = "ExpansionBus",
    [string]$DriverClass    = "eAI_SurvivorM_Boris",
    [int]   $RespawnDelay   = 300,
    [float] $AverageSpeedMS = 11.0,
    [int]   $StopDuration   = 2,
    [float] $StopRadius     = 60.0,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

# 1. Resolver CSV de entrada
if ($CsvPath -eq "") {
    $logDir = "$env:LOCALAPPDATA\DayZ\BrigadaZ_Transport_PathLogger"
    $latest = Get-ChildItem "$logDir\*.csv" -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw "No se encontro ningun CSV en $logDir" }
    $CsvPath = $latest.FullName
}
if (-not (Test-Path $CsvPath)) { throw "CSV no existe: $CsvPath" }

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  csv_to_route.ps1" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "CSV input:  $CsvPath"
Write-Host "JSON out:   $OutputPath"
Write-Host ""

# 2. Cargar bus_stops.json (para mapear coords -> nombres)
$stopsConfig = $null
if (Test-Path $StopsJsonPath) {
    $stopsConfig = Get-Content $StopsJsonPath -Raw | ConvertFrom-Json
    Write-Host "Paradas conocidas: $($stopsConfig.Stops.Count)"
} else {
    Write-Host "WARN: bus_stops.json no encontrado en $StopsJsonPath. Las paradas se nombran genericamente."
}

function Find-NearestStop {
    param([float]$x, [float]$z, [float]$maxDist = 50.0)
    if (-not $stopsConfig) { return $null }
    $closest = $null
    $closestDist = [float]::MaxValue
    foreach ($s in $stopsConfig.Stops) {
        $d = [Math]::Sqrt(($s.x - $x) * ($s.x - $x) + ($s.z - $z) * ($s.z - $z))
        if ($d -lt $closestDist) {
            $closestDist = $d
            $closest = $s
        }
    }
    if ($closestDist -le $maxDist) { return $closest }
    return $null
}

# 3. Leer CSV
$csvLines = Get-Content $CsvPath
$header = $csvLines[0] -split ","
$idxX        = [array]::IndexOf($header, "x")
$idxY        = [array]::IndexOf($header, "y")
$idxZ        = [array]::IndexOf($header, "z")
$idxSpeed    = [array]::IndexOf($header, "speed_kmh")
$idxIsStop   = [array]::IndexOf($header, "is_stop")
$idxGear     = [array]::IndexOf($header, "gear")
$idxThrottle = [array]::IndexOf($header, "throttle")
$idxBrake    = [array]::IndexOf($header, "brake")

if ($idxThrottle -lt 0 -or $idxBrake -lt 0) {
    Write-Host "WARN: el CSV no tiene columnas throttle/brake. Sera un recording viejo. hasInputData quedara en false." -ForegroundColor Yellow
}

$waypoints = @()
$stopIdx = 0

for ($i = 1; $i -lt $csvLines.Count; $i++) {
    $line = $csvLines[$i]
    if (-not $line.Trim()) { continue }
    $cols = $line -split ","

    $x = [float]$cols[$idxX]
    $y = [float]$cols[$idxY]
    $z = [float]$cols[$idxZ]
    $speedKmh = [float]$cols[$idxSpeed]
    $isStop   = ([int]$cols[$idxIsStop]) -eq 1
    $gear     = [int]$cols[$idxGear]

    $throttle = 0.0
    $brake    = 0.0
    $hasInput = $false
    if ($idxThrottle -ge 0 -and $idxBrake -ge 0) {
        $throttle = [float]$cols[$idxThrottle]
        $brake    = [float]$cols[$idxBrake]
        $hasInput = $true
    }

    $name = ""
    $stopDur = 0
    $stopRad = 0.0
    if ($isStop) {
        $stopIdx++
        $matchedStop = Find-NearestStop -x $x -z $z -maxDist 80.0
        if ($matchedStop) {
            $name = $matchedStop.name
        } else {
            $name = "Parada $stopIdx"
        }
        $stopDur = $StopDuration
        $stopRad = $StopRadius
    }

    $wp = [PSCustomObject]@{
        pos            = @($x, $y, $z)
        isStop         = $isStop
        name           = $name
        stopDuration   = $stopDur
        stopRadius     = $stopRad
        targetSpeed    = [Math]::Round($speedKmh, 2)
        targetGear     = $gear
        targetThrottle = [Math]::Round($throttle, 3)
        targetBrake    = [Math]::Round($brake, 3)
        hasInputData   = $hasInput
    }
    $waypoints += $wp
}

# 4. Construir JSON final
$route = [PSCustomObject]@{
    RespawnDelay   = $RespawnDelay
    AverageSpeedMS = $AverageSpeedMS
    VehicleClass   = $VehicleClass
    DriverClass    = $DriverClass
    Waypoints      = $waypoints
}

$json = $route | ConvertTo-Json -Depth 10

# 5. Backup y escribir (usando UTF8 sin BOM para no romper el parser de DayZ)
if (-not $DryRun) {
    if (Test-Path $OutputPath) {
        $ts = (Get-Date).ToString("HHmmss")
        $bak = "$OutputPath.bak_$ts"
        Copy-Item $OutputPath $bak
        Write-Host "Backup creado: $bak"
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($OutputPath, $json, $utf8NoBom)
    Write-Host "JSON escrito en: $OutputPath" -ForegroundColor Green
} else {
    Write-Host "[DRY RUN] No se escribio archivo. Vista previa de los primeros 800 chars:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host $json.Substring(0, [Math]::Min(800, $json.Length))
}

# 6. Resumen
Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  Resumen" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Waypoints totales: $($waypoints.Count)"
$stops = $waypoints | Where-Object { $_.isStop }
Write-Host "Paradas:           $($stops.Count)"
foreach ($s in $stops) {
    Write-Host "  -> $($s.name) en ($([Math]::Round($s.pos[0],1)), $([Math]::Round($s.pos[2],1)))"
}
$withInput = $waypoints | Where-Object { $_.hasInputData }
Write-Host "Con throttle/brake grabado: $($withInput.Count)"
