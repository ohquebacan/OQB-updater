# Reporte — OQB-updater

Rama: `feat/forwarders` · Versión: 1.7.2 · Fecha: 2026-08-30

## Estado

Probado en consola y funcionando: forwarders al descargar apps, y la
corrección del ícono de nube al reinstalar.

Sin probar en consola: todo lo de 1.7.1 en adelante (apps `.zip`, crear desde
la SD, gestionar accesos directos, escaneo de actualizaciones, y la
preservación de carpetas al instalar un HATS pack).

## Recorrido

| Versión | Qué salió |
|---|---|
| 1.6.0 | Forwarders al menú HOME al descargar una app `.nro` |
| 1.6.1 | Fix: la barra de progreso se quedaba en 0% para siempre |
| 1.6.2 | Botón "volver a descargar app" aunque ya esté al día |
| 1.6.3 | Fix: reinstalar un forwarder lo dejaba con el ícono de nube |
| 1.7.1 | Forwarders para `.zip`, crear desde la SD, gestionar, marcar instaladas |
| 1.7.1* | Fix: crash al borrar, escaneo lento, textos cortados, icono roto |
| 1.7.2 | Conservar carpetas de `contents` al instalar un HATS pack |

(*) 1.7.1 se republicó dos veces sobre el mismo tag. Por eso el bump a 1.7.2:
mirando el footer no había forma de saber qué build tenías.

## Lo último: preservar carpetas al instalar un HATS pack

**El problema.** Instalar un pack borra `/atmosphere/contents/` y `/SaltySD/`
antes de extraer, para evitar conflictos de arranque con restos
incompatibles. Ese borrado usaba `removeDir()` a secas e ignoraba
`preserve.txt`: ese archivo sólo evitaba la **sobrescritura** durante la
extracción, no el **borrado previo**. Lo que estuviera listado ya no existía
cuando la extracción llegaba a consultarlo.

El caso concreto: una carpeta de juego de ~50 GB en
`/atmosphere/contents/0100B00B51230000`.

**La solución.** `fs::removeDirContentsExcept()` vacía el directorio salvo lo
listado. Con `preserve.txt` vacío o inexistente el comportamiento es idéntico
al anterior. `0100B00B51230000` está en una lista built-in
(`AMS_CONTENTS_KEEP` en `constants.hpp`) para que se conserve de fábrica sin
que nadie tenga que crear ese archivo.

**Un agujero que apareció al endurecerlo.** La primera versión comparaba
prefijos de *texto*, no de *ruta*. Preservar `.../0100B00B51230000` también
salvaba `.../0100B00B51230000FF`, y listar un prefijo corto habría salvado
todo lo que empezara igual. Ahora el prefijo tiene que cortar en un separador,
y se normalizan barras dobles y finales — ahí un falso negativo no es
cosmético: borra la carpeta.

**Verificación.** `tests/preserve_matching_test.cpp` corre en el host y cubre
11 casos (mayúsculas cruzadas, barra final, barra doble, contenido interno,
tids que comparten prefijo, prefijos cortos). Pasan todos.

**Auditoría de borrados.** El único punto del código que borra `contents` es
ese. `removeSysmodulesFlags` sólo elimina archivos `boot2.flag`; la limpieza
de cheats sólo borra una carpeta si quedó vacía. Y todos los modos de falla
del borrado salen sin borrar nada: si el directorio no se puede enumerar o la
ruta no existe, no toca nada.

**Lo que no está cubierto.** Si el pack trajera su propia
`0100B00B51230000`, la extracción la pisaría igual — la lista built-in sólo
protege del borrado previo. No aplica a este caso porque un HATS pack no trae
una carpeta de 50 GB adentro.

## Infraestructura

`docker_build.sh` limpia `build/` cuando cambia el Makefile. `APP_VERSION`
llega a los fuentes como `-D` y make no ve esa dependencia, así que cambiar la
versión sin limpiar dejaba el binario reportando la vieja — justo el dato con
el que la app decide si hay actualización. Pasó dos veces antes de arreglarlo.

Nota: colima se apagó a mitad de sesión y un build "vacío" resultó ser el
daemon caído, no un problema de make. Si un build no produce salida, revisar
eso primero.

## Pendientes

- Nada de 1.7.1/1.7.2 está probado en consola.
- Strings en español hardcodeados en `tools_tab.cpp` y `color_picker_page.cpp`
  en vez de pasar por i18n.
- La rama sigue sin mergear a `master`.
