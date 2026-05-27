class BZBusService {
    private static ref BZBusService s_Instance;

    static const string CONFIG_PATH       = "$profile:BrigadaZ_Transport/BZBusRoute.json";
    static const float  WAYPOINT_RADIUS   = 15.0;   // trigger del scan de pasajeros en paradas
    static const float  STOP_FINAL_RADIUS = 3.0;    // distancia real a la que el bus frena full en parada
    static const float  LOOKAHEAD_DIST    = 10.0;   // pure pursuit: apuntar a un punto interpolado sobre la ruta a esta distancia. Aim al PATH (no al centroide, que cortaba curvas por adentro).
    static const float  LOOKAHEAD_DIST_MIN = 5.0;   // lookahead en curvas pronunciadas (zigzag, rotonda)
    static const float  LOOKAHEAD_CURVATURE_WINDOW = 10; // wps adelante para medir curvatura local
    static const float  LOOKAHEAD_CURVATURE_LOW    = 0.3; // rad: por debajo de esto, lookahead full
    static const float  LOOKAHEAD_CURVATURE_HIGH   = 1.5; // rad: por encima, lookahead minimo
    // Corredor (rieles) - Stanley controller simplificado. Antes habia un
    // deadband absoluto: dentro del corredor target yaw = segmento, fuera =
    // lookahead. Eso producia zigzags amplios a alta velocidad por la
    // discontinuidad del input al cruzar el umbral (volantazo + inercia del
    // chasis -> sobreoscila al otro lado -> volantazo opuesto -> repeat).
    //
    // Reemplazado por Stanley simplificado (2026-05-24 sesion segunda corrida):
    //   targetYaw = segmentHeading - atan(K * lateralOffset / velocity)
    // Corregimos PROPORCIONALMENTE al offset, sin escalon. Replica el patron
    // del operador humano con teclado (a-a-a-d-d-d): correcciones leves y
    // frecuentes, no volantazos discretos. Bonus: el divisor por velocidad
    // atenua la correccion a alta velocidad (rectas) y la fortalece a baja
    // velocidad (rotonda, maniobras precisas), exactamente la asimetria que
    // observamos empiricamente en la primer corrida con corredor naive.
    //
    // CORRIDOR_HALF_WIDTH ya no es un umbral de activacion, ahora se usa solo
    // como referencia para el AI logger (clasificar samples en _inC / _outC
    // para analisis posterior).
    static const float  CORRIDOR_HALF_WIDTH   = 1.05;
    static const int    CORRIDOR_SEARCH_BACK  = 20; // wps atras
    static const int    CORRIDOR_SEARCH_FWD   = 5;  // wps adelante
    // STANLEY_K: ganancia de correccion lateral. K=1.0 (clasico de literatura)
    // dio el mejor resultado empirico (dev avg lento 0.31m, medio 0.74m,
    // rapido 1.14m). Probamos K modulado por curvatura local (1.0 recta,
    // 2.5 curva) pero EMPEORO: la metrica de curvatura cumulativa detecta
    // rotonda (donde sobre-corrige) pero NO curvas largas a alta velocidad
    // (curvas problematicas reales, donde no actua). Resultado: peor en
    // ambos extremos. Lecciones a paper:
    //   - El residual a alta velocidad es feedback honesto al operador sobre
    //     su estilo de conduccion, no bug del control. Forzarlo con K alto
    //     ocultaria el mismatch grabacion <-> capacidades del vehiculo.
    //   - Curvatura local no es la metrica correcta para detectar zonas
    //     donde se necesita mas correccion. La metrica deberia ser proxy
    //     de fuerza centrifuga (curvatura x velocidad^2), pero el atan(K/v)
    //     ya integrado en Stanley provee la atenuacion necesaria.
    static const float  STANLEY_K             = 1.0;
    static const float  STUCK_TIMEOUT     = 60.0;
    // Auto-retry del spawn: si a los SPAWN_VALIDATION_DELAY_MS ms el bus no
    // se movio al menos MIN_DIST metros del spawn point, asumimos que el
    // driver no se sento bien y reintentamos hasta MAX_SPAWN_RETRIES veces.
    // SPAWN_VALIDATION_DELAY_MS = pre-roll (3s) + tiempo real para moverse (5s).
    static const int    MAX_SPAWN_RETRIES         = 5;
    static const int    SPAWN_VALIDATION_DELAY_MS = 8000;
    static const float  SPAWN_VALIDATION_MIN_DIST = 2.0;

    // Pre-roll: tiempo desde SpawnBus hasta que la IA empieza a manejar.
    // Durante este intervalo el bus queda con brake aplicado mientras se
    // asegura el driver, motor y estabilizacion. Articulado por Sonom4n
    // 2026-05-24: "la grabacion es el momento en que Boris arranca motor",
    // separar pre-roll de playback hace el spawn robusto contra timing
    // (driver tarda en sentarse, motor en encender, etc).
    static const float  SPAWN_PREROLL_SECONDS     = 3.0;

    private EntityAI             m_Bus;
    private eAIBase              m_Driver;
    private eAIGroup             m_Group;
    private ref BZBusRouteConfig m_Config;
    private int                  m_WaypointIndex;
    private int                  m_NextStopIndex; // idx del proximo wp con isStop=true, para frenado por proximidad en DriveTowards
    private bool                 m_AtStop;
    private bool                 m_StopDecided;  // decision tomada en OnWaypointReached, aprox suave hasta STOP_FINAL_RADIUS
    private bool                 m_Reverse;
    private float                m_StuckTimer;
    private int                  m_SpawnAttempt;     // contador de intentos para auto-retry del spawn
    private vector               m_SpawnInitialPos;  // pos donde spawneó el bus, para chequear si se movió
    private ref array<BZBusStopSign> m_Signs = new array<BZBusStopSign>();

    // AI logging — graba la trayectoria del bus mientras la maneja la IA, a CSV.
    // Se usa para comparar contra la grabacion humana y encontrar zonas de
    // divergencia. Hotkey NUMPAD 7 (cliente) -> RPC -> ToggleAILogging.
    private bool   m_AILoggerActive;
    private string m_AILogPath;
    private float  m_AILogStartTime;
    private int    m_AILogSampleCount;
    private int    m_AILogNextMarker;  // se setea a 1 con NUMPAD - desde el cliente; LogAITick lo escribe y resetea

    // System Identification — experimentos para caracterizar la funcion de
    // transferencia interna de eAI (que hace internamente con nuestros inputs).
    // Cuando estos modos estan activos, el Tick() IGNORA la ruta y aplica
    // inputs programados para revelar el patron de respuesta del sistema.
    //   m_SysIDMode: 0=off, 1=step response, 2=curva de respuesta
    private int    m_SysIDMode;
    private float  m_SysIDStartTime;
    private string m_SysIDLogPath;
    private int    m_SysIDSampleCount;

    // PAUSE mode — congela el bus en su posicion actual con brake aplicado.
    // Util para setup de experimentos: NUMPAD 2 (spawn) -> NUMPAD . (pausa)
    // -> COT teleport + alinear -> NUMPAD 8/9 (experimento) -> queda pausado al
    // terminar. NUMPAD 2 vuelve a modo ruta normal.
    private bool   m_Paused;

    // Pre-roll: hasta este tickTime, la IA no maneja (brake aplicado, motor
    // encendiendo, driver acomodandose). Se setea en SpawnBus a now+3s.
    private float  m_PreRollEndTime;

    // Corredor (rieles): resultado cacheado del calculo en DriveTowards. Se
    // expone via getters para que LogAITick pueda escribir si el bus iba
    // "in_corridor" o "out_corridor" al CSV.
    private float  m_CorridorLateralOffset; // firmado, + = bus a la derecha del segmento
    private float  m_CorridorSegmentHeading; // rad, heading del segmento del recording
    private bool   m_CorridorValid;          // false si no se encontro segmento valido

    // -------------------------------------------------------------------------

    static BZBusService GetInstance() {
        if (!s_Instance)
            s_Instance = new BZBusService();
        return s_Instance;
    }

    BZBusRouteConfig GetConfig() {
        return m_Config;
    }

    // Llamado desde modded CarScript.OnInput cada frame para saber si este
    // vehiculo es nuestro bus
    bool IsBusActive(EntityAI candidate) {
        return m_Bus && candidate == m_Bus;
    }

    // Cached input que se reinyecta cada frame desde OnInput modded
    private float m_CachedThrottle;
    private float m_CachedSteering;
    private float m_CachedBrake;

    // Gear deseado por la AT - eAI mete ShiftTo(FIRST) cada frame en su OnInput,
    // nosotros lo sobreescribimos despues de super.OnInput con este valor.
    private int m_DesiredGear = 2; // 2 = CarGear.FIRST (arranque)

    void SetCachedInput(float throttle, float steering, float brake) {
        m_CachedThrottle = throttle;
        m_CachedSteering = steering;
        m_CachedBrake    = brake;
    }

    float GetCachedSteering() { return m_CachedSteering; }

    int  GetDesiredGear() { return m_DesiredGear; }
    void SetDesiredGear(int g) { m_DesiredGear = g; }

    // Max gear configurable por vehiculo desde el JSON. Default 6 (5ta).
    int GetMaxGear() {
        if (m_Config && m_Config.MaxGear > 0) return m_Config.MaxGear;
        return 6;
    }

    // Umbral de aceleracion (km/h por segundo) para shift up anti-catapulta.
    // Por encima de este valor, la AT shiftea UP para suavizar la aceleracion.
    // Default 999 (deshabilitado).
    float GetAccelShiftThreshold() {
        if (m_Config && m_Config.AccelShiftThreshold > 0) return m_Config.AccelShiftThreshold;
        return 999.0;
    }

    // Escala del steering final del Stanley. Compensa wheelbase corto que
    // produce sobre-rotacion para el mismo input nominal. Default 1.0.
    float GetSteeringScale() {
        if (m_Config && m_Config.SteeringScale > 0) return m_Config.SteeringScale;
        return 1.0;
    }

    // Aplica los inputs cacheados (llamado cada frame por modded CarScript)
    void ApplyBusInput(Car bus, float dt) {
        if (!bus) return;
        bus.SetThrottle(m_CachedThrottle);
        bus.SetSteering(m_CachedSteering);
        bus.SetBrake(m_CachedBrake);
    }

    void Init() {
        if (!GetGame().IsServer()) return;
        LoadConfig();
        if (!ValidateConfig()) return;
        SpawnStopSigns();   // una sola vez, persisten entre respawns del bus
        // NOTA: auto-spawn al startup no esta implementado en esta version.
        // Probamos varias variantes (directo, CallLater 10s) sin exito —
        // el mundo no esta listo en Init() para crear el bus consistentemente.
        // El admin del server debe spawnear el bus manualmente con NUMPAD 2
        // despues del startup. Para detenerlo: NUMPAD 1.
    }

    // Cierre manual del bus (NUMPAD 1 -> RPC -> aca). Limpia el vehiculo
    // y al driver sin respawnearlos. Util para liberar recursos durante
    // grabaciones humanas o test.
    void StopBus() {
        BZBusLog.Info("StopBus solicitado (manual)");
        CleanupEntities();
        BroadcastGlobal("Bus Costero detenido. NUMPAD 2 para reiniciar.");
    }

    // -------------------------------------------------------------------------
    // Config

    private void LoadConfig() {
        m_Config = new BZBusRouteConfig();

        EnsureProfileDir();

        if (!FileExist(CONFIG_PATH)) {
            WriteDefaultConfig();
            BZBusLog.Warn("Config generado en: " + CONFIG_PATH + " - completar waypoints y reiniciar.");
            return;
        }

        JsonFileLoader<BZBusRouteConfig>.JsonLoadFile(CONFIG_PATH, m_Config);
        BZBusLog.Info("Config cargado. Waypoints: " + m_Config.Waypoints.Count());
    }

    private void EnsureProfileDir() {
        string dir = "$profile:BrigadaZ_Transport\\";
        if (!FileExist(dir))
            MakeDirectory(dir);
    }

    private void WriteDefaultConfig() {
        BZBusRouteConfig def = new BZBusRouteConfig();
        def.RespawnDelay   = 300;
        def.AverageSpeedMS = 11.0;
        def.VehicleClass   = "ExpansionBus";
        def.DriverClass    = "eAI_SurvivorM_Boris";

        // Template con 9 waypoints alternando stop/intermedio. Coords en cero,
        // el admin completa a mano antes del primer arranque real.
        AddTemplateStop(def, "Kamenka");
        AddTemplateMid(def);
        AddTemplateStop(def, "Komarovo");
        AddTemplateMid(def);
        AddTemplateStop(def, "Balota");
        AddTemplateMid(def);
        AddTemplateStop(def, "Chernogorsk");
        AddTemplateMid(def);
        AddTemplateStop(def, "Elektrozavodsk");

        JsonFileLoader<BZBusRouteConfig>.JsonSaveFile(CONFIG_PATH, def);
    }

    private void AddTemplateStop(BZBusRouteConfig cfg, string name) {
        BZWaypoint wp = new BZWaypoint();
        wp.pos[0] = 0; wp.pos[1] = 0; wp.pos[2] = 0;
        wp.isStop = true;
        wp.name = name;
        wp.stopDuration = 7;
        wp.stopRadius = 60;
        cfg.Waypoints.Insert(wp);
    }

    private void AddTemplateMid(BZBusRouteConfig cfg) {
        BZWaypoint wp = new BZWaypoint();
        wp.pos[0] = 0; wp.pos[1] = 0; wp.pos[2] = 0;
        wp.isStop = false;
        wp.name = "";
        wp.stopDuration = 0;
        wp.stopRadius = 0;
        cfg.Waypoints.Insert(wp);
    }

    private bool ValidateConfig() {
        if (!m_Config || m_Config.Waypoints.Count() < 2) {
            BZBusLog.Warn("Ruta incompleta (< 2 waypoints), servicio no iniciado.");
            return false;
        }

        // Si todos los waypoints estan en [0,0,0] es el template sin completar
        foreach (BZWaypoint wp : m_Config.Waypoints) {
            if (!wp.IsZero())
                return true;
        }

        BZBusLog.Warn("Todos los waypoints son [0,0,0] - completar " + CONFIG_PATH + " y reiniciar.");
        return false;
    }

    // -------------------------------------------------------------------------
    // Spawn

    // Respawn manual (NUMPAD 2 desde cliente -> RPC -> aca)
    // NO recarga el JSON — el archivo se lee solo en Init() porque cargar
    // un JSON grande (5MB / 7004 wps) en runtime bloquea el thread del
    // servidor 2+ minutos. Para cambiar entre estados (BUS / LANDROVER /
    // etc) hay que cerrar el server, hacer swap_route.ps1, y reabrir.
    void RespawnBus() {
        BZBusLog.Info("RespawnBus solicitado (manual)");
        m_VehicleClassOverride = "";
        m_SpawnAttempt         = 0;
        m_Paused               = false;
        m_SysIDMode            = 0;
        CleanupEntities();
        if (!ValidateConfig()) return;
        SpawnBus();
    }

    // Respawn con vehiculo distinto (debug del problema de shift con eAI driver)
    void RespawnAs(string vehicleClass) {
        BZBusLog.Info("RespawnAs: " + vehicleClass);
        m_VehicleClassOverride = vehicleClass;
        CleanupEntities();
        if (!ValidateConfig()) return;
        SpawnBus();
    }

    private string m_VehicleClassOverride;

    private void SpawnBus() {
        m_WaypointIndex = 0;
        m_NextStopIndex = FindNextStopIndex(0);
        m_AtStop        = false;
        m_StopDecided   = false;
        m_Reverse       = false;
        m_StuckTimer    = 0;
        m_DesiredGear   = 2; // CarGear.FIRST
        m_SpawnAttempt++; // contador para auto-retry, RespawnBus lo resetea a 0 antes
        // Reset cached input para evitar residuos de la sesion anterior
        m_CachedThrottle = 0;
        m_CachedSteering = 0;
        m_CachedBrake    = 1.0; // pre-roll: brake aplicado desde el primer frame
        m_PreRollEndTime = GetGame().GetTickTime() + SPAWN_PREROLL_SECONDS;

        vector startPos = m_Config.Waypoints[0].GetVector();
        // Asegurar Y sobre el suelo (algunos vehiculos spawnean hundidos o flotando)
        float surfY = GetGame().SurfaceY(startPos[0], startPos[2]);
        startPos[1] = surfY + 0.5;
        m_SpawnInitialPos = startPos; // guardado para ValidateSpawn

        string vc = m_Config.VehicleClass;
        if (m_VehicleClassOverride != "") vc = m_VehicleClassOverride;
        // initAI=true solo para vehiculos AI (ExpansionBus, etc). Vanilla cars => false.
        bool initAI = (vc.Contains("Bus") || vc.Contains("bus"));
        m_Bus = EntityAI.Cast(GetGame().CreateObject(vc, startPos, false, initAI));
        if (!m_Bus) {
            BZBusLog.Err("No se pudo crear vehiculo: " + vc);
            return;
        }
        BZBusLog.Info("SpawnBus: surfY=" + surfY + " startPos=" + startPos.ToString());
        BZBusLog.Info("CarGear constants: NEUTRAL=" + CarGear.NEUTRAL + " REVERSE=" + CarGear.REVERSE + " FIRST=" + CarGear.FIRST + " SECOND=" + CarGear.SECOND);

        // Activar POSTSIMULATE para que EOnPostSimulate (auto transmission) se dispare
        m_Bus.SetEventMask(EntityEvent.POSTSIMULATE);

        // Bus invulnerable durante el servicio (no se debe romper en la ruta)
        m_Bus.SetAllowDamage(false);

        // Equipar el bus con ruedas/bateria/etc (algunos vehiculos no traen attachments default)
        EquipBus(m_Bus);

        // Orientar el bus hacia el siguiente waypoint para que arranque mirando la ruta
        OrientBusToNext();

        // Spawnear el driver: para vehiculos grandes (bus) offset al frente para no quedar en puerta del medio.
        // Para autos/camiones chicos: spawnea en el mismo punto que el vehiculo.
        vector driverPos = m_Bus.GetPosition();
        if (vc.Contains("Bus") || vc.Contains("bus")) {
            vector orient = m_Bus.GetOrientation();
            float yawRad = orient[0] * Math.DEG2RAD;
            vector forward = Vector(Math.Sin(yawRad), 0, Math.Cos(yawRad));
            driverPos = m_Bus.GetPosition() + forward * 2.0;
        }
        m_Driver = eAIBase.Cast(GetGame().CreateObject(m_Config.DriverClass, driverPos, false, true));
        if (!m_Driver) {
            BZBusLog.Err("No se pudo crear conductor: " + m_Config.DriverClass);
            m_Bus.Delete(); m_Bus = null;
            return;
        }

        // Driver invulnerable durante el servicio: si lo matan, el bus queda inerte
        // y la ruta se rompe hasta el proximo respawn. Mismo criterio que el bus.
        m_Driver.SetAllowDamage(false);

        m_Group = eAIGroup.CreateGroup(eAIFaction.Create("Passive"));
        m_Driver.SetGroup(m_Group);

        // Vestir a Boris de policia
        DressDriver(m_Driver);

        SetNextWaypoint();

        // Diferir el embarque: el motor necesita 1s para inicializar bus+driver
        // antes de aceptar CrewGetIn. Hacerlo inmediato suele fallar.
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.BoardDriver, 1000, false);
        // Tick a 500ms: equilibrio entre responsividad y no saturar
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(Tick, 500, true);

        // Auto-retry: chequear a los 5s si el bus se movio. Si no, asumir
        // que Boris no se sento y reintentar (hasta MAX_SPAWN_RETRIES).
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.ValidateSpawn, SPAWN_VALIDATION_DELAY_MS, false);

        BroadcastGlobal("Bus Costero BrigadaZ salio de " + m_Config.Waypoints[0].name + ". (intento " + m_SpawnAttempt + ")");
    }

    // Chequea 5s post-spawn si el bus se movió. Si no se movió (driver no
    // boardeo, o trabado en colisión, o similar), reintenta el spawn.
    void ValidateSpawn() {
        if (!m_Bus) {
            BZBusLog.Warn("ValidateSpawn: m_Bus es null, validacion cancelada");
            return;
        }

        // Si el bus esta en parada (m_AtStop), NO es spawn fallido — es el bus
        // dentro de la zona de un wp con isStop=true. Reset retry y exit.
        if (m_AtStop) {
            BZBusLog.Info("ValidateSpawn OK: bus en parada (m_AtStop=true), spawn exitoso, no retry");
            m_SpawnAttempt = 0;
            return;
        }

        // Si el bus esta en movimiento (kmh > 0.5), el driver se sento, el motor
        // respondio y la cadena del control funciona. Es spawn exitoso aunque
        // no haya alcanzado 2m fisicos. Pasa cuando el operador grabo los
        // primeros samples quieto en la terminal: los primeros wps tienen
        // targetSpeed=0, la cruise control aplica throttle bajo, el bus arranca
        // despacio. La validacion por distancia falla pero el spawn es valido.
        Car bus_v = Car.Cast(m_Bus);
        if (bus_v && bus_v.GetSpeedometerAbsolute() > 0.5) {
            BZBusLog.Info("ValidateSpawn OK: bus en movimiento (kmh=" + bus_v.GetSpeedometerAbsolute() + "), spawn exitoso, no retry");
            m_SpawnAttempt = 0;
            return;
        }

        float distFromSpawn = vector.Distance(m_Bus.GetPosition(), m_SpawnInitialPos);
        if (distFromSpawn >= SPAWN_VALIDATION_MIN_DIST) {
            BZBusLog.Info("ValidateSpawn OK: bus se movio " + distFromSpawn + "m desde spawn en intento #" + m_SpawnAttempt);
            m_SpawnAttempt = 0; // reset, todo bien
            return;
        }

        // No se movió. Si todavía hay retries disponibles, reintentar.
        if (m_SpawnAttempt >= MAX_SPAWN_RETRIES) {
            BZBusLog.Err("ValidateSpawn: agote " + MAX_SPAWN_RETRIES + " intentos, bus no arranca. Abortando auto-retry.");
            BroadcastGlobal("Bus Costero: fallo de spawn tras " + MAX_SPAWN_RETRIES + " intentos. NUMPAD 2 para reintentar manualmente.");
            m_SpawnAttempt = 0; // reset por si hace falta intentarlo manualmente despues
            return;
        }

        BZBusLog.Warn("ValidateSpawn: bus no se movio (" + distFromSpawn + "m), intento #" + m_SpawnAttempt + " de " + MAX_SPAWN_RETRIES + ". Reintentando...");
        CleanupEntities();
        SpawnBus(); // SpawnBus incrementa m_SpawnAttempt
    }

    void BoardDriver() {
        BZBusLog.Info("BoardDriver: entrando");
        if (!m_Bus || !m_Driver) {
            BZBusLog.Warn("BoardDriver: bus o driver null");
            return;
        }
        Car bus = Car.Cast(m_Bus);
        if (!bus) { BZBusLog.Warn("BoardDriver: bus no es Car"); return; }

        Transport transport = Transport.Cast(m_Bus);
        if (!transport) { BZBusLog.Warn("BoardDriver: bus no es Transport"); return; }

        // Teleportar al driver a la posicion del bus
        m_Driver.SetPosition(m_Bus.GetPosition());

        // API correcta de Expansion-AI para que un eAI use el seat de un vehiculo
        // StartCommand_Vehicle(Transport, seatIdx, seat_anim, fromUnconscious)
        m_Driver.StartCommand_Vehicle(transport, 0, 0, false);
        m_Driver.Notify_Transport(transport, 0);

        BZBusLog.Info("BoardDriver: StartCommand_Vehicle ejecutado en seat 0");
    }

    // Crea attachments del vehiculo desde la lista configurable del JSON
    // (m_Config.Attachments). Rellena fluidos siempre. Si la lista del JSON
    // esta vacia, el vehiculo spawnea desnudo y hay que equiparlo con COT.
    // Asi soporta cualquier vehiculo sin hardcodear sus partes en codigo.
    private void EquipBus(EntityAI bus) {
        if (!bus) return;
        if (m_Config && m_Config.Attachments) {
            foreach (string p : m_Config.Attachments) {
                if (p != "") bus.GetInventory().CreateAttachment(p);
            }
        }

        // Rellenar fluidos al maximo (aplica a cualquier Car)
        Car car = Car.Cast(bus);
        if (car) {
            car.Fill(CarFluid.FUEL,    car.GetFluidCapacity(CarFluid.FUEL));
            car.Fill(CarFluid.OIL,     car.GetFluidCapacity(CarFluid.OIL));
            car.Fill(CarFluid.COOLANT, car.GetFluidCapacity(CarFluid.COOLANT));
            car.Fill(CarFluid.BRAKE,   car.GetFluidCapacity(CarFluid.BRAKE));
        }
    }

    // Aplica steering + throttle + auto gearbox para que el bus vaya hacia el waypoint.
    private void DriveTowards(Car bus, BZWaypoint target) {
        vector targetPos = target.GetVector();
        vector busPos = bus.GetPosition();
        vector toTarget = targetPos - busPos;
        float dist = toTarget.Length();
        if (dist < 0.01) return;

        // Pure pursuit con lookahead ADAPTATIVO: en rectas usamos LOOKAHEAD_DIST
        // (10m) para suavidad. En curvas pronunciadas (zigzag, rotonda) reducimos
        // a LOOKAHEAD_DIST_MIN (5m) para que el bus no "corte" la curva apuntando
        // a un punto que ya esta del otro lado. La curvatura local se mide como
        // suma de cambios absolutos de heading en los proximos WINDOW waypoints.
        // El analisis de 3 tomas IA vs grabacion humana identifico 8 wp criticos
        // (zigzag wp 543/605, rotonda Cherno wp 1557-1577) donde el lookahead
        // fijo causaba desviacion de 2-5m del trazo humano.
        float adaptiveLookahead = ComputeAdaptiveLookahead();
        vector lookahead = ComputeLookahead(busPos, adaptiveLookahead);
        vector toLookahead = lookahead - busPos;

        // v1.0: el bus frena en CADA parada, sin chequear pasajeros
        bool willStop = target.isStop;

        // Heading actual del bus: usar GetOrientation() (valor controlado por SetOrientation())
        // en vez de GetDirection() — el segundo puede lagear al render/physics y devolver
        // la direccion default del modelo en los primeros ticks post-spawn, causando que
        // pure pursuit calcule dYaw=180 y haga full turn al arrancar.
        vector busOrient = bus.GetOrientation();
        float currentYaw = busOrient[0] * Math.DEG2RAD;

        // === CORREDOR (Stanley controller simplificado) ===
        // targetYaw = segmentHeading - atan(K * lateralOffset / velocity)
        // Correccion proporcional al offset (sin deadband), atenuada por velocidad.
        // Replica el patron del operador humano (a-a-a-d-d-d, tap-tap continuo).
        // En cada tick aplicamos una correccion leve proporcional al offset
        // actual; nunca un volantazo discreto. Bonus: a alta velocidad el
        // divisor por v_ms reduce la correccion automaticamente (atan tiende
        // a 0), evitando zigzag en rectas. A baja velocidad la correccion
        // crece (atan tiende al limite), util en maniobras precisas (rotonda,
        // ajuste al stop).
        //
        // Si no hay segmento valido (bus muy lateral fuera del trazado, p.ej.
        // trabado contra una estructura), fallback a pure pursuit clasico.
        ComputeCorridorInfo(busPos);
        float kmhForStanley = bus.GetSpeedometerAbsolute();
        float vForStanley = kmhForStanley / 3.6;
        if (vForStanley < 1.0) vForStanley = 1.0; // safety: evitar dividir por 0 y atan saturando
        float targetYaw;
        if (m_CorridorValid) {
            // Convencion de signo: cross product > 0 = bus a la derecha del
            // segmento. Para volver al centro hay que girar a la izquierda, que
            // en yaw DayZ (0=norte, 90=este, creciente clockwise) significa
            // RESTAR del segmentHeading. De ahi el signo negativo en atan.
            float crossCorrection = Math.Atan2(STANLEY_K * m_CorridorLateralOffset, vForStanley);
            targetYaw = m_CorridorSegmentHeading - crossCorrection;
        } else {
            // Fallback: pure pursuit clasico (apuntar al lookahead point)
            targetYaw = Math.Atan2(toLookahead[0], toLookahead[2]);
        }

        // Diferencia normalizada a -PI..PI
        float dYaw = targetYaw - currentYaw;
        while (dYaw > Math.PI)  dYaw -= 2.0 * Math.PI;
        while (dYaw < -Math.PI) dYaw += 2.0 * Math.PI;

        // Steering en -1..1, sensibilidad mas suave para no oscilar
        float steering = dYaw / (Math.PI * 0.5);
        if (steering > 1.0)  steering = 1.0;
        if (steering < -1.0) steering = -1.0;
        // Compensacion por wheelbase del vehiculo: escala el steering final
        // para evitar sobre-rotacion en vehiculos cortos (Land Rover, Hatchback).
        // Default 1.0 (bus, sin escala). Configurable por JSON via SteeringScale.
        steering = steering * GetSteeringScale();

        float throttle = 1.0;
        float brake    = 0.0;
        float kmh = bus.GetSpeedometerAbsolute();

        // Distancia al proximo wp con isStop=true. Si no hay mas (fin de linea),
        // tratamos al ultimo waypoint como "stop" para que tambien frene al final.
        float distToNextStop = 99999.0;
        if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            distToNextStop = vector.Distance(busPos, m_Config.Waypoints[m_NextStopIndex].GetVector());
        }

        // === MODO PARKING: control fisico predictivo ===
        // En vez de rampa lineal, calculamos cuanto freno necesitamos AHORA
        // para llegar a 0 km/h exactamente a STOP_FINAL_RADIUS metros del cartel,
        // dada la velocidad actual y distancia restante. Cinematica clasica:
        //   v² = u² - 2·a·s  →  a = u² / (2·s)
        // Donde u = velocidad actual (m/s), s = distancia hasta el punto de parada
        // ideal, a = deceleracion necesaria. Convertimos a fraccion de freno
        // dividiendo por MAX_BRAKE_DECEL (deceleracion del bus a freno fondo).
        //
        // Ventaja sobre la rampa: el bus llega EXACTO al cartel sin "stop fake".
        // Si va rapido, frena fuerte; si va lento, deja correr; si esta parado
        // lejos, throttle suave para acercarse.
        const float PARKING_THRESHOLD = 40.0;
        // VALOR DE REFERENCIA (good known state, validado 2026-05-22):
        // MAX_BRAKE_DECEL = 20.0 hace que el bus pare ~1m antes del cartel.
        // Aceptable para el caso de uso (jugador agazapado esperando, precision
        // sobre realismo). Si experimentos posteriores rompen el comportamiento,
        // volver a este valor. Iteracion empirica: 6 -> 9 -> 12 -> 16 -> 20.
        //
        // PRUEBA v2.7 (2026-05-22 tarde): 50.0. Con 30 el bus no se paso
        // del cartel (hipotesis: drag del juego frena mas de lo asumido,
        // o el bus real frena mas fuerte que ~7 m/s²). Subimos a 50 para
        // confirmar limite empirico.
        const float MAX_BRAKE_DECEL   = 50.0;  // m/s² a freno fondo, EXPERIMENTO LIMITE (rollback a 20.0 si rompe)
        if (distToNextStop <= PARKING_THRESHOLD) {
            float distRemaining = distToNextStop - STOP_FINAL_RADIUS;
            if (distRemaining <= 0.0) {
                // Ya en el cartel o nos pasamos: freno a fondo
                brake    = 1.0;
                throttle = 0;
            } else {
                float u_ms = kmh / 3.6;
                float aNeeded = (u_ms * u_ms) / (2.0 * distRemaining);

                // Factor altura (2026-05-23): validamos empiricamente en 3 terrenos
                // que el modelo simple a_eff = a_motor +/- g*sin(theta) predice
                // con <10% error. Aplicamos correccion por pendiente al aNeeded:
                //   - Bajada (pendiente negativa): gravedad asiste al movimiento
                //     -> bus viene con velocidad extra -> necesitamos MAS freno
                //   - Subida (pendiente positiva): gravedad opone al movimiento
                //     -> bus se frena solo -> necesitamos MENOS freno
                // pendiente = (Y_stop - Y_bus) / dist_horizontal (signed)
                // Para angulos pequeños, sin(theta) ~ tan(theta) ~ pendiente.
                float pendiente = 0;
                if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                    vector stopWpPos = m_Config.Waypoints[m_NextStopIndex].GetVector();
                    vector busHoriz = Vector(busPos[0], 0, busPos[2]);
                    vector stopHoriz = Vector(stopWpPos[0], 0, stopWpPos[2]);
                    float horizDist = vector.Distance(busHoriz, stopHoriz);
                    if (horizDist > 1.0) {
                        pendiente = (stopWpPos[1] - busPos[1]) / horizDist;
                    }
                }
                float gAssist = -9.8 * pendiente; // positivo en bajada
                aNeeded += gAssist;
                if (aNeeded < 0) aNeeded = 0; // gravedad sola alcanza para frenar

                float brakeFrac = aNeeded / MAX_BRAKE_DECEL;

                if (brakeFrac > 1.0) {
                    // No alcanza con freno fondo, igual aplicamos max
                    brake    = 1.0;
                    throttle = 0;
                } else if (brakeFrac > 0.05) {
                    // Freno modulado segun fisica
                    brake    = brakeFrac;
                    throttle = 0;
                } else if (kmh < 3.0) {
                    // Vamos muy lento dentro de zona parking: empujar para llegar
                    // al cartel. 0.2 no alcanzaba para vencer la inercia estatica
                    // del bus parado en gear FIRST, dejaba al bus a 3-4m del cartel.
                    brake    = 0;
                    throttle = 0.35;
                } else {
                    // Coast: la inercia nos lleva, no hace falta tocar nada
                    brake    = 0;
                    throttle = 0;
                }
            }
        }
        // === MODO CRUCERO: lejos del stop, seguir el perfil de velocidad ===
        else if (target.hasInputData && target.targetSpeed > 5) {
            // Usar targetSpeed del recording como referencia, pero derivar el
            // throttle del speedDelta para mantenerla. NO obedecer targetBrake
            // del recording lejos de las paradas (eso fue para frenar al stop).
            float speedDelta = kmh - target.targetSpeed;
            if (speedDelta > 10) {
                brake    = Math.Clamp(speedDelta / 30.0, 0, 0.7);
                throttle = 0;
            } else if (kmh > target.targetSpeed * 1.05) {
                throttle = 0.3;
            } else if (kmh > target.targetSpeed * 0.9) {
                throttle = 0.6;
            }
            // throttle queda en 1.0 (default) si vamos por debajo de targetSpeed
        } else if (target.targetSpeed > 5) {
            // Fallback sin recording: cruise control puro
            float speedDelta2 = kmh - target.targetSpeed;
            if (speedDelta2 > 10) {
                brake    = Math.Clamp(speedDelta2 / 30.0, 0, 0.7);
                throttle = 0;
            } else if (kmh > target.targetSpeed * 1.05) {
                throttle = 0.3;
            } else if (kmh > target.targetSpeed * 0.9) {
                throttle = 0.6;
            }
        }

        // NO forzar gear desde el waypoint. La logica AT (RPM-based en
        // modded CarScript) maneja los shifts. Si pisamos ShiftTo(targetGear)
        // aca cada 500ms, anula el ShiftUp del AT y el bus queda clavado en
        // FIRST porque los primeros waypoints fueron grabados a baja velocidad.

        // Guardar input para que modded CarScript lo reinyecte cada frame
        SetCachedInput(throttle, steering, brake);
    }

    // Calcula un punto a 'distance' metros adelante sobre la ruta a partir del
    // waypoint actual. Pure pursuit clasico: el punto interpolado esta SOBRE el
    // path, no inside como seria con un centroide. En curvas el bus sigue el
    // trazado real, no lo corta. Si la ruta no alcanza la distancia pedida
    // (fin de ruta), devuelve el ultimo waypoint disponible.
    private vector ComputeLookahead(vector startPos, float distance) {
        if (!m_Config) return startPos;
        int count = m_Config.Waypoints.Count();
        int step = 1;
        if (m_Reverse) step = -1;

        int idx = m_WaypointIndex;
        vector prev = startPos;
        float acc = 0;

        while (idx >= 0 && idx < count) {
            BZWaypoint wp = m_Config.Waypoints[idx];
            vector wpPos = wp.GetVector();
            float segLen = vector.Distance(prev, wpPos);

            if (acc + segLen >= distance) {
                // El punto buscado esta en este segmento. Interpolar linealmente.
                float remaining = distance - acc;
                float t = remaining / segLen;
                vector interp;
                interp[0] = prev[0] + (wpPos[0] - prev[0]) * t;
                interp[1] = prev[1] + (wpPos[1] - prev[1]) * t;
                interp[2] = prev[2] + (wpPos[2] - prev[2]) * t;
                return interp;
            }

            acc += segLen;
            prev = wpPos;
            idx += step;
        }

        return prev;
    }

    // Mide la curvatura local de los proximos LOOKAHEAD_CURVATURE_WINDOW
    // waypoints sumando los cambios absolutos de heading entre segmentos
    // consecutivos. Devuelve total en radianes.
    //   - Recta:                 ~0 rad
    //   - Curva moderada (45°):  ~0.78 rad
    //   - Curva fuerte (90°):    ~1.57 rad
    //   - Zigzag o rotonda:      acumula varios cambios -> 1.0-2.0+ rad
    private float ComputeLocalCurvature() {
        if (!m_Config) return 0;
        int count = m_Config.Waypoints.Count();
        int windowEnd = m_WaypointIndex + LOOKAHEAD_CURVATURE_WINDOW;
        if (windowEnd >= count) windowEnd = count - 1;
        if (windowEnd - m_WaypointIndex < 2) return 0;

        float totalDelta = 0;
        float prevHeading = 0;
        bool hasPrev = false;

        for (int i = m_WaypointIndex; i < windowEnd; i++) {
            vector pA = m_Config.Waypoints[i].GetVector();
            vector pB = m_Config.Waypoints[i + 1].GetVector();
            float dx = pB[0] - pA[0];
            float dz = pB[2] - pA[2];
            if (dx * dx + dz * dz < 0.01) continue; // segmento degenerado (bus parado)
            float h = Math.Atan2(dx, dz);

            if (hasPrev) {
                float dh = h - prevHeading;
                while (dh > Math.PI)  dh -= 2.0 * Math.PI;
                while (dh < -Math.PI) dh += 2.0 * Math.PI;
                if (dh < 0) dh = -dh;
                totalDelta += dh;
            }
            prevHeading = h;
            hasPrev = true;
        }
        return totalDelta;
    }

    // Calcula el lookahead apropiado para la curvatura local actual.
    // Interpola linealmente entre LOOKAHEAD_DIST (recta) y LOOKAHEAD_DIST_MIN
    // (curva fuerte) usando la curvatura medida.
    private float ComputeAdaptiveLookahead() {
        float curvature = ComputeLocalCurvature();
        if (curvature <= LOOKAHEAD_CURVATURE_LOW) return LOOKAHEAD_DIST;
        if (curvature >= LOOKAHEAD_CURVATURE_HIGH) return LOOKAHEAD_DIST_MIN;
        // Interpolacion lineal
        float range = LOOKAHEAD_CURVATURE_HIGH - LOOKAHEAD_CURVATURE_LOW;
        float factor = (curvature - LOOKAHEAD_CURVATURE_LOW) / range;
        return LOOKAHEAD_DIST - factor * (LOOKAHEAD_DIST - LOOKAHEAD_DIST_MIN);
    }

    // Corredor (rieles): busca el segmento del recording mas cercano al bus en
    // una ventana local [-BACK, +FWD] alrededor de m_WaypointIndex. Calcula la
    // distancia perpendicular firmada (positiva si bus a la derecha del
    // segmento) y el heading del segmento. Solo considera segmentos donde la
    // proyeccion del bus cae DENTRO del segmento (0 <= t <= 1), descartando
    // wps fuera del rango lateral. Actualiza m_Corridor* y devuelve true si
    // encontro segmento valido.
    private bool ComputeCorridorInfo(vector busPos) {
        m_CorridorValid = false;
        if (!m_Config) return false;

        int count = m_Config.Waypoints.Count();
        int from = m_WaypointIndex - CORRIDOR_SEARCH_BACK;
        if (from < 0) from = 0;
        int toIdx = m_WaypointIndex + CORRIDOR_SEARCH_FWD;
        if (toIdx >= count - 1) toIdx = count - 2;
        if (toIdx < from) return false;

        float bestPerpAbs = 99999.0;
        float bestSignedPerp = 0;
        float bestHeading = 0;
        bool found = false;

        for (int i = from; i <= toIdx; i++) {
            vector A = m_Config.Waypoints[i].GetVector();
            vector B = m_Config.Waypoints[i + 1].GetVector();
            float ABx = B[0] - A[0];
            float ABz = B[2] - A[2];
            float segLen2 = ABx * ABx + ABz * ABz;
            if (segLen2 < 0.01) continue; // segmento degenerado

            float APx = busPos[0] - A[0];
            float APz = busPos[2] - A[2];

            // t parametriza posicion del bus a lo largo del segmento (0=A, 1=B)
            float t = (APx * ABx + APz * ABz) / segLen2;
            if (t < 0.0 || t > 1.0) continue; // proyeccion fuera del segmento

            // Cross product Y component en coords DayZ (left-handed con Y arriba,
            // X este, Z norte): cross.Y = AB.z * AP.x - AB.x * AP.z
            // Positivo si bus a la derecha de AB. La version original (ABx*APz - ABz*APx)
            // estaba invertida y mando al bus al agua con Stanley K=1.0 — el bus
            // se autocorregia al lado contrario.
            float cross = ABz * APx - ABx * APz;
            float segLen = Math.Sqrt(segLen2);
            float signedPerp = cross / segLen;

            float perpAbs = signedPerp;
            if (perpAbs < 0) perpAbs = -perpAbs;

            if (perpAbs < bestPerpAbs) {
                bestPerpAbs    = perpAbs;
                bestSignedPerp = signedPerp;
                bestHeading    = Math.Atan2(ABx, ABz);
                found          = true;
            }
        }

        if (found) {
            m_CorridorLateralOffset  = bestSignedPerp;
            m_CorridorSegmentHeading = bestHeading;
            m_CorridorValid          = true;
            return true;
        }
        return false;
    }

    // Getters para LogAITick: permite escribir al CSV si el bus iba dentro o
    // fuera del corredor en ese sample.
    bool   IsCorridorValid()       { return m_CorridorValid; }
    float  GetCorridorOffset()     { return m_CorridorLateralOffset; }

    // Viste al driver con uniforme policial (si alguna clase no existe, falla en silencio)
    private void DressDriver(eAIBase driver) {
        if (!driver) return;
        array<string> outfit = {
            "PoliceJacket",
            "PolicePants",
            "PoliceCap",
            "CombatBoots_Black"
        };
        foreach (string item : outfit) {
            driver.GetInventory().CreateAttachment(item);
        }
    }

    private void OrientBusToNext() {
        if (!m_Bus || m_Config.Waypoints.Count() < 2) return;
        vector a = m_Config.Waypoints[0].GetVector();

        // Caminar por los waypoints hasta encontrar uno suficientemente lejos
        // del inicial. Si el bus estaba quieto al empezar a grabar, los primeros
        // samples estan en la misma posicion y dan direccion=0 (norte) random.
        vector b = a;
        int i = 1;
        int count = m_Config.Waypoints.Count();
        while (i < count) {
            vector candidate = m_Config.Waypoints[i].GetVector();
            if (vector.Distance(a, candidate) > 2.0) {
                b = candidate;
                break;
            }
            i++;
        }

        if (vector.Distance(a, b) < 0.01) return; // safety: ruta degenerada

        vector dir = b - a;
        float yaw = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        m_Bus.SetOrientation(Vector(yaw, 0, 0));
        BZBusLog.Info("OrientBusToNext: waypoint inicial -> waypoint " + i + " (dist=" + vector.Distance(a, b) + "m), yaw=" + yaw);
    }

    // -------------------------------------------------------------------------
    // Carteles de parada - se crean una sola vez en Init() y persisten

    private void SpawnStopSigns() {
        if (m_Signs.Count() > 0) return;

        foreach (BZWaypoint wp : m_Config.Waypoints) {
            if (!wp.isStop) continue;

            BZBusStopSign sign = BZBusStopSign.Cast(GetGame().CreateObject("BZBusStopSign", wp.GetVector(), false, true));
            if (sign) {
                sign.SetStopName(wp.name);
                m_Signs.Insert(sign);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Tick - se ejecuta cada segundo

    private void Tick() {
        if (!m_Bus) {
            BZBusLog.Warn("Tick: m_Bus es null, llamando OnBusDestroyed");
            OnBusDestroyed();
            return;
        }
        if (m_Bus.IsRuined()) {
            BZBusLog.Warn("Tick: m_Bus IsRuined=true health=" + m_Bus.GetHealth() + ", llamando OnBusDestroyed");
            OnBusDestroyed();
            return;
        }

        Car bus = Car.Cast(m_Bus);
        if (!bus) return;

        // === SYSTEM IDENTIFICATION MODE (prioridad sobre pause) ===
        // Si hay un experimento activo, ignoramos la ruta Y el pause.
        // SysID toma control absoluto.
        if (m_SysIDMode > 0) {
            SysIDTick(bus);
            return;
        }

        // === PRE-ROLL (3s post-spawn, antes de que la IA empiece a manejar) ===
        // Da tiempo a que el driver se siente, el motor encienda y el bus se
        // estabilice antes de aplicar inputs del recording. Sin esto, en
        // ticks tempranos el bus puede no responder a throttle (driver no
        // listo) y ValidateSpawn dispara retry innecesario, generando loop
        // con buses fantasma acumulados.
        if (GetGame().GetTickTime() < m_PreRollEndTime) {
            if (!bus.EngineIsOn()) bus.EngineStart();
            SetCachedInput(0, 0, 1.0);
            return;
        }

        // === PAUSE MODE ===
        // Si esta pausado y NO hay experimento, mantener el bus quieto con
        // brake aplicado. Usado para teleportar el bus a la pista sin que
        // arranque solo.
        if (m_Paused) {
            if (!bus.EngineIsOn()) bus.EngineStart();
            SetCachedInput(0, 0, 1.0);
            return;
        }

        // Si el bot se bajo del bus por cualquier motivo, volver a subirlo
        if (m_Driver && m_Driver.GetParent() != m_Bus) {
            Transport transport = Transport.Cast(m_Bus);
            if (transport) {
                m_Driver.SetPosition(m_Bus.GetPosition());
                m_Driver.StartCommand_Vehicle(transport, 0, 0, false);
                m_Driver.Notify_Transport(transport, 0);
                BZBusLog.Info("Driver se bajo, lo meto de vuelta");
            }
        }

        // Asegurar motor encendido
        if (!bus.EngineIsOn()) {
            bus.EngineStart();
            BZBusLog.Info("Tick: EngineStart, gear=" + bus.GetGear() + " kmh=" + bus.GetSpeedometerAbsolute());
        }

        // === Activacion de m_AtStop por proximidad al proximo stop ===
        // Antes la activacion requeria pasar fisicamente por el waypoint stop
        // (m_StopDecided via OnWaypointReached). Pero el while loop no avanza
        // waypoints si el bus esta parado lejos del wp actual, y el modo parking
        // puede dejar el bus paralizado a 15-20m del stop. Resultado: loop muerto.
        //
        // Nueva logica: detectamos "estamos en la parada" por distancia al wp
        // que tiene isStop=true (m_NextStopIndex), independiente del wp actual.
        // Disparamos m_AtStop por dos caminos:
        //   a) reachedRadius:    llegamos a <STOP_FINAL_RADIUS del stop
        //   b) basicallyStopped: estamos parados dentro de la zona parking
        // Tambien sincronizamos m_WaypointIndex al stop para que al salir el
        // bus continue por el segmento correcto.
        if (!m_AtStop && m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
            BZWaypoint stopWp = m_Config.Waypoints[m_NextStopIndex];
            float distToStop = vector.Distance(m_Bus.GetPosition(), stopWp.GetVector());
            float stopCheckKmh = bus.GetSpeedometerAbsolute();
            bool reachedRadius    = (distToStop <= STOP_FINAL_RADIUS);
            // Safety net: si el control predictivo falla y el bus queda parado
            // CERCA del cartel (<5m), igual activamos m_AtStop. Antes era <40m
            // pero eso enmascaraba el bug del stop-fake del control lineal viejo.
            bool basicallyStopped = (stopCheckKmh < 1.0 && distToStop <= 5.0);
            if (reachedRadius || basicallyStopped) {
                m_AtStop      = true;
                m_StopDecided = false;
                m_WaypointIndex = m_NextStopIndex;
                BZBusLog.Info("AtStop activado en " + stopWp.name + ": dist=" + distToStop.ToString() + "m vel=" + stopCheckKmh.ToString() + " km/h wpIdx sync=" + m_WaypointIndex);
                BroadcastGlobal("Bus Costero en parada " + stopWp.name + ". Sale en " + stopWp.stopDuration + " seg.");
                BroadcastDistances(stopWp.name);
                GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(OnStopFinished, stopWp.stopDuration * 1000, false);
            }
        }

        if (m_AtStop) {
            // Ajuste fino al stop (2026-05-24): cuando el bus quedo PARADO en
            // m_AtStop pero todavia esta a 0.5-3m del wp exacto del recording,
            // empujamos suave (throttle=0.25) para que se acomode al punto
            // grabado. Esto es para casos donde el operador paro en posicion
            // especifica (acceso a parada, futuro garage, etc.) y queremos
            // fidelidad punto-a-punto.
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                vector stopExactPos = m_Config.Waypoints[m_NextStopIndex].GetVector();
                float distToExact = vector.Distance(m_Bus.GetPosition(), stopExactPos);
                float stopAdjustKmh = bus.GetSpeedometerAbsolute();
                // Solo empujar si: estamos a 0.5-3m del exacto Y casi parado
                if (distToExact > 0.5 && distToExact < 3.0 && stopAdjustKmh < 5.0) {
                    SetCachedInput(0.25, 0, 0);
                    return;
                }
            }
            // Llegamos al punto exacto (o ya estamos dentro de 0.5m): brake fondo
            SetCachedInput(0, 0, 1.0);
            return;
        }

        // Avanzar el indice de waypoint TANTAS VECES como sea necesario para que
        // apunte a un waypoint que el bus aun no haya pasado. A velocidad de
        // crucero el bus pasa por 2-3 waypoints por tick (500ms a 50 km/h con
        // waypoints cada ~3.5m), entonces avanzar de a uno deja el indice atras
        // y el lookahead empieza a "mirar para atras", causando volantazos.
        int advancesThisTick = 0;
        int maxAdvances = 50; // safety
        while (advancesThisTick < maxAdvances) {
            BZWaypoint cur = CurrentWaypoint();
            if (!cur) break;
            if (vector.Distance(m_Bus.GetPosition(), cur.GetVector()) >= WAYPOINT_RADIUS) break;

            OnWaypointReached(cur);
            advancesThisTick++;

            // Si se decidio parar o ya estamos detenidos, no avanzar mas
            if (m_StopDecided || m_AtStop) break;
        }
        if (advancesThisTick > 1) {
            BZBusLog.Info("Tick: avance " + advancesThisTick + " waypoints en este tick, wpIdx ahora=" + m_WaypointIndex);
        }

        if (m_AtStop) {
            // Ajuste fino al stop (mismo bloque que arriba, por si el while loop
            // activo m_AtStop dentro de OnWaypointReached): empuje suave hasta
            // llegar al wp exacto del recording.
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                vector stopExactPos2 = m_Config.Waypoints[m_NextStopIndex].GetVector();
                float distToExact2 = vector.Distance(m_Bus.GetPosition(), stopExactPos2);
                float currentKmh2 = bus.GetSpeedometerAbsolute();
                if (distToExact2 > 0.5 && distToExact2 < 3.0 && currentKmh2 < 5.0) {
                    SetCachedInput(0.25, 0, 0);
                    return;
                }
            }
            SetCachedInput(0, 0, 1.0);
            return;
        }

        BZWaypoint target = CurrentWaypoint();
        if (!target) return;

        // Gear inferido desde velocidad actual usando la tabla de gear ranges
        // del vehiculo (data/gear_ranges.json, medido empiricamente con
        // NUMPAD 3 / NUMPAD 0). Reemplaza el uso del targetGear grabado, que
        // dependia de cuan bien hizo los cambios el operador durante grabacion.
        // Fallback: si el vehiculo no esta en la tabla, dejar que la AT
        // (CarScript EOnPostSimulate) maneje el gear basandose en RPM.
        string vehicleClass = bus.GetType();
        float currentKmh = bus.GetSpeedometerAbsolute();
        int inferred = BZGearRangeTable.GetInstance().InferGear(vehicleClass, currentKmh, m_DesiredGear);
        if (inferred > 0) {
            m_DesiredGear = inferred;
        }
        // Si inferred == -1 (vehiculo no en tabla), m_DesiredGear queda como esta
        // y la AT lo modula basandose en RPM.

        DriveTowards(bus, target);

        // Log de estado post-DriveTowards
        BZBusLog.Info("Tick: gear=" + bus.GetGear() + " kmh=" + bus.GetSpeedometerAbsolute());

        // Stuck timer: si no avanzamos nada en este tick, incrementar; si pasa
        // el timeout, forzar un advance manual (escape hatch para bug raro)
        if (advancesThisTick == 0) {
            m_StuckTimer += 1.0;
            if (m_StuckTimer >= STUCK_TIMEOUT) {
                m_StuckTimer = 0;
                BZBusLog.Warn("Bus trabado en waypoint " + m_WaypointIndex + ", forzando avance.");
                AdvanceWaypoint();
            }
        } else {
            m_StuckTimer = 0;
        }

        // AI logging: graba el estado actual del bus al CSV si esta activo.
        // Determinamos el modo actual (parking si esta cerca de un stop, crucero
        // si no) y la distancia al proximo stop para incluirlos en el log.
        if (m_AILoggerActive) {
            float distToNextStopForLog = 99999.0;
            string activeMode = "cruise";
            if (m_NextStopIndex >= 0 && m_NextStopIndex < m_Config.Waypoints.Count()) {
                distToNextStopForLog = vector.Distance(m_Bus.GetPosition(), m_Config.Waypoints[m_NextStopIndex].GetVector());
                if (distToNextStopForLog <= 40.0) activeMode = "parking";
            }
            if (m_AtStop) activeMode = "at_stop";
            // Anotar si el bus iba dentro o fuera del corredor en este sample.
            // Util para validar empiricamente que el corredor estaba absorbiendo
            // micro-desviaciones (in) vs corrigiendo activamente (out).
            if (m_CorridorValid) {
                float aOff = m_CorridorLateralOffset;
                if (aOff < 0) aOff = -aOff;
                if (aOff < CORRIDOR_HALF_WIDTH) activeMode = activeMode + "_inC";
                else                            activeMode = activeMode + "_outC";
            }
            LogAITick(activeMode, distToNextStopForLog);
        }
    }

    // -------------------------------------------------------------------------
    // Waypoints

    private void OnWaypointReached(BZWaypoint wp) {
        if (!wp.isStop) {
            AdvanceWaypoint();
            return;
        }

        // Idempotente: si ya decidimos parar, no re-procesar
        if (m_StopDecided) return;

        // v1.0: el bus para SIEMPRE en cada parada. No sabemos a priori quien
        // del bus quiere bajarse ni quien va a llegar a la parada en el momento
        // del scan. Mejor parar siempre como un colectivo real.
        //
        // v2 podria agregar un sistema "ring the bell": jugador onboard senaliza
        // en que parada se baja, jugador esperando levanta la mano. Las funciones
        // HasWaitingPlayers / HasOnboardPassengers quedan disponibles para esa
        // version, ahora no se usan.
        m_StopDecided = true;
        BZBusLog.Info("OnWaypointReached: parada " + wp.name + ", aproximacion suave hasta " + STOP_FINAL_RADIUS + "m");
    }

    private void OnStopFinished() {
        // TODO: tocar bocina (UseHorn no existe en Car vanilla; investigar EffectSound + Truck_01_Horn_Short_SoundSet)
        // Por ahora: espera adicional 1s antes de arrancar para simular pausa de bocinazo
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.OnHornFinished, 1000, false);
    }

    private void OnHornFinished() {
        m_AtStop = false;
        AdvanceWaypoint();
        m_NextStopIndex = FindNextStopIndex(m_WaypointIndex);
        BZBusLog.Info("OnHornFinished: proximo stop idx=" + m_NextStopIndex);
    }

    // Devuelve el indice del primer waypoint con isStop=true desde fromIdx (inclusive)
    // hacia adelante. Devuelve -1 si no hay mas stops (fin de linea).
    private int FindNextStopIndex(int fromIdx) {
        if (!m_Config) return -1;
        int count = m_Config.Waypoints.Count();
        for (int i = fromIdx; i < count; i++) {
            if (m_Config.Waypoints[i].isStop) return i;
        }
        return -1;
    }

    private void AdvanceWaypoint() {
        int last = m_Config.Waypoints.Count() - 1;

        if (!m_Reverse) {
            m_WaypointIndex++;
            if (m_WaypointIndex > last) {
                // Fin de linea: el bus queda parado en la ultima parada y se
                // respawnea automaticamente en wp 0 despues de RespawnDelay
                // segundos (default 60s, configurable por JSON). Esto permite
                // operacion continua del servicio sin intervencion manual.
                m_WaypointIndex = last;
                m_AtStop        = true;
                int respawnDelaySec = 60;
                if (m_Config && m_Config.RespawnDelay > 0) respawnDelaySec = m_Config.RespawnDelay;
                BroadcastGlobal("Bus Costero llego al fin de linea. Reiniciando en " + respawnDelaySec + " seg.");
                GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.RespawnBus, respawnDelaySec * 1000, false);
                return;
            }
        } else {
            m_WaypointIndex--;
            if (m_WaypointIndex < 0) {
                m_Reverse       = false;
                m_WaypointIndex = 1;
                BroadcastGlobal("Bus Costero salio de " + m_Config.Waypoints[0].name + ".");
            }
        }

        SetNextWaypoint();
    }

    private void SetNextWaypoint() {
        BZWaypoint wp = CurrentWaypoint();
        if (!wp || !m_Group) return;

        m_Group.ClearWaypoints();
        m_Group.AddWaypoint(wp.GetVector());
    }

    private BZWaypoint CurrentWaypoint() {
        if (!m_Config) return null;
        if (m_WaypointIndex < 0 || m_WaypointIndex >= m_Config.Waypoints.Count()) return null;
        return m_Config.Waypoints[m_WaypointIndex];
    }

    // -------------------------------------------------------------------------
    // Deteccion de pasajeros

    // Jugadores dentro del radio de la parada (esperando en plataforma)
    private bool HasWaitingPlayers(BZWaypoint stop) {
        array<Man> players = new array<Man>();
        GetGame().GetWorld().GetPlayerList(players);

        vector stopPos = stop.GetVector();
        float  radius  = 50.0;
        if (stop.stopRadius > 0) radius = stop.stopRadius;

        foreach (Man man : players) {
            if (vector.Distance(man.GetPosition(), stopPos) < radius)
                return true;
        }
        return false;
    }

    // Jugadores sentados dentro del bus (viajando)
    private bool HasOnboardPassengers() {
        if (!m_Bus) return false;
        Car bus = Car.Cast(m_Bus);
        if (!bus) return false;

        // Asiento 0 = conductor AI, empezamos en 1
        for (int i = 1; i < bus.CrewSize(); i++) {
            Human member = bus.CrewMember(i);
            if (member && PlayerBase.Cast(member))
                return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // RPC handler - servidor responde al REQUEST_STATUS del cliente

    void HandleStatusRequest(ParamsReadContext ctx, PlayerIdentity sender) {
        vector signPos;
        if (!ctx.Read(signPos)) {
            BZBusLog.Warn("REQUEST_STATUS: no se pudo leer posicion");
            return;
        }

        string stopName = FindStopNameByPosition(signPos);
        BZBusStopInfo info = BuildStopInfo(stopName);

        Man recipient = GetGame().GetPlayerByIdentity(sender);

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(info.stopName);
        rpc.Write(info.status);
        rpc.Write(info.etaSeconds);
        rpc.Write(info.distanceMeters);
        rpc.Write(info.upcomingStops.Count());
        foreach (string s : info.upcomingStops)
            rpc.Write(s);

        rpc.Send(recipient, BZBusRPC.RECEIVE_STATUS, true, sender);
    }

    private BZBusStopInfo BuildStopInfo(string stopName) {
        BZBusStopInfo info = new BZBusStopInfo();
        info.stopName = stopName;

        if (!m_Bus || m_Bus.IsRuined()) {
            info.status       = "fuera de servicio";
            info.etaSeconds   = -1;
            info.distanceMeters = -1;
            return info;
        }

        BZWaypoint targetStop = FindStop(stopName);
        if (!targetStop) {
            info.status = "parada desconocida";
            return info;
        }

        info.distanceMeters = EstimateRemainingDistance(targetStop);
        float speed = 11.0;
        if (m_Config.AverageSpeedMS > 0) speed = m_Config.AverageSpeedMS;
        info.etaSeconds = (int)(info.distanceMeters / speed);

        if (m_AtStop && CurrentWaypoint() == targetStop) {
            info.status = "en parada aqui";
        } else if (m_AtStop) {
            BZWaypoint cur = CurrentWaypoint();
            string curName = "";
            if (cur) curName = cur.name;
            info.status = "en parada " + curName;
        } else {
            info.status = "en camino";
        }

        // Proximas paradas en direccion actual despues del target
        int idx  = m_WaypointIndex;
        int step = 1;
        if (m_Reverse) step = -1;
        bool pastTarget = false;
        int  stopsAdded = 0;

        while (stopsAdded < 4) {
            idx += step;
            if (idx < 0 || idx >= m_Config.Waypoints.Count()) break;
            BZWaypoint wp = m_Config.Waypoints[idx];
            if (!pastTarget && wp == targetStop) { pastTarget = true; continue; }
            if (pastTarget && wp.isStop && wp.name != "") {
                info.upcomingStops.Insert(wp.name);
                stopsAdded++;
            }
        }

        return info;
    }

    private float EstimateRemainingDistance(BZWaypoint targetStop) {
        if (!m_Bus || !m_Config) return 0.0;

        float total = vector.Distance(m_Bus.GetPosition(), CurrentWaypoint().GetVector());

        int idx  = m_WaypointIndex;
        int step = 1;
        if (m_Reverse) step = -1;

        while (true) {
            int next = idx + step;
            if (next < 0 || next >= m_Config.Waypoints.Count()) break;

            BZWaypoint a = m_Config.Waypoints[idx];
            BZWaypoint b = m_Config.Waypoints[next];
            total += vector.Distance(a.GetVector(), b.GetVector());
            idx = next;

            if (b == targetStop) break;
        }

        return total;
    }

    private string FindStopNameByPosition(vector pos) {
        // Usar BZBusStops (15 paradas reales del bus_stops.json) en vez de
        // m_Config.Waypoints (que arranca en template [0,0,0] hasta que el
        // admin complete BZBusRoute.json).
        BZBusStopAnchor closest;
        float closestDist = 99999;

        BZBusStopsConfig stops = BZBusStops.GetInstance().GetConfig();
        if (stops && stops.Stops) {
            foreach (BZBusStopAnchor s : stops.Stops) {
                if (!s) continue;
                float d = vector.Distance(pos, s.GetVector());
                if (d < closestDist) {
                    closestDist = d;
                    closest = s;
                }
            }
        }

        if (closest) return closest.name;
        return "";
    }

    private BZWaypoint FindStop(string name) {
        foreach (BZWaypoint wp : m_Config.Waypoints) {
            if (wp.isStop && wp.name == name)
                return wp;
        }
        return null;
    }

    // -------------------------------------------------------------------------
    // Destruccion y respawn

    private void OnBusDestroyed() {
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(Tick);
        CleanupEntities();
        BroadcastGlobal("Bus Costero fuera de servicio. Reiniciar con NUMPAD 2.");
        // Respawn automatico desactivado: solo se dispara con NUMPAD 2 (RequestRespawn)
    }

    private void CleanupEntities() {
        // Cancelar timers para evitar multiples Ticks/BoardDriver/ValidateSpawn acumulados
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.Tick);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.BoardDriver);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.OnStopFinished);
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.ValidateSpawn);

        if (m_Driver) { m_Driver.Delete(); m_Driver = null; }
        if (m_Bus)    { m_Bus.Delete();    m_Bus    = null; }
        m_Group = null;
    }

    // -------------------------------------------------------------------------
    // AI logging — graba la trayectoria del bus a CSV mientras la IA maneja.
    // Activado por NUMPAD 7 (cliente RPC). Cada corrida genera un CSV con
    // timestamp unico. Permite comparar contra la grabacion humana del
    // PathLogger normal, identificando zonas de divergencia entre lo grabado
    // y lo reproducido por la IA.

    void ToggleAILogging() {
        if (m_AILoggerActive) {
            StopAILogging();
        } else {
            StartAILogging();
        }
    }

    // Marca un evento en el CSV del AI logger. La proxima muestra que escriba
    // LogAITick tendra is_marker=1. Usado por el operador para anotar eventos
    // visuales relevantes (rozo el extremo, frenazo no esperado, etc.).
    void MarkAIEvent() {
        if (!m_AILoggerActive) {
            BZBusLog.Warn("MarkAIEvent: AI logger no esta activo");
            BroadcastGlobal("[AI MARK] AI logger NO activo. Apreta NUMPAD 7 primero.");
            return;
        }
        m_AILogNextMarker = 1;
        BZBusLog.Info("AI MARK: proxima muestra marcada (sample " + m_AILogSampleCount + ")");
        BroadcastGlobal("[AI MARK] Evento marcado en sample " + m_AILogSampleCount + ".");
    }

    private string Pad2(int n) {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }

    private void StartAILogging() {
        string dir = "$profile:BrigadaZ_Transport_PathLogger\\";
        if (!FileExist(dir)) MakeDirectory(dir);

        int y, mo, d, h, mi, s;
        GetYearMonthDay(y, mo, d);
        GetHourMinuteSecond(h, mi, s);
        // Concatenacion directa para evitar problemas de parser con %1%2 pegados.
        // Ademas agregamos GetTime() (ms desde server start) como sufijo para
        // garantizar unicidad entre tomas en el mismo segundo - el primer intento
        // mostro que el filename quedaba igual entre tomas y sobrescribia.
        int tickMs = GetGame().GetTime();
        string ts = y.ToString() + Pad2(mo) + Pad2(d) + "_" + Pad2(h) + Pad2(mi) + Pad2(s) + "_t" + tickMs.ToString();

        m_AILogPath = dir + "ai_run_" + ts + ".csv";

        FileHandle f = OpenFile(m_AILogPath, FileMode.WRITE);
        if (!f) {
            BZBusLog.Err("AILogging: no se pudo crear " + m_AILogPath);
            return;
        }
        FPrint(f, "time_s,x,y,z,heading_deg,speed_kmh,gear,throttle,brake,steering,mode,dist_to_next_stop,next_stop_idx,wp_idx,lateral_dev_m,is_marker\n");
        CloseFile(f);

        m_AILoggerActive   = true;
        m_AILogStartTime   = GetGame().GetTickTime();
        m_AILogSampleCount = 0;
        BZBusLog.Info("AILogging START: " + m_AILogPath);
        BroadcastGlobal("[AI LOG] Iniciado: " + m_AILogPath);
    }

    private void StopAILogging() {
        m_AILoggerActive = false;
        BZBusLog.Info("AILogging STOP: " + m_AILogPath + " | " + m_AILogSampleCount + " samples");
        BroadcastGlobal("[AI LOG] Cerrado. " + m_AILogSampleCount + " muestras escritas.");
    }

    // Llamado desde Tick() al final si m_AILoggerActive. Captura el estado del
    // bus + el modo de control activo (parking/crucero) + distancia al stop.
    private void LogAITick(string mode, float distToNextStop) {
        if (!m_AILoggerActive || !m_Bus) return;

        Car bus = Car.Cast(m_Bus);
        if (!bus) return;

        vector pos = bus.GetPosition();
        vector dir = bus.GetDirection();
        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        if (heading < 0) heading += 360.0;

        float t        = GetGame().GetTickTime() - m_AILogStartTime;
        float speedKmh = bus.GetSpeedometerAbsolute();
        int   gear     = bus.GetGear();
        float throttle = m_CachedThrottle;
        float brake    = m_CachedBrake;
        float steering = m_CachedSteering;

        // Desviacion lateral del recording: distancia al wp MAS CERCANO en
        // ventana local. Antes usabamos m_WaypointIndex directo, pero ese es
        // el wp objetivo del lookahead (siempre adelante del bus por ~15m),
        // entonces la metrica daba ~16m promedio sin importar la trayectoria.
        // Bug corregido 2026-05-24: buscamos en ventana [-30, +5] el wp mas
        // cercano. Asi mide la distancia real al trazo, no al objetivo del
        // lookahead.
        float lateralDev = 99999.0;
        if (m_Config) {
            int count = m_Config.Waypoints.Count();
            int from = m_WaypointIndex - 30;
            if (from < 0) from = 0;
            int toIdx = m_WaypointIndex + 5;
            if (toIdx >= count) toIdx = count - 1;
            for (int i = from; i <= toIdx; i++) {
                vector wpPos = m_Config.Waypoints[i].GetVector();
                float dx = pos[0] - wpPos[0];
                float dz = pos[2] - wpPos[2];
                float d = Math.Sqrt(dx * dx + dz * dz);
                if (d < lateralDev) lateralDev = d;
            }
        }

        // Split de string para evitar "Formula too complex"
        string line = "" + t + "," + pos[0] + "," + pos[1] + "," + pos[2];
        line += "," + heading + "," + speedKmh;
        line += "," + gear + "," + throttle + "," + brake + "," + steering;
        line += "," + mode + "," + distToNextStop;
        line += "," + m_NextStopIndex + "," + m_WaypointIndex;
        line += "," + lateralDev + "," + m_AILogNextMarker + "\n";
        m_AILogNextMarker = 0; // consumir el marker para que solo se aplique a UNA muestra

        FileHandle f = OpenFile(m_AILogPath, FileMode.APPEND);
        if (!f) return;
        FPrint(f, line);
        CloseFile(f);

        m_AILogSampleCount++;

        // Log al RPT cada 20 muestras (~10s) con resumen para vista en vivo
        if (m_AILogSampleCount % 20 == 0) {
            BZBusLog.Info("[AI LOG] " + m_AILogSampleCount + " samples | wpIdx=" + m_WaypointIndex + " mode=" + mode + " lateral_dev=" + lateralDev.ToString() + "m kmh=" + speedKmh.ToString());
        }
    }

    // -------------------------------------------------------------------------
    // SYSTEM IDENTIFICATION — caracterizacion de la funcion de transferencia
    // interna de eAI. Aplicamos inputs programados (no del recording) y
    // grabamos la respuesta del sistema en un CSV para analisis posterior.
    //
    // Experimentos:
    //  1) Step response: throttle salta de 0 a 1 instantaneamente.
    //     Revela si eAI filtra/suaviza inputs (low-pass interno).
    //  2) Curva de respuesta: throttle escalonado 0.2/0.4/0.6/0.8/1.0
    //     cada 20s. Revela si la relacion throttle->velocidad es lineal
    //     o tiene saturacion/no-linealidad.

    // Rampa de test para experimentos System ID — generada por script.
    // Spawnea N placas inclinadas en posicion hardcoded de Vybor.
    // Pasa directo por la API de DayZ (CreateObject + SetOrientation), no por
    // el formato .dze, asi se evita el bug de pivot offset que tiene el editor.
    private ref array<Object> m_RampObjects = new array<Object>();

    void ToggleRamp() {
        if (m_RampObjects.Count() > 0) {
            ClearRamp();
        } else {
            SpawnRamp();
        }
    }

    private void SpawnRamp() {
        // Fallback automatico: probamos varios candidatos hasta dar con uno
        // que spawnee. DayZ classnames cambian entre versiones y mods, asi
        // que adivinar no funciona. Probamos vanilla, BBP, RaG, etc.
        array<string> candidates = {
            "Land_HelipadConcrete",
            "Land_HelipadCircle",
            "Land_HelipadSquare_F",
            "Land_Concrete_Plate",
            "Land_Wall_IndCnc_10",
            "Land_Wall_IndCnc_25",
            "BBP_ConcreteFloorKit",
            "BBP_Foundation_Concrete_Kit",
            "Container_Base",
            "Land_Container_1Bo",
            "Land_Misc_ConcreteHedgehog",
            "Wreck_OffroadHatchback",
            "Hatchback_02_Black",
            "WoodenLog",
            "Stone"
        };
        string plateClass = "";
        foreach (string candidate : candidates) {
            Object test = GetGame().CreateObjectEx(candidate, "1000 1000 1000", ECE_LOCAL);
            if (test) {
                GetGame().ObjectDelete(test);
                plateClass = candidate;
                BZBusLog.Info("Rampa: classname valido encontrado: " + candidate);
                break;
            }
        }
        if (plateClass == "") {
            BZBusLog.Err("Rampa: NINGUN classname de la lista funciono. Verificar mods cargados.");
            BroadcastGlobal("[RAMPA] ERROR: no se encontro classname valido. Ver RPT.");
            return;
        }

        int    plateCount    = 38;
        float  plateLength   = 8.0;
        float  pitchDeg      = 5.0;      // pendiente
        vector origin        = "4119.542 338.99 10824.290"; // extremo norte Vybor
        float  headingDeg    = -30.25;   // yaw, segun la linea central de la pista

        float headingRad = headingDeg * Math.DEG2RAD;
        float pitchTan   = Math.Tan(pitchDeg * Math.DEG2RAD);

        // Vector unitario en direccion del heading (DayZ: x=este, z=norte)
        vector forward = Vector(Math.Sin(headingRad), 0, Math.Cos(headingRad));

        int spawned = 0;
        for (int i = 0; i < plateCount; i++) {
            vector pos;
            pos[0] = origin[0] + forward[0] * (i * plateLength);
            pos[1] = origin[1] + (i * plateLength * pitchTan);
            pos[2] = origin[2] + forward[2] * (i * plateLength);

            Object plate = GetGame().CreateObjectEx(plateClass, pos, ECE_PLACE_ON_SURFACE | ECE_LOCAL);
            if (plate) {
                plate.SetOrientation(Vector(headingDeg, pitchDeg, 0));
                plate.SetPosition(pos); // re-set despues de orientation para evitar snap
                m_RampObjects.Insert(plate);
                spawned++;
            }
        }

        BZBusLog.Info("Rampa generada: " + spawned + "/" + plateCount + " objetos. Origen: " + origin.ToString() + " heading=" + headingDeg + " pitch=" + pitchDeg);
        BroadcastGlobal("[RAMPA] Generada (" + spawned + " placas) en Vybor.");
    }

    private void ClearRamp() {
        int cleared = 0;
        foreach (Object o : m_RampObjects) {
            if (o) {
                GetGame().ObjectDelete(o);
                cleared++;
            }
        }
        m_RampObjects.Clear();
        BZBusLog.Info("Rampa borrada: " + cleared + " objetos");
        BroadcastGlobal("[RAMPA] Borrada (" + cleared + " placas).");
    }

    void TogglePause() {
        m_Paused = !m_Paused;
        if (m_Paused) {
            BZBusLog.Info("Bus PAUSADO (NUMPAD .): listo para teleport/setup experimento.");
            BroadcastGlobal("[BUS] PAUSADO. Listo para teleport.");
            // Reset cached input para evitar residuos
            SetCachedInput(0, 0, 1.0);
        } else {
            BZBusLog.Info("Bus REANUDADO (NUMPAD .): ruta normal activa.");
            BroadcastGlobal("[BUS] REANUDADO. Volviendo a ruta normal.");
        }
    }

    void ToggleSysIDStep() {
        if (m_SysIDMode == 1) {
            StopSysID();
        } else {
            StartSysID(1, "step");
        }
    }

    void ToggleSysIDCurve() {
        if (m_SysIDMode == 2) {
            StopSysID();
        } else {
            StartSysID(2, "curve");
        }
    }

    private void StartSysID(int mode, string label) {
        m_Paused = false; // SysID toma control, ignora pause si estaba activo
        string dir = "$profile:BrigadaZ_Transport_PathLogger\\";
        if (!FileExist(dir)) MakeDirectory(dir);

        int tickMs = GetGame().GetTime();
        m_SysIDLogPath = dir + "sysid_" + label + "_t" + tickMs.ToString() + ".csv";

        FileHandle f = OpenFile(m_SysIDLogPath, FileMode.WRITE);
        if (!f) {
            BZBusLog.Err("SysID: no se pudo crear " + m_SysIDLogPath);
            return;
        }
        FPrint(f, "time_s,phase,target_throttle,actual_throttle,actual_brake,speed_kmh,rpm,gear,x,y,z,heading_deg\n");
        CloseFile(f);

        m_SysIDMode        = mode;
        m_SysIDStartTime   = GetGame().GetTickTime();
        m_SysIDSampleCount = 0;
        BZBusLog.Info("SysID START mode=" + mode + " (" + label + "): " + m_SysIDLogPath);
        BroadcastGlobal("[SYS-ID " + label + "] Iniciado. Mantene el bus en linea recta.");
    }

    private void StopSysID() {
        BZBusLog.Info("SysID STOP: " + m_SysIDLogPath + " | " + m_SysIDSampleCount + " samples");
        BroadcastGlobal("[SYS-ID] Cerrado. " + m_SysIDSampleCount + " muestras. Bus PAUSADO.");
        m_SysIDMode = 0;
        // Al terminar el experimento, dejar el bus pausado para evitar que
        // arranque solo de vuelta a la ruta original. Para reanudar:
        // NUMPAD . (toggle pause off) o NUMPAD 2 (respawn fresh).
        m_Paused = true;
        SetCachedInput(0, 0, 1.0);
    }

    // Llamado desde Tick() cuando hay experimento activo. Ignora la ruta y
    // aplica inputs programados segun el experimento y el tiempo transcurrido.
    private void SysIDTick(Car bus) {
        if (!bus.EngineIsOn()) bus.EngineStart();

        float t = GetGame().GetTickTime() - m_SysIDStartTime;
        float targetThrottle = 0;
        string phase = "idle";

        float targetBrake = 0;
        if (m_SysIDMode == 1) {
            // Step response: 3s frenando full (asegura bus parado) + 10s step a 1.0
            // Total 13s. Menor distancia recorrida = menos chance de chocar en ruta.
            if (t < 3.0)       { targetThrottle = 0;   targetBrake = 1.0; phase = "brake"; }
            else if (t < 13.0) { targetThrottle = 1.0; targetBrake = 0;   phase = "step"; }
            else               { StopSysID(); return; }
        } else if (m_SysIDMode == 2) {
            // Curva de respuesta: throttle escalonado 0.2 -> 0.4 -> 0.6 -> 0.8 -> 1.0
            // Cada paso 20s. Total 100s.
            if (t < 1.0)        { targetThrottle = 0;   phase = "pre"; }
            else if (t < 21.0)  { targetThrottle = 0.2; phase = "0.2"; }
            else if (t < 41.0)  { targetThrottle = 0.4; phase = "0.4"; }
            else if (t < 61.0)  { targetThrottle = 0.6; phase = "0.6"; }
            else if (t < 81.0)  { targetThrottle = 0.8; phase = "0.8"; }
            else if (t < 101.0) { targetThrottle = 1.0; phase = "1.0"; }
            else                { StopSysID(); return; }
        }

        // Aplicar input (steering=0 para mantener recta)
        SetCachedInput(targetThrottle, 0, targetBrake);

        // Loggear la respuesta del sistema
        vector pos = bus.GetPosition();
        vector dir = bus.GetDirection();
        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        if (heading < 0) heading += 360.0;

        float speedKmh = bus.GetSpeedometerAbsolute();
        float rpm      = bus.EngineGetRPM();
        int   gear     = bus.GetGear();
        float actualThrottle = m_CachedThrottle;
        float actualBrake    = m_CachedBrake;

        string line = "" + t + "," + phase + "," + targetThrottle;
        line += "," + actualThrottle + "," + actualBrake;
        line += "," + speedKmh + "," + rpm + "," + gear;
        line += "," + pos[0] + "," + pos[1] + "," + pos[2];
        line += "," + heading + "\n";

        FileHandle f = OpenFile(m_SysIDLogPath, FileMode.APPEND);
        if (f) { FPrint(f, line); CloseFile(f); }
        m_SysIDSampleCount++;
    }

    // -------------------------------------------------------------------------
    // Notificaciones broadcast

    private void BroadcastDistances(string stopName) {
        array<Man> players = new array<Man>();
        GetGame().GetWorld().GetPlayerList(players);

        vector busPos = m_Bus.GetPosition();

        foreach (Man man : players) {
            PlayerBase player = PlayerBase.Cast(man);
            if (!player || !player.GetIdentity()) continue;

            float  dist = vector.Distance(busPos, player.GetPosition());
            string msg  = FormatDistance(dist, stopName);
            ExpansionNotification("[Bus Costero]", msg).Create(player.GetIdentity());
        }
    }

    private string FormatDistance(float dist, string stopName) {
        if (dist < 50)
            return "Estas en la parada " + stopName + ".";
        if (dist < 1000)
            return "Bus en " + stopName + " - estas a " + Math.Round(dist) + " m.";

        // Rounding manual a 1 decimal (Enforce no soporta %.1f en string.Format)
        float km     = dist / 1000.0;
        int   whole  = (int)km;
        int   tenths = (int)((km - whole) * 10);
        if (tenths < 0) tenths = -tenths;
        return "Bus en " + stopName + " - estas a " + whole + "." + tenths + " km.";
    }

    private void BroadcastGlobal(string msg) {
        BZBusLog.Info("[GLOBAL] " + msg);
        // TODO: chat real a todos los clientes via RPC propio
    }
}
