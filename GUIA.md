# AC Scene Folders — Guía

Plugin para OBS Studio que agrega un panel para organizar las escenas en carpetas, con búsqueda, colores y favoritas.

> **Estado: v0.2.0.** Compila en macOS (universal), Windows x64 y Ubuntu mediante GitHub Actions. Falta la prueba dentro de OBS (paso 6).

## 1. Qué hace

- **Carpetas y subcarpetas** para las escenas, con arrastrar y soltar.
- **Clic en toda la fila** de la carpeta para abrirla o cerrarla (no solo en la flecha).
- **Búsqueda**: escribe y filtra; Enter salta a la primera coincidencia.
- **Favoritas**: marca escenas con ★ y usa el botón ★ para ver solo esas.
- **Colores por carpeta**: cian, ámbar, morado, rojo, verde y azul.
- **Modo Estudio**: un clic manda la escena a vista previa, doble clic a programa.
- **Indicador de en vivo**: punto rojo y fila resaltada en la escena al aire; punto hueco para la vista previa del Modo Estudio.
- Cada carpeta muestra **cuántas escenas tiene** y el punto rojo si contiene la escena en vivo, aunque esté cerrada.
- **Selección múltiple** con Ctrl/Cmd o Shift + clic, para arrastrar varias a la vez o usar clic derecho → **Mover a**.
- **Ayuda inicial** con botón para crear la primera carpeta.
- **La estructura se guarda dentro de la colección de escenas**: viaja al exportar, importar o duplicar la colección, y cada colección tiene su propio árbol.
- Las escenas se identifican por su ID interno, así que renombrarlas no las saca de su carpeta.
- Interfaz en español e inglés.

Las carpetas son solo organización visual: no modifican las escenas ni sus fuentes. Si desinstalas el plugin, tus escenas quedan intactas.

## 2. Requisitos

| | |
|---|---|
| OBS Studio | 31.1 o superior (probado el diseño contra la API de OBS 32) |
| macOS | Apple Silicon o Intel, Xcode 16 y CMake 3.30 para compilar |
| Windows | Visual Studio 2022 y CMake 3.30 para compilar |

## 3. Compilar en Mac

Desde la carpeta del proyecto:

```bash
cmake --preset macos
cmake --build --preset macos
```

La primera vez descarga las dependencias de OBS y Qt, tarda unos minutos. Para trabajar en Xcode abre `build_macos/ac-scene-folders.xcodeproj`.

El resultado es `ac-scene-folders.plugin` dentro de `build_macos/`.

Cada push a `main` también compila en GitHub Actions; los binarios quedan como artefactos de la ejecución.

## 4. Instalar

**macOS** — copia `ac-scene-folders.plugin` a:

```
~/Library/Application Support/obs-studio/plugins/
```

**Windows** — copia el contenido del zip generado dentro de la carpeta de instalación de OBS (`C:\Program Files\obs-studio\`).

Reinicia OBS y activa el panel en **Paneles → Carpetas de escenas** (Docks → Scene Folders).

## 5. Uso

1. Al abrirlo por primera vez verás todas tus escenas en la raíz.
2. Botón **+** o clic derecho → **Nueva carpeta**.
3. Arrastra escenas (o carpetas) dentro de una carpeta. Para mover varias: Ctrl/Cmd + clic para seleccionarlas, luego arrástralas o clic derecho → **Mover a**.
4. Clic derecho en una carpeta: renombrar, cambiar color, crear subcarpeta o eliminarla. Eliminar una carpeta **no borra escenas**: su contenido sube un nivel.
5. Clic derecho en una escena: agregar o quitar de favoritas.
6. Las escenas nuevas que crees en OBS aparecen al final de la raíz.

Crear, borrar y renombrar escenas se sigue haciendo en el panel normal de Escenas de OBS.

## 6. Lista de pruebas antes de publicar

- [ ] El panel aparece y lista todas las escenas.
- [ ] Crear carpeta, subcarpeta, renombrar, color, eliminar.
- [ ] Arrastrar escenas y carpetas; no se puede soltar dentro de una escena.
- [ ] Cerrar y abrir OBS: la estructura y las carpetas abiertas/cerradas se conservan.
- [ ] Cambiar de colección de escenas y volver: cada una conserva su árbol.
- [ ] Exportar la colección, importarla con otro nombre: llega con carpetas.
- [ ] Renombrar y borrar escenas desde OBS: el panel se actualiza.
- [ ] Modo Estudio: clic = vista previa, doble clic = programa.
- [ ] Panel acoplado a la izquierda y en pestaña junto a otro panel: no queda en blanco al reabrir.
- [ ] Selección múltiple: arrastrar varias y "Mover a" (carpeta existente, nivel principal, carpeta nueva).
- [ ] Punto rojo en la escena en vivo y en su carpeta cerrada; contador de escenas correcto.
- [ ] Doble clic en una carpeta no parpadea.
- [ ] Búsqueda y filtro de favoritas; al limpiar, las carpetas vuelven a su estado.

## 7. Cómo está hecho

```
src/plugin-main.cpp       Registro del módulo, el panel y los callbacks de OBS
src/scene-tree-dock.hpp   Clase del panel
src/scene-tree-dock.cpp   Árbol, sincronización con OBS, guardado y menús
data/locale/              Textos en-US y es-ES
buildspec.json            Nombre, versión, bundle ID y dependencias
```

- El panel es un `QTreeWidget` registrado con `obs_frontend_add_dock_by_id`.
- Se sincroniza con los eventos de OBS (lista de escenas, escena actual, cambio de colección) y con la señal de renombrado.
- Guarda con `obs_frontend_add_save_callback`, bajo la clave `ac-scene-folders` del archivo de la colección.

## 8. Firma y notarización en Mac

Sin firma, macOS bloquea el plugin y el usuario tiene que usar la Terminal. Para evitarlo necesitas una cuenta de Apple Developer:

1. Certificado **Developer ID Application** (y **Developer ID Installer** para el .pkg).
2. Exporta `CODESIGN_IDENT` y `CODESIGN_TEAM` antes de compilar, o configura los secretos del repositorio para que GitHub Actions firme y notarice (el flujo ya viene en `.github/`).
3. Detalle oficial: https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS

Este es el punto que más te diferencia de la alternativa actual, que se distribuye sin firmar.

## 9. Publicar en los foros de OBS

1. Sube el código a GitHub (`alejox/ac-scene-folders`) y crea un release `0.1.0` con los instaladores.
2. En obsproject.com/forum → Resources → **Add resource** → categoría OBS Studio Plugins, con tu cuenta alejoxcode.
3. Ficha lista para pegar:

**Título:** AC Scene Folders

**Tagline:** Organize your scenes into folders, with search, colors and favorites.

**Descripción:**

```
AC Scene Folders adds a dock that lets you organize your scenes into folders.

Features
• Folders and subfolders with drag & drop
• Click anywhere on a folder row to open or close it
• Quick search — press Enter to jump to the first match
• Favorites and a favorites-only view
• Folder colors
• Studio Mode aware: click = preview, double click = program
• The folder layout is stored inside the scene collection, so it
  survives export, import and duplicate
• English and Spanish

Folders are purely visual: your scenes and sources are never modified.

Requirements: OBS Studio 31.1 or newer. Windows and macOS.

Source code and bug reports: https://github.com/alejox/ac-scene-folders
More plugins: https://www.alejoxcode.com/plugins
```

**Campos:** Minimum OBS Studio Version 31.1.0 · Platforms: Windows, macOS · Source Code URL: el repositorio · Licencia GPL-2.0.

Los foros piden declarar el uso de IA en el desarrollo: inclúyelo en la descripción.

## 10. Siguientes pasos

- Miniatura de la escena al pasar el cursor.
- Exponer las carpetas a Speed Deck: cada carpeta como una página de botones.
- Íconos por carpeta.
- Instalador .pkg firmado y build para Linux.
