// The subathon editor, served by the plugin at /subathon-editor and shown in
// the web panel: starts, pauses and changes the clock, sets how much time
// each event adds, and the look.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
const PER = [["perSub", "PerSub"], ["perGiftSub", "PerGiftSub"], ["perBits100", "PerBits"],
  ["perDonation", "PerDonation"], ["perMember", "PerMember"], ["perFollow", "PerFollow"]];
const FONTS = ["Poppins", "Montserrat", "Nunito", "Fredoka", "Rubik", "Inter", "Roboto", "Lilita One",
  "Bebas Neue", "Oswald", "Press Start 2P", "Arial", "Verdana"];
let S = {};
let state = null; // {config, timer, texts}
let deadline = 0;

// ---- helpers ----
function t(key, ...args) {
  let s = S["Subathon." + key];
  if (s === undefined) s = S["Alerts.Editor." + key];
  if (s === undefined) s = key;
  args.forEach((a, i) => { s = s.replace("%" + (i + 1), a); });
  return s;
}

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === "class") node.className = v;
    else if (k === "text") node.textContent = v;
    else if (k.startsWith("on")) node.addEventListener(k.slice(2), v);
    else if (v !== undefined && v !== null && v !== false) node.setAttribute(k, v === true ? "" : v);
  }
  for (const c of children.flat()) if (c !== null && c !== undefined && c !== false) node.append(c);
  return node;
}

async function api(path, options = {}) {
  const res = await fetch(path, { ...options, headers: { "X-Token": token, ...(options.headers || {}) } });
  const body = await res.json().catch(() => ({}));
  if (res.status === 403) throw new Error("forbidden");
  if (!res.ok) throw new Error(body.error || String(res.status));
  return body;
}

function post(path, body) {
  return api(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body || {}) });
}

function fatal(message) {
  const box = document.getElementById("fatal");
  box.textContent = message;
  box.hidden = false;
  document.getElementById("app").hidden = true;
}

function setSave(kind, text) {
  const node = document.getElementById("saveState");
  node.className = "save " + (kind === "ok" ? "ok" : kind === "bad" ? "bad" : "");
  node.textContent = text || (kind === "ok" ? t("Saved") : kind === "saving" ? t("Saving") : "");
}

function failed(e) {
  setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
}

function remaining() {
  return state.timer.state === "running" ? Math.max(0, deadline - Date.now()) : state.timer.remainingMs;
}

// Only the clock comes in: the settings being typed stay.
function takeTimer(msg) {
  const before = state.timer.state;
  state.timer = msg.timer;
  deadline = Date.now() + msg.timer.remainingMs;
  if (msg.added) SubathonRender.added(document.getElementById("ptimer"), msg.added);
  if (before !== msg.timer.state) renderForm();
  preview();
}

async function control(action, seconds) {
  try {
    takeTimer(await post("/api/subathon-control", { action, seconds }));
  } catch (e) {
    failed(e);
  }
}

// ---- saving the settings ----
let saveTimer = null;
function changed() {
  setSave("saving");
  clearTimeout(saveTimer);
  saveTimer = setTimeout(async () => {
    try {
      takeTimer(await post("/api/subathon-config", state.config));
      setSave("ok");
    } catch (e) {
      failed(e);
    }
  }, 500);
  preview();
}

// ---- preview ----
function preview() {
  // Over here first: the plugin only says so when something changes.
  if (state.timer.state === "running" && remaining() <= 0) {
    state.timer = { state: "ended", remainingMs: 0 };
    renderForm();
  }
  const box = document.getElementById("ptimer");
  const idle = state.timer.state === "idle";
  SubathonRender.apply(box, state.config);
  // Before it starts, a made-up clock shows the look.
  SubathonRender.draw(box, state.config, idle ? "running" : state.timer.state, idle ? 3 * 3600 * 1000 : remaining(), state.texts);
  document.getElementById("previewHint").textContent = idle ? t("PreviewIdle") : t("PreviewLive");
  const clock = document.getElementById("liveClock");
  if (clock) clock.textContent = SubathonRender.clock(remaining());
  fit();
}

// The preview is a 480 px wide source, scaled down to the frame.
function fit() {
  const frame = document.getElementById("frame");
  const inner = document.getElementById("pscale");
  const scale = Math.min(1, (frame.clientWidth - 12) / 480);
  inner.style.transform = `scale(${scale})`;
  frame.style.height = Math.ceil(inner.offsetHeight * scale) + 12 + "px";
}

// ---- controls ----
function row(label, control, note) {
  return el("div", { class: "row" }, el("label", { text: label }), control, note ? el("div", { class: "note", text: note }) : null);
}

function switchCtl(checked, onChange) {
  const input = el("input", { type: "checkbox" });
  input.checked = checked;
  input.addEventListener("change", () => onChange(input.checked));
  return el("label", { class: "switch" }, input, el("span"));
}

function rangeCtl(min, max, step, value, unit, onChange) {
  const out = el("span", { class: "value", text: value + unit });
  const input = el("input", { type: "range", min, max, step, value });
  input.addEventListener("input", () => {
    out.textContent = input.value + unit;
    onChange(Number(input.value));
  });
  return el("div", { class: "inline" }, input, out);
}

function colorCtl(value, onChange) {
  const input = el("input", { type: "color", value });
  input.addEventListener("input", () => onChange(input.value.toUpperCase()));
  return input;
}

function copyCtl(url) {
  const copy = el("button", { type: "button", class: "btn small", text: t("Copy") });
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(url);
      copy.textContent = t("Copied");
    } catch (e) {
      copy.textContent = url;
    }
  });
  return el("div", { class: "inline" }, el("code", { text: url }), copy);
}

// Hours and minutes; answers the seconds.
function durationCtl(hours, minutes) {
  const h = el("input", { type: "number", min: 0, max: 8760, step: 1, value: hours, class: "num" });
  const m = el("input", { type: "number", min: 0, max: 59, step: 1, value: minutes, class: "num" });
  const node = el("div", { class: "inline" }, h, el("span", { class: "muted", text: t("Hours") }), m,
    el("span", { class: "muted", text: t("Minutes") }));
  node.seconds = () => (Number(h.value) || 0) * 3600 + (Number(m.value) || 0) * 60;
  return node;
}

function button(text, onclick, primary) {
  return el("button", { type: "button", class: "btn small" + (primary ? " primary" : ""), text, onclick });
}

// ---- form ----
function renderForm() {
  const c = state.config;
  const s = state.timer.state;
  const timerCard = el("section", { class: "card" }, el("h3", { text: t("Clock") }),
    row(t("State"), el("div", { class: "inline" }, el("span", { class: "value", id: "liveClock", text: SubathonRender.clock(remaining()) }),
      el("span", { class: "muted", text: t("State." + s) }))));
  const startTime = durationCtl(3, 0);
  timerCard.append(row(t("StartWith"), el("div", { class: "inline" }, startTime,
    button(s === "idle" ? t("Start") : t("Restart"), () => {
      if (s !== "idle" && !confirm(t("RestartConfirm"))) return;
      control("start", startTime.seconds());
    }, s === "idle"))));
  if (s === "running") timerCard.append(row("", el("div", { class: "inline" }, button(t("Pause"), () => control("pause")))));
  if (s === "paused") timerCard.append(row("", el("div", { class: "inline" }, button(t("Resume"), () => control("resume"), true))));
  if (s !== "idle") {
    timerCard.append(row(t("AddTime"), el("div", { class: "inline" },
      [-600, -60, 60, 300, 600].map((sec) => button((sec < 0 ? "−" : "+") + Math.abs(sec) / 60 + " min", () => control("add", sec))))));
    const setTime = durationCtl(1, 0);
    timerCard.append(row(t("SetTime"), el("div", { class: "inline" }, setTime, button(t("Set"), () => control("set", setTime.seconds())))));
    timerCard.append(row("", el("div", { class: "inline" }, button(t("Reset"), () => { if (confirm(t("ResetConfirm"))) control("reset"); }))));
  }

  const perCard = el("section", { class: "card" }, el("h3", { text: t("PerEvent") }), el("p", { class: "hint", text: t("PerEventNote") }));
  for (const [k, label] of PER) {
    const input = el("input", { type: "number", min: 0, max: 3600, step: 1, value: c[k], class: "num" });
    input.addEventListener("input", () => { c[k] = Math.max(0, Number(input.value) || 0); changed(); });
    perCard.append(row(t(label), el("div", { class: "inline" }, input, el("span", { class: "muted", text: t("SecondsUnit") }))));
  }

  const title = el("input", { type: "text", value: c.title, placeholder: t("TitlePlaceholder"), spellcheck: "false" });
  title.addEventListener("input", () => { c.title = title.value; changed(); });
  const font = el("input", { type: "text", value: c.font, list: "fonts", spellcheck: "false" });
  font.addEventListener("change", () => { c.font = font.value.trim() || "Poppins"; changed(); });
  const lookCard = el("section", { class: "card" }, el("h3", { text: t("Look") }),
    row(t("ScreenTitle"), title),
    row(t("Font"), font),
    row(t("FontSize"), rangeCtl(12, 200, 2, c.fontSize, " px", (v) => { c.fontSize = v; changed(); })),
    row(t("TextColor"), colorCtl(c.textColor, (v) => { c.textColor = v; changed(); })),
    row(t("Accent"), colorCtl(c.accent, (v) => { c.accent = v; changed(); })),
    row(t("Bubble"), el("div", { class: "inline" },
      colorCtl(c.bubbleColor, (v) => { c.bubbleColor = v; changed(); }),
      rangeCtl(0, 100, 5, c.bubbleOpacity, "%", (v) => { c.bubbleOpacity = v; changed(); }))),
    row(t("Shadow"), switchCtl(c.shadow, (v) => { c.shadow = v; changed(); })),
    row(t("Link"), copyCtl(location.origin + "/subathon")));

  document.getElementById("form").replaceChildren(el("h2", { text: t("Title") }), timerCard, perCard, lookCard);
}

// Time added by events while the editor is open.
function listen() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (event) => {
    let msg;
    try {
      msg = JSON.parse(event.data);
    } catch (e) {
      return;
    }
    if (msg.type === "subathon" && state) takeTimer(msg);
  };
  ws.onclose = () => setTimeout(listen, 2000);
}

async function init() {
  try {
    S = await api("/api/i18n" + location.search);
  } catch (e) {
    S = {};
  }
  document.querySelectorAll("[data-i18n]").forEach((n) => { n.textContent = t(n.dataset.i18n); });
  document.title = "Meketreve · " + t("Title");
  document.documentElement.lang = S.lang === "pt" ? "pt-BR" : "en";
  if (!token) {
    fatal(t("NoToken"));
    return;
  }
  try {
    state = await api("/api/subathon");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : t("LoadFailed", e.message));
    return;
  }
  deadline = Date.now() + state.timer.remainingMs;
  document.body.append(el("datalist", { id: "fonts" }, FONTS.map((f) => el("option", { value: f }))));
  document.getElementById("app").hidden = false;
  renderForm();
  preview();
  setInterval(preview, 500);
  new ResizeObserver(fit).observe(document.getElementById("frame"));
  listen();
}

init();
