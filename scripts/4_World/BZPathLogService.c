// ============================================================================
//  BZPathLogService - graba la posicion del jugador cada SAMPLE_INTERVAL.
//  Singleton client-side. Se llama desde el modded MissionGameplay (NUMPAD 4/5/6).
// ============================================================================

class BZPathLogService {
    private static ref BZPathLogService s_Instance;

    static const float SAMPLE_INTERVAL_MS  = 250;     // 0.25 s (4 samples/s, mas precisa en curvas)
    static const float FEEDBACK_INTERVAL_S = 10.0;

    private int    m_State;
    private string m_FilePath;
    private float  m_StartTickTime;
    private int    m_SampleCount;
    private int    m_StopMarkCount;
    private float  m_TotalDistance;
    private vector m_LastPos;
    private bool   m_HasLastPos;
    private float  m_LastFeedbackTime;

    // -------------------------------------------------------------------------

    static BZPathLogService GetInstance() {
        if (!s_Instance) s_Instance = new BZPathLogService();
        return s_Instance;
    }

    void BZPathLogService() {
        m_State = BZPathLogState.OFF;
    }

    int GetState() { return m_State; }

    // -------------------------------------------------------------------------
    //  Acciones publicas (las invoca el OnKeyPress)

    void ToggleStartStop() {
        if (m_State == BZPathLogState.OFF) {
            Start();
        } else {
            Stop();
        }
    }

    void TogglePause() {
        if (m_State == BZPathLogState.OFF) {
            BigNotif("Path Logger apagado. NUMPAD 5 para iniciar.");
            return;
        }

        if (m_State == BZPathLogState.RECORDING) {
            m_State = BZPathLogState.PAUSED;
            BigNotif("PAUSADO - NUMPAD 6 para reanudar | " + m_SampleCount + " pts");
        } else if (m_State == BZPathLogState.PAUSED) {
            m_State = BZPathLogState.RECORDING;
            BigNotif("REANUDADO - grabando");
        }
    }

    void MarkStop() {
        if (m_State == BZPathLogState.OFF) {
            BigNotif("Path Logger apagado. NUMPAD 5 para iniciar.");
            return;
        }
        bool wasRecording = (m_State == BZPathLogState.RECORDING);
        WriteSample(true);
        m_StopMarkCount++;
        BigNotif("PARADA #" + m_StopMarkCount + " marcada (sample " + m_SampleCount + ")");
        if (!wasRecording) {
            BZPathLog.Info("MarkStop con grabador en PAUSED — sample escrito, sigue pausado.");
        }
    }

    // -------------------------------------------------------------------------
    //  Captura de gear ranges (modo medicion de vehiculo)
    //
    //  NUMPAD 3 = SHIFT_POINT: velocidad en la que el conductor cambiaria
    //  normalmente (aguja por llegar a redline)
    //  NUMPAD 0 = MAX_SUSTAINED: velocidad maxima sostenida del gear (aguja
    //  pegada al redline, ya no acelera mas)
    //
    //  Independiente del PathLogger normal — no necesita NUMPAD 5 activo.
    //  Escribe a gear_ranges_<timestamp>.csv (uno por sesion).

    void MarkShiftPoint() {
        WriteGearRangeSample("SHIFT_POINT");
    }

    void MarkMaxSustained() {
        WriteGearRangeSample("MAX_SUSTAINED");
    }

    private string m_GearRangePath;
    private bool   m_GearRangeInited;

    private void WriteGearRangeSample(string markType) {
        Man player = GetGame().GetPlayer();
        if (!player) {
            BigNotif("No hay player activo");
            return;
        }
        Car car = Car.Cast(player.GetParent());
        if (!car) {
            BigNotif("[" + markType + "] No estas en un vehiculo");
            return;
        }

        // Inicializar archivo si es la primera marca
        if (!m_GearRangeInited) {
            EnsureProfileDir();
            m_GearRangePath = "$profile:BrigadaZ_Transport_PathLogger\\gear_ranges_" + BuildTimestamp() + ".csv";
            FileHandle h = OpenFile(m_GearRangePath, FileMode.WRITE);
            if (h) {
                FPrint(h, "time_s,gear,speed_kmh,rpm,redline_rpm,mark_type,vehicle_class\n");
                CloseFile(h);
                m_GearRangeInited = true;
                BZPathLog.Info("Gear ranges init: " + m_GearRangePath);
            } else {
                BigNotif("ERROR: no se pudo crear gear_ranges file");
                return;
            }
        }

        int   gear        = car.GetGear();
        float speedKmh    = car.GetSpeedometerAbsolute();
        float rpm         = car.EngineGetRPM();
        float redlineRpm  = car.EngineGetRPMRedline();
        float t           = GetGame().GetTickTime();
        string vehicleClass = car.GetType();

        // Split de string para evitar "Formula too complex"
        string line = "" + t + "," + gear + "," + speedKmh + "," + rpm;
        line += "," + redlineRpm + "," + markType;
        line += "," + vehicleClass + "\n";

        FileHandle f = OpenFile(m_GearRangePath, FileMode.APPEND);
        if (!f) {
            BZPathLog.Err("Append gear_ranges fallo: " + m_GearRangePath);
            return;
        }
        FPrint(f, line);
        CloseFile(f);

        BigNotif("[" + markType + "] gear=" + gear + " speed=" + (int)speedKmh + " km/h rpm=" + (int)rpm);
        BZPathLog.Info("MarkGearRange " + markType + ": gear=" + gear + " speed=" + speedKmh + " rpm=" + rpm);
    }

    // -------------------------------------------------------------------------
    //  Start / Stop internos

    private void Start() {
        EnsureProfileDir();
        m_FilePath        = "$profile:BrigadaZ_Transport_PathLogger\\path_" + BuildTimestamp() + ".csv";
        m_StartTickTime   = GetGame().GetTickTime();
        m_SampleCount     = 0;
        m_StopMarkCount   = 0;
        m_TotalDistance   = 0;
        m_HasLastPos      = false;
        m_LastFeedbackTime = m_StartTickTime;

        if (!WriteHeader()) {
            BZPathLog.Err("No se pudo crear archivo: " + m_FilePath);
            BigNotif("ERROR: no se pudo crear archivo " + m_FilePath);
            return;
        }

        m_State = BZPathLogState.RECORDING;
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.SampleTick, SAMPLE_INTERVAL_MS, true);

        BigNotif("INICIANDO grabado: " + m_FilePath);
        BZPathLog.Info("Start: " + m_FilePath);
    }

    private void Stop() {
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(this.SampleTick);
        m_State = BZPathLogState.OFF;

        float km = m_TotalDistance / 1000.0;
        BigNotif("STOP - " + m_SampleCount + " pts | " + string.Format("%.2f", km) + " km | " + m_StopMarkCount + " paradas");
        BZPathLog.Info("Stop: " + m_FilePath + " | " + m_SampleCount + " samples, " + string.Format("%.2f", km) + " km");
    }

    // -------------------------------------------------------------------------
    //  Tick + escritura

    private void SampleTick() {
        if (m_State != BZPathLogState.RECORDING) return;
        WriteSample(false);
        MaybeFeedback();
    }

    private void WriteSample(bool isStop) {
        Man player = GetGame().GetPlayer();
        if (!player) return;

        vector pos;
        vector dir;
        float  speedKmh = 0.0;

        Object parent = player.GetParent();
        Car    car    = Car.Cast(parent);
        int    gear     = 0;
        float  brake    = 0;
        float  throttle = 0;
        float  steering = 0;

        if (car) {
            pos      = car.GetPosition();
            dir      = car.GetDirection();
            speedKmh = car.GetSpeedometerAbsolute();
            gear     = car.GetGear();
            // Si alguno de estos getters no compila en esta version del juego,
            // el build va a fallar y los sacamos / reemplazamos por GetController().
            brake    = car.GetBrake();
            throttle = car.GetThrottle();
            steering = car.GetSteering();
        } else {
            pos = player.GetPosition();
            dir = player.GetDirection();
        }

        float heading = Math.Atan2(dir[0], dir[2]) * Math.RAD2DEG;
        if (heading < 0) heading += 360.0;

        float t = GetGame().GetTickTime() - m_StartTickTime;

        if (m_HasLastPos)
            m_TotalDistance += vector.Distance(pos, m_LastPos);
        m_LastPos    = pos;
        m_HasLastPos = true;
        m_SampleCount++;

        int isStopInt = 0;
        if (isStop) isStopInt = 1;

        string line = "" + t + "," + pos[0] + "," + pos[1] + "," + pos[2];
        line += "," + heading + "," + speedKmh;
        line += "," + isStopInt + "," + gear;
        line += "," + throttle + "," + brake;
        line += "," + steering + "\n";

        AppendLine(line);
    }

    // -------------------------------------------------------------------------
    //  IO

    private void EnsureProfileDir() {
        string dir = "$profile:BrigadaZ_Transport_PathLogger\\";
        if (!FileExist(dir))
            MakeDirectory(dir);
    }

    private bool WriteHeader() {
        FileHandle f = OpenFile(m_FilePath, FileMode.WRITE);
        if (!f) return false;
        FPrint(f, "time_s,x,y,z,heading_deg,speed_kmh,is_stop,gear,throttle,brake,steering\n");
        CloseFile(f);
        return true;
    }

    // Open/Close en cada sample: si crashea el cliente, lo escrito ya esta en disco.
    private void AppendLine(string line) {
        FileHandle f = OpenFile(m_FilePath, FileMode.APPEND);
        if (!f) {
            BZPathLog.Err("Append fallo: " + m_FilePath);
            return;
        }
        FPrint(f, line);
        CloseFile(f);
    }

    // -------------------------------------------------------------------------
    //  Feedback

    private void MaybeFeedback() {
        float now = GetGame().GetTickTime();
        if (now - m_LastFeedbackTime < FEEDBACK_INTERVAL_S) return;
        m_LastFeedbackTime = now;

        Man player = GetGame().GetPlayer();
        float speedKmh = 0.0;
        if (player) {
            Car car = Car.Cast(player.GetParent());
            if (car) speedKmh = car.GetSpeedometerAbsolute();
        }

        float km = m_TotalDistance / 1000.0;
        string msg = "REC | " + m_SampleCount + " pts | " + string.Format("%.2f", km) + " km | " + string.Format("%.0f", speedKmh) + " km/h";
        SmallNotif(msg);
    }

    // -------------------------------------------------------------------------
    //  Mensajes al jugador

    private void BigNotif(string msg) {
        PlayerIdentity id = GetIdentity();
        if (id)
            ExpansionNotification("[PATH LOGGER]", msg).Create(id);
        BZPathLog.Info(msg);
    }

    private void SmallNotif(string msg) {
        PlayerIdentity id = GetIdentity();
        if (id)
            ExpansionNotification("[PATH]", msg).Create(id);
    }

    private PlayerIdentity GetIdentity() {
        PlayerBase pb = PlayerBase.Cast(GetGame().GetPlayer());
        if (pb) return pb.GetIdentity();
        return null;
    }

    // -------------------------------------------------------------------------
    //  Timestamp para el nombre del archivo

    private string BuildTimestamp() {
        int y, mo, d, h, mi, s;
        GetYearMonthDay(y, mo, d);
        GetHourMinuteSecond(h, mi, s);
        return string.Format("%1%2%3-%4%5%6",
            y.ToString(),
            Pad2(mo), Pad2(d),
            Pad2(h),  Pad2(mi), Pad2(s));
    }

    private string Pad2(int n) {
        if (n < 10) return "0" + n.ToString();
        return n.ToString();
    }
}
