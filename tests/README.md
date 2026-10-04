# tests

Pruebas que corren en la máquina de desarrollo, no en la consola.

## preserve_matching_test.cpp

Copia de la lógica de decisión de `fs::removeDirContentsExcept()`: qué se
conserva y qué se borra al limpiar `/atmosphere/contents/` antes de instalar
un HATS pack.

Está separada porque un falso negativo acá **borra datos del usuario** —
puede ser una carpeta de decenas de GB— y esa decisión no debería depender
sólo de leer el código con atención.

Si tocás el matching en `source/fs.cpp`, actualizá la copia y corré:

```bash
c++ -std=c++17 -o /tmp/t tests/preserve_matching_test.cpp && /tmp/t
```

## protection_test.cpp

Comprueba `protection::run()` — el verificador de protecciones del tab Tools.
Aquí se compila **el código real**, no una copia: `run()` acepta una raíz, así
que la prueba le arma una SD de mentira en `/tmp`.

Está separada por la misma razón que la otra: un falso negativo deja la consola
al descubierto sin avisar, y un falso positivo es casi igual de malo, porque una
herramienta que da alarmas falsas se termina ignorando.

```bash
c++ -std=c++17 -Iinclude -o /tmp/pt tests/protection_test.cpp source/protection.cpp && /tmp/pt
```
