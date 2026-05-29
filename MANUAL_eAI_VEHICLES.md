# Manual de eAI Vehicular para DayZ Standalone

**Framework de reproducción de intención humana para vehículos guiados por NPCs en DayZ Standalone**

---

## Resumen ejecutivo

Este documento describe la arquitectura, implementación y principios de diseño de un framework para vehículos manejados por NPCs (eAI) en DayZ Standalone. El framework permite que cualquier vehículo derivado de `CarScript` sea conducido autónomamente por un NPC siguiendo una ruta grabada previamente por un operador humano, reproduciendo no solo la trayectoria espacial sino también el estilo de manejo (velocidad, momento de frenado, técnica de cambios, anticipación a curvas).

Validación empírica primaria: un servicio de transporte público entre 14 paradas a lo largo de la costa de Chernarus (24.92 km), con desviación lateral media de 1.00m y tracking dentro del corredor de tolerancia en el 94.5% del recorrido. Generalización validada con un segundo vehículo (Land Rover Defender) sin modificaciones al código del control, únicamente cambios al perfil del vehículo en su archivo de configuración JSON.

El framework se publica bajo licencia abierta junto con su código fuente, este manual, y un AI knowledge pack diseñado para que otros modders puedan ser asistidos por modelos de lenguaje (Claude, GPT, Gemini) en sus propios casos de uso derivados.

---

## Tabla de Contenidos

1. Introducción
2. Breakthrough técnico: el override hardcoded de eAI
3. Compatibilidad automática vía herencia de CarScript
4. Arquitectura del framework
5. Control vehicular
6. Buenas prácticas para grabar rutas
7. Casos de uso
8. Alcance y límites del framework
9. Validación empírica
10. Roadmap
- Apéndice A — Disclosure de uso de IA
- Apéndice B — Créditos externos
- Apéndice C — AI knowledge pack
- Apéndice D — Autores

---

## Capítulo 1 — Introducción

### 1.1 Definición

eAI driving es la capacidad de que un NPC controlado por la mod **Expansion-AI** (eAI) conduzca un vehículo del juego DayZ Standalone de manera autónoma y confiable a lo largo de una ruta predefinida. El framework descrito aquí provee la infraestructura técnica completa para implementar esta capacidad en cualquier vehículo derivado de `CarScript` del motor.

### 1.2 Por qué importa: las posibilidades

DayZ Standalone es un juego centrado en supervivencia donde el vehículo es una pieza crítica del gameplay, pero los vehículos del juego solo pueden ser manejados por jugadores reales. Esto excluye categorías enteras de mecánicas que requieren NPCs vehiculares: servicios de transporte público, convoyes con escolta AI, patrullas motorizadas, ladrones de vehículos, misiones con NPCs conduciendo, sistemas de logística automatizada, y muchos otros.

Esta limitación llevó a una situación donde los modders intentaban soluciones parciales (movimiento por script forzado con `SetPosition`, animaciones predefinidas, vehículos manejados por código sin pasar por las físicas reales del juego). Estas soluciones producían resultados visualmente artificiales y limitados.

El framework aquí descrito provee la **alternativa correcta**: el NPC realmente conduce el vehículo a través del sistema vehicular nativo del juego, aplicando inputs reales (volante, acelerador, freno) a las físicas del motor.

### 1.3 Qué provee este documento

- Descripción técnica del breakthrough que destrabó el problema
- Arquitectura completa del framework, reutilizable para cualquier vehículo
- Patrones de control vehicular validados empíricamente
- Buenas prácticas para grabar rutas con calidad de producción
- Casos de uso documentados con sus particularidades
- Articulación honesta de alcance, límites y trade-offs del framework
- Roadmap de capacidades futuras

### 1.4 Estado del arte previo

Antes de este trabajo, el conocimiento público sobre conducción AI en DayZ Standalone era prácticamente inexistente. Los modders que intentaron implementarlo se encontraban con vehículos que no aceleraban, NPCs que se bajaban del asiento, o comportamientos errátiles que hacían inutilizable el patrón en producción. La causa raíz no estaba documentada en ningún foro accesible.

Recientemente apareció un intento independiente (RoadPilot) que aplicaba el approach más simple: grabar posiciones y reproducir mediante teleport. Esa aproximación produce movimiento "sprite-like" que no aprovecha las físicas reales del vehículo. El framework presentado aquí es arquitectónicamente distinto y se beneficia del comportamiento físico nativo del juego.

---

## Capítulo 2 — Breakthrough técnico: el override hardcoded de eAI

### 2.1 El problema

Los NPCs de eAI tienen capacidad de manejar vehículos, pero al sentarlos al volante y proveerles waypoints, el vehículo **no aceleraba en absoluto**. El NPC quedaba estático con el motor encendido y los controles inputados, pero el vehículo no se movía.

La inspección del código fuente de eAI reveló la causa: la función `OnInput` modded de `CarScript` en Expansion-AI ejecuta cada frame la llamada `ShiftTo(CarGear.FIRST)`. Esto fuerza el vehículo permanentemente a primera marcha, pero con un detalle adicional crítico: en DayZ, el valor numérico de `CarGear.FIRST` es 2 (después de `CarGear.REVERSE = 0` y `CarGear.NEUTRAL = 1`). Sin embargo, lo que en muchos vehículos vanilla del juego corresponde a la marcha "punto muerto activa" — es decir, motor encendido pero sin transmisión engranada efectivamente.

El resultado es que cada vez que el NPC intenta acelerar, eAI inmediatamente sobreescribe el gear y el vehículo no engrana correctamente. Los inputs del NPC son válidos pero ignorados por la transmisión.

### 2.2 La solución arquitectónica

La solución no es modificar Expansion-AI (sería invasiva y rompería compatibilidad con futuras versiones del mod). Es **interceptar el orden de ejecución**: nuestro propio `modded class CarScript` se ejecuta DESPUÉS del `OnInput` de eAI, y sobreescribe el gear con el valor deseado.

```enforce
modded class CarScript {
    override void OnInput(float dt) {
        super.OnInput(dt);  // eAI ejecuta su lógica, incluyendo ShiftTo(FIRST)
        if (!GetGame().IsServer()) return;

        BZBusService srv = BZBusService.GetInstance();
        if (!srv || !srv.IsBusActive(this)) return;

        // Aplicar inputs del playback (sobrescribe los de eAI)
        srv.ApplyBusInput(this, dt);

        // Sobrescribir el gear deseado por nuestra AT
        int desired = srv.GetDesiredGear();
        if (GetGear() != desired) {
            ShiftTo(desired);
        }
    }
}
```

El gear deseado es calculado por una transmisión automática (AT) modded basada en RPM, que vive en nuestro framework y reemplaza al hardcoded `ShiftTo(FIRST)` de eAI.

### 2.3 Patrón "subordinar pero no reemplazar"

El framework completo deriva de este patrón. eAI no se reemplaza ni se desactiva: se le permite ejecutar toda su lógica (animaciones del NPC al volante, pathfinding global, audio del motor, IK de las manos sobre el volante, etc.) y solo se sobrescriben los outputs específicos donde su comportamiento es incorrecto para el caso de uso (en este caso: el gear).

Esta filosofía arquitectónica permite que:

- Las animaciones visuales del NPC se mantienen perfectas (el framework no las controla)
- El audio del motor responde naturalmente
- Las físicas del vehículo operan en su modo normal
- Si eAI publica una nueva versión, las animaciones y comportamientos visuales se actualizan automáticamente sin que el framework necesite cambios

Cada decisión arquitectónica del framework deriva de este principio: subordinar, no reemplazar.

---

## Capítulo 3 — Compatibilidad automática vía herencia de CarScript

### 3.1 Herencia automática

DayZ define la jerarquía vehicular base en `CarScript`. Cualquier mod de vehículo del Workshop que extienda `CarScript` (que es prácticamente la totalidad de mods de vehículos) hereda automáticamente el patrón modded descrito en el Capítulo 2.

Esto significa que el framework cubre, sin escribir una línea adicional de código por vehículo, aproximadamente el 95% de los vehículos del Workshop. Validado empíricamente con vehículos modded de la comunidad (autos deportivos, off-road, camiones de distintos autores) y con el Land Rover Defender de Expansion-Vehicles: **mismo código de control, vehículos físicamente muy distintos, todos funcionan**.

### 3.2 Cinco casos donde la herencia automática se rompe

**Caso 1 — El mod no es un coche aunque lo parezca**: algunos mods estéticamente similares a vehículos (carritos de golf decorativos, vehículos como props) no extienden `CarScript`. No se aplican el framework. Solución: detectar previamente con `Car.Cast(entity)`.

**Caso 2 — Mod viejo previo al refactor de DayZ**: mods anteriores a la actualización 1.10 de DayZ pueden tener una jerarquía vehicular obsoleta. Solución: actualizar el mod o usar un adapter.

**Caso 3 — El autor del mod metió override de OnInput sin llamar a super()**: rompe la herencia. Solución: contactar al autor o sobrescribir manualmente.

**Caso 4 — Vehículo desde una clase totalmente distinta**: barcos (`BoatScript`), helicópteros (`Helicopter`). Requieren adapters específicos (ver sección 3.4).

**Caso 5 — El autor hizo su propia jerarquía paralela**: muy raro pero existe. Solución: el operador del framework debe agregar un override específico para esa clase.

### 3.3 Verificación rápida de compatibilidad

Antes de integrar un vehículo nuevo:

1. Spawnear el vehículo manualmente en testing
2. Inspeccionar con `entity.GetType()` cuál es su classname efectivo
3. Verificar herencia con `CarScript.Cast(entity) != null`
4. Si retorna no null: compatibilidad confirmada, proceder con el vehicle profile

### 3.4 Adapters pendientes

- **BoatScript**: adapter pendiente para v1.2 (transporte marítimo)
- **Helicopter**: adapter pendiente para v2.0+ (mayor complejidad por dimensión vertical)

Estos adapters mantienen la misma arquitectura subordinada, ajustando los hooks específicos del subsistema (en helicópteros: throttle del rotor principal y cola, en vez de pedales).

---

## Capítulo 4 — Arquitectura del framework

### 4.1 Componentes principales

El framework se compone de los siguientes módulos:

```
┌─────────────────────────────────────────────────────────────┐
│  PathLogger (cliente)                                       │
│  Graba la posición + inputs del operador humano             │
│  Output: CSV con time, x, y, z, kmh, throttle, brake, etc. │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│  csv_to_route.ps1 (herramienta)                             │
│  Convierte CSV → JSON con waypoints + vehicle profile       │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│  BZBusRouteConfig (servidor)                                │
│  Lectura del JSON, validación, exposición de getters        │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│  BZBusService (servidor)                                    │
│  Servicio central que spawnea vehículo, posiciona driver,   │
│  ejecuta Tick cada 500ms con DriveTowards (Stanley)         │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│  CarScript modded (servidor)                                │
│  Intercepta OnInput, aplica cached inputs del service       │
│  AT por RPM + anti-catapulta por aceleración                │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 Vehicle profile

Cada vehículo se describe completamente con un archivo JSON que contiene los siguientes campos:

```json
{
  "RespawnDelay":         300,
  "AverageSpeedMS":       11.0,
  "VehicleClass":         "Expansion_Landrover",
  "DriverClass":          "eAI_SurvivorM_Boris",
  "MaxGear":              7,
  "Attachments":          ["expansion_landrover_wheel", ...],
  "AccelShiftThreshold":  15.0,
  "SteeringScale":        0.7,
  "Waypoints":            [...]
}
```

**Significado de cada campo**:

| Campo | Función | Ejemplo |
|---|---|---|
| `VehicleClass` | classname del vehículo a spawnear | `ExpansionBus`, `Expansion_Landrover`, `Hatchback_02` |
| `DriverClass` | classname del NPC eAI que va al volante | `eAI_SurvivorM_Boris` |
| `MaxGear` | gear máximo (convención CarGear: FIRST=2, ... SIXTH=7) | 6 para vehículos de 5 marchas, 7 para 6 marchas |
| `Attachments` | lista de classnames de partes a equipar (ruedas, batería, etc.) | Específico de cada vehículo |
| `AccelShiftThreshold` | umbral de aceleración (km/h por segundo) para anti-catapulta | 999 = deshabilitado (vehículos pesados), 15 = Land Rover, 10 = Hatchback |
| `SteeringScale` | escala lineal del steering del Stanley (compensa wheelbase) | 1.0 = bus, 0.7 = Land Rover, 0.5 = Hatchback ágil |
| `Waypoints` | lista de waypoints con coords, isStop, targetSpeed, etc. | Generado automáticamente desde CSV |

### 4.3 Sistema de estados

Para soportar múltiples rutas y vehículos sin pisar unos con otros, se provee un sistema de estados:

```
BZBusRoute_BUS_CANONICO.json    ← preserva la ruta canónica del bus
BZBusRoute_LANDROVER.json       ← versión Land Rover
BZBusRoute_<MI_NUEVO>.json      ← cualquier estado guardado por el usuario
BZBusRoute.json                 ← estado activo (apunta a uno de los anteriores)
```

La herramienta `swap_route.ps1` permite cambiar entre estados:

```powershell
.\swap_route.ps1                          # ver estados disponibles + activo
.\swap_route.ps1 -To LANDROVER            # activar el estado Land Rover
.\swap_route.ps1 -SaveCurrentAs MI_TEST   # guardar el activo con un nombre
```

El switcheo es manual y deliberado: nunca se pierde una grabación por accidente.

### 4.4 Captura de rutas: PathLogger

PathLogger es el componente que captura la ruta del operador durante la grabación. Es client-side (corre en el cliente DayZ del operador) y graba un sample cada ~250ms con la siguiente información:

```
time_s, x, y, z, heading_deg, speed_kmh, is_stop, gear, throttle, brake, steering
```

**Hotkeys**:

| Tecla | Función |
|---|---|
| NUMPAD 5 | Start / Stop grabación |
| NUMPAD 4 | Marker (paradas de pasajeros, paradas técnicas, eventos notables) |
| NUMPAD 6 | Pause / Resume |

El operador graba conduciendo el vehículo normalmente. Los markers (NUMPAD 4) se usan para anotar puntos de interés que el framework usará luego para forzar paradas o aproximaciones de precisión.

> **Punto importante**: PathLogger es **client-side**. El CSV NO se guarda en el server — se guarda en la PC del operador, en:
>
> ```
> C:\Users\<windows_user>\AppData\Local\DayZ\BrigadaZ_Transport_PathLogger\path_<timestamp>.csv
> ```
>
> Esto vale incluso si el server donde estás conectado es remoto (HostHavoc, Nitrado, dedicated, etc.). El admin tiene que ir a SU propia PC para recuperar la grabación. El archivo aparece sólo cuando NUMPAD 5 cierra la grabación, no mientras corre.

### 4.4.1 Workflow: grabar una ruta nueva en otro mapa

El framework es **map-agnostic**: el código de control no depende del mapa (Chernarus vanilla, Chernarus 2035, Livonia, Sakhal, Banov, etc.). Lo que sí depende del mapa son **las coordenadas de la ruta**. El JSON canónico bundleado en el PBO tiene coordenadas de Chernarus vanilla; en cualquier otro mapa el bus va a spawnear en lugares sin sentido.

Para usar el mod en un mapa distinto, el admin tiene que grabar su propia ruta. Receta paso a paso:

**Pre-requisitos**:
- **El workflow es PC-only**. DayZ console (Xbox/PS) no soporta mods del Workshop, así que esto no aplica. Para PC, el operador necesita Windows con DayZ instalado y acceso al filesystem (no funciona desde servicios de cloud gaming sin acceso a AppData).
- DayZ Tools instalado en tu PC local (para ejecutar `csv_to_route.ps1`)
- El repo de GitHub clonado o descargado como ZIP (link en la descripción del Workshop)
- Acceso al server: levantarlo, parar, editar `profiles/BrigadaZ_Transport/BZBusRoute.json`. El server puede ser local o remoto (hosted en HostHavoc, Nitrado, dedicated, etc.) — lo importante es que vos tengas la grabación, que ocurre en tu PC local sin importar dónde está el server.

**Paso 1 — Limpiar el route bundleado (opcional pero recomendado)**

Con el server detenido:

```
<server_path>/profiles/BrigadaZ_Transport/BZBusRoute.json
```

Renombralo a `BZBusRoute.json.old` o borralo. La próxima vez que el server arranque sin esta ruta cargada y vos grabes, vas a poder reemplazarla con la tuya.

**Paso 2 — Grabar la trayectoria como humano**

1. Levantá el server (con su ruta actual cargada, da igual cuál — la grabación no depende del estado del bus)
2. Joineá como admin desde tu PC
3. Conseguí cualquier vehículo (un auto propio, un vehículo que spawnees con COT, lo que sea)
4. Manejá hasta el punto de inicio de tu ruta nueva
5. **NUMPAD 5** → empieza a grabar
6. Manejá la ruta completa, pasando por cada parada que vas a definir
7. **NUMPAD 4** al estar parado en cada parada → marca esa fila del CSV como `is_stop=1`
8. **NUMPAD 5** al final → cierra la grabación y guarda el CSV

**Paso 3 — Encontrar el CSV**

El CSV está en tu PC, no en el server:

```
C:\Users\<tu_user>\AppData\Local\DayZ\BrigadaZ_Transport_PathLogger\path_<timestamp>.csv
```

Si AppData no se ve en Explorer, pegá el path completo en la barra de direcciones.

**Paso 4 — Convertir CSV a JSON**

Abrí PowerShell (Win+R, escribí `powershell`, Enter). Navegá a la carpeta `tools/` del repo:

```
cd "C:\path\to\BrigadaZ_Transport-main\tools"
```

Corré:

```
.\csv_to_route.ps1 -InputCsv "C:\Users\<tu_user>\AppData\Local\DayZ\BrigadaZ_Transport_PathLogger\path_<timestamp>.csv"
```

El JSON aparece al lado del CSV. Si tu mapa usa paradas con nombres distintos a los de Chernarus, también podés editar `data/bus_stops.json` antes de correr el script para que detecte tus paradas por proximidad.

**Paso 5 — Instalar la ruta nueva en el server**

Subí el JSON generado al server. Para servers remotos (HostHavoc, etc.), usá el panel del proveedor para subir el archivo a:

```
<server_path>/profiles/BrigadaZ_Transport/BZBusRoute.json
```

**Paso 6 — Reiniciar y probar**

Restart server, joineá ingame, **NUMPAD 2** spawnea el bus en tu ruta nueva. Si el bus arranca y avanza, ya está. Si no avanza, abrí el RPT del server, buscá líneas con `[BZBus]` para diagnosticar.

**Troubleshooting común**:

| Síntoma | Causa probable |
|---|---|
| Bus aparece pero no se mueve | Coordenadas del CSV no coinciden con superficie del mapa (Y=0). Revisá que grabaste en piso, no en agua/edificio |
| Bus aparece sin ruedas | Olvidaste editar `Attachments` en el JSON para tu vehículo (el bundle solo tiene los del ExpansionBus) |
| Bus aparece pero no detecta paradas | No apretaste NUMPAD 4 en las paradas, o `bus_stops.json` no tiene los nombres correctos |
| Error de compilación al iniciar server | JSON mal formado (probablemente edición manual). Validá con un linter JSON antes de subir |

Para todos los demás casos, abrir un Issue en el repo de GitHub con el log del RPT.

### 4.5 Coexistencia con el entorno del mundo

DayZ tiene contenido del mundo (vehículos wreck, escombros, obstáculos) que puede interferir con vehículos que sigan trayectorias predefinidas. El framework no controla obstáculos en runtime; en cambio, provee mecanismos de limpieza del entorno por configuración:

- `ExpansionWorldObjectsModule` (boolean por server) — activa/desactiva enhancements visuales pesados
- Purga selectiva de `mapgrouppos.xml` para eliminar objetos en la ruta
- Desactivación de eventos `Static` en `events.xml`
- Sweep al startup mediante `BZRouteCleanup` con anchors de la ruta

La estrategia óptima depende del nivel de contenido decorativo deseado. El framework provee 5 capas de control independientes.

---

## Capítulo 5 — Control vehicular

### 5.1 Stanley controller con corredor

El componente central del control lateral es un **Stanley controller** clásico de literatura, modificado para operar sobre el concepto de **corredor de movimiento** (en lugar de línea central).

**Fórmula**:

```
targetYaw = segmentHeading - atan(K · lateralOffset / velocity)
```

Donde:
- `segmentHeading`: dirección del segmento de la ruta entre los dos waypoints más cercanos
- `lateralOffset`: distancia perpendicular signed del vehículo al segmento (cross product en sistema left-handed de DayZ)
- `velocity`: velocidad actual en m/s
- `K`: ganancia configurable (default 1.0)

**Propiedades**:

1. **Corrección continua y proporcional**, sin deadbands escalonados que produzcan zigzag
2. **Atenuación natural por velocidad**: el divisor `1/v` reduce la corrección a alta velocidad (evita oscilación) y la amplifica a baja velocidad (precisión en maniobras)
3. **Estabilidad asintótica**: converge al centro del segmento aunque oscile transitoriamente
4. **Convergencia rápida**: validada empíricamente al 94.5% de samples dentro del corredor de tolerancia en 24.92 km

**Convención de signo crítica**: el cross product en el sistema left-handed de DayZ es:

```enforce
cross = AB.z * AP.x - AB.x * AP.z
```

(donde AB es el segmento y AP el vector del waypoint anterior al vehículo). El signo es ambiguo entre implementaciones según convención del motor; validar empíricamente antes de productivizar.

### 5.2 Modelo físico predictivo de freno

Para el frenado anticipado a paradas y curvas pronunciadas, el framework usa cinemática clásica:

```
aNeeded = u² / (2·s) + g·sin(θ)
```

Donde:
- `u`: velocidad actual (m/s)
- `s`: distancia restante hasta el objetivo
- `g·sin(θ)`: factor de pendiente (positivo en bajadas, negativo en subidas)
- `aNeeded`: deceleración necesaria
- `MAX_BRAKE_DECEL`: deceleración máxima del vehículo (50 m/s² calibrado empíricamente para el bus)

El brake aplicado se calcula como `brake = aNeeded / MAX_BRAKE_DECEL`, clamp en [0, 1].

**Propiedad clave**: la fórmula es **agnóstica a la masa del vehículo**. La masa se cancela porque depende solo de velocidad observable y distancia. Esto significa que el control predictivo de freno es **escalable a vehículos con carga variable** sin recalibración.

### 5.3 Anti-catapulta por aceleración medida

Vehículos ágiles con alto torque (off-road, deportivos) en marchas bajas presentan un problema: aceleración brutal que rompe la trayectoria. El framework implementa un trigger anti-catapulta:

```enforce
float accelKmhPerSec = (kmh_now - kmh_prev) / dt;
if (accelKmhPerSec > AccelShiftThreshold && desired < maxGear) {
    // Subir gear automáticamente para reducir torque a las ruedas
    SetDesiredGear(desired + 1);
}
```

El umbral es configurable por vehículo (campo `AccelShiftThreshold` del vehicle profile). Para el bus pesado: 999 (deshabilitado). Para Land Rover Defender: 15 km/h/s (~4 m/s²). Para vehículos más livianos: 10 km/h/s o menos.

Este trigger replica la técnica humana de manejo "subir gear + pisar fuerte = manejo suave" sin copiar literalmente el `targetGear` del recording (que tiene su propio bug por waypoints iniciales con velocidad cero).

### 5.4 Steering scale por wheelbase

Vehículos con wheelbase corto producen yaw rate mayor para el mismo input nominal de steering. La fórmula física:

```
yaw_rate ≈ velocidad · tan(steering_angle) / wheelbase
```

A wheelbase la mitad, el yaw rate efectivo es el doble. El framework compensa esto con un `SteeringScale` multiplicativo después del clamp del Stanley:

| Vehículo | Wheelbase | SteeringScale recomendado |
|---|---|---|
| Bus | ~5-6m | 1.0 (sin escala) |
| V3S | ~4m | 0.7 |
| Land Rover Defender | ~2.7m | 0.7 (después de calibración) |
| Hatchback | ~2.5m | 0.5 |
| Sedan | ~2.7m | 0.5–0.6 |

**Trade-off**: SteeringScale bajo suaviza correcciones (evita sobre-rotación) pero limita la corrección máxima en situaciones críticas. Calibrar empíricamente según la ruta y el vehículo.

### 5.5 Puntos ciegos del framework identificados

Durante el desarrollo y la validación empírica, se identificaron tres puntos donde el framework "decide" sin consultar al recording, asumiendo implícitamente un perfil de vehículo específico (el bus original):

**Punto ciego 1 — Throttle**: en cruise mode, cuando `kmh < target.targetSpeed * 0.9`, el framework aplica `throttle = 1.0` por default ignorando el `targetThrottle` del recording. Para vehículos pesados es correcto; para vehículos livianos genera wheelspin. **Fix**: respetar `target.targetThrottle` cuando la desviación de velocidad es moderada (±10 km/h), override solo en desviación grande.

**Punto ciego 2 — Gear**: el framework usa una AT por RPM para inferir el gear, ignorando el `targetGear` del recording. La técnica humana de "subir gear para suavizar aceleración" se pierde. El anti-catapulta (sección 5.3) ataja esto desde el resultado físico, no copiando el input humano.

**Punto ciego 3 — Steering scale por wheelbase**: resuelto en v1.x con el campo `SteeringScale` del vehicle profile.

Los tres puntos ciegos son consecuencia de un patrón común: el framework opera mejor cuanto MENOS inventa y MÁS reproduce. Cada override silencioso del recording es una asunción de vehículo. Eliminar overrides es el principio rector de la generalización.

### 5.6 Gotchas técnicos de Enforce

**Cross product case-sensitive**: classnames de Expansion usan snake_case minúscula (`expansion_landrover_wheel`); vanilla usa PascalCase (`CarBattery`). Mezclar es fácil y silencioso (DayZ no avisa). Verificar siempre.

**eAI hereda PlayerBase**: para distinguir un player real de un eAI, usar `GetIdentity()` (los players reales tienen `PlayerIdentity`, los eAI no). `PlayerBase.Cast()` por sí solo no alcanza.

**"Formula too complex"**: el compilador Enforce tiene un límite ~9 operandos encadenados con `+`. Dividir expresiones largas con `+=` en múltiples líneas.

**Clases del engine no admiten `modded class`**: `CGame` y similares engine: `modded class CGame` rompe al cargar. AddonBuilder no valida Enforce, solo el runtime.

**`new Class(args)` no funciona**: en Enforce, `new ClassName(arg1, arg2)` revienta. Crear vacío y setear campos.

**Orden de scopes**: 3_Game → 4_World → 5_Mission. Helpers compartidos deben estar al scope más temprano.

---

## Capítulo 6 — Buenas prácticas para grabar rutas

### 6.1 Steering

- **1ra persona** preferentemente. La 3ra persona induce manejo arcade con curvas tomadas a alta velocidad por falta de sensación de masa.
- Si la curva es muy pronunciada y necesitás referencia espacial, alternar a 3ra persona puntualmente durante la maniobra. Equivalente al "girar la cabeza" del conductor real.
- Steering progresivo, nunca de golpe.

### 6.2 Freno

- Anticipar la frenada. Aplicar gradualmente, no de golpe.
- Soltar el acelerador en pendientes (engine braking) es captura activa: el framework reproduce literalmente.

### 6.3 Throttle

- Sostenido cuando se quiere mantener velocidad de crucero.
- En marchas bajas con vehículos torquey: throttle moderado (~0.4) para evitar wheelspin en grabación que la IA va a reproducir.

### 6.4 Cambios

- Subir gear para suavizar aceleración es técnica humana válida y capturada por el framework.
- En curvas pronunciadas: 3ra marcha es óptima para vehículos ágiles (gear alto reduce torque a las ruedas, steering continuo posible).

### 6.5 Paradas

**Paradas de pasajeros**: marcar con NUMPAD 4 en la coord exacta del cartel. El sistema mapeará automáticamente al anchor más cercano de `bus_stops.json` si está dentro del radio configurado.

**Paradas técnicas**: marcar con NUMPAD 4 antes de curvas 90° críticas, encrucijadas, maniobras de precisión. El bus IA va a parar 2 segundos en ese punto, garantizando aproximación a velocidad cero.

### 6.6 Generales

- Empezar con el vehículo en marcha (no desde quieto) o saltear los samples iniciales con velocidad cero (auto-trim por el framework en v1.x).
- Hacer la ruta entera de una vez si es posible. Si no, usar el hand-off (sección 6.7) para grabar en sesiones.
- Conducir como si fuera real: cuidado con peatones (no hay, pero el principio aplica), respetar limites razonables.

### 6.7 Hand-off operador↔IA durante una corrida

Durante una corrida de la IA, el operador puede tomar el bus como conductor. El framework detecta automáticamente (`GetIdentity()` chequeo) que es humano y se hace a un lado: no aplica cached inputs, deja al operador conducir libremente. Al bajarse, la IA retoma.

Esto habilita:

- Grabaciones largas en sesiones cortas (retomar tramos)
- Iteración por sub-tramos (regrabar solo lo problemático)
- Colaboración multi-operador
- Hot-patching en vivo (corrección puntual durante corrida IA)

---

## Capítulo 7 — Casos de uso

### 7.1 Transporte público

Servicio de bus o taxi entre puntos fijos, con paradas de pasajeros. Caso de uso primario del framework. Ejemplo validado: ruta costera de Chernarus, 14 paradas, 24.92 km.

### 7.2 Convoyes con escolta

Múltiples vehículos siguiendo recordings paralelos por sentido. Requiere implementación de multi-rutas (roadmap v1.x). Cada vehículo carga su propio recording, no hay coordinación dinámica entre ellos.

### 7.3 Patrullas

Vehículos militares siguiendo recordings cíclicos. Combinado con AI armado por separado, puede generar checkpoints de patrulla, escoltas, fuerzas hostiles motorizadas.

### 7.4 Logística automatizada

Camiones llevando carga entre bases, recorridos de abastecimiento. Vehículos pesados con perfil físico distinto al bus deben ser calibrados (vehicle profile específico).

### 7.5 Misiones con vehículos como elemento central

Misiones de "ladrones de vehículos" donde NPCs roban autos y huyen siguiendo recordings predefinidos. NPCs en heli crashes que conducen para recuperar carga. Aplicaciones de gameplay nuevas habilitadas por el framework.

### 7.6 PVE vs PVP

El framework es agnóstico al modo del server. En PVE, los buses son protegidos por `SetAllowDamage(false)`. En PVP, el operador puede dejarlos vulnerables y el comportamiento físico se mantiene (el bus puede ser destruido, en cuyo caso `OnBusDestroyed` dispara cleanup).

---

## Capítulo 8 — Alcance y límites del framework

### 8.1 El espectro fidelidad / naturalidad

Cualquier sistema que "mueve un vehículo de A a B" en un juego opera en algún punto de un espectro de cuatro categorías:

| Enfoque | Fidelidad | Naturalidad | Caso de uso óptimo |
|---|---|---|---|
| eAI vanilla sin guía | ~0% (errático) | media | autos genéricos de background |
| **Recording-driven puro (este framework)** | ~95% | **máxima** | servicios, vehículos cotidianos |
| Híbrido con snapshot correctivo | ~99% | media (sprite-like en momentos críticos) | convoyes precisos, misiones críticas |
| Snapshot puro (SetPosition) | 100% | **cero** | cinematics, scripts narrativos |

Cada enfoque tiene un trade-off inevitable: subir fidelidad sacrifica naturalidad.

El framework v1 vive en el segundo punto. Toda fidelidad viene de respetar la física del vehículo. Las desviaciones residuales son el costo de mantener naturalidad.

### 8.2 Discretización temporal como umbral

El framework opera a frecuencia de Tick (~2-4 Hz). Un operador humano opera a ~100 Hz efectivo. Esa asimetría es el primer umbral:

| Vehículo y velocidad | Latencia × velocidad | Síntoma |
|---|---|---|
| Bus a 50 km/h | 7m de "ceguera" entre Ticks | dentro del corredor (1m), invisible |
| Land Rover a 80 km/h | 11m | marginal, visible en pendientes |
| DeLorean a 200 km/h | 28m | catastrófico |

El umbral no es la velocidad máxima del vehículo: es `velocidad × latencia_del_control`. Bajar la latencia (micro-tuning v2: samples a 100Hz, inputs interpolados) sube el umbral sin cambiar de paradigma.

### 8.3 Respuesta de masa como umbral

El segundo umbral es la masa del vehículo. Vehículos pesados atenúan los overrides del framework hasta hacerlos invisibles. Vehículos livianos los manifiestan visiblemente:

| Vehículo | Masa típica | Síntoma de los overrides |
|---|---|---|
| Bus | ~10000 kg | latente, invisible salvo en zonas costeras |
| V3S | ~7000 kg | latente, visible en pendientes pronunciadas |
| **Land Rover Defender** | **~2000 kg** | **manifiesto: catapulta + zigzag** |
| Hatchback | ~1100 kg | muy manifiesto |
| Sedan | ~1300 kg | similar |

El framework opera dentro de un **volumen 2D** del espacio de vehículos: masa moderada-alta × velocidad cotidiana. Salirse de ese volumen expone bugs latentes.

### 8.4 Principio rector

El framework reproduce comportamiento humano sobre vehículos físicamente válidos. Sus límites son:

1. **El vehículo físicamente no puede** → cambiar el vehículo o modificarlo, no el framework
2. **La discretización temporal es insuficiente** → micro-tunings v2, dentro del mismo paradigma
3. **El caso de uso requiere fidelidad >99%** → cambiar de paradigma (snapshot puro o híbrido), sacrificando naturalidad

El framework NO es una técnica universal de movimiento controlado. Es una técnica de **fidelidad de intención** que opera donde la física del juego es realista y la frecuencia de control es suficiente.

> Definí lo que querés antes de elegir. Si necesitás "que el coche llegue exacto al punto" independientemente de cómo se vea — usá snapshot. Si necesitás "que se sienta real para los pasajeros" — este framework es óptimo. **No hay almuerzo gratis**.

---

## Capítulo 9 — Validación empírica

### 9.1 Bus canónico

Ruta de ida del bus de Chernarus, recording incremental de 7004 waypoints construido en tres segmentos sucesivos con perfiles de operador distintos (agresivo, conservador en 1ra persona, regrabado para corrección puntual).

**Scorecard**:

```
Distancia:          24.92 km
Duración:           31.3 min
Velocidad promedio: 47.8 km/h
Waypoints:          7004
Paradas:            14 (todas activadas correctamente)
Llegada a terminal: sí

Métricas de calidad:
- dev lateral avg:      1.00m
- dev lateral median:   0.88m
- dev lateral p95:      2.22m
- dev lateral max:      3.18m
- |steer| avg:          0.026
- |steer| max:          0.593
- _inC (en corredor):   94.5%
- _outC (corrigiendo):  1.7%
```

**Cruzando con observación humana**: 9 markers (NUMPAD-) en 31 minutos por el operador anotando eventos notables. 8 de 9 markers corresponden a momentos donde la IA superó las expectativas (dev media de 0.69m en esos puntos, mejor que el promedio global). Solo 1 marker negativo (5% final de la ruta, atribuible a freno excesivo del operador en la grabación, no a falla del control).

### 9.2 Generalización a Land Rover Defender

Mismo framework, vehículo radicalmente distinto. Tres grabaciones con escalada de control:

| Toma | Estilo | Vel avg | Vel max | Gear max |
|---|---|---|---|---|
| Stress test | Agresivo, todo el rango del vehículo | 60.5 km/h | 100.0 | 6ta (gear 7) |
| Moderada | Inverso, "vivir en 30-60" | 46.3 | 96.7 | 5ta |
| Controlada con paradas técnicas | Manejo cauto + NUMPAD 4 en curvas 90° | 38.5 | 77.5 | 5ta |

**Resultado de la corrida IA con la toma controlada**:

- dev avg lateral: 0.91m
- _inC: 92.7%
- Llegada a la terminal: sí
- Las 3 paradas técnicas activaron correctamente (dev max <0.75m en las maniobras)

Cambios al código del control: **cero**. Cambios al vehicle profile: clase, max gear (7), attachments (lista de classnames del Land Rover), anti-catapulta threshold (15 km/h/s), steering scale (0.5-0.7).

Esto valida empíricamente el principio del Capítulo 3: el framework cubre cualquier `CarScript` sin modificación del control, solo configuración por JSON.

### 9.3 Validación del modelo físico

Tests específicos en pendientes (Sonomir, ~4.43°):

- Modelo `a = u²/(2s) + g·sin(θ)` con error <10% vs medición física en 3 terrenos (plano NWAF, pendiente NWAF norte, pendiente Sonomir)
- Invariancia al peso del pasajero: misma frenada con bus vacío vs con jugador a bordo (validación de que la fórmula es masa-agnóstica)

---

## Capítulo 10 — Trabajo en curso y roadmap

Este capítulo documenta honestamente el estado del framework al momento de la publicación: qué está cerrado y validado, qué está identificado pero no implementado, y qué constituye trabajo de investigación futura. La intención es que la comunidad pueda contribuir a partir del estado actual sin sorpresas.

### 10.0 Estado al cierre de v1.0 — lo que está y lo que no

**Implementado y validado**:

- Breakthrough técnico (Cap 2): override del `ShiftTo(FIRST)` de eAI mediante CarScript modded
- Herencia automática (Cap 3): validada con bus + Land Rover sin tocar código del control
- Stanley controller con corredor (Cap 5.1) — validado en 24.92 km de bus con dev avg 1.00m
- Modelo físico predictivo de freno (Cap 5.2) — error <10% vs medición en 3 terrenos
- Anti-catapulta por aceleración medida (Cap 5.3) — validado con Land Rover usando 6ta marcha
- Steering scale por wheelbase (Cap 5.4) — configurable por JSON
- Vehicle profile completo (Cap 4.2): 5 campos configurables por JSON
- Sistema de estados (Cap 4.3): preservación de múltiples recordings sin pisarse
- Hand-off operador↔IA (Cap 6.7): operador puede tomar el bus durante una corrida IA
- Auto-spawn del bus al startup del server
- Auto-respawn al llegar a terminal (loop continuo del servicio)

**Identificado pero no implementado** (roadmap prioridad alta):

1. **Respetar `targetThrottle` del recording en cruise mode** — actualmente, cuando la desviación de velocidad es moderada, el framework usa hardcoded `throttle = 1.0` ignorando el `targetThrottle` capturado. Para vehículos pesados (bus) es benigno; para vehículos livianos (Land Rover, Hatchback) genera wheelspin y aceleración brutal en pendientes.

2. **Refinamiento del anti-catapulta**: lock asimétrico de shifts. Cuando el trigger anti-catapulta sube de gear, la AT por RPM bajo lo deshace pocos ms después. Necesita un lock más largo (~3 segundos) solo para shifts disparados por anti-catapulta, mientras los shifts por RPM mantienen lock corto (800ms).

3. **Auto-trim de leading silence** en el recording: si los primeros samples tienen velocidad <1 km/h (operador parado al inicio), saltarlos al convertir CSV → JSON. Evita que el framework arranque la corrida desde un estado físicamente quieto.

4. **Vehicle profile extendido**:
   - `STANLEY_K`: ganancia del Stanley configurable por vehículo
   - `MAX_BRAKE_DECEL`: capacidad de freno máxima
   - `MASS_FACTOR`: escala el factor `g·sin(θ)` del modelo de freno según masa del vehículo
   - `PARKING_PUSH_THROTTLE`: hoy hardcoded 0.35, debería configurarse por vehículo
   - `FailsafeMode`: "FAITHFUL" (default) o "CONSERVATIVE" con anti-stall y safety nets para producción

5. **Fix de invulnerabilidad del driver**: `SetAllowDamage(false)` en Boris al spawnearlo, + monitor de respawn si muere o se cae del bus.

6. **Multi-rutas**: soporte de N buses simultáneos siguiendo recordings distintos (ida + vuelta del bus costero, convoyes con varios vehículos, etc.). Refactor del JSON config a array de rutas.

7. **Adapter de BoatScript** para transporte marítimo.

8. **Adapter de Helicopter** (v2.0+) para transporte aéreo, con hooks específicos para rotor principal y cola.

### 10.1 Investigación futura

Pendientes que requieren investigación empírica adicional antes de implementación:

**Iterative Learning Control (ILC) automatizado**:

El framework hace 3-5 corridas internas, mide error vs el recording, ajusta parámetros automáticamente. Requiere desarrollar el loop de feedback, la métrica de error multi-dimensional, y los criterios de convergencia. Tecnología conocida en literatura de robótica, transferible al juego con cuidado.

**Corredor v2 con slip angle**:

PathLogger extendido que captura posiciones de las 4 ruedas en lugar del centroide del vehículo. El "corredor" resultante captura naturalmente el off-tracking del eje trasero y el slip angle en curvas. Mejora la fidelidad geométrica en vehículos largos (buses, camiones). Implica modificar el formato del recording y todas las herramientas de procesamiento downstream.

**Pathfinding reactivo híbrido**:

Usar el sistema `m_PathFinding` de eAI a pie (navmesh-based, más sofisticado que el vehicular) para micro-correcciones puntuales en zonas marcadas como críticas en el JSON. Combinado con velocidad baja, podría dar precisión sub-metro en rotondas, estacionamientos, maniobras complejas. Requiere experimentación para entender cómo integrar dos sistemas de navegación (uno por waypoints, otro reactivo).

**Micro-tuning de discretización temporal**:

PathLogger a 100 Hz en lugar de los 4 Hz actuales. Aplicación de inputs interpolados entre Ticks (en lugar de cambiar `m_Cached*` cada 250ms y aplicar el mismo valor todos los frames). Look-ahead temporal del input (aplicar el input del recording correspondiente a N samples adelante, no el sample actual). Estas optimizaciones suben el umbral de discretización temporal descrito en el Capítulo 8.2 sin cambiar el paradigma recording-driven.

**Experimento de peso variable**:

Validar empíricamente la propiedad de invariancia al peso del modelo predictivo de freno con cargas extremas: 5 NPCs como pasajeros + baúl lleno de munición. La predicción teórica es que el control de freno no requiere recalibración (la fórmula `a = u²/(2s)` es masa-agnóstica), pero el rozamiento variable, la transferencia de peso en frenadas, y la respuesta del motor pueden requerir ajustes empíricos.

**Calibración automática por toma 1 (vehicle profile auto)**:

Reemplazar la calibración manual del vehicle profile por una **toma de caracterización** que el operador hace una vez al integrar un vehículo nuevo. La toma 1 incluye: frenada full desde velocidad alta (mide `MAX_BRAKE_DECEL`), aceleración full desde cero (curva de aceleración), coast test (drag/inercia), shift points (gear ranges), step response (delay del control). El framework infiere el vehicle profile completo de esta toma. La toma 2 es la grabación de la ruta. Workflow simplificado: 2 minutos de caracterización + grabación de ruta + done.

### 10.2 Tareas operativas pendientes (no técnicas)

- **Grabación humana de vuelta del bus** (Terminal → Kamenka por carril opuesto) — primera validación práctica de bidireccionalidad
- **Regrabación de curvas 90° del Land Rover** con técnica óptima en 3ra a 20-25 km/h
- **Drill de curvas 90°** en grilla urbana para caracterizar empíricamente el comportamiento en intersecciones (relevante para convoyes urbanos)
- **Limpieza de wrecks en la ruta del bus** (capas mapgrouppos.xml + events.xml + sweep al startup) según Capítulo 4.6

### 10.3 v1.x prioridades

**Fix de los puntos ciegos del framework**:

- Respetar `targetThrottle` del recording en cruise mode (cuando desviación de velocidad es moderada)
- Refinar anti-catapulta: lock asimétrico (shift-up por anti-catapulta lockea más fuerte que shift-down por RPM bajo)
- Auto-trim de leading silence en el recording (saltar samples iniciales con velocidad cero)

**Vehicle profile extendido**:

- `STANLEY_K`: ganancia lateral por vehículo (configurable)
- `MAX_BRAKE_DECEL`: capacidad de freno (configurable)
- `MASS_FACTOR`: escala el `g·sin(θ)` del modelo de freno
- `FailsafeMode`: "FAITHFUL" (default) o "CONSERVATIVE" (con anti-stall y safety nets para producción robusta)

**Features nuevos**:

- Adapter de BoatScript (transporte marítimo)
- Multi-rutas: soporte de N buses simultáneos (ida + vuelta + servicios paralelos)
- Fix de invulnerabilidad del driver (`SetAllowDamage(false)` + monitor de respawn)
- Auto-detección de wheelbase del vehículo para sugerir `SteeringScale`

### 10.2 v2.0 visión

**Iterative Learning Control (ILC) automatizado**:

El framework hace 3-5 corridas internas, mide error vs el recording, ajusta parámetros (STANLEY_K, MAX_BRAKE_DECEL, etc.) automáticamente. El usuario solo graba la ruta, el framework calibra solo.

**Corredor v2 con slip angle**:

PathLogger captura posiciones de las 4 ruedas en lugar del centroide. El "corredor" se vuelve la envolvente real de las trayectorias de las ruedas, capturando off-tracking del eje trasero, slip angle en curvas, geometría dinámica del chasis. Mejora fidelidad en vehículos largos.

**Pathfinding reactivo**:

Usar el sistema `m_PathFinding` de eAI a pie (navmesh-based, más sofisticado que el vehicular) para micro-correcciones en zonas críticas. Combinar con velocidad baja para precisión sub-metro en rotondas y estacionamientos.

**Micro-tuning de frecuencias**:

PathLogger a 100 Hz en lugar de 4 Hz. Inputs interpolados entre Ticks. Look-ahead temporal del input. Sube el umbral de discretización temporal (Capítulo 8.2) sin cambiar de paradigma.

**Adapter de Helicopter**:

Mismo principio que para barcos, ajustando hooks específicos del rotor principal y cola. Mayor complejidad por la dimensión vertical.

---

## Apéndice A — Disclosure de uso de IA

Este documento, el código del framework y la metodología empírica fueron desarrollados en colaboración con **Claude Opus 4.7 (Anthropic)** como asistente técnico. La división de roles:

- **Operadores humanos**: visión del producto, diseño de gameplay, conducción de pruebas in-game, diagnóstico empírico, decisiones estratégicas, dirección sesión a sesión, articulación de principios filosóficos del framework.
- **Asistente IA**: implementación de código en Enforce, exploración del fuente de terceros (notablemente eAI y Expansion), drafting del manual, síntesis técnica, análisis de datos del logger.

El breakthrough técnico del Capítulo 2 emergió de esa colaboración: la hipótesis "eAI hardcodea el gear" fue de los operadores; la verificación en el código de Expansion y la implementación del override fue del asistente.

Recomendamos este setup a otros modders. El Apéndice C contiene un AI knowledge pack diseñado para que cualquier modder pueda reproducir este tipo de colaboración con el asistente preferido (Opus, GPT, Gemini, otros) sobre nuestro framework.

---

## Apéndice B — Créditos externos

### Paradas físicas del bus: @Brutalist Bus Stops

Los modelos de las paradas del bus en el server BrigadaZ son del mod **@Brutalist Bus Stops** de **Buddy** (docbuddy en Discord). En la versión a publicar en Workshop los modelos van repackeados dentro del Transport con crédito explícito al autor (autorización expresa otorgada por Buddy).

**Importante**: la UI del Transport no depende de los modelos físicos. El detector de proximidad usa solo coordenadas + radio definidos en el JSON. Si el mod externo no está cargado, las paradas no se ven en el mundo pero la mecánica del servicio funciona igual.

### Vehículos de prueba

Durante el desarrollo del framework usamos vehículos de varios mods de la comunidad del Workshop para validar la herencia automática de `CarScript`. Probamos autos deportivos, off-road y camiones de distintos autores — todos funcionaron sin escribir una línea adicional de código por vehículo.

### Land Rover Defender: @DayZ-Expansion-Vehicles

El Land Rover Defender usado en la validación del Capítulo 9.2 es del mod **@DayZ-Expansion-Vehicles** (parte del bundle de Expansion). Classname: `Expansion_Landrover`.

---

## Apéndice C — AI knowledge pack para modders

Este apéndice referencia un archivo separado: **`MOD_CONTEXT_FOR_AI.md`** en la raíz del repositorio. Es un documento diseñado para ser adjuntado al workspace de un asistente IA (Claude / GPT / Gemini / Opus / otro) junto con los archivos del mod, para que esa IA tenga el contexto técnico completo del framework y pueda asistir respondiendo preguntas con precisión.

### Cómo usarlo

1. Clonar el repositorio del framework
2. En el workspace de tu asistente IA preferido, adjuntar:
   - Todos los archivos `.c` del framework (`scripts/`)
   - El archivo `MOD_CONTEXT_FOR_AI.md` como instrucciones de contexto
   - Tu vehicle profile específico (JSON) si lo tenés
3. Hacer preguntas sobre el framework: el asistente tiene contexto completo para responder con precisión

### Limitaciones

El AI knowledge pack es un snapshot. Si el framework evoluciona (cambios al control, parámetros nuevos del vehicle profile, etc.), regenerar el knowledge pack desde el repositorio actualizado.

---

## Apéndice D — Sobre los autores

El proyecto **BrigadaZ_Transport** y este framework asociado fueron desarrollados por **Sonom4n** e **Hiperhipo10** como parte del ecosistema **BrigadaZ PVE Server** (Chernarus).

El equipo viene desarrollando mods para el server desde hace años, incluyendo entre otros:

- **@BrigadaZRadio** — sistema de radio in-game
- **@BrigadaZ_Info** — UI informativa contextual
- **@BrigadaZ_Finanzas** — sistema financiero con re-cotización
- **@Losrollinmod** v1/v2 — retexturado de carteles + pantalla de presentación
- **@BrigadaZ_Mission** (en desarrollo) — extensión del sistema Quest de Expansion

El framework presentado aquí emergió del intento de implementar un servicio de bus público que recorriera la costa de Chernarus. Tras varios callejones sin salida con los enfoques convencionales, el breakthrough del Capítulo 2 permitió convertir el proyecto puntual en un framework reusable + manual público + AI knowledge pack para la comunidad de modding.

### Disponibilidad y soporte

El mod se publica en Steam Workshop como **BZ_Transport (Bus eAI Driving)**. El código y el manual están disponibles en GitHub bajo licencia abierta. Para preguntas, los modders pueden:

1. Adjuntar el AI knowledge pack a su workspace de IA y preguntar directamente
2. Abrir issues en el repositorio de GitHub
3. Contactar al equipo en Discord para casos puntuales

### Licencia y uso

**No comercializamos este proyecto.** El mod, el framework, el manual y todos los archivos que lo acompañan son completamente libres para uso, repack, rebuild, fork, modificación y redistribución. No pedimos permiso ni atribución obligatoria (aunque siempre se agradece). El único pedido es que los componentes de terceros incorporados mantengan sus propias condiciones de uso.

Invitamos a la comunidad de modding a mejorar, reconstruir, desarrollar y potenciar este framework. Mejor que un mod particular es un patrón que muchos adoptan, mejoran y reinterpretan para casos que nosotros nunca imaginamos.

---

*Fin del manual.*
