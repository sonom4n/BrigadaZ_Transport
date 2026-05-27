# Examples — Archivos de ejemplo para administradores de server

Esta carpeta contiene **archivos auxiliares de referencia** que no son parte del
PBO del mod, pero que son útiles para integrar el framework en un server real.

---

## `EditorFiles/BusA.dze`

Archivo de DayZ Editor Loader que define los **objetos físicos de las 14 paradas**
del bus a lo largo de la costa de Chernarus (Kamenka, Komarovo, Balota, Vysotovo,
Chernogorsk, Prigorodki, Golova, Elektrozavodsk, Kamyshovo, Solnechny, Nizhnoe,
Berezino, Rify, Svetloyarsk).

### Cuándo usarlo

**Solo si**:

- Tu server corre el mapa **Chernarus** (`dayzOffline.chernarusplus`)
- Tenés el mod **DayZ Editor Loader** cargado en el server
- Querés que los carteles físicos de las paradas aparezcan en el mundo (los
  modelos vienen del mod **@Brutalist Bus Stops** de Buddy/docbuddy)

**El framework del bus funciona sin este archivo** — la mecánica del servicio
(detección de proximidad, UI de pasajero, paradas del recorrido) usa solo las
coordenadas + radio del JSON de la ruta. Sin el `.dze` los carteles físicos no
aparecen en el mundo, pero el bus sigue parando en cada coordenada correctamente.

### Cómo instalarlo

1. Asegurate de tener cargados los mods:
   - `@DayZ Editor Loader` (lee los `.dze` y spawnea los objetos)
   - `@Brutalist Bus Stops` (provee los modelos de los carteles)
2. Copiá `BusA.dze` a tu carpeta:
   ```
   <ruta_a_tu_server>/mpmissions/dayzOffline.chernarusplus/EditorFiles/BusA.dze
   ```
3. Reiniciá el server. Los carteles deberían aparecer en cada parada.

### Para otros mapas (Sakhal, Enoch, Namalsk, etc.)

Este `.dze` es específico de **Chernarus**. Las coordenadas de los objetos
están en el sistema de coords de ese mapa.

Para otros mapas tenés que:

1. Cargarlo en el editor de DayZ Editor Loader (in-game)
2. Borrar los objetos existentes (son de coords de Chernarus, no aplican)
3. Posicionar los carteles en las coords de las paradas de tu mapa
4. Guardar el `.dze` con el nombre de tu elección

O alternativamente, no usar `.dze` para nada y dejar las paradas sin carteles
físicos — el framework funciona igual.

### Personalización

Si querés usar otros modelos de carteles (no los de @Brutalist Bus Stops):

1. Abrí el `.dze` con DayZ Editor Loader in-game
2. Reemplazá cada objeto por el modelo que prefieras (cualquier prop del juego o
   de un mod cargado)
3. Guardá

---

## Estructura del repo

```
BrigadaZ_Transport/
├── README.md
├── PAPER_PUBLICO.md/pdf
├── MANUAL_eAI_VEHICLES.md/pdf
├── MOD_CONTEXT_FOR_AI.md
├── scripts/        ← código del mod (va al PBO)
├── data/           ← JSONs de configuración (va al PBO)
├── gui/            ← layouts de la UI (va al PBO)
├── tools/          ← herramientas PowerShell (NO va al PBO)
└── examples/       ← assets de ejemplo (NO va al PBO) ← VOS ESTÁS ACÁ
    └── EditorFiles/
        └── BusA.dze
```
