// The poll editor, served by the plugin at /enquete-editor and shown in the
// web panel: opens a poll, follows the votes live, closes it, and sets the
// look. Votes come from "!voto N" on the chats, counted by the plugin.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
const DURATIONS = [30, 60, 120, 180, 300, 600, 0];
const FONTS = ["Poppins", "Montserrat", "Nunito", "Fredoka", "Rubik", "Inter", "Roboto", "Lilita One",
  "Bebas Neue", "Oswald", "Press Start 2P", "Arial", "Verdana"];
let S = {};
let state = null; // {config, poll, texts}
let deadline = 0;
const draft = { question: "", options: ["", "", "", "", "", ""], seconds: 60 };

// ---- helpers ----
function t(key, ...args) {
  let s = S["Poll." + key];
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
  const known = { question: t("NeedQuestion"), options: t("NeedOptions"), duration: t("NeedDuration") };
  setSave("bad", e.message === "forbidden" ? t("BadToken") : known[e.message] || t("SaveFailed", e.message));
}

function take(msg) {
  state = msg;
  deadline = Date.now() + ((msg.poll && msg.poll.remainingMs) || 0);
  preview();
}

// ---- saving the look ----
let saveTimer = null;
function changed() {
  setSave("saving");
  clearTimeout(saveTimer);
  saveTimer = setTimeout(async () => {
    try {
      take(await post("/api/poll-config", state.config));
      setSave("ok");
    } catch (e) {
      failed(e);
    }
  }, 500);
  preview();
}

// ---- preview: the poll as it is now, or a made-up one ----
function sample() {
  return { question: t("SampleQuestion"), open: true, timed: true, total: 10, winner: 2,
    options: [{ text: t("SampleA"), votes: 3 }, { text: t("SampleB"), votes: 5 }, { text: t("SampleC"), votes: 2 }] };
}

function preview() {
  const box = document.getElementById("ppoll");
  const real = state.poll && state.poll.options;
  PollRender.apply(box, state.config);
  PollRender.draw(box, real ? state.poll : sample(), state.texts, real ? Math.max(0, deadline - Date.now()) : 45000);
  document.getElementById("stopBtn").disabled = !(real && state.poll.open);
  document.getElementById("clearBtn").disabled = !real;
  document.getElementById("previewHint").textContent = !real ? t("PreviewSample")
    : state.poll.open ? t("PreviewOpen", state.poll.total) : t("PreviewClosed");
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

function textCtl(value, placeholder, onChange) {
  const input = el("input", { type: "text", value, placeholder, spellcheck: "false" });
  input.addEventListener("input", () => onChange(input.value));
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

// ---- form ----
function renderForm() {
  const c = state.config;
  const duration = el("select", {}, DURATIONS.map((s) => el("option", { value: s,
    text: s === 0 ? t("NoLimit") : s < 60 ? t("Seconds", s) : t("Minutes", s / 60) })));
  duration.value = draft.seconds;
  duration.addEventListener("change", () => { draft.seconds = Number(duration.value); });
  const start = el("button", { type: "button", class: "btn small primary", text: t("Start") });
  start.addEventListener("click", async () => {
    if (state.poll && state.poll.open && !confirm(t("ReplaceConfirm"))) return;
    setSave("saving");
    try {
      take(await post("/api/poll-start", { question: draft.question, options: draft.options, seconds: draft.seconds }));
      setSave("ok", t("Started"));
    } catch (e) {
      failed(e);
    }
  });
  const newCard = el("section", { class: "card" }, el("h3", { text: t("New") }),
    el("p", { class: "hint", text: t("NewNote") }),
    row(t("Question"), textCtl(draft.question, t("QuestionPlaceholder"), (v) => { draft.question = v; })),
    draft.options.map((o, i) => row(t("Option", i + 1),
      textCtl(o, i < 2 ? t("Required") : t("Optional"), (v) => { draft.options[i] = v; }))),
    row(t("Duration"), duration),
    row("", el("div", { class: "inline" }, start)));

  const chatCard = el("section", { class: "card" }, el("h3", { text: t("Chat") }),
    row(t("Announce"), switchCtl(c.announce, (v) => { c.announce = v; changed(); }), t("AnnounceNote")),
    row(t("ResultSeconds"), rangeCtl(0, 120, 5, c.resultSeconds, " s", (v) => { c.resultSeconds = v; changed(); }), t("ResultSecondsNote")),
    row(t("Link"), copyCtl(location.origin + "/enquete")));

  const font = el("input", { type: "text", value: c.font, list: "fonts", spellcheck: "false" });
  font.addEventListener("change", () => { c.font = font.value.trim() || "Poppins"; changed(); });
  const lookCard = el("section", { class: "card" }, el("h3", { text: t("Look") }),
    row(t("Font"), font),
    row(t("FontSize"), rangeCtl(12, 72, 1, c.fontSize, " px", (v) => { c.fontSize = v; changed(); })),
    row(t("TextColor"), colorCtl(c.textColor, (v) => { c.textColor = v; changed(); })),
    row(t("Accent"), colorCtl(c.accent, (v) => { c.accent = v; changed(); })),
    row(t("Bubble"), el("div", { class: "inline" },
      colorCtl(c.bubbleColor, (v) => { c.bubbleColor = v; changed(); }),
      rangeCtl(0, 100, 5, c.bubbleOpacity, "%", (v) => { c.bubbleOpacity = v; changed(); }))),
    row(t("Shadow"), switchCtl(c.shadow, (v) => { c.shadow = v; changed(); })));

  document.getElementById("form").replaceChildren(el("h2", { text: t("Title") }), newCard, chatCard, lookCard);
}

// Votes arriving while the editor is open.
function listen() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (event) => {
    let msg;
    try {
      msg = JSON.parse(event.data);
    } catch (e) {
      return;
    }
    // The look being edited stays as typed; only the poll comes in.
    if (msg.type === "poll" && state) take({ ...msg, config: state.config });
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
    state = await api("/api/poll");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : t("LoadFailed", e.message));
    return;
  }
  if (state.poll && state.poll.options) {
    draft.question = state.poll.question;
    state.poll.options.forEach((o, i) => { draft.options[i] = o.text; });
  }
  document.body.append(el("datalist", { id: "fonts" }, FONTS.map((f) => el("option", { value: f }))));
  document.getElementById("app").hidden = false;
  document.getElementById("stopBtn").addEventListener("click", async () => {
    try {
      take(await post("/api/poll-stop"));
    } catch (e) {
      failed(e);
    }
  });
  document.getElementById("clearBtn").addEventListener("click", async () => {
    try {
      take(await post("/api/poll-clear"));
    } catch (e) {
      failed(e);
    }
  });
  renderForm();
  preview();
  setInterval(() => { if (state.poll && state.poll.open && state.poll.timed) preview(); }, 500);
  new ResizeObserver(fit).observe(document.getElementById("frame"));
  listen();
}

init();
