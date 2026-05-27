// ============================================================================
//  BZBusClientManager (3_Game para que 5_Mission lo vea)
//  Cliente: envía REQUEST_STATUS, recibe RECEIVE_STATUS y abre la UI.
// ============================================================================

class BZBusClientManager {

    static ref BZBusStopInfo s_PendingInfo;

    // Cliente: pide estado del bus mandando la posición del cartel
    // El servidor matchea la posición con el waypoint más cercano
    static void RequestStatus(vector signPos) {
        Man player = GetGame().GetPlayer();
        if (!player) return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(signPos);
        rpc.Send(player, BZBusRPC.REQUEST_STATUS, true, null);
        BZBusLog.Info("RequestStatus pos: " + signPos.ToString());
    }

    // Cliente: pide al servidor respawnear el bus (debug)
    static void RequestRespawn() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_RESPAWN, true, null);
        BZBusLog.Info("RequestRespawn enviado");
    }

    // Cliente: pide al servidor detener y limpiar el bus sin respawnear
    static void RequestStopBus() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_STOP_BUS, true, null);
        BZBusLog.Info("RequestStopBus enviado");
    }

    // Cliente: pide respawn con vehiculo de test (Hatchback) - debug shift problem
    static void RequestRespawnTest() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_RESPAWN_TEST, true, null);
        BZBusLog.Info("RequestRespawnTest enviado");
    }

    // Cliente: pide al servidor toggle de AI logging (graba trayectoria del bus)
    static void RequestAILoggingToggle() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_AI_LOG_TOGGLE, true, null);
        BZBusLog.Info("RequestAILoggingToggle enviado");
    }

    // Cliente: arranca/para experimento System ID 1 (step response del throttle)
    static void RequestSysIDStep() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_SYSID_STEP, true, null);
        BZBusLog.Info("RequestSysIDStep enviado");
    }

    // Cliente: arranca/para experimento System ID 2 (curva de respuesta del throttle)
    static void RequestSysIDCurve() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_SYSID_CURVE, true, null);
        BZBusLog.Info("RequestSysIDCurve enviado");
    }

    // Cliente: pausa/reanuda la ruta del bus (para teleport + setup de experimentos)
    static void RequestPauseToggle() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_PAUSE_TOGGLE, true, null);
        BZBusLog.Info("RequestPauseToggle enviado");
    }

    // Cliente: toggle spawn/borrado de rampa de test en Vybor (System ID experiments)
    static void RequestRampToggle() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_RAMP_TOGGLE, true, null);
        BZBusLog.Info("RequestRampToggle enviado");
    }

    // Cliente: marca evento en el AI logger CSV (la proxima muestra tendra is_marker=1)
    static void RequestAIMark() {
        Man player = GetGame().GetPlayer();
        if (!player) return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, BZBusRPC.REQUEST_AI_MARK, true, null);
        BZBusLog.Info("RequestAIMark enviado");
    }

    // Cliente: recibe la respuesta del servidor y abre la UI
    static void OnReceiveAndOpen(ParamsReadContext ctx) {
        BZBusStopInfo info = Deserialize(ctx);
        if (!info) return;

        s_PendingInfo = info;
        GetGame().GetUIManager().EnterScriptedMenu(MENU_BZ_TRANSPORT, null);
    }

    static BZBusStopInfo Deserialize(ParamsReadContext ctx) {
        BZBusStopInfo info = new BZBusStopInfo();
        int stopCount;

        if (!ctx.Read(info.stopName))      { BZBusLog.Err("RX: stopName");    return null; }
        if (!ctx.Read(info.status))        { BZBusLog.Err("RX: status");      return null; }
        if (!ctx.Read(info.etaSeconds))    { BZBusLog.Err("RX: eta");         return null; }
        if (!ctx.Read(info.distanceMeters)){ BZBusLog.Err("RX: distance");    return null; }
        if (!ctx.Read(stopCount))          { BZBusLog.Err("RX: stopCount");   return null; }

        for (int i = 0; i < stopCount; i++) {
            string s;
            if (!ctx.Read(s)) break;
            info.upcomingStops.Insert(s);
        }

        return info;
    }
}
