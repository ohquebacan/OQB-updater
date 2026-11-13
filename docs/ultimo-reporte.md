# Último reporte — 13-nov-2026

## La sección Nexus Bacán en OQB Updater: construida y compilada; falta probarla en la consola

Nada está publicado ni guardado en git. La rama es `feat/forwarders`, que no se ha pusheado.

### Qué hace
- **Menú:** «Nexus Bacán», con el icono de la U-USB, después de Nextendo. Solo aparece si el catálogo trae `nexus_bacan`. Lleva un punto violeta si hay una versión nueva o si el pack normal quitó Nexus al actualizarse.
- **Sin Nexus:**
  - Muestra qué es y tres datos de tu consola: el firmware (Compatible o Todavía no), tu pack y el disco que hace falta.
  - Tiene un botón para instalar, marcado «En pruebas».
  - Si el pack normal lo quitó, lo explica y el botón dice «Reinstalar».
- **Con Nexus:**
  - Un tablero con tres cajas: el USB (conectado, formato, espacio y barra), los juegos que hay en el USB con sus nombres y la SD libre. El estado se vuelve a leer cada 3 s.
  - Herramientas: Abrir Nexus Bacán, Expulsar con seguridad (espera hasta 15 s la confirmación), Overlay en Ultrahand y Volver al pack normal.
- **Ayuda:**
  - «Cómo se usa»: 5 pasos.
  - «Diagnóstico para soporte»: guarda `/oqb/soporte/nexus-AAAAMMDD-HHMM.txt` con las versiones, el registro, las líneas del ini, los estados, el final de la traza y el log de la app. No incluye claves ni partidas.
- **Instalar:**
  1. Descarga el zip, o lo copia de `zip_local` en el modo prueba.
  2. Respalda `package3`, `stratosphere.romfs` y `fusee.bin` en `/config/aio-switch-updater/nexus_respaldo/`, solo si Nexus todavía no estaba.
  3. Extrae el zip. Borra el `fusee.bin` si el pack normal no lo tenía.
  4. Pone las dos líneas del USB en `[atmosphere]`.
  5. Guarda el registro en `nexus_bacan.json`.
  6. Reinicia (en Mariko, apaga).
- **Volver al pack normal:**
  - Restaura el respaldo, apaga el USB y quita el .nro y el .ovl. Las rutas de los juegos (`usb_content`) se conservan.
  - Sin respaldo (como en tu SD de la 23.5, donde Nexus se puso a mano), apaga el USB y abre la reinstalación de tu pack.

### Protección (las dos apps)
`/atmosphere/usb_content` y `/atmosphere/usb_badges` ya no los borra la limpieza al instalar un pack. Está en `source/deep_clean.cpp` de la Switch y en `src-tauri/src/instalar.rs` de OQB Updater PC/Mac (`cargo check` pasa).

### Archivos
- `source/nexus_tab.cpp` y `include/nexus_tab.hpp` (nuevos).
- `include/nexus_eject.hpp`: copia del protocolo de expulsión del overlay.
- `include/iconos_oqb.hpp`: icono `Usb`.
- `source/main_frame.cpp` y `include/main_frame.hpp`.
- `resources/i18n/es` y `en-US/menus.json`: 70 textos cada uno.
- El NRO compilado tiene md5 34db5112.

### Pack
`~/oqb-usb/pack-nexus/OQB-NEXUS-BACAN.zip` (md5 1e369e1a) contiene:
- `package3` 57649002 y `stratosphere.romfs` 85a390f4;
- `fusee.bin`;
- `NexusManager.nro` 152517f3;
- `nexus-bacan.ovl` 9562de8d.

Las entradas del catálogo están listas en `~/oqb-usb/pack-nexus/catalogo-nexus.json`. La URL del release (`nexus-1.0`) se publica solo cuando digas «súbelo».

### Para probar sin publicar
`~/oqb-usb/sd-prueba-updater/copiar-a-sd.sh "/Volumes/SD"` copia tres cosas:
- la app a `/switch/OQB-updater.nro`;
- el zip a `/oqb/nexus/`;
- `nexus_bacan_prueba.json`, que activa la sección con el zip local.

## 13-nov — copiado a la SD de la 23.0.1 (SWITCH SD)
- `/switch/OQB-updater.nro` (34db5112), `/oqb/nexus/OQB-NEXUS-BACAN.zip` (1e369e1a) y `nexus_bacan_prueba.json`. Sin archivos `._*`. SD expulsada.
- Esa SD tiene Nexus puesto a mano y es una versión vieja: package3 b089f619 y app 0eb315ee, sin el arreglo de expulsar. Además usa el pack OC.
- Orden de prueba: ver la sección y el tablero → Diagnóstico → Volver al pack normal (sin respaldo: apaga el USB y abre la reinstalación del pack OC) → Instalar Nexus Bacán desde la sección (zip local, con respaldo) → Expulsar.

## 13-nov (2) — prueba en la 23.0.1 revisada
- **Instalar desde la app:** funcionó completo (registro, respaldo, package3 57649002, app, overlay, líneas del USB). La pantalla final «Apagar» salió. Lo único: la copia del zip local no muestra una barra.
- **Expulsar desde OQB Updater siempre fallaba.** Nexus rechaza en el acto si hay un proceso de aplicación abierto (`ApplicationOrAlbumInUse`), y OQB Updater es una aplicación. Arreglado en dos partes:
  - Atmosphère 6621d1aad (`usb_eject_commands.hpp`): si lo único que estorba es una app o un juego abierto, el pedido espera hasta 15 s a que se cierre, después expulsa y avisa con su notificación. El ack se publica recién cuando se decide. package3 fa6fd9d0; romfs y fusee sin cambios.
  - OQB Updater: «Expulsar con seguridad» explica, escribe el pedido y cierra la app (botón «Cerrar y expulsar»).
- **Juegos en el USB:** ahora suma `/atmosphere/usb_badges/<id>-0.ubg`, para contar los que el USB trae de otra consola. El nombre se lee primero de la caché del sistema (CacheOnly) y después del contenido; antes a veces salía el ID.
- **Celeste no abre:** la revisión de Nexus no encontró nada mal (0 diferencias) y aclara que no quita la marca de «datos dañados» de la consola. En la traza la carga avanza sin errores. Falta saber qué error muestra la consola.
- **Copiado a la SD de la 23.0.1:** package3 fa6fd9d0, OQB-updater.nro 6f4ad379 y el zip nuevo a7907af8 (en `~/oqb-usb/pack-nexus`). El package3 nuevo también quedó en `~/oqb-usb/sd-23.5`.

## 13-nov (3) — acceso directo, logo E, ventanas y Celeste
- **OQB Updater:**
  - Al terminar de instalar Nexus pregunta si crear el acceso directo en el HOME. Si ya existe, no pregunta.
  - En Herramientas aparece «Acceso directo en el HOME» cuando falta.
  - Los avisos usan DialogoCentrado.
  - «Cómo se usa» abre arriba del texto: un párrafo enfocable, para que el foco no caiga en «Atrás» y corte el principio.
  - NRO 769f7d1b.
- **Logo E:** la D invertida (U violeta sobre fondo oscuro), en `~/oqb-usb/icono-app/final-E-1024.png`, `final-E-256.jpg` y `comparacion-D-E.png`. Falta que Emilio elija.
- **Nexus (bacan 7519256):**
  - Traducción por fragmentos (`~` en la tabla) para las listas de «Preparar el USB»: «Ya preparado», «Juego base», «Actualización», «Saltado» y otras.
  - Plantillas para el resumen, «Archivos que no coinciden», «Revisando archivo» y la frase de la comprobación.
  - El título del panel derecho se traduce entero y después se parte en líneas.
  - La última página de detalles de la revisión dice «Listo» y no repite «Resumen».
  - NRO 33715e42. Pack zip 89fc8683.
- **La expulsión desde OQB Updater ya funcionó:** el overlay mostró «Expulsado».
- **Celeste da «No se puede iniciar el programa» con el USB conectado.** La traza llega a la resolución del programa (stage 102) y se corta sin error.
  - La reinstalación del pack OC (01:57) rehízo hekate_ipl.ini y quitó [NEXUS PRUEBA]. Ahora se arranca con [EMUNAND CFW], que carga kip1 hoc.kip y secmon exosphere.bin del OC.
  - También borró `/atmosphere/usb-import-*`, aunque eso solo eran respaldos.
  - Volví a agregar [NEXUS PRUEBA] al final de hekate_ipl.ini; la copia de antes quedó en `config/aio-switch-updater/hekate_ipl.ini.antes-nexus-prueba`. Prueba pendiente: abrir Celeste desde esa entrada.
  - Si así abre, el instalador tiene que tener en cuenta el pack OC (hoc.kip/secmon), y deep_clean debería proteger también `/atmosphere/usb-import-*`.

## 13-nov (4) — Celeste con [NEXUS PRUEBA] y el port Scarface
- **Celeste abre desde [NEXUS PRUEBA].** Queda confirmado que con [EMUNAND CFW] del pack OC (kip1 hoc.kip y secmon exosphere.bin del OC) no abre desde el USB. Pendiente: que el instalador agregue una entrada propia o avise con el pack OC.
- **Scarface** (ID 05C7A4F9F5A56000, un port empaquetado como juego instalado) se movió completo al USB (7,9 GB). En el HOME quedó el ícono cargando y Emilio lo borró desde el HOME.
  - El registro se fue, pero los archivos parecen seguir en el USB: 41,7 GB libres antes y después.
  - `title_map.bin` todavía lo tiene.
  - La traza ya rotó: no quedan eventos de ese momento.
- **Causa probable:** en usb_mitm y ncm hay unos 30 `IsApplicationId(...)` (rango 0100000000010000–01FFFFFFFFFFFFFF) que descartan los IDs 05... Afecta al control (ícono), al alias y a la carga del programa. Ya pasó antes con HBmenu (050CDEB674DD0000).
- **Nexus 4719ebf:** como seguro provisional, esos títulos no se mueven al USB (solo se pueden traer de vuelta a la SD). Etiqueta «Se queda en la SD» y una explicación. NRO 786ba8a5, zip ed684cad, copiado a la SD de la 23.0.1.
- **Pregunta de Emilio:** ¿se pueden hacer compatibles? Probablemente sí: cambiar esos chequeos por «programa de usuario, no del sistema», revisando uno por uno (algunos protegen a los títulos del sistema). Es trabajo de varias pruebas en consola.

## 13-nov (5) — ports con ID 05... en el USB: preparación de la prueba
- El código de usb_mitm ya prevé programas fuera del rango de aplicaciones en la carga desde el USB (`GetUsbProgramContentPathImpl` busca en el disco). La ruta normal del ícono (`OpenUsbControlFileSystem`) no exige ID 0100. Todavía no se sabe dónde se corta Scarface.
- Los `!IsApplicationId(alias)` tratan los IDs que no son de juego como alias de un juego real. El código menciona un incidente previo («R16, Farlands»). No se cambian a ciegas.
- **Atmosphère 4100dba9a:** las vistas del HOME que salen bien se anotan una vez por minuto por juego. Antes llenaban la traza de 384 eventos y taparon el momento del port. package3 a0d84b55.
- **Nexus 49095c5:** con `sdmc:/config/nexusmanager/probar-ports-usb` deja mover IDs 05... al USB, solo para la prueba. NRO c0f995cc. Pack zip actualizado.
- `~/oqb-usb/prueba-ports/copiar-a-sd.sh` copia el package3, la app y la llave.
- **Prueba:** mover un port 05... chico al USB, volver al HOME, esperar el ícono, intentar abrirlo y traer la SD enseguida (sin reiniciar).
- 13-nov 03:22: copiado a la SD de la 23.0.1 package3 a0d84b55, Nexus c0f995cc y la llave probar-ports-usb. [NEXUS PRUEBA] sigue en hekate_ipl.ini.

## 13-nov (6) — el overclock confirmado como causa, límite en OQB Updater y Scarface
- **Prueba desde [NEXUS PRUEBA]:** el port de Sheep Raider (forwarder de sphaira) pasó al USB, carga su ícono con la marca y abre. La causa era el arranque del pack OC.
- **OQB Updater (sin commit, feat/forwarders):** con el pack OC (clave con `-OC-` o `kips/hoc.kip` en hekate_ipl.ini):
  - «Tu pack» dice «Todavía no» y explica por qué.
  - Instalar ofrece «Instalar el pack normal», con `instalarPorClave` de la clave sin `-OC`.
  - Con Nexus ya instalado sobre el OC, aparece un aviso arriba.
  - NRO 99b0081a.
  - Para después: compatibilidad con Horizon OC; Emilio sugiere que sys-clk-oc podría servir.
- **Scarface:** sus tres archivos siguen anotados en el USB, en `title_content_map.bin` y `content_locations.bin`: meta 4793fe61, control 08325ac1, programa d8bb3712. Ocupan unos 3,7 GB.
  - «Revisar USB» no lo veía porque `UserId` acepta solo IDs 01.... Sale como «Contenido que no se reconoce».
- **Nexus 3860e20:** el escaneo del USB acepta CNMT de aplicación con ID 05... (`PortId`/`UsbUserId`).
  - Las aplicaciones 05... instaladas en la consola (accesos directos) siguen excluidas: `InstalledIdentity` y el filtro de la línea 450 sin cambios.
  - Pruebas: repair 46 casos + control negativo 0x1759 OK, orchestration 62 OK, scan core OK.
  - NRO 8faa18ce, zip b1d7dec0, copiado a la SD de la 23.0.1 con OQB Updater.
- **Pendiente con Scarface:** agregarlo con «Revisar USB» desde [NEXUS PRUEBA] y ver si abre desde el USB. Esa es la prueba de un ID 05... instalado en el USB. Si no abre, moverlo a la SD, o borrarlo desde ahí para recuperar el espacio.

## 13-nov (7) — Scarface vuelve a cargar eterno; opciones de Nexus agrupadas
- **Scarface:** Nexus ya lo reconoce y lo agregó, pero el ícono del HOME sigue cargando, también desde [NEXUS PRUEBA]. Un ID 05... instalado en el USB no carga el ícono, aunque no haya overclock. Emilio lo va a mover a la SD. Con la traza limpia, la SD debería mostrar dónde se corta el control.
- **Nexus c324018:** «Ayuda y opciones» agrupado en cinco entradas:
  - Tu USB: conexión, juegos del USB y otra consola, reparar la biblioteca, restos, instalaciones a medias, retomar.
  - Íconos del HOME.
  - Tema y colores.
  - Red.
  - Ayuda.
  - NRO 6634e33a. Pendiente de copiar (la SD está en la consola).

## 13-nov (8) — maqueta «Un USB, varias consolas»
- `~/oqb-usb/maquetas/nexus-varias-consolas.html`, 5 pantallas:
  1. Cómo funciona.
  2. Aviso «Este USB trae 3 juegos» al conectar.
  3. Agregar, con tres estados: Se puede agregar / Ya está / Necesita su archivo.
  4. Listo y reiniciar.
  5. En la consola de origen: «Llevar el USB a otra consola», con el interruptor «Preparar solos los juegos que muevo al USB».
- **Idea clave:** guardar el permiso (ticket) en el USB al mover cada juego, para que preparar no sea un paso aparte. La otra consola detecta sola lo que puede agregar.
- **Por construir si Emilio aprueba:**
  - en Nexus, preparar al mover (reusar `usb_ticket_prepare` para un solo título al final de TransferGame);
  - el aviso en Tus juegos (escaneo de importación en segundo plano, que ya existe al arrancar);
  - la lista simplificada sobre ReviewMenu.

## 13-nov (9) — causa de Scarface, la marca «!» y el rediseño de varias consolas
- **Scarface desde el USB:**
  - El ícono ya salía después de reiniciar.
  - Al abrirlo daba 2123-0011 y quedaba la marca «!». «Revisar datos dañados» de la Configuración la quitó.
  - La traza (ya limpia) muestra stage 21 RuntimeCodeOpen = 0x234C02 = fs NcaHeaderSignature1VerificationFailed. Es un NSP casero sin firma.
  - `AcceptNcaHeaderSignature` (fssystem_usb_signature_policy.hpp) solo aceptaba 0100...; la prueba de Nexus `tests/usb_recovery/usb_homebrew_signature_policy_tests.cpp` exige también 05.... Corregido en **Atmosphère 3daca208d**; la prueba pasa.
  - package3 22e7a143, romfs 44a8c6e9, fusee 2f27b0d6 sin cambios.
- **Nexus 12e2999:**
  - Y en Inicio = «Actualizar HOME» (antes era Tema; Tema sigue en Ayuda y opciones).
  - Opción por juego «Quitar el aviso «!» de dañado»: `nsRequestVerifyApplication`, lo mismo que la Configuración.
- **Nexus 360283d** (maqueta «Un USB, varias consolas»):
  - Juegos del USB con estados Se puede agregar / Necesita su archivo / Incompleto / Ya está.
  - «Agregar N juegos».
  - Aviso al abrir: «Este USB trae juegos para agregar» con Ver y agregar.
  - Pantalla Listo con «Reiniciar ahora» (spsmShutdown) y Detalles.
  - Y = «Llevar a otra consola», con la explicación.
  - Tu USB › «Agregar juegos del USB» y «Llevar el USB a otra consola» (DisplayUsbPrepare, directo).
  - Ayuda › «Un USB, varias consolas».
  - Estados de la preparación: Se puede preparar / Listo para viajar / Solo en esta consola.
  - Pruebas del repair OK. NRO 5fdce77a.
- **Sin hacer todavía de la maqueta:**
  - el interruptor «Preparar solos al mover» (preparar los permisos al terminar cada movimiento al USB);
  - el aviso como franja dentro de Tus juegos (por ahora es la ventana que ya existía, con el texto nuevo).
- Pack zip 30fd7bea. Copiado a la SD de la 23.0.1 junto con OQB Updater 99b0081a (límite del pack OC).

## 13-nov (10) — preparar al mover y ajustes de «Llevar a otra consola»
- Scarface abre desde el USB después de la revisión oficial. La de la app (`nsRequestVerifyApplication`) confirma los archivos, pero no quita la «!». La opción ahora explica el camino oficial. En la captura de las 04:17 la «!» había vuelto; falta saber por qué.
- **Nexus ce89d1d:**
  - Al mover un juego al USB se escanea, se planifica y se guardan los permisos (`ScanUsbImports`, `PlanUsbTicketPreparation`, `PrepareUsbTickets`) en el mismo progreso. El Listo dice si quedó listo para otra consola.
  - Interruptor en Tu USB, «Preparar solos al mover: Sí/No» (sdmc:/config/nexusmanager/no-preparar-al-mover).
  - Estado Incomplete de la preparación = «Incompleto en el USB: restos de un movimiento cancelado».
  - Panel «TODO LISTO PARA VIAJAR» cuando no hay nada por guardar.
  - NRO 0be5eb35, copiado a la SD de la 23.0.1 y al pack.
- **Por qué salían «Saltado»** el DLC de MK8 y la actualización de Easy Delivery: en el USB solo hay restos incompletos (estado Incomplete del escaneo), no un problema de permisos.
- **Sobre llevar todo entre consolas propias:** los juegos con permiso común (instalados desde NSP) viajan. Los de la eShop tienen permiso personalizado, cifrado con la clave de la consola. Para que viajen habría que convertirlos a permiso común en la consola de origen, como hace nxdumptool. Es trabajo aparte.
- La prueba `run_usb_import_ui_tests.py` falla desde antes: busca el Draw original de Nexus, que ya cambió con el rediseño.

## 13-nov (11) — la SD de la 23.5 al día
- **Nexus 4f8024b:** los ports 05... pasan al USB sin la llave de prueba. NRO a88d4b18, en el pack y en `sd-23.5`.
- **Copiado a la SD de la 23.5** (pack normal, hekate 6.5.5, sin hoc):
  - package3 22e7a143, romfs 44a8c6e9 y `bootloader/payloads/fusee.bin`;
  - Nexus a88d4b18 y overlay 9562de8d;
  - OQB Updater 99b0081a;
  - el zip y `nexus_bacan_prueba.json` para ver la sección en OQB Updater.
- La SD de la 23.0.1 todavía tiene Nexus 0be5eb35: le falta 4f8024b, que es solo el seguro 05.... Ahí la llave de prueba ya lo permitía.
