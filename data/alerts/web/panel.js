// The web panel at /painel: one tab per overlay, each tab the overlay's own
// editor in a frame. The dock opens it with the token after "#"; the tab in
// use goes there too ("#t=...&aba=chat"), so a reload stays on it.
"use strict";

const TABS = [["alertas", "/editor", "Alerts.Title"], ["chat", "/chat-editor", "ChatOverlay.Title"],
  ["eventos", "/eventos-editor", "EventsOverlay.Title"], ["desfile", "/desfile-editor", "Texuguito.Parade.Title"],
  ["bot", "/bot-editor", "Texuguito.BotPanel.Tab"],
  ["tocando", "/tocando-editor", "NowPlaying.Title"]];
const hash = new URLSearchParams(location.hash.slice(1));
const token = hash.get("t") || "";
let S = {};

function open(name) {
  const tab = TABS.find((t) => t[0] === name) || TABS[0];
  document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("active", b.dataset.tab === tab[0]));
  document.getElementById("frame").src = `${tab[1]}?embed=1#t=${encodeURIComponent(token)}`;
  history.replaceState(null, "", `#t=${encodeURIComponent(token)}&aba=${tab[0]}`);
}

async function init() {
  try {
    S = await (await fetch("/api/i18n")).json();
  } catch (e) {
    S = {};
  }
  const title = S["Overlays.Title"] || "Overlays";
  document.getElementById("title").textContent = title;
  document.title = "Meketreve · " + title;
  document.documentElement.lang = S.lang === "pt" ? "pt-BR" : "en";
  const bar = document.getElementById("tabs");
  for (const [name, , key] of TABS) {
    const b = document.createElement("button");
    b.type = "button";
    b.dataset.tab = name;
    b.textContent = S[key] || name;
    b.addEventListener("click", () => open(name));
    bar.append(b);
  }
  open(hash.get("aba") || "alertas");
}

init();
