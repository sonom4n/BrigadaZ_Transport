// ============================================================================
//  BZBusNum - numeros que pueden no ser numeros (DayZ 1.30+).
//
//  En la 1.30, GetSpeedometerAbsolute() puede devolver NaN durante unos segundos
//  despues de spawnear el vehiculo (medido en AutoDrive y en Transport). En
//  Enforce, comparar con NaN no es confiable ('nan > 0.5' dio verdadero), asi
//  que un NaN que llega a los bucles que normalizan angulos los deja girando
//  para siempre y el server se cuelga. Todas las lecturas del velocimetro pasan
//  por Kmh(); en la 1.29 devuelve exactamente lo mismo que antes.
// ============================================================================
class BZBusNum {
    private static bool s_AvisoNan;

    // NaN e infinitos: v != v, y al pasar a entero caen a -2147483648 (la prueba
    // que no depende de como compare el motor con NaN; receta de AutoDrive)
    static bool Finite(float v) {
        if (v != v) return false;
        int iv = (int)v;
        if (iv < -2000000000) return false;
        if (v > 1000000000.0) return false;
        if (v < -1000000000.0) return false;
        return true;
    }

    // un NaN a proposito (solo para pruebas)
    static float NaN() {
        float n = Math.Sqrt(-1.0);
        if (!Finite(n)) return n;
        float z = 0.0;
        n = z / z;
        return n;
    }

    // km/h del vehiculo. Si el velocimetro no da un numero, la velocidad del
    // cuerpo; si tampoco, 0. Avisa una vez por sesion.
    static float Kmh(Car c) {
        if (!c) return 0;
        float kmh = c.GetSpeedometerAbsolute();
#ifndef DAYZ_1_29
        if (Finite(kmh)) return kmh;
        vector vel = GetVelocity(c);
        float kmhCuerpo = 0;
        if (Finite(vel[0]) && Finite(vel[1]) && Finite(vel[2])) kmhCuerpo = vel.Length() * 3.6;
        if (!s_AvisoNan) {
            s_AvisoNan = true;
            BZBusLog.Warn("[Velocimetro] GetSpeedometerAbsolute no da un numero en " + c.GetType() + ": se usa la velocidad del cuerpo (" + kmhCuerpo + " km/h) mientras dure");
        }
        return kmhCuerpo;
#endif
        return kmh;
    }
}
