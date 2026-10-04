// Draws the subathon overlay (/subathon) and the subathon editor's preview:
// the title, the time left as H:MM:SS and "paused" or "ended" under it, plus
// a "+1:00" that floats up when time is added. Uses ChatRender for the fonts.
"use strict";

const SubathonRender = (() => {
  function rgba(hex, opacity) {
    const n = parseInt((hex || "#000000").slice(1), 16);
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${Math.max(0, Math.min(100, opacity)) / 100})`;
  }

  function apply(box, config) {
    ChatRender.loadFont(config.font);
    box.classList.add("sb-box");
    box.classList.toggle("sb-shadow", !!config.shadow);
    box.style.setProperty("--sb-font", `"${config.font}", "Poppins", sans-serif`);
    box.style.setProperty("--sb-size", config.fontSize + "px");
    box.style.setProperty("--sb-text", config.textColor);
    box.style.setProperty("--sb-accent", config.accent);
    box.style.setProperty("--sb-bubble", rgba(config.bubbleColor, config.bubbleOpacity));
  }

  // 3725000 -> "1:02:05"; seconds round up, so 0:00:00 means it is over.
  function clock(ms) {
    const s = Math.ceil(Math.max(0, ms) / 1000);
    return `${Math.floor(s / 3600)}:${String(Math.floor(s / 60) % 60).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`;
  }

  // 90 -> "+1:30", -60 -> "−1:00".
  function delta(seconds) {
    const s = Math.abs(seconds);
    const text = s >= 3600 ? clock(s * 1000) : `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
    return (seconds < 0 ? "−" : "+") + text;
  }

  function part(card, className) {
    let node = card.querySelector("." + className);
    if (!node) {
      node = document.createElement("div");
      node.className = className;
      card.append(node);
    }
    return node;
  }

  // state: idle|running|paused|ended; texts: {paused, ended}.
  function draw(box, config, state, remainingMs, texts) {
    if (state === "idle") {
      box.replaceChildren();
      return;
    }
    let card = box.querySelector(".sb-card");
    if (!card) {
      card = document.createElement("div");
      box.replaceChildren(card);
    }
    const ended = state === "ended" || (state === "running" && remainingMs <= 0);
    card.className = "sb-card" + (ended ? " sb-ended" : state === "paused" ? " sb-paused" : "");
    const title = part(card, "sb-title");
    title.textContent = config.title || "";
    title.hidden = !config.title;
    part(card, "sb-clock").textContent = clock(remainingMs);
    const note = part(card, "sb-note");
    note.textContent = ended ? texts.ended : state === "paused" ? texts.paused : "";
    note.hidden = !note.textContent;
  }

  function added(box, seconds) {
    const card = box.querySelector(".sb-card");
    if (!card || !seconds) return;
    const node = document.createElement("div");
    node.className = "sb-added";
    node.textContent = delta(seconds);
    card.append(node);
    setTimeout(() => node.remove(), 2500);
  }

  return { apply, draw, added, clock };
})();
