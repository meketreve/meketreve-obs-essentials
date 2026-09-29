// The Browser Source page: takes alerts from the plugin over the WebSocket and
// shows them one at a time.
"use strict";

const stage = document.getElementById("stage");
const queue = [];
let config = null;
let busy = false;

async function drain() {
  if (busy || !config) return;
  busy = true;
  while (queue.length) {
    const { alert, tts } = queue.shift();
    try {
      await AlertRender.show(stage, config, alert, tts);
    } catch (e) {
      console.error("[alerts]", e);
    }
    if (queue.length && config.gap > 0) await new Promise((r) => setTimeout(r, config.gap * 1000));
  }
  busy = false;
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
    if (msg.type === "config") {
      config = msg.config;
      for (const type of Object.values(config.types || {})) AlertRender.loadFont(type.font);
      drain();
    } else if (msg.type === "alert") {
      queue.push({ alert: msg.alert, tts: msg.tts || "" });
      drain();
    } else if (msg.type === "skip") {
      AlertRender.skip();
    }
  };
  ws.onclose = () => setTimeout(connect, 2000);
}

connect();
