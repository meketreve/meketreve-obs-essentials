// The Now Playing tab of the web panel. The overlay runs on its own server
// (so its 30 fps bars stay off the other overlays); its settings come and go
// through /api/tab/tocando on this one.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
let S = {};
let state = null;

function t(key, ...args) {
  let s = S["NowPlaying.Panel." + key];
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

let saveTimer = null;
function changed() {
  setSave("saving");
  clearTimeout(saveTimer);
  saveTimer = setTimeout(save, 400);
}

async function save() {
  try {
    const { color, card, bars, always, source } = state;
    state = await api("/api/tab/tocando", { method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ color, card, bars, always, source }) });
    setSave("ok");
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
  }
}

function row(label, control, note) {
  return el("div", { class: "row" }, el("label", { text: label }), control, note ? el("div", { class: "note", text: note }) : null);
}

function switchCtl(checked, onChange) {
  const input = el("input", { type: "checkbox" });
  input.checked = checked;
  input.addEventListener("change", () => onChange(input.checked));
  return el("label", { class: "switch" }, input, el("span"));
}

function renderForm() {
  const source = el("select", {}, state.sources.map((s) => el("option", { value: s.name, text: s.label })));
  source.value = state.source;
  source.addEventListener("change", () => { state.source = source.value; changed(); });
  const color = el("input", { type: "color", value: state.color });
  color.addEventListener("input", () => { state.color = color.value.toUpperCase(); changed(); });
  const copy = el("button", { type: "button", class: "btn small", text: t("Copy") });
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(state.url);
      copy.textContent = t("Copied");
    } catch (e) {
      copy.textContent = state.url;
    }
  });
  document.getElementById("form").replaceChildren(
    el("h2", { text: S["NowPlaying.Title"] || "Now playing" }),
    el("section", { class: "card" }, el("h3", { text: t("Sound") }),
      row(t("Source"), source, t("SourceNote"))),
    el("section", { class: "card" }, el("h3", { text: t("Look") }),
      row(t("Color"), color),
      row(t("Card"), switchCtl(state.card, (v) => { state.card = v; changed(); }), t("CardNote")),
      row(t("Bars"), switchCtl(state.bars, (v) => { state.bars = v; changed(); })),
      row(t("Always"), switchCtl(state.always, (v) => { state.always = v; changed(); }), t("AlwaysNote"))),
    el("section", { class: "card" }, el("h3", { text: t("Link") }),
      row(t("BrowserSource"), el("div", { class: "inline" }, el("code", { text: state.url }), copy), t("LinkNote"))));
}

async function init() {
  try {
    S = await api("/api/i18n");
  } catch (e) {
    S = {};
  }
  document.querySelectorAll("[data-i18n]").forEach((n) => { n.textContent = S[n.dataset.i18n] || n.textContent; });
  document.documentElement.lang = S.lang === "pt" ? "pt-BR" : "en";
  if (!token) {
    fatal(t("NoToken"));
    return;
  }
  try {
    state = await api("/api/tab/tocando");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : e.message === "not found" ? t("Missing") : t("LoadFailed", e.message));
    return;
  }
  document.getElementById("app").hidden = false;
  // The real overlay, live: the look updates through its own connection.
  document.getElementById("preview").src = state.url + "?always=1";
  renderForm();
}

init();
