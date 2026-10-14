# Unified Chat (fork personal)

Fork de Chatterino 2 que muestra, en el split de tu canal de Twitch, los
mensajes del chat de un canal de Kick y de una transmisión en vivo de YouTube.
Cada mensaje lleva un icono de la plataforma de origen delante del nombre.
Solo lectura: no se envían mensajes a Kick ni a YouTube.

Con la opción apagada (el default) el fork se comporta exactamente igual que
Chatterino original y no hace ninguna petición a Kick ni a YouTube.

## Uso

1. Settings → **Unified Chat** → activar "Activar chat unificado".
2. Canal de Twitch objetivo: vacío usa tu propio canal (la cuenta con la que
   iniciaste sesión).
3. Kick: activar y escribir el slug del canal (lo que va en `kick.com/<slug>`).
   Si Kick bloquea la consulta automática del chatroom id, se puede escribir a
   mano.
4. YouTube: activar y escribir `@handle`, la URL del canal, la URL del video o
   el video ID. Con un handle o canal se busca la transmisión en vivo
   automáticamente y se reintenta cada 60 s si no hay ninguna. La API key de
   YouTube Data v3 es opcional: solo se usa como respaldo si el método interno
   deja de funcionar, y consume cuota diaria.

Los cambios se aplican sin reiniciar. Para ver los logs del módulo:

```
QT_LOGGING_RULES="chatterino.unified-chat*=true" chatterino
```

## Compilar en macOS

```
brew install boost openssl@3 rapidjson cmake ninja qt@6
git clone --recurse-submodules https://github.com/h0kd/chatterino2.git
cd chatterino2 && git checkout unified-chat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@6 -DBUILD_WITH_QT6=ON
cmake --build build
open build/bin/chatterino.app
```

Formato antes de cada commit (CI usa clang-format 19):

```
uv tool install clang-format==19.1.7
clang-format -i <archivos tocados>
uv run --python 3.12 --no-project scripts/make_luals_meta.py   # si se tocó MessageElementFlag
```

## Build de Windows

Cada push a la rama `unified-chat` corre el workflow **Build** en GitHub
Actions del fork. En la pestaña Actions, abrir el run y descargar el artifact
`chatterino-windows-x86-64-Qt-6.8.3.zip` (versión portable, sin instalador).
Un push nuevo cancela el run anterior.

## Rebase sobre upstream

```
git fetch upstream
git rebase upstream/master
```

Todos los puntos de contacto con el código original están marcados con el
comentario `// UNIFIED-CHAT` (o `# UNIFIED-CHAT` en CMake y YAML):

| Archivo                                       | Qué toca                                                 |
| --------------------------------------------- | -------------------------------------------------------- |
| `.github/workflows/build.yml`                 | Trigger de push para la rama `unified-chat`              |
| `src/Application.hpp` / `src/Application.cpp` | Crea, inicializa y destruye `UnifiedChatController`      |
| `src/CMakeLists.txt`                          | Lista de archivos nuevos                                 |
| `src/common/QLogging.hpp` / `.cpp`            | Categoría de log `chatterino.unified-chat`               |
| `src/messages/MessageElement.hpp`             | Flag `BadgePlatform` (y `docs/lua-meta/globals.lua`)     |
| `src/messages/MessageBuilder.cpp`             | Icono de Twitch antes de los badges en el canal objetivo |
| `src/singletons/Settings.hpp`                 | Settings `unifiedChat*`                                  |
| `src/widgets/dialogs/SettingsDialog.cpp`      | Registro de la página "Unified Chat"                     |

Código nuevo, sin dependencias del resto más allá de las APIs públicas:

- `src/providers/unified/` — controller, mensaje normalizado, icono de plataforma.
- `src/providers/kick/` — cliente Pusher de Kick y parser.
- `src/providers/youtube/` — cliente Innertube / Data API de YouTube y parser.
- `src/widgets/settingspages/UnifiedChatPage.*` — página de settings.
- `resources/unified/*.png` — iconos (36×36; reemplazar para cambiar el aspecto).

## Si una plataforma deja de funcionar

- **Kick**: la app key de Pusher rota. El cliente la renegocia solo contra
  `web.kick.com/api/v1/realtime/connection` al recibir el error 4001; si eso
  también falla, revisar qué hace hoy el front de kick.com y actualizar
  `DEFAULT_PUSHER_KEY` en `KickChatClient.cpp`.
- **YouTube**: el formato de Innertube cambia sin aviso. Todo el parseo está
  en `YouTubeChatParser.cpp`. Mientras tanto se puede usar una API key de
  Data v3 como respaldo.
