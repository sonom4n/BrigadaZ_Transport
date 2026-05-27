# BrigadaZ_Transport

**Framework de eAI Vehicular para DayZ Standalone**
Un patrón técnico reusable para que cualquier vehículo del juego pueda ser
manejado autónomamente por un NPC eAI, siguiendo una ruta grabada por un
operador humano.

> Proyecto público en desarrollo activo. v1.0 funcional y validada empíricamente
> con un servicio de bus de 24.92 km en Chernarus + generalización a Land Rover
> Defender. Las siguientes versiones agregan adapters de boats/helicópteros,
> multi-rutas, ILC automatizado, y más.

---

## ¿Qué problema resuelve?

DayZ Standalone permite que los jugadores manejen vehículos, pero los NPCs no
tienen capacidad funcional de conducir. Esto excluye categorías enteras de
gameplay (transporte público, convoyes, patrullas motorizadas, misiones con
NPCs vehiculares, logística automatizada).

Este framework provee la **infraestructura técnica completa** para implementar
esa capacidad: el NPC realmente conduce el vehículo a través del sistema
vehicular nativo del juego (aplicando inputs reales de volante, acelerador,
freno), no por scripts forzados de teleport. El resultado se ve y se siente
como un vehículo real manejado por una persona.

---

## Scorecard de validación empírica

**Bus canónico (servicio de transporte costero de Chernarus)**:

```
Distancia:         24.92 km
Duración:          31.3 min
Paradas:           14 (todas activadas correctamente)
Llegada a terminal: sí
dev lateral avg:   1.00m
% dentro corredor: 94.5%
```

**Generalización a Land Rover Defender (Capítulo 3 del paper)**:

Mismo framework, vehículo radicalmente distinto. Cambios al código del control:
**cero**. Cambios al vehicle profile (JSON): 5 campos (clase, max gear,
attachments, anti-catapulta threshold, steering scale).

Validación del principio: el framework cubre cualquier `CarScript` sin
modificación del control, solo configuración por JSON.

---

## Cómo empezar

### Si sos un jugador de DayZ

Suscribite al mod en Steam Workshop (link cuando esté publicado). Cuando el
server lo cargue, vas a ver buses circulando por las rutas configuradas.
**Subite (es F cerca de la parada para abrir la UI)** y dejá que te lleve.

### Si sos un admin de server

1. Suscribite al mod desde Workshop o descargá el código de este repo
2. Buildeá el PBO con tu key privada
3. Agregá `@BrigadaZ_Transport` a tus mods activos
4. **Comandos admin (NUMPAD)**:
   - `NUMPAD 2` — iniciar / respawnear el bus (Boris arranca desde Kamenka)
   - `NUMPAD 1` — detener y limpiar el bus (sin respawnear)
   - `NUMPAD .` — pausar / reanudar el bus en su posición actual
5. **Loop automático**: cuando el bus llega a la terminal, se respawnea
   automáticamente en Kamenka después de `RespawnDelay` segundos (configurable
   en el JSON, default 60s). No requiere intervención.

### Si sos un modder

1. Cloná este repo
2. Leé el [paper público](PAPER_PUBLICO.md) — describe la arquitectura completa
3. Leé el [AI knowledge pack](MOD_CONTEXT_FOR_AI.md) — subilo a tu asistente IA
4. Adaptá el framework a tu caso de uso siguiendo las instrucciones del paper
   (Capítulo 4 para integrar un vehículo nuevo, Capítulo 5 para modificar el
   control, etc.)

---

## Documentación

| Archivo | Para quién | Qué contiene |
|---|---|---|
| [PAPER_PUBLICO.md](PAPER_PUBLICO.md) / [.pdf](PAPER_PUBLICO.pdf) | Modders, investigadores | Paper técnico completo: arquitectura, control vehicular, casos de uso, límites del framework |
| [MANUAL_eAI_VEHICLES.md](MANUAL_eAI_VEHICLES.md) / [.pdf](MANUAL_eAI_VEHICLES.pdf) | Modders apurados | Versión esquemática del paper, 511 líneas |
| [MOD_CONTEXT_FOR_AI.md](MOD_CONTEXT_FOR_AI.md) | Asistentes IA | Knowledge pack para subir al workspace de Claude/GPT/Gemini |
| [PAPER_NOTES.md](PAPER_NOTES.md) | Curiosos / contributors | Notas internas de desarrollo (con citas del operador y observaciones de proceso) |

---

## Arquitectura en una imagen

```
PathLogger (cliente)
  graba CSV del operador humano
       │
       ▼
csv_to_route.ps1 (tool)
  CSV → JSON con vehicle profile + waypoints
       │
       ▼
BZBusService (server)
  spawnea vehículo + driver al startup
  Tick cada 500ms: Stanley controller, control predictivo de freno
       │
       ▼
CarScript modded (server)
  intercepta OnInput de eAI
  aplica cached inputs del service
  AT por RPM + anti-catapulta por aceleración medida
```

**Principio rector**: subordination architecture. Dejar que eAI y el motor del
juego hagan su trabajo (animaciones, audio, físicas), interceptar únicamente
los outputs específicos donde el comportamiento del mod base es incorrecto.

---

## Componentes principales

- `scripts/3_Game/`: helpers compartidos, RPC enum, common types
- `scripts/4_World/`: BZBusService (central), CarScript modded, BZWaypoint/BZBusRouteConfig (JSON), BZPathLogService (grabación humana)
- `scripts/5_Mission/`: MissionGameplay modded (hotkeys cliente), MissionServer modded (Init del service), BZBusUI (interfaz pasajeros)
- `tools/csv_to_route.ps1`: convierte recording humano (CSV) a JSON de ruta
- `tools/merge_routes.ps1`: combina múltiples CSVs en una ruta unificada (para grabaciones incrementales)
- `tools/swap_route.ps1`: switchea entre estados/vehículos preservados (BUS_CANONICO / LANDROVER / etc.)
- `data/bus_stops.json`: coordenadas y radios de las paradas conocidas (mapeo automático en csv_to_route.ps1)
- `data/gear_ranges.json`: rangos de RPM por marcha (calibrado para el bus)
- `data/wrecks_cleanup.json`: anchors para el sweep de wrecks al startup

---

## Dependencias

### Requeridas

- **DayZ-Expansion-AI** (eAI): provee la capacidad base de NPCs vehiculares
- **DayZ-Expansion-Core**: dependencia de Expansion-AI
- **DayZ-Expansion-Vehicles**: para usar `ExpansionBus`, `Expansion_Landrover`, etc.

- **@Brutalist Bus Stops** (Buddy/docbuddy): modelos físicos de las paradas en el mundo. El mod los referencia por classname; si el mod no está cargado, la mecánica del servicio (detección de proximidad por coordenadas + UI) sigue funcionando, pero los carteles físicos no aparecen visualmente

### Opcionales

- **CommunityOnlineTools (COT)**: para spawn manual y debugging
- **Mods de vehículos del Workshop**: cualquier mod que extienda `CarScript` (la mayoría) es compatible automáticamente para validar la generalización del framework

---

## Estado de desarrollo

**v1.0 — Funcional y publicado** (estado actual):

- ✅ Breakthrough técnico: override del `ShiftTo(FIRST)` de eAI
- ✅ Herencia automática validada (bus + Land Rover)
- ✅ Stanley controller con corredor + control predictivo de freno
- ✅ Anti-catapulta por aceleración medida (vehículos torquey)
- ✅ Steering scale por wheelbase
- ✅ Vehicle profile completo configurable por JSON
- ✅ Sistema de estados (swap_route.ps1)
- ✅ Hand-off operador↔IA durante una corrida
- ✅ Auto-spawn al startup + auto-respawn al fin de línea
- ✅ Paper técnico documentado (12 capítulos)
- ✅ AI knowledge pack para asistencia con IA

**v1.x — En desarrollo** (siguientes publicaciones):

- ⏳ Respetar `targetThrottle` / `targetGear` del recording (eliminar overrides silenciosos)
- ⏳ Multi-rutas con N buses simultáneos
- ⏳ Adapter de BoatScript (transporte marítimo)
- ⏳ Fix de invulnerabilidad del driver
- ⏳ Vehicle profile extendido (STANLEY_K, MAX_BRAKE_DECEL configurables)
- ⏳ Grabación de vuelta del bus (bidireccionalidad validada en producción)

**v2.0 — Roadmap**:

- 🔮 ILC (Iterative Learning Control) automatizado
- 🔮 Corredor v2 con slip angle implícito
- 🔮 Pathfinding reactivo híbrido
- 🔮 Micro-tuning de frecuencias (samples 100 Hz)
- 🔮 Adapter de Helicopter

---

## Licencia

Libre uso, modificación, repack y redistribución. Sin atribución obligatoria
(aunque siempre se agradece). Los componentes de terceros referenciados como
dependencias (@Brutalist Bus Stops, @DayZ-Expansion bundle) mantienen sus
propias condiciones.

Si querés colaborar o reportar problemas, abrí un issue en este repo.

---

## ☕ Apoyar el proyecto

Si este framework te sirve y querés contribuir a que sigamos desarrollándolo
(adapters de boat/heli, ILC automatizado, multi-rutas, micro-tuning de
frecuencias, etc.), podés dejar un aporte voluntario:

**[paypal.me/Sonom4n](https://paypal.me/Sonom4n)**

El framework es **libre con o sin donaciones**. Cualquier aporte va a tiempo
de desarrollo de futuras versiones del framework y a la investigación de
nuevos patrones técnicos para la comunidad de modding de DayZ.

---

## Créditos

**Desarrollado por**: Sonom4n e Hiperhipo10 (BrigadaZ PVE Server)

**Asistente IA**: Claude Opus 4.7 (Anthropic), como pair-programmer durante el
desarrollo. Ver Apéndice A del paper para el disclosure completo del uso de IA.

**Modelos de paradas**: @Brutalist Bus Stops por Buddy (docbuddy), usado como
dependencia (el mod se carga por separado en el server, no está repackeado en
BrigadaZ_Transport).

**Vehículos de validación**: durante el desarrollo se probó la generalización
con vehículos de mods del Workshop (autos deportivos, off-road, camiones)
para validar que la herencia automática de `CarScript` cubre el 95% de los
car mods de la comunidad. Mismo framework, sin cambios al código del control.

**Land Rover Defender** del bundle @DayZ-Expansion-Vehicles.

---

*"Mejor que un mod particular es un patrón que muchos adoptan, mejoran y
reinterpretan para casos que nosotros nunca imaginamos."*
