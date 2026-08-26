# Reporte — Forwarders y mejoras (OQB-updater)

Rama: `feat/forwarders` · Fecha: 2026-08-26 · Versión: 1.7.1

## Estado

La función de forwarders está **probada y funcionando en consola real**. Lo de
esta última tanda todavía no.

## Recorrido de la sesión

| Versión | Qué salió |
|---|---|
| 1.6.0 | Forwarders al menú HOME al descargar una app (`.nro`) |
| 1.6.1 | Fix: la barra de progreso se quedaba en 0% para siempre |
| 1.6.2 | Botón "volver a descargar app" aunque ya esté al día |
| 1.6.3 | Fix: reinstalar un forwarder lo dejaba con el ícono de nube |
| 1.7.1 | Forwarders para apps `.zip`, crear desde la SD, gestionar, marcar instaladas |

## Los dos bugs que aparecieron y por qué

**Barra congelada en 0% (1.6.1).** `WorkerPage` sólo avanza de stage cuando
`ProgressEvent` llega a su máximo (`_current == _max`, con `_max = 60` por
defecto). Los workers que ya existían actualizan ese contador por dentro; el
lambda del forwarder no lo tocaba. No era un cuelgue: la instalación terminaba
bien y la UI seguía esperando una señal que nadie mandaba.

De paso se corrigió que el error se mostraba con `util::showDialogBoxInfo` desde
el hilo del worker. Todos los demás usos de esa función están en el hilo de la
UI, y abrir vistas fuera de él no es seguro en borealis — habría sido un crash
intermitente justo cuando algo fallara. Ahora el mensaje lo muestra
`ConfirmPage_Deferred`, que resuelve su texto al dibujarse.

**Ícono de nube al reinstalar (1.6.3).** `nsDeleteApplicationEntity(tid)` corría
*después* de escribir y registrar los NCAs nuevos. Como el content id de un NCA
es el sha256 de su propio contenido, reinstalar el mismo NRO genera ids
idénticos: esa limpieza borraba justamente los NCAs recién escritos, y el record
quedaba apuntando a contenido inexistente. No se notaba en la primera
instalación porque no había nada que borrar.

Ahora la limpieza va antes de escribir nada, con `nsDeleteApplicationCompletely`
para llevarse también el record viejo. **Este bug viene heredado de sphaira tal
cual — probablemente esté también allá.**

## Lo que se agregó en 1.7.1

**Forwarders para apps `.zip`.** 8 de las 19 apps del catálogo llegan
comprimidas y caían por otra rama del código que ni ofrecía la pregunta. Se
unificó el flujo: un helper nuevo (`extract::findNroInArchive`) escanea las
entradas del zip *antes* de extraerlo y anota qué `.nro` va a quedar en la SD,
prefiriendo el que caiga en `/switch/`. Si el zip no trae ninguno, la pregunta
se saltea sola en vez de ofrecer algo imposible.

**Crear forwarder desde la SD.** Página nueva en Tools que lista los `.nro` de
`/switch/`, incluyendo la convención `/switch/<app>/<app>.nro` de hbmenu, y
marca cuáles ya tienen acceso directo. Cubre todo lo instalado desde antes.

**Gestionar accesos directos.** Lista los forwarders instalados filtrando los
títulos con prefijo `0x05`, con su icono y nombre reales, y permite borrarlos.
Antes había que ir a Gestión de datos del sistema.

**Marcar apps ya instaladas.** Sólo para los `.nro`: los `.zip` no dicen dónde
terminan sus archivos, así que ahí no se puede saber sin extraerlos.

Las dos entradas nuevas de Tools quedaron en `hide_tabs.json`.

## Infraestructura

`docker_build.sh` ahora limpia `build/` cuando el Makefile cambió. `APP_VERSION`
llega a los fuentes como `-D` y make no ve esa dependencia, así que cambiar la
versión sin limpiar dejaba el binario reportando la vieja — que es justo el dato
con el que la app decide si hay actualización disponible. Pasó dos veces en esta
sesión. CI no lo necesita porque siempre parte de un checkout limpio.

Se intentó primero resolverlo en el Makefile con `$(OFILES_SRC): $(TOPDIR)/Makefile`,
pero no dispara la recompilación; se descartó por no seguir peleando con
semántica sutil de make cuando el script resuelve el caso de forma directa.

## Pendientes

- Nada de 1.7.1 está probado en consola.
- El fork tiene strings en español hardcodeados en `tools_tab.cpp` y
  `color_picker_page.cpp` en vez de pasar por i18n.
- Si alguna vez se publica un rebuild bajo el **mismo** tag (`gh release --clobber`),
  `APP_URL` apunta a `releases/latest/download/...` y la descarga no manda
  cabeceras anti-caché. Normalmente el clobber genera una URL firmada nueva, pero
  si "volver a descargar" no trae los cambios, ese es el sospechoso.
- La rama sigue sin mergear a `master`.
