// The events overlay editor, served by the plugin at /eventos-editor. The dock
// opens it with a token after "#"; every API call carries it.
"use strict";

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
const TYPES = ["follow", "sub", "resub", "giftsub", "bits", "donation", "raid", "membership", "gift"];
const LABELS = [["ultimo-follow", "LastFollow"], ["ultimo-sub", "LastSub"], ["ultimo-presente", "LastGift"],
  ["ultima-doacao", "LastDonation"], ["ultimo-raid", "LastRaid"], ["ultimo-bits", "LastBits"],
  ["ultimo-membro", "LastMember"], ["top-doador", "TopDonor"], ["top-bits", "TopBits"]];
const FONTS = ["Poppins", "Montserrat", "Nunito", "Fredoka", "Rubik", "Inter", "Roboto", "Lilita One",
  "Bebas Neue", "Oswald", "Press Start 2P", "Arial", "Verdana"];
let S = {};
let config = null;
let sample = { recent: [], labels: {} };

// ---- helpers ----
function t(key, ...args) {
  let s = S["EventsOverlay." + key];
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
    else if (k === "style") node.style.cssText = v;
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

function setSave(state, text) {
  const node = document.getElementById("saveState");
  node.className = "save " + (state === "ok" ? "ok" : state === "bad" ? "bad" : "");
  node.textContent = text || (state === "ok" ? t("Saved") : state === "saving" ? t("Saving") : "");
}

// ---- saving ----
let saveTimer = null;
function changed() {
  setSave("saving");
  clearTimeout(saveTimer);
  saveTimer = setTimeout(save, 500);
  preview();
}

async function save() {
  try {
    await api("/api/events-config", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(config) });
    setSave("ok");
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
  }
}

// ---- preview: the list of made-up events, then three labels ----
function preview() {
  const list = document.getElementById("plist");
  EventsRender.apply(list, config);
  EventsRender.list(list, config, sample.recent, "");
  const labels = document.getElementById("plabels");
  labels.replaceChildren();
  for (const kind of ["ultimo-sub", "top-doador", "ultimo-follow"]) {
    const one = el("div");
    EventsRender.apply(one, config);
    EventsRender.label(one, config, kind, sample.labels);
    labels.append(one);
  }
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

async function post(path, hintOk) {
  const hint = document.getElementById("liveHint");
  try {
    const r = await api(path, { method: "POST" });
    hint.textContent = hintOk(r);
  } catch (e) {
    hint.textContent = e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message);
  }
}

// ---- controls ----
function row(label, control, note) {
  return el("div", { class: "row" }, el("label", { text: label }), control, note ? el("div", { class: "note", text: note }) : null);
}

function switchCtl(checked, onChange, label) {
  const input = el("input", { type: "checkbox", "aria-label": label || "" });
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

function textCtl(value, onChange) {
  const input = el("input", { type: "text", value, spellcheck: "false" });
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
  const listCard = el("section", { class: "card" }, el("h3", { text: t("List") }),
    el("p", { class: "hint", text: t("ListNote") }));
  for (const type of TYPES) {
    listCard.append(row(S["Alerts.Type." + type] || type,
      switchCtl(config.list.types[type], (v) => { config.list.types[type] = v; changed(); })));
  }
  listCard.append(row(t("ListMax"), rangeCtl(1, 20, 1, config.list.max, "", (v) => { config.list.max = v; changed(); })));
  listCard.append(row(t("ListLink"), copyCtl(location.origin + "/eventos")));

  const labelCard = el("section", { class: "card" }, el("h3", { text: t("Labels") }),
    el("p", { class: "hint", text: t("LabelsNote") }));
  for (const [kind, key] of LABELS) {
    labelCard.append(row(t("Label." + key), textCtl(config.labels[kind], (v) => { config.labels[kind] = v; changed(); })));
    labelCard.append(row("", copyCtl(location.origin + "/eventos?mostrar=" + kind)));
  }
  labelCard.append(row(t("EmptyText"), textCtl(config.emptyText, (v) => { config.emptyText = v; changed(); }), t("EmptyTextNote")));

  const reset = el("button", { type: "button", class: "btn small", text: t("ResetTop") });
  reset.addEventListener("click", () => post("/api/events-reset", () => t("ResetDone")));
  const topCard = el("section", { class: "card" }, el("h3", { text: t("Top") }),
    row(t("ResetOnLive"), switchCtl(config.resetTopOnLive, (v) => { config.resetTopOnLive = v; changed(); }), t("ResetOnLiveNote")),
    row("", reset));

  const font = el("input", { type: "text", value: config.font, list: "fonts", spellcheck: "false" });
  font.addEventListener("change", () => { config.font = font.value.trim() || "Poppins"; changed(); });
  const lookCard = el("section", { class: "card" }, el("h3", { text: t("Look") }),
    row(t("Font"), font),
    row(t("FontSize"), rangeCtl(12, 72, 1, config.fontSize, " px", (v) => { config.fontSize = v; changed(); })),
    row(t("TextColor"), colorCtl(config.textColor, (v) => { config.textColor = v; changed(); })),
    row(t("Accent"), colorCtl(config.accent, (v) => { config.accent = v; changed(); })),
    row(t("Bubble"), el("div", { class: "inline" },
      colorCtl(config.bubbleColor, (v) => { config.bubbleColor = v; changed(); }),
      rangeCtl(0, 100, 5, config.bubbleOpacity, "%", (v) => { config.bubbleOpacity = v; changed(); }))),
    row(t("Shadow"), switchCtl(config.shadow, (v) => { config.shadow = v; changed(); })),
    row(t("ShowPlatform"), switchCtl(config.showPlatform, (v) => { config.showPlatform = v; changed(); })));

  document.getElementById("form").replaceChildren(el("h2", { text: t("Title") }), listCard, labelCard, topCard, lookCard);
}

async function init() {
  try {
    S = await api("/api/i18n");
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
    config = await api("/api/events-config");
    sample = await api("/api/events-sample");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : t("LoadFailed", e.message));
    return;
  }
  document.body.append(el("datalist", { id: "fonts" }, FONTS.map((f) => el("option", { value: f }))));
  document.getElementById("app").hidden = false;
  document.getElementById("liveBtn").addEventListener("click",
    () => post("/api/events-test", (r) => (r.overlays > 0 ? t("SentLive", r.overlays) : t("NoOverlay"))));
  renderForm();
  preview();
  new ResizeObserver(fit).observe(document.getElementById("frame"));
}

init();
