// ============================================================================
//  BZBusCommon - constantes, estructuras y logger compartidos client/server
// ============================================================================

const int MENU_BZ_TRANSPORT = 51210;

class BZBusStopInfo {
    string stopName;
    string status;       // "en camino" / "en parada aqui" / "en parada en X" / "fuera de servicio"
    int    etaSeconds;
    float  distanceMeters;
    ref array<string> upcomingStops;

    void BZBusStopInfo() {
        upcomingStops = new array<string>();
    }
}

class BZBusLog {
    static void Info(string msg)  { Print("[BZBus] "        + msg); }
    static void Warn(string msg)  { Print("[BZBus][WARN] "  + msg); }
    static void Err(string msg)   { Print("[BZBus][ERROR] " + msg); }
}
