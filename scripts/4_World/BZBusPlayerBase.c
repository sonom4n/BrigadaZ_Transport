// ============================================================================
//  PlayerBase modded - enruta RPCs del bus
// ============================================================================

modded class PlayerBase {
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx) {
        super.OnRPC(sender, rpc_type, ctx);

        switch (rpc_type) {
            case BZBusRPC.REQUEST_STATUS:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().HandleStatusRequest(ctx, sender);
                break;

            case BZBusRPC.RECEIVE_STATUS:
                if (!GetGame().IsDedicatedServer())
                    BZBusClientManager.OnReceiveAndOpen(ctx);
                break;

            case BZBusRPC.REQUEST_RESPAWN:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().RespawnBus();
                break;

            case BZBusRPC.REQUEST_RESPAWN_TEST:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().RespawnAs("Hatchback_02_Grey");
                break;

            case BZBusRPC.REQUEST_AI_LOG_TOGGLE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleAILogging();
                break;

            case BZBusRPC.REQUEST_SYSID_STEP:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleSysIDStep();
                break;

            case BZBusRPC.REQUEST_SYSID_CURVE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleSysIDCurve();
                break;

            case BZBusRPC.REQUEST_PAUSE_TOGGLE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().TogglePause();
                break;

            case BZBusRPC.REQUEST_RAMP_TOGGLE:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().ToggleRamp();
                break;

            case BZBusRPC.REQUEST_AI_MARK:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().MarkAIEvent();
                break;

            case BZBusRPC.REQUEST_STOP_BUS:
                if (GetGame().IsServer())
                    BZBusService.GetInstance().StopBus();
                break;
        }
    }
}
