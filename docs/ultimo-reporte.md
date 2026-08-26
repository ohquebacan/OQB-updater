# Reporte — Forwarders en la sección Apps (OQB-updater)

Rama: `feat/forwarders` · Fecha: 2026-08-26

## Qué se pidió

Que al descargar una app desde la pestaña **Apps**, la app ofrezca crear un
**forwarder** de esa misma app (acceso directo en el menú HOME de la Switch).

Emilio señaló [sphaira](https://github.com/NaGaa95/sphaira) como referencia — fue
exactamente la guía correcta.

## Estado del repo al empezar

- El repo no existía en esta computadora. Clonado a `~/OQB-updater`.
- Es el fork de AIO-Switch-Updater (C++, borealis, devkitPro/devkitA64).
- Submódulos (`lib/borealis`, `TegraExplorer`) inicializados.
- `gh` no está autenticado en esta máquina (el clone se hizo por HTTPS público).

## Cómo funciona un forwarder (lo que se investigó)

Un forwarder es un título instalado (NCAs registrados en `ncm` + un record en
`ns`) cuyo programa es una copia de **nx-hbloader** con la ruta del NRO destino
metida en su romfs. El menú HOME lo ve como un juego más.

Hallazgo clave que hace esto viable: sphaira crea los NCAs con las secciones
**sin cifrar** (`EncryptionType_None`), así que la única clave necesaria es
`header_key` — y esa se **deriva de la propia consola** con `splCrypto`.
**No hace falta `prod.keys` en la SD**, a diferencia de NSP Forwarder Nx o NTON.

La firma RSA del header del NCA no se puede falsificar, pero los sigpatches
hacen que FS no la verifique — que es justamente por qué los forwarders son
cosa de CFW.

## Qué se construyó

### 1. Sub-proyecto `hbl/` (nuevo)

El loader que se empaqueta dentro de cada forwarder generado.

- `hbl/source/main.c`, `hbl/source/trampoline.s`, `hbl/hbl.json` — portados del
  fork de nx-hbloader que usa sphaira.
- `hbl/Makefile` — nuevo, estilo devkitPro. A diferencia del resto del proyecto
  no produce un `.nro` sino `exefs/main` (NSO vía `elf2nso`) y `exefs/main.npdm`
  (vía `npdmtool`).

### 2. Módulo de forwarders (nuevo)

| Archivo | Qué hace |
|---|---|
| `include/forwarder.hpp` | API pública: `configFromNro`, `install`, `remove`, `exists`, `resultToString` |
| `include/forwarder/nx_types.hpp` | Structs binarias de NCA, NPDM y `ContentStorageRecord` |
| `include/forwarder/ns_ex.hpp` + `source/forwarder/ns_ex.cpp` | Comandos de `ns:am` que libnx no expone (`PushApplicationRecord`, `InvalidateApplicationControlCache`) |
| `include/forwarder/result.hpp` | Códigos de error del módulo (módulo 424) |
| `source/forwarder/forwarder.cpp` | El grueso: construcción de romfs/PFS0/IVFC/NCA, parcheo de NPDM y NACP, CNMT, e instalación vía `ncm` + `ns` |
| `source/forwarder/nro_asset.cpp` | Lee el asset section del NRO para sacar icono y NACP (nombre/autor) |

Portado de `source/owo.cpp` de sphaira (GPLv3, misma licencia que este repo, así
que es compatible; queda atribuido en el README y en los encabezados).

Diferencias deliberadas respecto de sphaira:

- **No se borra el `old_tid`.** Sphaira, además del tid nuevo (prefijo `0x05`),
  borra un tid legacy con prefijo `0x01` — que cae dentro del rango de títulos
  retail. Sphaira lo hace para limpiar forwarders de sus versiones viejas; acá no
  hay legacy que limpiar, y dejarlo sería arriesgar borrar un juego real ante una
  colisión de hash. Se quitó.
- El hbl se carga desde `romfs:/hbl/` en vez de `#embed` (que necesita un
  compilador más nuevo que el de este proyecto).
- Se dejaron fuera las opciones de sphaira que no aplican acá: modo de 4 núcleos,
  logo/GIF de arranque, `prepare_core_launch`.

### 3. UI

- `DialoguePage_optional` (nuevo, en `dialogue_page.cpp/hpp`): pregunta sí/no
  donde **ambas respuestas continúan** al siguiente stage. El `DialoguePage_confirm`
  que ya existía manda al menú principal si respondés que no — no servía acá,
  porque decir "no quiero forwarder" no debería abortar el flujo después de que la
  app ya se descargó.
- `list_download_tab.cpp`: tras bajar un `.nro` en la pestaña Apps, se agregan dos
  stages — la pregunta y un worker que crea el forwarder si dijiste que sí.

### 4. i18n

Sección `apps` nueva en los 15 idiomas (español real, el resto en inglés):
`forwarder_ask`, `forwarder_creating`, `forwarder_error`.

De paso: `menus/main/apps` y `apps_text` sólo existían en `en-US`, así que la
pestaña Apps se veía en inglés en español. Agregados al `es`.

### 5. Infraestructura

- `docker_build.sh` (nuevo): build local en el contenedor `devkitpro/devkita64`,
  replicando el workflow de CI. Monta el repo en `/OQB-updater` porque el Makefile
  deriva `TARGET` del nombre de la carpeta.
- `.gitignore`: `hbl/exefs/` y `resources/hbl/` (artefactos de build).
- `Makefile`: `SOURCES` incluye `source/forwarder`; el target `$(ROMFS)` compila
  el `hbl` y copia su exefs a `resources/hbl/`. **CI no necesita cambios** — el
  workflow ya ejecuta `make`, que dispara todo esto.
- `resources/forwarder_icon.jpg`: icono de reserva para NROs sin assets. Se usa
  un JPEG 256×256 porque el menú HOME no acepta otra cosa (el `gui_icon.png` que
  se pensó usar primero es PNG de 52×52 y habría salido roto).
- `APP_VERSION` 1.5.0 → 1.6.0.

## Verificación

- Build completo desde cero en `devkitpro/devkita64`: **compila sin errores**,
  produce `OQB-updater.nro`.
- El `hbl` compila a NSO + NPDM correctamente.

**Lo que NO está verificado: nada de esto se probó en una consola real.** El
código construye NCAs e instala títulos en el sistema — es la parte más delicada
que toca esta app hasta ahora. Antes de subirlo a `master` conviene:

1. Probar el `.nro` en tu Switch y crear un forwarder de alguna app de prueba.
2. Confirmar que aparece en el menú HOME, que lanza la app, y que el icono y el
   nombre se ven bien.
3. Confirmar que volver a descargar la misma app reemplaza el acceso directo en
   vez de duplicarlo.
4. Confirmar que se puede borrar desde Gestión de datos del sistema.

## Pendientes / ideas

- `fwd::remove()` y `fwd::exists()` están implementados pero todavía no hay UI que
  los use. Faltaría una pantalla tipo "gestionar accesos directos" en Tools.
- Las apps que llegan como `.zip` (Dusk, SysDVR, ARCropolis, etc.) no pasan por
  esta rama del código, así que no ofrecen forwarder. Habría que buscar el `.nro`
  dentro del zip extraído para cubrirlas.
- Nada de esto está commiteado todavía.
