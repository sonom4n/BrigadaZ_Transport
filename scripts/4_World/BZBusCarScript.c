// ============================================================================
//  CarScript modded - automatic transmission + input forwarding para el bus
//
//  Tercer culpable identificado: eAI mete ShiftTo(CarGear.FIRST) cada frame
//  en su propio modded CarScript.OnInput (entities/carscript.c linea 111).
//  Si solo llamamos ShiftUp desde aca, eAI lo borra al siguiente frame.
//
//  Solucion: m_DesiredGear vive en BZBusService. La AT (RPM-based) lo
//  modifica. Nuestro OnInput corre DESPUES de super.OnInput (que ejecuta el
//  ShiftTo(FIRST) de eAI) y reinmediatamente sobreescribe con ShiftTo(desired).
//  Misma estrategia que ya usamos para throttle/steering.
// ============================================================================

modded class CarScript {

    const int BZBUS_SHIFTDOWN_TRESHOLD = 2000;
    const int BZBUS_SHIFT_DELAY_MS     = 800;
    const float BZBUS_SHIFT_UP_FACTOR  = 0.95;  // Subido de 0.75 -> 0.95 (2026-05-23): experimento System ID revelo que shift temprano a gear 5 dejaba el motor sin torque. Con 0.95 cada gear se exprime hasta cerca del redline.
    // BZBUS_MAX_GEAR ahora se lee desde el JSON (BZBusRouteConfig.MaxGear) via
    // srv.GetMaxGear(). Bus=6 (5ta), Land Rover Expansion=7 (6ta), etc.

    bool  m_BZBus_isShifting;
    int   m_BZBus_LogCounter;
    float m_BZBus_LastKmh;       // para medir aceleracion entre frames
    float m_BZBus_LastSampleTime; // tickTime del ultimo sample de kmh

    override void OnInput(float dt) {
        super.OnInput(dt);

        if (!GetGame().IsServer()) return;

        BZBusService srv = BZBusService.GetInstance();
        if (!srv || !srv.IsBusActive(this)) return;

        // Si el conductor en seat 0 es un PLAYER REAL (no eAI), el service se
        // hace a un lado: no aplicar cached input, dejar al player manejar
        // libremente. Util para "tomar el bus" y continuar una grabacion
        // humana desde donde quedo, sin matar el service ni regenerar el bus.
        //
        // IMPORTANTE: en DayZ, eAI hereda de PlayerBase, asi que PlayerBase.Cast
        // tambien devuelve true para Boris. La forma correcta de diferenciar
        // player real de eAI es por GetIdentity() — los players reales tienen
        // PlayerIdentity, los eAI no. Sin este check, el service "se hacia a
        // un lado" cuando Boris estaba al volante y el bus quedaba sin inputs.
        Human driver = CrewMember(0);
        if (driver) {
            PlayerBase realPlayer = PlayerBase.Cast(driver);
            if (realPlayer && realPlayer.GetIdentity()) {
                return;
            }
        }

        // Aplicar throttle/steering/brake cacheado (sobreescribe lo que eAI puso)
        srv.ApplyBusInput(this, dt);

        // Sobreescribir el gear: eAI metio FIRST en super.OnInput, ponemos el deseado
        int desired = srv.GetDesiredGear();
        if (GetGear() != desired) {
            ShiftTo(desired);
        }
    }

    override void EOnPostSimulate(IEntity other, float timeSlice) {
        super.EOnPostSimulate(other, timeSlice);

        BZBusService srv = BZBusService.GetInstance();
        if (!srv || !srv.IsBusActive(this)) return;

        if (!EngineIsOn() || m_BZBus_isShifting) return;

        int gear = GetGear();
        int desired = srv.GetDesiredGear();
        float rpm = EngineGetRPM();
        float redline = EngineGetRPMRedline();

        m_BZBus_LogCounter++;
        if (m_BZBus_LogCounter >= 60) {
            m_BZBus_LogCounter = 0;
            BZBusLog.Info("AT: gear=" + gear + " desired=" + desired + " rpm=" + rpm + " redline=" + redline);
        }

        // No shiftear si todavia estamos en REVERSE (0) o NEUTRAL (1)
        if (gear < 2) return;

        int maxGear = srv.GetMaxGear();

        // Medir aceleracion instantanea (km/h por segundo) para trigger
        // anti-catapulta. Si la aceleracion supera el umbral del vehicle
        // profile, shiftear UP para reducir torque a las ruedas.
        float kmhNow = GetSpeedometerAbsolute();
        float tNow = GetGame().GetTickTime();
        float accelKmhPerSec = 0;
        if (m_BZBus_LastSampleTime > 0) {
            float dt = tNow - m_BZBus_LastSampleTime;
            if (dt > 0.01) {
                accelKmhPerSec = (kmhNow - m_BZBus_LastKmh) / dt;
            }
        }
        m_BZBus_LastKmh = kmhNow;
        m_BZBus_LastSampleTime = tNow;

        float accelThreshold = srv.GetAccelShiftThreshold();

        if (rpm >= redline * BZBUS_SHIFT_UP_FACTOR && desired < maxGear) {
            srv.SetDesiredGear(desired + 1);
            BZBusLog.Info("AT desired ++ (RPM): " + desired + " -> " + (desired + 1) + " (rpm=" + rpm + ")");
            BZBus_LockShift();
        } else if (accelKmhPerSec > accelThreshold && desired < maxGear && gear >= 2 && kmhNow > 5) {
            // Anti-catapulta: aceleracion excesiva, shift up para suavizar
            srv.SetDesiredGear(desired + 1);
            BZBusLog.Info("AT desired ++ (ANTI-CATAPULTA): " + desired + " -> " + (desired + 1) + " (accel=" + accelKmhPerSec + " km/h/s > umbral=" + accelThreshold + ")");
            BZBus_LockShift();
        } else if (rpm <= BZBUS_SHIFTDOWN_TRESHOLD && desired > 2) {
            srv.SetDesiredGear(desired - 1);
            BZBusLog.Info("AT desired --: " + desired + " -> " + (desired - 1) + " (rpm=" + rpm + ")");
            BZBus_LockShift();
        }
    }

    void BZBus_LockShift() {
        m_BZBus_isShifting = true;
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(BZBus_DoneShifting, BZBUS_SHIFT_DELAY_MS);
    }

    void BZBus_DoneShifting() {
        m_BZBus_isShifting = false;
    }
}
