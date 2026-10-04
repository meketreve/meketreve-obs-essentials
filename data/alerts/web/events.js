// The events Browser Source. Without "?mostrar" it lists the latest events;
// with "?mostrar=ultimo-sub" (or top-doador, ultimo-raid...) it shows that
// one label.
"use strict";

const box = document.getElementById("box");
const kind = new URLSearchParams(location.search).get("mostrar");
let state = null;

function draw(freshId) {
  if (!state) return;
  EventsRender.apply(box, state.config);
  if (kind) EventsRender.label(box, state.config, kind, state.labels || {});
  else EventsRender.list(box, state.config, state.recent || [], freshId);
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
    if (msg.type === "config" && msg.events) {
      state = msg.events;
      draw();
    } else if (msg.type === "events-history") {
      const newest = msg.recent && msg.recent[0] && msg.recent[0].id;
      const fresh = state && state.recent && state.recent[0] && newest !== state.recent[0].id ? newest : "";
      state = msg;
      draw(fresh);
    } else if (msg.type === "events-test" && state && !kind) {
      // Only on screen for a moment: the real history stays as it is.
      const id = msg.entry.id;
      state = { ...state, recent: [msg.entry, ...(state.recent || [])] };
      draw(id);
      setTimeout(() => {
        state = { ...state, recent: (state.recent || []).filter((e) => e.id !== id) };
        draw();
      }, 8000);
    }
  };
  ws.onclose = () => setTimeout(connect, 2000);
}

connect();
