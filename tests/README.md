# Developer tools

Local-only helpers. They are **not** part of the plugin and CI never builds them.

```bash
cmake -S . -B build_dev -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_FRONTEND_API=ON -DENABLE_QT=ON -DENABLE_DEV_TOOLS=ON
cmake --build build_dev -j"$(nproc)"
(cd build_dev && ctest --output-on-failure)       # unit tests
```

| Tool | What it does |
| --- | --- |
| `chat-probe <twitch\|youtube\|kick\|tiktok> <channel> [seconds]` | Runs one chat connector and prints states and messages. Exit 0 if a message arrived. |
| `dock-harness <widget> <out.png> [seconds] [--locale pt-BR] [--config dir]` | Shows a plugin widget outside OBS and saves a screenshot. Use `QT_QPA_PLATFORM=offscreen`. |
| `test-*` | QtTest unit tests for the pure logic (parsers, config codec). |

Channels that are usually live: Twitch `xqc`, Kick `westcol`, YouTube `@LofiGirl` (24/7).
For TikTok, look one up with `api-live/user/room` (status 2 = live) and avoid bursts
of requests (TikTok answers 403 for a while).
