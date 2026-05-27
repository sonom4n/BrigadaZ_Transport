// ============================================================================
//  BZBusUI - pantalla de parada del bus.
//  Tab 0: Estado actual + ETA a esta parada.
//  Tab 1: Próximas paradas en la dirección actual.
//  Tab 2: Avisos para el jugador (no obstruir la ruta, etc).
// ============================================================================

class BZBusUI extends UIScriptedMenu {

    static BZBusUI s_Instance;

    private ref BZBusStopInfo m_Info;
    private int               m_ActiveTab;

    private TextWidget              m_BodyText;
    private ButtonWidget            m_CloseBtn;
    private ref array<ButtonWidget> m_TabButtons;
    private ref array<Widget>       m_TabOverlays;

    static const int TAB_COUNT = 3;

    void SetInfo(BZBusStopInfo info) {
        m_Info      = info;
        m_ActiveTab = 0;
        Render();
    }

    override Widget Init() {
        layoutRoot = GetGame().GetWorkspace().CreateWidgets("BrigadaZ_Transport/gui/layouts/bus_stop.layout");

        m_BodyText = TextWidget.Cast(layoutRoot.FindAnyWidget("BodyText"));

        m_CloseBtn    = ButtonWidget.Cast(layoutRoot.FindAnyWidget("CloseBtn"));
        m_TabButtons  = new array<ButtonWidget>();
        m_TabOverlays = new array<Widget>();

        for (int i = 0; i < TAB_COUNT; i++) {
            m_TabButtons.Insert(ButtonWidget.Cast(layoutRoot.FindAnyWidget("Tab" + i)));
            m_TabOverlays.Insert(layoutRoot.FindAnyWidget("Tab" + i + "_Overlay"));
        }

        s_Instance = this;

        if (BZBusClientManager.s_PendingInfo) {
            SetInfo(BZBusClientManager.s_PendingInfo);
            BZBusClientManager.s_PendingInfo = null;
        }

        return layoutRoot;
    }

    override void OnShow() {
        super.OnShow();
        GetGame().GetInput().ChangeGameFocus(1);
        GetGame().GetMission().PlayerControlEnable(false);
        SetFocus(layoutRoot);
    }

    override void OnHide() {
        super.OnHide();
        GetGame().GetInput().ChangeGameFocus(-1);
        GetGame().GetMission().PlayerControlEnable(true);
        if (s_Instance == this) s_Instance = null;
    }

    override bool OnClick(Widget w, int x, int y, int button) {
        if (w == m_CloseBtn) {
            GetGame().GetUIManager().HideScriptedMenu(this);
            return true;
        }
        for (int i = 0; i < m_TabButtons.Count(); i++) {
            if (w == m_TabButtons[i]) {
                m_ActiveTab = i;
                Render();
                return true;
            }
        }
        return super.OnClick(w, x, y, button);
    }

    void Render() {
        if (!m_Info || !layoutRoot) return;

        for (int i = 0; i < m_TabOverlays.Count(); i++) {
            if (m_TabOverlays[i])
                m_TabOverlays[i].Show(i == m_ActiveTab);
        }

        if (m_BodyText)
            m_BodyText.SetText(BuildTabText(m_ActiveTab));
    }

    private string BuildTabText(int tab) {
        if (!m_Info) return "";

        if (tab == 0) return BuildStatusTab();
        if (tab == 1) return BuildRouteTab();
        if (tab == 2) return BuildWarningsTab();
        return "";
    }

    private string BuildStatusTab() {
        string t = "=== Bus Costero BrigadaZ ===\n\n";
        t += "Parada:  " + m_Info.stopName + "\n";
        t += "Estado:  " + m_Info.status + "\n\n";

        if (m_Info.etaSeconds <= 0) {
            t += "ETA: El bus está aqui ahora.\n";
        } else if (m_Info.etaSeconds < 60) {
            t += "ETA: menos de 1 minuto\n";
        } else {
            int min = m_Info.etaSeconds / 60;
            int sec = m_Info.etaSeconds % 60;
            t += "ETA: ~" + min + " min " + sec + " seg\n";
        }

        if (m_Info.distanceMeters > 0) {
            if (m_Info.distanceMeters < 1000) {
                t += "Distancia: " + (int)m_Info.distanceMeters + " m\n";
            } else {
                float distKm  = m_Info.distanceMeters / 1000.0;
                int   whole   = (int)distKm;
                int   tenths  = (int)((distKm - whole) * 10);
                if (tenths < 0) tenths = -tenths;
                t += "Distancia: " + whole + "." + tenths + " km\n";
            }
        }

        t += "\nPresiona F o ESC para cerrar.";
        return t;
    }

    private string BuildRouteTab() {
        string t = "=== Próximas paradas ===\n\n";

        if (m_Info.upcomingStops.Count() == 0) {
            t += "(sin información de ruta)\n";
        } else {
            foreach (string stop : m_Info.upcomingStops) {
                t += "  > " + stop + "\n";
            }
        }

        return t;
    }

    private string BuildWarningsTab() {
        string t = "=== Avisos importantes ===\n\n";
        t += "El bus es manejado por IA y NO se detiene\n";
        t += "ante obstaculos imprevistos. Para evitar\n";
        t += "problemas, tene presente:\n\n";
        t += "  > No dejes vehiculos estacionados en las\n";
        t += "    paradas. El bus va a colisionar al llegar.\n\n";
        t += "  > No dejes vehiculos abandonados sobre la\n";
        t += "    ruta. El bus no los esquiva.\n\n";
        t += "  > No te interpongas en el camino del bus.\n";
        t += "    No frena ante jugadores ni zombies.\n\n";
        t += "  > El bus es indestructible durante el\n";
        t += "    servicio: si chocas con el vehiculo tuyo,\n";
        t += "    el que se rompe es el tuyo.\n\n";
        t += "  > Para subirte, esperalo en la parada y\n";
        t += "    sentate en cualquier asiento de pasajero\n";
        t += "    apenas se detenga.";
        return t;
    }

    override bool UseKeyboard() { return true; }
    override bool UseMouse()    { return true; }
}
