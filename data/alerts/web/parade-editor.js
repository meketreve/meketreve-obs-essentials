// The chat parade tab of the web panel. The parade runs on the bot's own
// server; its look comes and goes through /api/tab/desfile on this one.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
let S = {};
let state = null;

function t(key, ...args) {
  let s = S["Texuguito.Parade." + key];
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
  saveTimer = setTimeout(save, 300);
}

async function save() {
  try {
    const { scale, speed, names, nameSize } = state;
    state = await api("/api/tab/desfile", { method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ scale, speed, names, nameSize }) });
    setSave("ok");
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
  }
}

function row(label, control, note) {
  return el("div", { class: "row" }, el("label", { text: label }), control, note ? el("div", { class: "note", text: note }) : null);
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

function switchCtl(checked, onChange) {
  const input = el("input", { type: "checkbox" });
  input.checked = checked;
  input.addEventListener("change", () => onChange(input.checked));
  return el("label", { class: "switch" }, input, el("span"));
}

function renderForm() {
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
    el("h2", { text: t("Title") }),
    el("section", { class: "card" }, el("h3", { text: t("Look") }),
      row(t("Scale"), rangeCtl(0.5, 4, 0.5, state.scale, "×", (v) => { state.scale = v; changed(); }), t("ScaleNote")),
      row(t("Speed"), rangeCtl(0.25, 3, 0.25, state.speed, "×", (v) => { state.speed = v; changed(); })),
      row(t("Names"), switchCtl(state.names, (v) => { state.names = v; changed(); })),
      row(t("NameSize"), rangeCtl(6, 32, 1, state.nameSize, " px", (v) => { state.nameSize = v; changed(); }))),
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
    state = await api("/api/tab/desfile");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : e.message === "not found" ? t("Missing") : t("LoadFailed", e.message));
    return;
  }
  document.getElementById("app").hidden = false;
  // The real parade, live: the look updates through its own connection.
  document.getElementById("preview").src = state.url;
  renderForm();
}

init();
