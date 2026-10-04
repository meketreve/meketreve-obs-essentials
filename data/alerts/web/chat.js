// The chat-on-screen Browser Source: takes chat lines from the plugin over the
// WebSocket. "?p=twitch,kick" in the link picks the platforms for this source
// only; without it the editor's choice applies.
"use strict";

const box = document.getElementById("box");
box.style.height = "100vh";
const only = new URLSearchParams(location.search).get("p");
const picked = only ? new Set(only.toLowerCase().split(",").map((s) => s.trim())) : null;
let config = null;

function shows(platform) {
  if (picked) return picked.has(platform);
  return !config.platforms || config.platforms[platform] !== false;
}

function connect() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (event) => {
    let msg;
    try {
      msg = JSON.parse(event.data);
    } catch (e) {
      return;
    }
    if ((msg.type === "config" && msg.chat) || msg.type === "chat-config") {
      config = msg.type === "config" ? msg.chat : msg.config;
      ChatRender.apply(box, config);
    } else if (msg.type === "chat" && config && shows(msg.message.platform)) {
      ChatRender.add(box, config, msg.message);
    } else if (msg.type === "chat-remove") {
      ChatRender.remove(box, msg);
    }
  };
  ws.onclose = () => setTimeout(connect, 2000);
}

connect();
