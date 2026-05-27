# ============================================================================
#  swap_route.ps1 - Switchea entre estados guardados de BZBusRoute.json
#
#  Estados disponibles (preservados en profile dir):
#    - BUS_CANONICO  : ruta canonica del bus (Kamenka -> Terminal, 14 paradas)
#    - LANDROVER     : stress-test del Land Rover (Stary Sobor -> Zenit)
#    - (lo que vayas agregando con el patron BZBusRoute_<NOMBRE>.json)
#
#  Uso:
#    .\swap_route.ps1                 # lista estados disponibles + actual
#    .\swap_route.ps1 -To BUS_CANONICO
#    .\swap_route.ps1 -To LANDROVER
#    .\swap_route.ps1 -SaveCurrentAs MI_NUEVO_ESTADO   # guarda el actual con ese nombre
#
#  El estado activo siempre es BZBusRoute.json (lo que el server lee al Init).
#  Switchear NO destruye nada: el JSON actual se preserva con su nombre
#  identificable, y el deseado se copia sobre BZBusRoute.json.
#
#  Despues de switchear: NUMPAD 2 ingame para que el servicio re-spawnee el
#  vehiculo con la ruta nueva (o restart server si quieres asegurar).
# ============================================================================

param(
    [string]$To           = "",
    [string]$SaveCurrentAs = "",
    [string]$ProfileDir   = "C:\DayZServer\profiles\BrigadaZ_Transport",
    [string]$RemoteDir    = "Y:\profiles\BrigadaZ_Transport"
)

$ErrorActionPreference = "Stop"

$activePath = Join-Path $ProfileDir "BZBusRoute.json"

# Detectar estados disponibles (archivos BZBusRoute_*.json)
$states = Get-ChildItem -Path $ProfileDir -Filter "BZBusRoute_*.json" -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -notlike "*.bak_*" -and $_.Name -notlike "*.merge_info*" } |
          ForEach-Object { ($_.BaseName -replace "^BZBusRoute_", "") }

# Identificar estado actual por hash (comparar con cada estado guardado)
function Get-MatchingState {
    param([string]$activePath, [array]$states, [string]$ProfileDir)
    if (-not (Test-Path $activePath)) { return "(no existe BZBusRoute.json)" }
    $activeHash = (Get-FileHash $activePath -Algorithm SHA1).Hash
    foreach ($s in $states) {
        $sp = Join-Path $ProfileDir "BZBusRoute_$s.json"
        if (Test-Path $sp) {
            $sh = (Get-FileHash $sp -Algorithm SHA1).Hash
            if ($sh -eq $activeHash) { return $s }
        }
    }
    return "(custom o sin match)"
}

# Si pidio guardar el actual con nombre
if ($SaveCurrentAs -ne "") {
    if (-not (Test-Path $activePath)) {
        Write-Host "ERROR: BZBusRoute.json no existe, no hay que guardar" -ForegroundColor Red
        exit 1
    }
    $saveLocal = Join-Path $ProfileDir "BZBusRoute_$SaveCurrentAs.json"
    Copy-Item $activePath $saveLocal -Force
    # Tambien el .merge_info.md si existe
    if (Test-Path "$activePath.merge_info.md") {
        Copy-Item "$activePath.merge_info.md" "$saveLocal.merge_info.md" -Force
    }
    Write-Host "Estado actual guardado como: BZBusRoute_$SaveCurrentAs.json" -ForegroundColor Green
    exit 0
}

# Listar estados si no se pidio swap
if ($To -eq "") {
    Write-Host ""
    Write-Host "===== Estados disponibles =====" -ForegroundColor Cyan
    if ($states.Count -eq 0) {
        Write-Host "  (ninguno guardado todavia)"
    } else {
        foreach ($s in $states) {
            $sp = Join-Path $ProfileDir "BZBusRoute_$s.json"
            $route = Get-Content $sp -Raw | ConvertFrom-Json
            $vc = $route.VehicleClass
            $wps = $route.Waypoints.Count
            $stops = ($route.Waypoints | Where-Object { $_.isStop }).Count
            Write-Host ("  - $s  ($vc, $wps wps, $stops paradas)")
        }
    }
    Write-Host ""
    $current = Get-MatchingState $activePath $states $ProfileDir
    Write-Host "Estado actual activo: $current" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Para switchear: .\swap_route.ps1 -To <NOMBRE>"
    Write-Host "Para guardar el actual: .\swap_route.ps1 -SaveCurrentAs <NOMBRE>"
    exit 0
}

# Switch
$targetPath = Join-Path $ProfileDir "BZBusRoute_$To.json"
if (-not (Test-Path $targetPath)) {
    Write-Host "ERROR: no existe estado $To en $ProfileDir" -ForegroundColor Red
    Write-Host "Estados disponibles: $($states -join ', ')"
    exit 1
}

# Antes de pisar, sugerir guardar el actual si no esta guardado
$currentState = Get-MatchingState $activePath $states $ProfileDir
if ($currentState -like "(custom*") {
    Write-Host ""
    Write-Host "WARN: el BZBusRoute.json actual no matchea ningun estado guardado." -ForegroundColor Yellow
    Write-Host "Si querias preservarlo: cancela y corre: .\swap_route.ps1 -SaveCurrentAs <NOMBRE>" -ForegroundColor Yellow
    Write-Host "Continuando en 3 segundos..."
    Start-Sleep -Seconds 3
}

Copy-Item $targetPath $activePath -Force
# Replicar merge_info si existe
if (Test-Path "$targetPath.merge_info.md") {
    Copy-Item "$targetPath.merge_info.md" "$activePath.merge_info.md" -Force
}

# Replicar a remote (server B)
if (Test-Path $RemoteDir) {
    $remoteActive = Join-Path $RemoteDir "BZBusRoute.json"
    Copy-Item $activePath $remoteActive -Force
    if (Test-Path "$activePath.merge_info.md") {
        Copy-Item "$activePath.merge_info.md" "$remoteActive.merge_info.md" -Force
    }
    Write-Host "Replicado a server B ($remoteActive)" -ForegroundColor Green
}

# Resumen del nuevo estado activo
$route = Get-Content $activePath -Raw | ConvertFrom-Json
Write-Host ""
Write-Host "===== SWITCH OK: BZBusRoute.json ahora es $To =====" -ForegroundColor Green
Write-Host ("  VehicleClass: $($route.VehicleClass)")
Write-Host ("  DriverClass:  $($route.DriverClass)")
if ($route.PSObject.Properties.Name -contains "MaxGear") {
    Write-Host ("  MaxGear:      $($route.MaxGear)")
}
Write-Host ("  Waypoints:    $($route.Waypoints.Count)")
$stops = $route.Waypoints | Where-Object { $_.isStop }
Write-Host ("  Paradas:      $($stops.Count)")
Write-Host ""
Write-Host "Ingame: NUMPAD 2 para que el servicio cargue la ruta nueva." -ForegroundColor Cyan
