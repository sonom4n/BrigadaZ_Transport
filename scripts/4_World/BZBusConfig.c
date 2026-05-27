class BZWaypoint {
    float pos[3];
    bool  isStop;
    string name;
    int   stopDuration;
    float stopRadius;       // radio en metros para detectar jugadores esperando
    float targetSpeed;      // km/h grabados en este tramo (referencia para throttle)
    int   targetGear;       // gear grabado en este tramo (referencia para ShiftTo)
    float targetThrottle;   // throttle 0..1 grabado (cuando hasInputData=true)
    float targetBrake;      // brake 0..1 grabado (cuando hasInputData=true)
    bool  hasInputData;     // true si el waypoint viene del PathLogger nuevo (con throttle/brake reales). Si false, se usa logica derivada.

    vector GetVector() {
        return Vector(pos[0], pos[1], pos[2]);
    }

    bool IsZero() {
        return pos[0] == 0 && pos[1] == 0 && pos[2] == 0;
    }
}

class BZBusRouteConfig {
    int    RespawnDelay   = 300;
    float  AverageSpeedMS = 11.0;    // ~40 km/h en m/s, usado para calcular ETA
    string VehicleClass   = "ExpansionBus";       // override si el bus no esta registrado
    string DriverClass    = "eAI_SurvivorM_Boris";
    // Max gear que la AT puede shiftear arriba. En convencion CarGear:
    // FIRST=2, SECOND=3, THIRD=4, FOURTH=5, FIFTH=6, SIXTH=7.
    // Bus = 6 (5ta), Land Rover Expansion = 7 (6ta), Hatchback = 6, V3S = 7, etc.
    int    MaxGear        = 6;
    // Attachments a aplicar al spawnear el vehiculo. Lista de classnames de
    // partes (ruedas, bateria, bujias, etc). Cada vehiculo tiene su propia
    // lista — bus usa ExpansionBusWheel/Double, Land Rover usa otros. Si la
    // lista esta vacia, el vehiculo spawnea desnudo y hay que equiparlo con
    // COT manualmente. Para vehiculos nuevos: empezar con lista vacia, probar
    // classnames de attachments empiricamente, llenar el JSON cuando funcione.
    ref array<string> Attachments = new array<string>();
    // Anti-catapulta: umbral de aceleracion (km/h por segundo) por encima
    // del cual la AT shiftea UP para reducir torque a las ruedas y suavizar.
    // Replica la tecnica humana "subir gear + pisar fuerte = manejo suave"
    // sin copiar literalmente el targetGear del recording (que bugueo antes).
    // Bus pesado: 999 (deshabilitado, no catapulta). Land Rover liviano: 15
    // (~4 m/s², a partir de ahi shift up). Hatchback futuro: 10 (~2.8 m/s²).
    float AccelShiftThreshold = 999.0;
    // Sensibilidad del steering. Escala lineal aplicada al output final del
    // Stanley. Para vehiculos con wheelbase corto (yaw rate alto por el
    // mismo input nominal) bajar este valor evita sobre-rotacion.
    // Bus wheelbase ~5-6m: 1.0 (default, sin escala). Land Rover ~2.7m: 0.5.
    // Hatchback ~2.5m: 0.45. V3S ~4m: 0.7.
    float SteeringScale = 1.0;
    ref array<ref BZWaypoint> Waypoints = new array<ref BZWaypoint>();
}
