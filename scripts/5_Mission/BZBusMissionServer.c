modded class MissionServer {
    override void OnInit() {
        super.OnInit();
        BZBusService.GetInstance().Init();
        BZRouteCleanup.GetInstance().Init();
    }
}
