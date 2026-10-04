// The subathon Browser Source: counts down here between messages, so the
// clock moves every second without the plugin sending each one.
"use strict";

const box = document.getElementById("box");
let state = null;
let deadline = 0;

function remaining() {
  if (!state) return 0;
  return state.timer.state === "running" ? Math.max(0, deadline - Date.now()) : state.timer.remainingMs;
}

function draw() {
  if (!state) return;
  SubathonRender.apply(box, state.config);
  SubathonRender.draw(box, state.config, state.timer.state, remaining(), state.texts);
}

function take(msg) {
  state = msg;
  deadline = Date.now() + msg.timer.remainingMs;
  draw();
  if (msg.added) SubathonRender.added(box, msg.added);
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
    if (msg.type === "config" && msg.subathon) take(msg.subathon);
    else if (msg.type === "subathon") take(msg);
  };
  ws.onclose = () => setTimeout(connect, 2000);
}

setInterval(draw, 250);
connect();
