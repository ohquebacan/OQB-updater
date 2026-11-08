# Último reporte — Instalación limpia del pack (8-nov-2026)

Está en Herramientas › Archivos, debajo de «Limpiar archivos de Mac», y se esconde junto con ella si `hide_tabs.json` oculta «cleanup». Compila sin errores y ya está en la SD (md5 78f3335f…). No se probó en la consola ni se publicó.

## El recorrido
1. **Elegir el pack:** salen los packs propios del catálogo, y el que tienes instalado lleva la marca «En tu SD».
2. **Descarga y revisión de la SD:** hasta aquí no se borra nada.
3. **Vista previa:**
   - arriba, cuánto se borra y cuánto se conserva;
   - lo que siempre se conserva;
   - un interruptor por cada categoría que decides tú;
   - lo que siempre se borra;
   - «Ver la lista completa» y «Continuar». Ⓑ cancela y vuelve al inicio sin tocar nada.
4. **Confirmar:** hay que mantener Ⓐ 3 segundos. Si se suelta antes, la barra vuelve a cero.
5. **Borrar e instalar:** se borra lo elegido y cada cosa se anota en `/config/aio-switch-updater/limpieza.log`. Después se instala el pack por el camino de siempre y se borra el `ams.zip` descargado.
6. **Reiniciar o apagar**, como en cualquier pack.

## Nunca se toca
- `/Nintendo`, `/emuMMC`, `/backup` y `/JKSV`
- `atmosphere/automatic_backups` y todo lo protegido en la limpieza normal
- lo que esté en preserve.txt
- juegos sueltos en la raíz (.nsp, .nsz, .xci, .xcz)
- las llaves de la consola (`*.keys`)
- todo lo de Prelude y Nextendo: `rootCA.pem`, `prelude_*` y cualquier cosa con «nextendo» en el nombre
- la carpeta de datos de cada app del pack (Checkpoint guarda las partidas en `/switch/Checkpoint`)
- las carpetas ocultas de `/switch`, como `.packages` de HB App Store
- la propia app

## Categorías
| Categoría | Por omisión |
|---|---|
| Restos de otros packs y descargas viejas | siempre se borra |
| Apps que no son del pack | se borra |
| Ports y emuladores con sus juegos (app ajena de más de 256 MB) | se queda |
| Mods de juegos (`/ultimate`, `/mods`, `/SaltySD`) | se queda |
| Temas (lo de `/themes` que no trae el pack) | se queda |
| Otras cosas en la raíz | se borra |

## Simulación contra tu SD (solo lectura)
Con el pack OC y las opciones por omisión se borrarían unos 950 MB:
- **Restos (69 MB):** `custom.zip`, `gb_mod.zip`, `app.zip`, `.mesa`, `.fseventsd` y `.Spotlight-V100`.
- **Apps (553 MB, 22 cosas):** `sonicjump_nx`, `SwitchU`, `freeshop-client`, `CyberFoil`, `xuperswitch`, `switch/tico`, `scene_eshop`, `chiaki.nro`, `IconGrabber-OQB` y otras.
- **Raíz (331 MB):** `/firmware`, `hbmenu.nro.homenx-original`, `/forwarders`, `/dusk`, `dolphin-nx.log`, `/gameIcons` y `/iconos`.

Se quedarían por omisión:
- **Ports (41,9 GB):** `/tico` (29 GB), `nfsmw-nx`, `codmw`, `wwhd`, `LainNX` y `pipensx`.
- **Mods:** `/ultimate`.
- **Temas:** `ThemezerNX` y `systemData`.

La simulación encontró tres riesgos en la primera versión, ya corregidos:
- habría borrado `switch/prod.keys` y `switch/title.keys`;
- habría metido 42 GB de ports y emuladores en «Apps», que se borra por omisión;
- no protegía `nextendo.nro`.

Script de simulación: `/private/tmp/claude-501/-Users-emiliorobles/7d8831f0-a561-4f45-b82c-c9560f3a376b/scratchpad/simular_plan.py`

## Pendiente
- Probarlo en la consola. Sirve hasta la vista previa y cancelar con Ⓑ: no borra nada.
- Sin commit y sin publicar en la 3.2.
