// ============================================================================
//  MissionGameplay modded (cliente)
//  F cerca de una parada (anchor en bus_stops.json) -> pide estado del bus al
//  servidor y abre la UI. No depende de objetos spawneados: solo coord+radio.
// ============================================================================

modded class MissionGameplay {

    override UIScriptedMenu CreateScriptedMenu(int id) {
        UIScriptedMenu menu = super.CreateScriptedMenu(id);
        if (!menu && id == MENU_BZ_TRANSPORT)
            menu = new BZBusUI();
        return menu;
    }

    override void OnKeyPress(int key) {
        // Cerrar el menu con ESC o F si esta abierto
        if (BZBusUI.s_Instance && (key == KeyCode.KC_ESCAPE || key == KeyCode.KC_F)) {
            GetGame().GetUIManager().HideScriptedMenu(BZBusUI.s_Instance);
            super.OnKeyPress(key);
            return;
        }

        if (key == KeyCode.KC_F) {
            PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
            if (player) {
                BZBusStopAnchor stop = BZBusStops.GetInstance().FindNearby(player.GetPosition());
                if (stop) {
                    BZBusLog.Info("F + parada cerca: " + stop.name);
                    BZBusClientManager.RequestStatus(stop.GetVector());
                    return;
                }
            }
        }

        // Admin: NUMPAD 2 = iniciar / respawnear el bus
        if (key == KeyCode.KC_NUMPAD2) {
            BZBusClientManager.RequestRespawn();
            return;
        }
        // Admin: NUMPAD 1 = detener y limpiar el bus sin respawnear
        // Util para liberar recursos durante grabaciones humanas o test
        if (key == KeyCode.KC_NUMPAD1) {
            BZBusClientManager.RequestStopBus();
            return;
        }

        // PathLogger (admin/grabacion de waypoints, cliente-only)
        if (key == KeyCode.KC_NUMPAD5) {
            BZPathLogService.GetInstance().ToggleStartStop();
            return;
        }
        if (key == KeyCode.KC_NUMPAD6) {
            BZPathLogService.GetInstance().TogglePause();
            return;
        }
        if (key == KeyCode.KC_NUMPAD4) {
            BZPathLogService.GetInstance().MarkStop();
            return;
        }
        // Captura de gear ranges (modo medicion de vehiculo, independiente del PathLogger normal)
        if (key == KeyCode.KC_NUMPAD3) {
            BZPathLogService.GetInstance().MarkShiftPoint();
            return;
        }
        if (key == KeyCode.KC_NUMPAD0) {
            BZPathLogService.GetInstance().MarkMaxSustained();
            return;
        }
        // AI logging: toggle grabacion server-side de la trayectoria del bus.
        // Cada corrida genera un CSV en $profile:BrigadaZ_Transport_PathLogger\\ai_run_<ts>.csv
        // Pareja con la grabacion humana (PathLogger normal con NUMPAD 5) para
        // comparar trayectorias y encontrar divergencias.
        if (key == KeyCode.KC_NUMPAD7) {
            BZBusClientManager.RequestAILoggingToggle();
            return;
        }
        // System Identification — caracterizacion del patron interno de eAI
        // NUMPAD 8: Experimento 1 (step response del throttle)
        // NUMPAD 9: Experimento 2 (curva de respuesta - throttle escalonado)
        if (key == KeyCode.KC_NUMPAD8) {
            BZBusClientManager.RequestSysIDStep();
            return;
        }
        if (key == KeyCode.KC_NUMPAD9) {
            BZBusClientManager.RequestSysIDCurve();
            return;
        }
        // PAUSA del bus - congela la ruta. Util para teleportar el bus a la
        // pista de experimentos sin que arranque solo. Toggle: NUMPAD .
        if (key == KeyCode.KC_DECIMAL) {
            BZBusClientManager.RequestPauseToggle();
            return;
        }
        // Marcar evento en AI logger CSV - permite anotar eventos visuales
        // (rozo el extremo, frenazo no esperado, etc.). La proxima muestra
        // tendra is_marker=1.
        if (key == KeyCode.KC_SUBTRACT) {
            BZBusClientManager.RequestAIMark();
            return;
        }

        super.OnKeyPress(key);
    }
}
