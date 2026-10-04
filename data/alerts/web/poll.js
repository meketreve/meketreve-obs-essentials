// The poll Browser Source: shows the open poll with the time left, then the
// result for a while ("resultSeconds"), then nothing until the next one.
"use strict";

const box = document.getElementById("box");
let state = null;
let deadline = 0;

function draw() {
  if (!state) return;
  const poll = state.poll;
  const hideAt = poll && !poll.open && state.config.resultSeconds > 0 ? poll.closedAt + state.config.resultSeconds * 1000 : 0;
  PollRender.apply(box, state.config);
  PollRender.draw(box, hideAt && Date.now() > hideAt ? null : poll, state.texts, Math.max(0, deadline - Date.now()));
}

function take(msg) {
  state = msg;
  deadline = Date.now() + ((msg.poll && msg.poll.remainingMs) || 0);
  draw();
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
    if (msg.type === "config" && msg.poll) take(msg.poll);
    else if (msg.type === "poll") take(msg);
  };
  ws.onclose = () => setTimeout(connect, 2000);
}

setInterval(draw, 250);
connect();
