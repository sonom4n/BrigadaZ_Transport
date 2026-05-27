# AI Knowledge Pack — BrigadaZ_Transport (Framework eAI Vehicular)

Este documento está diseñado para ser adjuntado al workspace de un asistente IA
(Claude, GPT, Gemini, Opus, etc.) junto con los archivos del mod, para que la
IA tenga contexto técnico completo del framework y pueda asistir respondiendo
preguntas con precisión y proponer adaptaciones a casos de uso particulares
del modder que lo consulta.

> Si sos un humano leyendo esto, andá mejor al `PAPER_NOTES.md` o
> `MANUAL_eAI_VEHICLES.md` — están escritos para vos. Este archivo es para
> tu asistente IA.

---

## 1. Quién es este proyecto

**BrigadaZ_Transport** es un mod para DayZ Standalone que implementa un servicio
de transporte público (bus costero en Chernarus) manejado autónomamente por un
NPC eAI. Pero el valor central del trabajo no es el bus en sí, sino el
**framework reutilizable** que descubre y publica el patrón técnico para que
cualquier modder pueda implementar vehículos manejados por NPCs en DayZ.

El framework opera bajo el principio de **subordination architecture**: dejar
que el motor del juego y eAI hagan su trabajo natural (físicas reales,
animaciones, audio del motor, IK del conductor al volante, pathfinding global),
interceptando únicamente los outputs específicos donde el comportamiento del
mod base es incorrecto para nuestro caso.

**Estado**: proyecto público en desarrollo. v1.0 funcional y validada
empíricamente con 24.92 km de servicio + generalización a Land Rover Defender.
Hay capacidades identificadas pero no implementadas (ver Capítulo 12 del
manual). Las publicaciones de la comunidad siguen activas.

---

## 2. Cuándo este framework es la solución correcta

**Casos donde este framework es óptimo**:

- Servicio de transporte público (bus, taxi, ambulancia, helicóptero futuro)
- Convoy con N vehículos siguiendo recordings paralelos
- Patrullas motorizadas cíclicas
- Misiones con NPCs vehiculares (ladrones de autos, mecánicos de heli crashes)
- Logística automatizada (camiones llevando carga entre bases)
- Cualquier caso donde un vehículo deba moverse "como un humano lo manejaría"

**Casos donde este framework NO es óptimo** (usar otra arquitectura):

- Cinemáticas precisas donde el vehículo debe estar exacto en una coord específica
- Misiones tipo "el camión llega exacto al checkpoint a las 14:00:00"
- Movimiento de sprites/objetos sin pretensión de físicas reales
- Vehículos a velocidades extremas que exceden las capacidades físicas del juego

Para esos casos, usar `SetPosition` directo con interpolación es más apropiado
(snapshot puro, sacrificando naturalidad por fidelidad geométrica perfecta).

---

## 3. Arquitectura técnica del framework

### 3.1 El breakthrough técnico (Capítulo 2 del manual)

eAI mete `ShiftTo(CarGear.FIRST)` cada frame en su `CarScript.OnInput` modded.
Esto hace que cualquier vehículo controlado por eAI no acelere correctamente.

**Solución arquitectónica**: nuestro propio `modded class CarScript` se ejecuta
DESPUÉS del OnInput de eAI y sobreescribe el gear:

```enforce
modded class CarScript {
    override void OnInput(float dt) {
        super.OnInput(dt);  // eAI ejecuta su lógica
        if (!GetGame().IsServer()) return;

        BZBusService srv = BZBusService.GetInstance();
        if (!srv || !srv.IsBusActive(this)) return;

        // Si seat 0 es un player real, dejarlo manejar (hand-off)
        Human driver = CrewMember(0);
        if (driver) {
            PlayerBase realPlayer = PlayerBase.Cast(driver);
            if (realPlayer && realPlayer.GetIdentity()) return;
        }

        // Aplicar inputs cacheados del service (sobrescribe eAI)
        srv.ApplyBusInput(this, dt);

        // Sobrescribir gear con el deseado por nuestra AT
        int desired = srv.GetDesiredGear();
        if (GetGear() != desired) ShiftTo(desired);
    }
}
```

**Nota crítica sobre eAI heredando PlayerBase**: en DayZ, eAI hereda de
`PlayerBase`, por lo que `PlayerBase.Cast()` también devuelve true para NPCs eAI.
Para discriminar player real de eAI, usar `GetIdentity()` — los players reales
tienen `PlayerIdentity`, los eAI no.

### 3.2 Componentes del framework

```
PathLogger (cliente, captura grabacion humana)
    -> CSV con time, x, y, z, kmh, throttle, brake, steering, gear, is_stop
    -> Herramienta csv_to_route.ps1 convierte a JSON

BZBusRouteConfig (JSON deserializable)
    -> VehicleClass, DriverClass, MaxGear, Attachments,
       AccelShiftThreshold, SteeringScale, Waypoints

BZBusService (servidor, central)
    -> Init() spawnea bus al startup
    -> Tick cada 500ms ejecuta DriveTowards (Stanley + corredor + control predictivo)
    -> AdvanceWaypoint() avanza la ruta y respawnea al llegar a terminal

CarScript modded (servidor, AT + override de eAI)
    -> Intercepta OnInput, aplica cached inputs del service
    -> AT por RPM + anti-catapulta por aceleracion medida

PlayerBase modded (servidor, RPCs)
    -> Maneja NUMPAD 2 (respawn), NUMPAD 7 (AI logging), etc.
```

### 3.3 Vehicle profile (configurable por JSON)

Cualquier vehículo nuevo se integra escribiendo un JSON:

```json
{
    "RespawnDelay":         300,
    "AverageSpeedMS":       11.0,
    "VehicleClass":         "Expansion_Landrover",
    "DriverClass":          "eAI_SurvivorM_Boris",
    "MaxGear":              7,
    "Attachments":          ["expansion_landrover_wheel", "expansion_landrover_wheel", "..."],
    "AccelShiftThreshold":  15.0,
    "SteeringScale":        0.7,
    "Waypoints":            ["..."]
}
```

**Campos y semántica**:

| Campo | Función | Cómo calibrar |
|---|---|---|
| `VehicleClass` | classname del vehículo | Verificar con `Car.Cast(entity)` en testing |
| `DriverClass` | classname del NPC eAI driver | `eAI_SurvivorM_Boris` u otro de Expansion-AI |
| `MaxGear` | gear máximo (CarGear: FIRST=2, ..., SIXTH=7) | Probar manualmente cuántas marchas tiene |
| `Attachments` | partes a equipar al spawnear | Mirar trader files de Expansion |
| `AccelShiftThreshold` | km/h por seg para anti-catapulta | Bus pesado: 999 (off). Liviano: 15. Ágil: 10 |
| `SteeringScale` | compensa wheelbase corto | Bus: 1.0. V3S: 0.7. Land Rover: 0.7. Hatchback: 0.5 |
| `Waypoints` | trayectoria | Generado por csv_to_route.ps1 desde PathLogger |

### 3.4 Stanley controller con corredor

```enforce
// Cross product en sistema left-handed de DayZ
// IMPORTANTE: la convención correcta es AB.z * AP.x - AB.x * AP.z
// (la otra orientación lleva el bus al agua por signo invertido)
float cross = ABz * APx - ABx * APz;
float lateralOffset = cross / segLen;

// Stanley clásico atenuado por velocidad
float crossCorrection = Math.Atan2(STANLEY_K * lateralOffset, velocity_ms);
float targetYaw = segmentHeading - crossCorrection;
```

**K = 1.0** funcionó empíricamente bien. K modulado por curvatura local se
probó y falló (la métrica curvatura no captura el problema real).

### 3.5 Modelo físico predictivo de freno

```enforce
// Cinemática clásica masa-agnóstica
float aNeeded = (kmh / 3.6) * (kmh / 3.6) / (2 * distance) + g * Math.Sin(pendiente);
float brake = aNeeded / MAX_BRAKE_DECEL;
```

`MAX_BRAKE_DECEL = 50` (m/s²) calibrado empíricamente para el bus. El factor de
pendiente (`g·sin(θ)`) corrige el cálculo en bajadas/subidas.

### 3.6 Anti-catapulta (shift up por aceleración medida)

```enforce
float accelKmhPerSec = (kmh_now - kmh_prev) / dt;
if (accelKmhPerSec > AccelShiftThreshold && desired < maxGear) {
    SetDesiredGear(desired + 1);  // shift up automatico
    BZBus_LockShift();
}
```

Replica la técnica humana "subir gear + pisar fuerte = manejo suave" sin copiar
literalmente el `targetGear` del recording (que tiene bugs por waypoints
iniciales con velocidad cero).

---

## 4. Cómo adaptar el framework a tu caso de uso

### 4.1 Si querés un servicio de transporte similar al nuestro

**El path más simple**:

1. Fork del repositorio
2. Cambiar el nombre del mod (`@MiTransporte` en lugar de `@BrigadaZ_Transport`)
3. Cambiar las referencias internas (BZBus → MiBus, BrigadaZ → MiOrg)
4. Definir tu vehicle profile en JSON con tu vehículo
5. Grabar tu ruta con PathLogger humano
6. Convertir CSV → JSON con `csv_to_route.ps1`
7. Build + deploy

### 4.2 Si querés un convoy militar

**Pasos adicionales**:

1. Implementar multi-rutas (extender `BZBusRouteConfig` a array de rutas, ver
   sección "Trabajo en curso" del manual)
2. Cada vehículo del convoy tiene su propio recording + vehicle profile
3. Coordinar los timings: grabar los recordings desfasados ~5-10 segundos para
   que los vehículos no choquen si uno frena
4. Considerar usar `FailsafeMode: CONSERVATIVE` (cuando esté implementado) para
   que el convoy continúe aunque un vehículo se trabe

### 4.3 Si querés un vehículo de patrulla cíclico

**Adaptación**:

1. Grabar una ruta cerrada (vuelve al punto de inicio)
2. En `AdvanceWaypoint`, en lugar de respawnear al llegar al final, reiniciar
   `m_WaypointIndex = 0` directamente (sin destruir el vehículo)
3. Ajustar `RespawnDelay = 0` o casi cero para loop fluido
4. Considerar que el motor + combustible del vehículo se agoten eventualmente
   (agregar refill periódico)

### 4.4 Si querés un vehículo distinto al bus

**Cómo integrar un vehículo nuevo (Hatchback, V3S, Olga, vehículos modded del Workshop, etc.)**:

1. **Verificar herencia**: spawnear el vehículo y confirmar que es `CarScript`
2. **Obtener classnames de attachments**: mirar los trader files de Expansion
   Market o la config.cpp del mod del vehículo
3. **Calibrar MaxGear**: probar manualmente cuántas marchas tiene
4. **Calibrar SteeringScale**: empezar con 0.7 y bajar a 0.5 si sobrerota, subir a 0.9 si subcorrige
5. **Calibrar AccelShiftThreshold**: empezar con 15 km/h/s, bajar si sigue catapultando
6. **Grabar el recording** con la técnica óptima del vehículo (vehículos torquey: 3ra en curvas, throttle moderado)

### 4.5 Si querés modificar el control (Stanley, freno, etc.)

**Antes de modificar**:

- Leer el Capítulo 5 del manual para entender cada componente
- Revisar la sección "Tres puntos ciegos del framework" — son lugares donde
  el código asume implícitamente el perfil del bus original
- Validar empíricamente con AI logging (NUMPAD 7) antes y después del cambio
- Comparar dev_avg, %_inC, %_outC entre corridas

---

## 5. Mods y dependencias

### 5.1 Dependencias requeridas

- **DayZ-Expansion-AI** (eAI): provee la capacidad base de NPCs vehiculares
- **DayZ-Expansion-Core**: dependencia de Expansion-AI
- **DayZ-Expansion-Vehicles**: si usás vehículos de Expansion (LandRover, Bus, etc.)
- **CommunityOnlineTools** (COT) o equivalente: para spawn manual y debugging

### 5.2 Dependencias opcionales

- **@Brutalist Bus Stops**: modelos físicos de las paradas del bus (repackeado
  en BrigadaZ_Transport con autorización del autor)
- **Mods de vehículos del Workshop**: cualquier mod que extienda `CarScript` es compatible automáticamente para validar generalización
- **@DayZ Editor Loader**: si querés agregar objetos custom al mundo

---

## 6. Patrones de código de referencia

### 6.1 Spawnear un vehículo con NPC al volante

```enforce
// Crear vehículo
EntityAI bus = EntityAI.Cast(GetGame().CreateObject("ExpansionBus", spawnPos, false, true));
bus.SetAllowDamage(false);  // invulnerable en producción

// Equipar attachments del vehicle profile
foreach (string attachClass : config.Attachments) {
    bus.GetInventory().CreateAttachment(attachClass);
}

// Llenar fluidos
Car car = Car.Cast(bus);
if (car) {
    car.Fill(CarFluid.FUEL,    car.GetFluidCapacity(CarFluid.FUEL));
    car.Fill(CarFluid.OIL,     car.GetFluidCapacity(CarFluid.OIL));
    car.Fill(CarFluid.COOLANT, car.GetFluidCapacity(CarFluid.COOLANT));
    car.Fill(CarFluid.BRAKE,   car.GetFluidCapacity(CarFluid.BRAKE));
}

// Spawnear NPC eAI
DayZPlayerImplement driver = DayZPlayerImplement.Cast(GetGame().CreateObject("eAI_SurvivorM_Boris", bus.GetPosition(), false, true));

// CallLater para que el driver se siente (timing crítico)
GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.BoardDriver, 1000, false);
```

### 6.2 Boarding del NPC al asiento del conductor

```enforce
void BoardDriver() {
    if (!m_Bus || !m_Driver) return;
    Transport transport = Transport.Cast(m_Bus);
    if (!transport) return;

    m_Driver.SetPosition(m_Bus.GetPosition());
    m_Driver.StartCommand_Vehicle(transport, 0, 0, false);
    m_Driver.Notify_Transport(transport, 0);
}
```

### 6.3 Tick periódico cada 500ms

```enforce
void Tick() {
    if (!m_Bus || m_Bus.IsRuined()) {
        OnBusDestroyed();
        return;
    }

    Car bus = Car.Cast(m_Bus);
    if (!bus) return;

    if (m_Paused) {
        SetCachedInput(0, 0, 1.0);  // brake fondo
        return;
    }

    if (GetGame().GetTickTime() < m_PreRollEndTime) {
        SetCachedInput(0, 0, 1.0);  // pre-roll de 3s
        return;
    }

    if (!bus.EngineIsOn()) bus.EngineStart();

    // Avanzar waypoints, calcular control, aplicar...
    // (ver BZBusService.c completo para detalles)
}
```

---

## 7. Errores comunes y gotchas

### 7.1 Vehículo no acelera

**Causa probable**: `ShiftTo(CarGear.FIRST)` de eAI no está siendo sobrescrito.

**Verificar**:
- ¿Tu `modded class CarScript` llama a `super.OnInput(dt)` ANTES de tu lógica?
- ¿Tu lógica setea `ShiftTo(desired)` DESPUÉS de super?
- ¿El servicio tiene un valor válido en `GetDesiredGear()` (>= 2)?

### 7.2 Vehículo se va al agua / trayectoria errática

**Causa probable**: convención de signo del cross product invertida.

**Verificar**:
- ¿La fórmula es `cross = AB.z * AP.x - AB.x * AP.z`? (correcta en DayZ left-handed)
- Si está como `AB.x * AP.z - AB.z * AP.x`, está invertida

### 7.3 Vehículo no responde con Boris al volante

**Causa probable**: tu check de "hay un humano al volante" detecta a Boris como humano.

**Verificar**:
- ¿Usás `PlayerBase.Cast() && GetIdentity()`?
- `PlayerBase.Cast()` por sí solo NO discrimina eAI de player (eAI hereda PlayerBase)

### 7.4 Vehículo spawnea desnudo (sin ruedas, batería, etc.)

**Causa probable**: classnames de attachments incorrectos o case-sensitive.

**Verificar**:
- Buscar en trader files de Expansion los classnames exactos
- Expansion usa snake_case minúscula (`expansion_landrover_wheel`)
- Vanilla usa PascalCase (`CarBattery`)

### 7.5 Bus se queda parado en loop al spawnear

**Causa probable**: `ValidateSpawn` mide distancia recorrida en lugar de movimiento.

**Verificar**:
- Si el primer wp del recording tiene velocidad ~0 (operador grabó parado al inicio), la IA acelera muy lento
- Cambiar la validación a `kmh > 0.5 = OK` en vez de `distFromSpawn >= 2m`

### 7.6 Bus catapulta en curvas con vehículos livianos

**Causa probable**: AT por RPM mantiene gear bajo + recording tiene throttle alto.

**Verificar**:
- ¿Configuraste `AccelShiftThreshold` apropiado para tu vehículo (15 para liviano)?
- ¿Configuraste `SteeringScale < 1.0` para vehículos con wheelbase corto?
- ¿La grabación humana es agresiva (operador grabó a fondo)? Si sí, regrabar con técnica moderada

---

## 8. Cómo continuar el desarrollo / contribuir

El framework es **proyecto público en desarrollo**. Para contribuir:

1. **Fork del repositorio** en GitHub
2. **Validar tu cambio empíricamente** con AI logging (NUMPAD 7) — comparar
   dev_avg y %_inC antes y después del cambio
3. **Documentar el cambio** en el PAPER_NOTES.md del repositorio
4. **Pull request** con descripción del cambio + datos empíricos

Áreas donde el framework necesita más trabajo (ver sección "Roadmap" del manual):

- Respetar `targetThrottle` y `targetGear` del recording en lugar de override
- Adapter de BoatScript y Helicopter
- Multi-rutas con N buses simultáneos
- Vehicle profile extendido (STANLEY_K, MAX_BRAKE_DECEL, PARKING_PUSH_THROTTLE configurables)
- ILC loop automatizado (v2.0)
- Corredor v2 con slip angle implícito (v2.0)
- Pathfinding reactivo híbrido (v2.0)

---

## 9. Glosario rápido

- **Stanley controller**: control lateral clásico de literatura, basado en
  heading error + cross-track error atenuado por velocidad
- **Corredor**: concepto del framework, "tubo de tolerancia" alrededor del
  recording donde el bus puede estar sin que se considere fuera del trazado
- **Vehicle profile**: conjunto de campos del JSON que describen el vehículo
  específico (clase, max gear, attachments, etc.)
- **Anti-catapulta**: trigger automático de shift up por aceleración medida,
  para vehículos torquey
- **SteeringScale**: compensación por wheelbase corto, escala lineal del
  output final del Stanley
- **Recording-driven**: principio rector del framework, el comportamiento de
  la IA viene del recording humano, no de algoritmos del control
- **Hand-off**: capacidad del operador de tomar el bus durante una corrida IA,
  manejarlo manualmente, soltarlo, y que la IA retome
- **Subordination architecture**: filosofía arquitectónica del framework,
  subordinar al motor del juego y eAI en lugar de reemplazarlos

---

## 10. Instrucciones para tu asistente IA

Cuando uses este documento con un asistente IA (Claude, GPT, Gemini, etc.):

1. **Subí este archivo** al workspace del asistente
2. **Subí también los archivos `.c` del framework** (scripts/3_Game, scripts/4_World, scripts/5_Mission)
3. **Indicale al asistente**: "Vas a ayudarme a adaptar el framework eAI Vehicular
   de BrigadaZ_Transport a mi caso de uso particular. El AI knowledge pack está
   en el archivo MOD_CONTEXT_FOR_AI.md. Lee primero ese archivo, después los
   .c, y después contestá mis preguntas."
4. **Empezá con preguntas específicas**: "¿Cómo modifico el framework para que
   X?" o "¿El framework soporta Y?" o "¿Cómo calibro Z para mi vehículo?"

El asistente debería poder responder con precisión técnica respaldada en este
documento + el código fuente.

---

*Este archivo es mantenido por el equipo de BrigadaZ. Si encontrás imprecisiones,
abrí un issue en el repositorio.*
