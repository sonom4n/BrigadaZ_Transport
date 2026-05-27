# ============================================================================
#  merge_routes.ps1 - combina N CSVs del PathLogger en un solo BZBusRoute.json
#
#  Toma dos (o mas) CSVs y los une en una sola ruta. El punto de empalme se
#  determina por matching espacial: para cada CSV adicional, busca en el
#  acumulado el sample mas cercano al primer punto del CSV nuevo, trunca ahi,
#  y concatena.
#
#  Genera tambien un archivo .merge_info.md al lado del JSON con la info del
#  split point para poder regrabar tramos individuales.
#
#  Uso:
#    .\merge_routes.ps1 -CsvList "viejo.csv","nuevo.csv"
#    .\merge_routes.ps1 -CsvList @("a.csv","b.csv","c.csv") -DryRun
# ============================================================================

param(
    [Parameter(Mandatory=$true)]
    [string[]]$CsvList,
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

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  merge_routes.ps1" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# Validar inputs
foreach ($p in $CsvList) {
    if (-not (Test-Path $p)) { throw "CSV no existe: $p" }
}

# Cargar bus_stops.json
$stopsConfig = $null
if (Test-Path $StopsJsonPath) {
    $stopsConfig = Get-Content $StopsJsonPath -Raw | ConvertFrom-Json
}

function Find-NearestStop {
    param([float]$x, [float]$z, [float]$maxDist = 80.0)
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

function Read-CsvSamples {
    param([string]$Path)
    $lines = Get-Content $Path
    $header = $lines[0] -split ","
    $idxX = [array]::IndexOf($header, "x")
    $idxY = [array]::IndexOf($header, "y")
    $idxZ = [array]::IndexOf($header, "z")
    $idxSpeed = [array]::IndexOf($header, "speed_kmh")
    $idxIsStop = [array]::IndexOf($header, "is_stop")
    $idxGear = [array]::IndexOf($header, "gear")
    $idxThrottle = [array]::IndexOf($header, "throttle")
    $idxBrake = [array]::IndexOf($header, "brake")

    $out = New-Object System.Collections.ArrayList
    for ($i = 1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if (-not $line.Trim()) { continue }
        $cols = $line -split ","

        $hasInput = ($idxThrottle -ge 0 -and $idxBrake -ge 0)
        $throttle = 0.0
        $brake = 0.0
        if ($hasInput) {
            $throttle = [float]$cols[$idxThrottle]
            $brake = [float]$cols[$idxBrake]
        }

        $sample = [PSCustomObject]@{
            x = [float]$cols[$idxX]
            y = [float]$cols[$idxY]
            z = [float]$cols[$idxZ]
            speedKmh = [float]$cols[$idxSpeed]
            isStop = ([int]$cols[$idxIsStop]) -eq 1
            gear = [int]$cols[$idxGear]
            throttle = $throttle
            brake = $brake
            hasInput = $hasInput
            sourceCsv = (Split-Path -Leaf $Path)
        }
        [void]$out.Add($sample)
    }
    return $out
}

# 1. Leer todos los CSVs
Write-Host ""
Write-Host "Leyendo CSVs..."
$allSegments = @()
foreach ($p in $CsvList) {
    $s = Read-CsvSamples -Path $p
    Write-Host ("  $((Split-Path -Leaf $p))  -> $($s.Count) samples")
    $allSegments += ,$s
}

# 2. Combinar por matching espacial
$merged = New-Object System.Collections.ArrayList
$splitMarkers = @()  # info para el .merge_info.md

# Primer segmento entero
foreach ($s in $allSegments[0]) {
    [void]$merged.Add($s)
}
Write-Host ""
Write-Host "Segmento 1 ($(Split-Path -Leaf $CsvList[0])): incluido entero ($($merged.Count) samples)"

# Para cada segmento adicional, buscar punto de empalme
for ($si = 1; $si -lt $allSegments.Count; $si++) {
    $newSegment = $allSegments[$si]
    if ($newSegment.Count -eq 0) { continue }

    $firstNew = $newSegment[0]
    # Buscar sample del merged mas cercano al primer punto del nuevo
    $bestIdx = -1
    $bestDist = [float]::MaxValue
    for ($i = 0; $i -lt $merged.Count; $i++) {
        $cur = $merged[$i]
        $d = [Math]::Sqrt(($cur.x - $firstNew.x) * ($cur.x - $firstNew.x) + ($cur.z - $firstNew.z) * ($cur.z - $firstNew.z))
        if ($d -lt $bestDist) {
            $bestDist = $d
            $bestIdx = $i
        }
    }

    Write-Host ""
    Write-Host ("Segmento $($si+1) ($(Split-Path -Leaf $CsvList[$si])):")
    Write-Host ("  Primer punto nuevo: ({0:F2}, {1:F2})" -f $firstNew.x, $firstNew.z)
    Write-Host ("  Match en merged idx=$bestIdx, dist=$([Math]::Round($bestDist,2))m")

    # Truncar merged en bestIdx (descartar lo posterior)
    $cutCount = $merged.Count - $bestIdx - 1
    if ($cutCount -gt 0) {
        $merged.RemoveRange($bestIdx + 1, $cutCount)
        Write-Host "  Truncado merged: descartados $cutCount samples posteriores"
    }

    # Marcar el split point (info para el .md aux)
    $splitMarkers += [PSCustomObject]@{
        wpIdx = $merged.Count - 1  # ultimo wp antes de empalme (0-indexed)
        coord = "$([Math]::Round($merged[$merged.Count - 1].x,2)) $([Math]::Round($merged[$merged.Count - 1].y,2)) $([Math]::Round($merged[$merged.Count - 1].z,2))"
        oldCsv = (Split-Path -Leaf $CsvList[$si - 1])
        newCsv = (Split-Path -Leaf $CsvList[$si])
        spaceMatchDist = [Math]::Round($bestDist, 2)
    }

    # Append el nuevo entero
    foreach ($s in $newSegment) {
        [void]$merged.Add($s)
    }
    Write-Host "  Total merged ahora: $($merged.Count) samples"
}

# 3. Convertir samples -> waypoints (mismo formato que csv_to_route.ps1)
$waypoints = @()
$stopIdx = 0
foreach ($s in $merged) {
    $name = ""
    $stopDur = 0
    $stopRad = 0.0
    if ($s.isStop) {
        $stopIdx++
        $matchedStop = Find-NearestStop -x $s.x -z $s.z -maxDist 80.0
        if ($matchedStop) {
            $name = $matchedStop.name
        } else {
            $name = "Parada $stopIdx"
        }
        $stopDur = $StopDuration
        $stopRad = $StopRadius
    }

    $wp = [PSCustomObject]@{
        pos            = @($s.x, $s.y, $s.z)
        isStop         = $s.isStop
        name           = $name
        stopDuration   = $stopDur
        stopRadius     = $stopRad
        targetSpeed    = [Math]::Round($s.speedKmh, 2)
        targetGear     = $s.gear
        targetThrottle = [Math]::Round($s.throttle, 3)
        targetBrake    = [Math]::Round($s.brake, 3)
        hasInputData   = $s.hasInput
    }
    $waypoints += $wp
}

# 4. Construir JSON
$route = [PSCustomObject]@{
    RespawnDelay   = $RespawnDelay
    AverageSpeedMS = $AverageSpeedMS
    VehicleClass   = $VehicleClass
    DriverClass    = $DriverClass
    Waypoints      = $waypoints
}
$json = $route | ConvertTo-Json -Depth 10

# 5. Generar .merge_info.md
$mdLines = @()
$mdLines += "# BZBusRoute - Merge Info"
$mdLines += ""
$mdLines += "Ruta generada por **merge_routes.ps1** combinando varios CSVs."
$mdLines += ""
$mdLines += "**Fecha**: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$mdLines += "**Output JSON**: $OutputPath"
$mdLines += "**Total waypoints**: $($waypoints.Count)"
$mdLines += ""
$mdLines += "## CSVs combinados (en orden)"
$mdLines += ""
for ($i = 0; $i -lt $CsvList.Count; $i++) {
    $mdLines += "$($i+1). ``$($CsvList[$i])``"
}
$mdLines += ""
$mdLines += "## Split points (donde se empalmaron CSVs)"
$mdLines += ""
if ($splitMarkers.Count -eq 0) {
    $mdLines += "_(sin splits - solo un CSV)_"
} else {
    $mdLines += "| Split # | wpIdx | coord (x y z) | CSV anterior | CSV siguiente | dist match (m) |"
    $mdLines += "|---|---|---|---|---|---|"
    for ($i = 0; $i -lt $splitMarkers.Count; $i++) {
        $sp = $splitMarkers[$i]
        $mdLines += "| $($i+1) | $($sp.wpIdx) | $($sp.coord) | $($sp.oldCsv) | $($sp.newCsv) | $($sp.spaceMatchDist) |"
    }
}
$mdLines += ""
$mdLines += "## Como regrabar un tramo"
$mdLines += ""
$mdLines += "Si querés cambiar un tramo:"
$mdLines += ""
$mdLines += "1. Grabar el tramo nuevo con PathLogger normal (NUMPAD 5/4)."
$mdLines += "2. Ejecutar:"
$mdLines += '   ```powershell'
$mdLines += "   .\merge_routes.ps1 -CsvList ``"
$mdLines += "       'csv_tramo_1.csv', ``"
$mdLines += "       'csv_tramo_2_nuevo.csv', ``"
$mdLines += "       'csv_tramo_3.csv'"
$mdLines += '   ```'
$mdLines += "3. El JSON se regenera respetando los matching espaciales."
$mdLines += ""
$mdLines += "Para regrabar **desde un split point específico**: tomar el bus a la coord indicada en la tabla (`COT spawn ExpansionBus en X Y Z` + manejo desde ahí con PathLogger humano)."

$mdPath = "$OutputPath.merge_info.md"

# 6. Backup, escribir JSON y .md
if (-not $DryRun) {
    if (Test-Path $OutputPath) {
        $ts = (Get-Date).ToString("HHmmss")
        $bak = "$OutputPath.bak_$ts"
        Copy-Item $OutputPath $bak
        Write-Host ""
        Write-Host "Backup creado: $bak" -ForegroundColor Yellow
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($OutputPath, $json, $utf8NoBom)
    [System.IO.File]::WriteAllText($mdPath, ($mdLines -join "`r`n"), $utf8NoBom)
    Write-Host "JSON escrito: $OutputPath" -ForegroundColor Green
    Write-Host "Merge info:   $mdPath" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[DRY RUN] No se escribio archivo." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Preview JSON (primeros 600 chars):"
    Write-Host $json.Substring(0, [Math]::Min(600, $json.Length))
    Write-Host ""
    Write-Host "Preview .merge_info.md:"
    Write-Host ($mdLines -join "`n")
}

# 7. Resumen
Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  Resumen final" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Waypoints totales: $($waypoints.Count)"
$stops = $waypoints | Where-Object { $_.isStop }
Write-Host "Paradas:           $($stops.Count)"
foreach ($s in $stops) {
    Write-Host ("  -> $($s.name) en ({0:F1}, {1:F1})" -f $s.pos[0], $s.pos[2])
}
$withInput = $waypoints | Where-Object { $_.hasInputData }
Write-Host "Con throttle/brake grabado: $($withInput.Count)"
Write-Host "Splits:            $($splitMarkers.Count)"
