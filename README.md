# AC Scene Groups

OBS Studio plugin that groups scenes by mode (stream, recording…) and lets you create, rename, duplicate and delete scenes from a single dock. Groups are stored inside the scene collection.

- Guía completa en español: [GUIA.md](GUIA.md)
- Requires OBS Studio 31.1 or newer (Windows, macOS)

## Build

```bash
cmake --preset macos        # or windows-x64
cmake --build --preset macos
```

Built on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). License: GPL-2.0.
