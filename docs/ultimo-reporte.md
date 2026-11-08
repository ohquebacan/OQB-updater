# Último reporte — OQB Updater (8-nov-2026)

## Bacán Shop seguía diciendo «actualización pendiente»
La actualización sí se instaló: en la SD está la 2.21.24, la misma que publica el catálogo, y quedó anotada.
El problema era que la app calculaba el aviso una sola vez, al abrirse, y no lo volvía a revisar después de instalar.
Ahora, al terminar cualquier instalación, se recalcula todo:
- el punto dorado de Bacán Shop y el «Nuevas» de Apps en el menú;
- la píldora y el botón de la pestaña Bacán Shop (pasa a «v2.21.24 · Al día» y «Volver a instalar»);
- las filas de las listas de apps y de las apps sugeridas;
- el «N nuevas» de cada sección.

## Íconos en las listas de apps
Cada fila de app lleva su ícono, y también las apps sugeridas en Bacán Shop. El ícono sale, en este orden:
1. Del catálogo: el script de OQB-HATS-PACK saca el ícono de cada .nro, lo achica a 96 px (unos 3 KB) y publica todos juntos en `iconos.zip`. La app lo baja una sola vez en segundo plano y lo guarda en `/config/aio-switch-updater/iconos/`. Solo vuelve a pedirlo si sale una app con un ícono nuevo.
2. Si el catálogo no lo trae: el ícono del .nro que ya está instalado en la SD.
3. Si no hay ninguno de los dos: un ícono neutro, para que todas las filas queden alineadas.

## Estado
- OQB Updater: compilado y copiado a la SD (`/switch/OQB-updater.nro`, md5 87774199121b5956f288768e6315780f). Sin commit. Falta probarlo en la consola.
- Catálogo (`~/oqb-hats-catalogo`): `update_app_versions.py` y el workflow quedaron listos, sin commit ni push. Hasta que se publique, solo se verán los íconos de las apps instaladas y el neutro en las demás. Al publicarlo, el workflow vuelve a abrir todos los archivos una vez para sacar los íconos.
- JKSV: lo que faltaba (la tarjeta de la Nube y la copia a la SD) sigue en otra sesión.
