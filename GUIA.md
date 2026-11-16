# AC Scene Groups — Guía

Plugin para OBS Studio que agrupa las escenas por modo (stream, grabación…) y permite gestionarlas desde un solo panel.

> **Estado: v0.4.1.** Rediseño completo: los grupos reemplazan a las carpetas de las versiones 0.1 y 0.2. Compila en macOS (universal), Windows x64 y Ubuntu mediante GitHub Actions. Falta la prueba dentro de OBS (paso 5).

El repositorio y el archivo del plugin conservan el nombre `ac-scene-folders` para que la actualización reemplace a la versión anterior.

## 1. Qué hace

- **Selector de grupos** arriba: cada grupo muestra solo sus escenas. "Todas" muestra la lista completa.
- **Una escena puede estar en varios grupos**; una etiqueta indica en cuántos.
- **Gestión de escenas en el mismo panel**: crear, renombrar (F2), duplicar y eliminar. Ya no hace falta el panel nativo de Escenas.
- **Nueva escena en el grupo activo**: se crea y queda dentro del grupo que estás viendo.
- **En vivo y vista previa**: etiqueta roja "EN VIVO" y, en Modo Estudio, etiqueta ámbar "VISTA PREVIA". Clic manda a vista previa, doble clic al aire.
- **Perfil de OBS vinculado (opcional)**: al elegir un grupo, OBS cambia a su perfil. No se aplica mientras transmites o grabas.
- **Carpetas dentro de cada grupo**: organiza las escenas de un grupo en carpetas que se abren y cierran (un nivel, sin subcarpetas). Cada grupo tiene sus propias carpetas. Con una carpeta cerrada, un punto rojo indica que dentro está la escena en vivo.
- **Orden propio por grupo**: arrastra escenas y carpetas. "Todas" sigue el orden de OBS y no muestra carpetas.
- **Búsqueda** por nombre.
- Los grupos se guardan dentro de la colección de escenas: viajan al exportarla o duplicarla.
- Si venías de la v0.2, cada carpeta de primer nivel se convierte en un grupo.
- **Integración con otros plugins**: expone los grupos para que AC Global Overlay pueda mostrar un overlay solo en ciertos grupos.
- El panel se abre solo la primera vez que se instala.
- **Aviso de actualización**: una vez al día consulta la última release en GitHub y, si hay una versión nueva, muestra un aviso en el panel con el botón Descargar. No descarga ni instala nada por su cuenta. Se desactiva con clic derecho en un espacio vacío de la lista → "Buscar actualizaciones". En Linux no está disponible.
- Interfaz en español e inglés.

Los grupos son un filtro del panel: los atajos, el Stream Deck y el panel nativo siguen viendo todas las escenas.

## 2. Requisitos

| | |
|---|---|
| OBS Studio | 31.1 o superior |
| macOS | Apple Silicon o Intel |
| Windows | x64 |

## 3. Instalar

**macOS**: copia `ac-scene-folders.plugin` a `~/Library/Application Support/obs-studio/plugins/`, reemplazando el anterior si existe.

**Windows**: copia el contenido del zip dentro de la carpeta de instalación de OBS.

Reinicia OBS. El panel aparece solo la primera vez; después se controla en **Paneles → Grupos de escenas**.

## 4. Uso

1. Crea tus grupos con los botones sugeridos ("Stream", "Grabación") o con **+**.
2. En "Todas", selecciona escenas (Cmd/Ctrl + clic para varias), clic derecho → **Mostrar en** y marca los grupos.
3. Cambia de grupo en el selector de arriba.
4. **Nueva escena** la crea dentro del grupo activo.
5. Carpetas: botón de carpeta del pie o clic derecho → **Nueva carpeta**. Para meter escenas, arrástralas o usa clic derecho → **Mover a carpeta**. Clic derecho en una carpeta: renombrar, nueva escena aquí, color o eliminar (las escenas se quedan en el grupo).
6. El engranaje abre los ajustes del grupo: nombre, color y perfil vinculado. Eliminar un grupo no borra sus escenas.
7. Cuando ya trabajes cómodo aquí, puedes ocultar el panel nativo de Escenas en el menú Paneles.

## 5. Lista de pruebas antes de publicar

- [ ] El panel se abre solo en la primera instalación.
- [ ] Crear grupos, renombrarlos, cambiar color y eliminarlos (las escenas se conservan).
- [ ] Añadir y quitar escenas de grupos; la etiqueta "N grupos" es correcta.
- [ ] Crear escena desde el panel: aparece en OBS y en el grupo activo.
- [ ] Renombrar con F2, duplicar y eliminar; nombre repetido muestra aviso.
- [ ] Crear una escena mientras grabas no cambia la escena al aire.
- [ ] Cerrar y abrir OBS: grupos, orden y grupo activo se conservan.
- [ ] Cambiar de colección de escenas y volver: cada una conserva sus grupos.
- [ ] Modo Estudio: clic = vista previa, doble clic = al aire.
- [ ] Grupo con perfil vinculado: cambia el perfil; mientras grabas muestra "perfil sin cambiar".
- [ ] Reordenar arrastrando dentro de un grupo.
- [ ] Carpetas: crear, renombrar (F2), color, eliminar (las escenas se conservan), abrir y cerrar con clic en la fila.
- [ ] Arrastrar escenas dentro y fuera de carpetas; una carpeta no se puede meter dentro de otra.
- [ ] "Mover a carpeta" con varias escenas seleccionadas.
- [ ] Carpeta cerrada con la escena en vivo dentro: muestra el punto rojo.
- [ ] Estado abierto o cerrado de cada carpeta se conserva al reiniciar OBS.
- [ ] Migración desde la v0.2: las carpetas aparecen como grupos.

## 6. Publicar una versión

1. Sube el número en `buildspec.json` (`"version"`, por ejemplo `0.4.2`) y haz push a `main`.
2. GitHub Actions compila y, si esa versión todavía no tiene release, crea la etiqueta y publica la release con el plugin suelto adjunto.

También funciona crear la release a mano con una etiqueta igual al número de `buildspec.json`. Si macOS no consigue máquina en GitHub, usa "Re-run failed jobs" en Actions y la release se publica al terminar.

## 7. Compilar

```bash
cmake --preset macos
cmake --build --preset macos
```

Cada push a `main` compila en GitHub Actions; los binarios quedan como artefactos de la ejecución.

## 8. Cómo está hecho

```
src/plugin-main.cpp         Registro del módulo, el panel y los callbacks de OBS
src/scene-groups-dock.hpp   Clase del panel
src/scene-groups-dock.cpp   Grupos, lista, sincronización con OBS, guardado y menús
src/update-check.cpp        Aviso de actualización (consulta diaria a GitHub)
src/net-get-*.{mm,cpp}      HTTPS con la red del sistema (NSURLSession, WinHTTP)
data/locale/                Textos en-US y es-ES
```

### API para otros plugins

- Procedimiento `void ac_scene_groups_get(out string json)` en `obs_get_proc_handler()`.
- Señal `void ac_scene_groups_changed()` en `obs_get_signal_handler()`, cuando cambian los grupos, sus escenas o la colección.
- JSON: `{"version":1,"groups":[{"id","name","color","scenes":[uuids]}]}`; `scenes` incluye las escenas dentro de carpetas.

Guarda con `obs_frontend_add_save_callback`, bajo la clave `ac-scene-groups` del archivo de la colección.

## 9. Firma en Mac

El plugin va sin firmar con Apple Developer, así que macOS puede bloquearlo la primera vez (Ajustes del Sistema → Privacidad y seguridad). Para firmarlo y notarizarlo: https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS

## 10. Ficha para los foros de OBS

**Título:** AC Scene Groups

**Tagline:** Group your scenes by mode and manage them from a single dock.

```
AC Scene Groups adds a dock that groups your scenes by mode: streaming,
recording, or anything else.

Features
• Group selector: each group shows only its scenes
• A scene can live in several groups
• Create, rename, duplicate and delete scenes from the dock
• Optional: link a group to an OBS profile
• LIVE and PREVIEW badges, Studio Mode aware
• Per-group scene order, quick search
• Groups are stored inside the scene collection
• English and Spanish

Groups are a view: hotkeys and other tools still see every scene.

Network use: once a day the plugin asks the GitHub API for the latest
release to show an update notice. It can be turned off from the dock.

Requirements: OBS Studio 31.1 or newer. Windows and macOS.
```

Los foros piden declarar el uso de IA en el desarrollo: inclúyelo en la descripción.
