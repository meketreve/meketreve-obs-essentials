// The chat bot tab of the web panel: sounds and their price, the wait per
// price, the volume and the chat-made commands. Every change is one "action"
// sent to /api/tab/bot, which answers the new state.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
let S = {};
let state = null;

function t(key, ...args) {
  let s = S["Texuguito.BotPanel." + key];
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

async function act(body, { redraw = true } = {}) {
  setSave("saving");
  try {
    state = await api("/api/tab/bot", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
    if (state.error) setSave("bad", state.error);
    else setSave("ok");
    if (redraw) render();
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
  }
}

let settingsTimer = null;
function saveSettings() {
  clearTimeout(settingsTimer);
  settingsTimer = setTimeout(() => act({ action: "settings", volume: state.volume, cooldowns: state.cooldowns }, { redraw: false }), 400);
}

function row(label, control, note) {
  return el("div", { class: "row" }, el("label", { text: label }), control, note ? el("div", { class: "note", text: note }) : null);
}

// What is typed in the import card survives the redraws.
const importDraft = { url: "", price: null, name: "" };
const PRICES = [0, 10, 20, 50, 100, 200, 300, 500, 1000];
let importTimer = null;

// While a link downloads, ask how it is going until it is done.
function followImport(before) {
  clearTimeout(importTimer);
  const now = state.import || {};
  if (now.state === "downloading") {
    setSave("saving", t("Downloading"));
    importTimer = setTimeout(async () => {
      try {
        state = await api("/api/tab/bot");
        render();
        followImport(now);
      } catch (e) {
        setSave("bad", t("SaveFailed", e.message));
      }
    }, 800);
  } else if (before && before.state === "downloading") {
    if (now.state === "done") {
      setSave("ok", t("Imported", now.name, now.price));
      importDraft.url = "";
      importDraft.name = "";
      render();
    } else if (now.state === "error") {
      setSave("bad", now.message);
    }
  }
}

function soundRow(s) {
  const price = el("input", { type: "number", min: 0, max: state.maxPrice, step: 1, value: s.price, class: "num", title: t("MovePrice") });
  price.addEventListener("change", () => act({ action: "price", name: s.name, price: Number(price.value) }));
  return el("div", { class: "list-row" },
    el("code", { text: "!tocar " + s.name }),
    el("div", { class: "inline" }, price, el("span", { class: "muted", text: t("Points") }),
      el("button", { type: "button", class: "btn small", text: t("Test"), onclick: () => act({ action: "test", name: s.name }, { redraw: false }) }),
      el("button", { type: "button", class: "btn small", text: t("Rename"),
        onclick: () => {
          const next = prompt(t("RenamePrompt", s.name), s.name);
          if (next !== null && next.trim() && next.trim() !== s.name) act({ action: "rename", name: s.name, newName: next });
        } }),
      el("button", { type: "button", class: "btn small", text: t("Remove"),
        onclick: () => { if (confirm(t("RemoveConfirm", s.name))) act({ action: "remove", name: s.name }); } })));
}

// One block per price: its wait between sounds, then its sounds.
function priceGroup(price, sounds) {
  const wait = el("input", { type: "number", min: 0, max: 3600, step: 5, value: state.cooldowns[price], class: "num" });
  wait.addEventListener("change", () => { state.cooldowns[price] = Number(wait.value); saveSettings(); });
  return el("div", { class: "price-group" },
    el("div", { class: "price-head" }, el("h4", { text: t("PricePoints", price) }),
      el("div", { class: "inline" }, el("span", { class: "muted", text: t("Wait") }), wait, el("span", { class: "muted", text: "s" }))),
    sounds.map(soundRow));
}

function importCard() {
  const prices = [...new Set([...PRICES, ...state.sounds.map((s) => s.price)])].filter((p) => p <= state.maxPrice).sort((a, b) => a - b);
  if (importDraft.price === null) importDraft.price = state.sounds.length ? state.sounds[0].price : 50;
  const url = el("input", { type: "url", value: importDraft.url, placeholder: t("ImportUrlPlaceholder"), spellcheck: "false" });
  url.addEventListener("input", () => { importDraft.url = url.value; });
  const price = el("select", {}, prices.map((p) => el("option", { value: p, text: t("PriceOption", p) })));
  price.value = importDraft.price;
  price.addEventListener("change", () => { importDraft.price = Number(price.value); });
  const name = el("input", { type: "text", value: importDraft.name, placeholder: t("ImportNamePlaceholder"), spellcheck: "false" });
  name.addEventListener("input", () => { importDraft.name = name.value; });
  const busy = state.import && state.import.state === "downloading";
  const go = el("button", { type: "button", class: "btn small primary", text: busy ? t("Downloading") : t("ImportButton"), disabled: busy });
  go.addEventListener("click", async () => {
    if (!importDraft.url.trim()) return;
    const before = { state: "downloading" };
    await act({ action: "import", url: importDraft.url, price: importDraft.price, name: importDraft.name });
    if (!state.error) followImport(before);
  });
  return el("section", { class: "card" }, el("h3", { text: t("Import") }), el("p", { class: "hint", text: t("ImportNote") }),
    row(t("ImportUrl"), url),
    row(t("ImportPrice"), price),
    row(t("ImportName"), name, t("ImportNameNote")),
    row("", el("div", { class: "inline" }, go)));
}

function render() {
  const volumeOut = el("span", { class: "value", text: state.volume + "%" });
  const volume = el("input", { type: "range", min: 0, max: 100, step: 5, value: state.volume });
  volume.addEventListener("input", () => { state.volume = Number(volume.value); volumeOut.textContent = volume.value + "%"; saveSettings(); });

  const byPrice = new Map();
  for (const s of state.sounds) {
    if (!byPrice.has(s.price)) byPrice.set(s.price, []);
    byPrice.get(s.price).push(s);
  }
  const groups = [...byPrice.keys()].sort((a, b) => a - b)
    .map((p) => priceGroup(p, byPrice.get(p).sort((a, b) => a.name.localeCompare(b.name))));

  const commandRows = state.commands.map((c) => {
    const reply = el("input", { type: "text", value: c.reply, spellcheck: "false" });
    return el("div", { class: "list-row" },
      el("code", { text: "!" + c.name }),
      el("div", { class: "inline grow" }, reply,
        el("button", { type: "button", class: "btn small", text: t("Save"), onclick: () => act({ action: "command", name: c.name, reply: reply.value }) }),
        el("button", { type: "button", class: "btn small", text: t("Remove"),
          onclick: () => { if (confirm(t("RemoveCommandConfirm", "!" + c.name))) act({ action: "deleteCommand", name: c.name }); } })));
  });
  const newName = el("input", { type: "text", placeholder: t("NewName"), spellcheck: "false", class: "name" });
  const newReply = el("input", { type: "text", placeholder: t("NewReply"), spellcheck: "false" });
  const create = el("button", { type: "button", class: "btn small primary", text: t("Create"),
    onclick: () => act({ action: "command", name: newName.value, reply: newReply.value }) });

  document.getElementById("form").replaceChildren(
    el("h2", { text: t("Tab") }),
    el("section", { class: "card" }, el("h3", { text: t("Sounds") }),
      row(t("Volume"), el("div", { class: "inline" }, volume, volumeOut)),
      el("p", { class: "hint", text: state.sounds.length ? t("SoundsNote") : t("NoSounds") }),
      groups),
    importCard(),
    el("section", { class: "card" }, el("h3", { text: t("Commands") }), el("p", { class: "hint", text: t("CommandsNote") }),
      commandRows, el("div", { class: "list-row" }, newName, el("div", { class: "inline grow" }, newReply, create))));
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
    state = await api("/api/tab/bot");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : e.message === "not found" ? t("Missing") : t("LoadFailed", e.message));
    return;
  }
  document.getElementById("app").hidden = false;
  render();
  followImport(null);
}

init();
