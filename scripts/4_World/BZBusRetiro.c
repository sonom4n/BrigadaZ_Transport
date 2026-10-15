// ============================================================================
//  BZBusRetiro - DayZ 1.30+: nunca se borra el bus con alguien sentado.
//
//  En la 1.30, borrar un vehiculo con un Man SENTADO (Boris o un jugador)
//  tumba el server (crash nativo). Borrar al chofer y al bus en el mismo
//  frame tampoco es seguro: el Delete es diferido y el orden no esta
//  garantizado. En la 1.29 CleanupEntities borra como siempre (este archivo
//  no compila ahi).
//
//    paso 1  frenar el bus; el servicio ya lo solto (puede respawnear)
//    paso 2  bajar a Boris: sin pedido de subida pendiente (si no, eAI lo
//            vuelve a sentar apenas queda a pie) y GetOutVehicle
//    paso 3  a los 3 s, JumpOutVehicle si sigue adentro
//    paso 4  Boris afuera -> delete; a los 5 s se borra igual
//    paso 5  en un frame POSTERIOR, y solo si el bus esta VACIO, delete del
//            bus. Si hay pasajeros (jugadores) NO se borra: queda frenado y
//            se reintenta cada 5 s; a los 10 min se deja en el mundo
//    paso 6  retiro terminado
//  Cada paso deja una linea [CLEANUP] en el RPT con el segundo del retiro.
// ============================================================================
#ifndef DAYZ_1_29
class BZBusRetiro {
    private static ref array<ref BZBusRetiro> s_Vivos;
    private static int s_Seq;
    static const int   POLL_MS             = 100;
    static const float BAJAR_TOPE_S        = 3.0;
    static const float SALTO_TOPE_S        = 5.0;
    static const int   BUS_TRAS_MS         = 500;
    static const int   PASAJEROS_REINTENTO_MS = 5000;
    static const float PASAJEROS_TOPE_S    = 600.0;
    static const float PASAJEROS_AVISO_CADA_S = 60.0;

    private EntityAI         m_Veh;
    private ref array<Human> m_Bots;
    private string           m_Rotulo;
    private int              m_Id;
    private float            m_T0;
    private bool             m_Salto;
    private float            m_TPasajeros;
    private float            m_TPasajerosAviso;
    private bool             m_Hecho;

    static void Retirar(EntityAI veh, array<Human> bots, string rotulo) {
        if (!s_Vivos) s_Vivos = new array<ref BZBusRetiro>();
        // los terminados se sueltan aca, no adentro de su propio metodo
        for (int i = s_Vivos.Count() - 1; i >= 0; i--) {
            BZBusRetiro viejo = s_Vivos.Get(i);
            if (!viejo || viejo.m_Hecho) s_Vivos.RemoveOrdered(i);
        }
        BZBusRetiro r = new BZBusRetiro();
        s_Seq = s_Seq + 1;
        r.m_Id = s_Seq;
        r.m_Veh = veh;
        r.m_Bots = new array<Human>();
        if (bots) {
            for (int k = 0; k < bots.Count(); k++) {
                if (bots.Get(k)) r.m_Bots.Insert(bots.Get(k));
            }
        }
        r.m_Rotulo = rotulo;
        r.m_T0 = GetGame().GetTickTime();
        s_Vivos.Insert(r);
        r.Empezar();
    }

    // true si un bus que se esta retirando sigue a menos de `radio` metros de
    // `pos` (SpawnBus espera: si no, el bus nuevo nace encima del viejo)
    static bool BusCerca(vector pos, float radio) {
        if (!s_Vivos) return false;
        foreach (BZBusRetiro r : s_Vivos) {
            if (!r || r.m_Hecho || !r.m_Veh) continue;
            if (vector.Distance(r.m_Veh.GetPosition(), pos) < radio) return true;
        }
        return false;
    }

    static int Ocupantes(Object o) {
        Transport tr = Transport.Cast(o);
        if (!tr) return 0;
        int n = 0;
        for (int i = 0; i < tr.CrewSize(); i++) {
            if (tr.CrewMember(i)) n++;
        }
        return n;
    }

    static string Seg(float s) {
        float red = Math.Round(s * 10) / 10;
        return red.ToString();
    }

    private string Pre() {
        float t = GetGame().GetTickTime() - m_T0;
        return "[CLEANUP] retiro " + m_Id.ToString() + " (" + m_Rotulo + ") t+" + Seg(t) + " s - ";
    }

    private void Frenar() {
        Car car = Car.Cast(m_Veh);
        if (!car) return;
        car.SetThrottle(0);
        car.SetBrake(1.0);
        car.SetHandbrake(1.0);
    }

    private bool EsNuestro(Human h) {
        return m_Bots.Find(h) >= 0;
    }

    // los sentados en el bus que NO son nuestros (jugadores, cadaveres, bots ajenos)
    private int Pasajeros() {
        Transport tr = Transport.Cast(m_Veh);
        if (!tr) return 0;
        int n = 0;
        for (int i = 0; i < tr.CrewSize(); i++) {
            Human h = tr.CrewMember(i);
            if (h && !EsNuestro(h)) n++;
        }
        return n;
    }

    private bool Afuera(Human b) {
        if (!b) return true;
        Transport tr = Transport.Cast(m_Veh);
        if (tr) {
            for (int i = 0; i < tr.CrewSize(); i++) {
                if (tr.CrewMember(i) == b) return false;
            }
        }
        if (b.GetCommand_Vehicle()) return false;
        return true;
    }

    private void Bajar(Human b) {
        if (!b) return;
        HumanCommandVehicle hcv = b.GetCommand_Vehicle();
        if (!hcv) return;
        eAIBase ai = eAIBase.Cast(b);
        if (ai) ai.Notify_Transport(null, -1);
        hcv.GetOutVehicle();
    }

    private void Empezar() {
        string tipo = "(sin vehiculo)";
        if (m_Veh) tipo = m_Veh.GetType();
        BZBusLog.Info(Pre() + "paso 1: " + tipo + " frenado y soltado - bots " + m_Bots.Count().ToString() + " - pasajeros " + Pasajeros().ToString());
        Frenar();
        int bajando = 0;
        for (int i = 0; i < m_Bots.Count(); i++) {
            Human b = m_Bots.Get(i);
            if (b && b.GetCommand_Vehicle()) {
                Bajar(b);
                bajando++;
            }
        }
        BZBusLog.Info(Pre() + "paso 2: GetOutVehicle a " + bajando.ToString() + " bot(s) sentado(s)");
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.EsperarAfuera, POLL_MS, false);
    }

    private void BorrarBot(Human b, string como) {
        BZBusLog.Info(Pre() + "paso 4: delete " + b.GetType() + " (" + como + ")");
        b.Delete();
    }

    private void EsperarAfuera() {
        float t = GetGame().GetTickTime() - m_T0;
        Frenar();
        for (int i = m_Bots.Count() - 1; i >= 0; i--) {
            Human b = m_Bots.Get(i);
            if (!b) {
                m_Bots.RemoveOrdered(i);
                continue;
            }
            if (Afuera(b)) {
                BorrarBot(b, "afuera");
                m_Bots.RemoveOrdered(i);
            }
        }
        if (m_Bots.Count() == 0) {
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.PasoBus, BUS_TRAS_MS, false);
            return;
        }
        if (!m_Salto && t >= BAJAR_TOPE_S) {
            m_Salto = true;
            for (int j = 0; j < m_Bots.Count(); j++) {
                Human bj = m_Bots.Get(j);
                if (!bj) continue;
                HumanCommandVehicle hj = bj.GetCommand_Vehicle();
                if (hj) hj.JumpOutVehicle();
            }
            BZBusLog.Warn(Pre() + "paso 3: " + m_Bots.Count().ToString() + " no bajaron en " + Seg(BAJAR_TOPE_S) + " s -> JumpOutVehicle");
        }
        if (t >= SALTO_TOPE_S) {
            BZBusLog.Warn(Pre() + "paso 3: " + m_Bots.Count().ToString() + " siguen adentro a los " + Seg(SALTO_TOPE_S) + " s -> se borran igual (el bus espera a estar vacio)");
            for (int k = m_Bots.Count() - 1; k >= 0; k--) {
                Human bk = m_Bots.Get(k);
                if (bk) BorrarBot(bk, "seguia adentro");
            }
            m_Bots.Clear();
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.PasoBus, BUS_TRAS_MS, false);
            return;
        }
        GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.EsperarAfuera, POLL_MS, false);
    }

    private void PasoBus() {
        if (!m_Veh) {
            BZBusLog.Info(Pre() + "paso 6: retiro terminado (sin vehiculo)");
            m_Hecho = true;
            return;
        }
        int ocupados = Ocupantes(m_Veh);
        if (ocupados > 0) {
            Frenar();
            if (m_TPasajeros == 0 || m_TPasajeros - m_TPasajerosAviso >= PASAJEROS_AVISO_CADA_S) {
                m_TPasajerosAviso = m_TPasajeros;
                BZBusLog.Warn(Pre() + "paso 5: " + ocupados.ToString() + " ocupante(s) sentado(s) -> el bus NO se borra (reintento cada " + (PASAJEROS_REINTENTO_MS / 1000).ToString() + " s, tope " + Seg(PASAJEROS_TOPE_S) + " s)");
            }
            if (m_TPasajeros >= PASAJEROS_TOPE_S) {
                BZBusLog.Warn(Pre() + "paso 6: TOPE con " + ocupados.ToString() + " ocupante(s): el bus queda en el mundo, frenado y sin servicio");
                m_Hecho = true;
                return;
            }
            m_TPasajeros = m_TPasajeros + PASAJEROS_REINTENTO_MS / 1000.0;
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(this.PasoBus, PASAJEROS_REINTENTO_MS, false);
            return;
        }
        BZBusLog.Info(Pre() + "paso 5: delete bus (vacio)");
        m_Veh.Delete();
        m_Veh = null;
        BZBusLog.Info(Pre() + "paso 6: retiro terminado");
        m_Hecho = true;
    }
}
#endif
