# Último reporte — limpieza al instalar un pack (8-nov-2026)

Revisé `deep_clean.cpp` y la limpieza de rutina de `utils::extractArchive`.

## Lo que ya hacía bien
- `/atmosphere`, salvo `contents`, queda igual a lo que trae el pack; se respeta lo que escriben Atmosphère y la consola.
- `atmosphere/contents` y `/SaltySD` se vacían, salvo lo que está en preserve.txt.
- En `/bootloader` se rehace todo con el pack, salvo `ini/` y `payloads/`.
- En `/config` se borra la carpeta de cada app que ya no está.
- En `/switch` caen las copias repetidas de apps que el pack ya trae y los .nro dañados.
- Se borran `ReiNX/`, `sxos/` y `sept/` de la raíz.
- Al final se aplica `pack_remove.txt`.

## Lo que agregué (sin commit)
1. **Carpetas:** se borran con `fs::removeDir`, la misma función que la limpieza de rutina y `pack_remove.txt`, en lugar de `std::filesystem::remove_all`, que en la Switch falla sin avisar.
2. **Raíz de la SD:** se borran los `hekate_ctcaer_*.bin` de otras versiones (solo si el pack trae el suyo) y `fusee-primary.bin` y `fusee-secondary.bin`, de antes de Atmosphère 1.0. No se tocan `payload.bin`, `boot.dat` ni `boot.ini`, porque los usan los chips.
3. **Basura de Mac:** se borran los `._`, los `.DS_Store` y las carpetas de indexado de macOS de toda la SD, salvo `/Nintendo` y `/emuMMC`. Es lo mismo que hace Herramientas.
4. **`bootloader/ini`, después de extraer:** se borra un .ini solo si NINGUNA de sus entradas puede arrancar, es decir, si faltan en la SD todos sus `payload=` y `fss0=`. Respeta preserve.txt.

Todo queda anotado en `deep_clean.log`.

## Sin probar
- Compila. No se probó en la consola ni se publicó.
- La basura conocida de otros packs en `/switch` sigue dependiendo de `pack_remove.txt`, que hoy solo lista lo del pack de overclock.
