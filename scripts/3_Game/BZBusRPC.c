// ============================================================================
//  BZBusRPC - IDs de RPC cliente <-> servidor
//  Rango: 32410+ (no colisiona con BZInfoBoardRPC que usa 32400+)
// ============================================================================

enum BZBusRPC {
    INVALID         = 32410,
    REQUEST_STATUS,     // Cliente -> Servidor: pide estado del bus para una parada
    RECEIVE_STATUS,     // Servidor -> Cliente: entrega estado + ETA
    REQUEST_RESPAWN,    // Cliente -> Servidor: respawn manual del bus (debug/admin)
    REQUEST_RESPAWN_TEST, // Cliente -> Servidor: respawn con vehiculo test (Hatchback) - debug
    REQUEST_AI_LOG_TOGGLE, // Cliente -> Servidor: toggle AI logging (graba trayectoria del bus al CSV para comparar con grabacion humana)
    REQUEST_SYSID_STEP,  // Cliente -> Servidor: experimento System ID 1 - step response del throttle
    REQUEST_SYSID_CURVE, // Cliente -> Servidor: experimento System ID 2 - curva de respuesta (throttle escalonado)
    REQUEST_PAUSE_TOGGLE, // Cliente -> Servidor: pausa/reanuda la ruta del bus (para teleport + experimentos controlados)
    REQUEST_RAMP_TOGGLE,  // Cliente -> Servidor: spawnea/borra rampa de test en posicion hardcoded de Vybor
    REQUEST_AI_MARK,      // Cliente -> Servidor: marca evento en el AI logger CSV (parada visible, choque, etc.)
    REQUEST_STOP_BUS      // Cliente -> Servidor: detiene y limpia el bus sin respawnear (manual stop)
}
