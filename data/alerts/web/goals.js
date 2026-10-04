// The goals Browser Source. Without "?meta" it shows every goal; with
// "?meta=<id>" only that one.
"use strict";

const box = document.getElementById("box");
const only = new URLSearchParams(location.search).get("meta");

function draw(msg) {
  GoalsRender.apply(box, msg.config);
  GoalsRender.draw(box, msg.config, msg.lang, only);
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
    if (msg.type === "config" && msg.goals) draw(msg.goals);
    else if (msg.type === "goals") draw(msg);
  };
  ws.onclose = () => setTimeout(connect, 2000);
}

connect();
