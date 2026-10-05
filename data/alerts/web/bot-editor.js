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
  settingsTimer = setTimeout(() => act({ action: "settings", volume: state.volume }, { redraw: false }), 400);
}

function row(label, control, note) {
  return el("div", { class: "row" }, el("label", { text: label }), control, note ? el("div", { class: "note", text: note }) : null);
}

// What is typed in the import card survives the redraws.
const importDraft = { url: "", group: "", name: "" };
let importTimer = null;
let dragged = ""; // the sound being dragged

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
      setSave("ok", t("Imported", now.name, now.group));
      importDraft.url = "";
      importDraft.name = "";
      render();
    } else if (now.state === "error") {
      setSave("bad", now.message);
    }
  }
}

function button(text, onclick, extra = "") {
  return el("button", { type: "button", class: "btn small " + extra, text, onclick });
}

// One sound: drag it by its row to another group or place.
function soundRow(group, name, index) {
  const tr = el("tr", { class: "sound-row", draggable: "true" },
    el("td", { class: "grip", text: "⠿", title: t("DragHint") }),
    el("td", { text: name }),
    el("td", {}, el("code", { text: "!tocar " + name })),
    el("td", { class: "sound-actions" },
      button(t("Test"), () => act({ action: "test", name }, { redraw: false })),
      button(t("Rename"), () => {
        const next = prompt(t("RenamePrompt", name), name);
        if (next !== null && next.trim() && next.trim() !== name) act({ action: "rename", name, newName: next });
      }),
      button(t("Remove"), () => { if (confirm(t("RemoveConfirm", name))) act({ action: "remove", name }); })));
  tr.addEventListener("dragstart", (e) => {
    dragged = name;
    e.dataTransfer.effectAllowed = "move";
    e.dataTransfer.setData("text/plain", name);
    tr.classList.add("dragging");
  });
  tr.addEventListener("dragend", () => {
    dragged = "";
    tr.classList.remove("dragging");
    document.querySelectorAll(".drop-before, .drop-into").forEach((n) => n.classList.remove("drop-before", "drop-into"));
  });
  tr.dataset.index = index;
  return tr;
}

// Where a drop lands: before the row under the pointer, or at the end.
function dropTarget(panel, group) {
  const clear = () => panel.querySelectorAll(".drop-before").forEach((n) => n.classList.remove("drop-before"));
  panel.addEventListener("dragover", (e) => {
    if (!dragged) return;
    e.preventDefault();
    clear();
    panel.classList.add("drop-into");
    const row = e.target.closest && e.target.closest(".sound-row");
    if (row && panel.contains(row)) row.classList.add("drop-before");
  });
  panel.addEventListener("dragleave", (e) => {
    if (!panel.contains(e.relatedTarget)) {
      panel.classList.remove("drop-into");
      clear();
    }
  });
  panel.addEventListener("drop", (e) => {
    e.preventDefault();
    const name = dragged || e.dataTransfer.getData("text/plain");
    const row = e.target.closest && e.target.closest(".sound-row");
    let index = row && panel.contains(row) ? Number(row.dataset.index) : group.sounds.length;
    // Inside the same group, the sound's own place goes away first.
    const from = group.sounds.indexOf(name);
    if (from >= 0 && from < index) index -= 1;
    panel.classList.remove("drop-into");
    clear();
    if (name && !(from >= 0 && from === index)) act({ action: "move", name, group: group.id, index });
  });
}

// One panel per group: its switch, name, price and wait on top, then its
// sounds as a table. Changing the price changes it for every sound in it.
function groupPanel(group) {
  const save = (changes) => act({ action: "group", group: group.id, ...changes });
  const on = el("input", { type: "checkbox" });
  on.checked = group.enabled;
  on.addEventListener("change", () => save({ enabled: on.checked }));
  const name = el("input", { type: "text", value: group.name, class: "group-name", spellcheck: "false", maxlength: 40 });
  name.addEventListener("change", () => { if (name.value.trim()) save({ name: name.value }); else name.value = group.name; });
  const price = el("input", { type: "number", min: 0, max: state.maxPrice, step: 1, value: group.price, class: "num" });
  price.addEventListener("change", () => save({ price: Number(price.value) }));
  const wait = el("input", { type: "number", min: 0, max: state.maxCooldown, step: 5, value: group.cooldown, class: "num" });
  wait.addEventListener("change", () => save({ cooldown: Number(wait.value) }));
  const remove = group.sounds.length ? null : button(t("DeleteGroup"), () => act({ action: "deleteGroup", group: group.id }));

  const body = el("tbody", {}, group.sounds.map((s, i) => soundRow(group, s, i)));
  if (!group.sounds.length) body.append(el("tr", { class: "empty-row" }, el("td", { colspan: 4, text: t("EmptyGroup") })));
  const panel = el("section", { class: "group-panel" + (group.enabled ? "" : " off") },
    el("div", { class: "group-head" },
      el("label", { class: "switch", title: group.enabled ? t("GroupOn") : t("GroupOff") }, on, el("span")),
      name,
      el("label", { class: "inline" }, el("span", { class: "muted", text: t("Price") }), price, el("span", { class: "muted", text: t("Points") })),
      el("label", { class: "inline" }, el("span", { class: "muted", text: t("Wait") }), wait, el("span", { class: "muted", text: "s" })),
      el("span", { class: "muted count", text: t("SoundCount", group.sounds.length) }),
      remove),
    el("table", { class: "sound-table" },
      el("thead", {}, el("tr", {}, el("th", {}), el("th", { text: t("ColSound") }), el("th", { text: t("ColCommand") }),
        el("th", { text: t("ColActions") }))),
      body));
  dropTarget(panel, group);
  return panel;
}

function importCard() {
  const groups = state.groups;
  if (!groups.some((g) => g.id === importDraft.group)) importDraft.group = groups.length ? groups[0].id : "";
  const url = el("input", { type: "url", value: importDraft.url, placeholder: t("ImportUrlPlaceholder"), spellcheck: "false" });
  url.addEventListener("input", () => { importDraft.url = url.value; });
  const group = el("select", {}, groups.map((g) => el("option", { value: g.id, text: t("GroupOption", g.name, g.price) })));
  group.value = importDraft.group;
  group.addEventListener("change", () => { importDraft.group = group.value; });
  const name = el("input", { type: "text", value: importDraft.name, placeholder: t("ImportNamePlaceholder"), spellcheck: "false" });
  name.addEventListener("input", () => { importDraft.name = name.value; });
  const busy = state.import && state.import.state === "downloading";
  const go = el("button", { type: "button", class: "btn small primary", text: busy ? t("Downloading") : t("ImportButton"),
    disabled: busy || !groups.length });
  go.addEventListener("click", async () => {
    if (!importDraft.url.trim()) return;
    await act({ action: "import", url: importDraft.url, group: importDraft.group, name: importDraft.name });
    if (!state.error) followImport({ state: "downloading" });
  });
  return el("section", { class: "card" }, el("h3", { text: t("Import") }), el("p", { class: "hint", text: t("ImportNote") }),
    row(t("ImportUrl"), url),
    row(t("ImportGroup"), groups.length ? group : el("span", { class: "muted", text: t("NoGroups") })),
    row(t("ImportName"), name, t("ImportNameNote")),
    row("", el("div", { class: "inline" }, go)));
}

function render() {
  const volumeOut = el("span", { class: "value", text: state.volume + "%" });
  const volume = el("input", { type: "range", min: 0, max: 100, step: 5, value: state.volume });
  volume.addEventListener("input", () => { state.volume = Number(volume.value); volumeOut.textContent = volume.value + "%"; saveSettings(); });

  const add = button(t("NewGroup"), () => act({ action: "group", group: "", name: t("NewGroupName"), price: 50, cooldown: 30, enabled: true }), "primary");

  // The chat-made commands as a table; the last row makes a new one, in
  // the same columns.
  const commandRows = state.commands.map((c) => {
    const reply = el("input", { type: "text", value: c.reply, spellcheck: "false" });
    return el("tr", {},
      el("td", {}, el("code", { text: "!" + c.name })),
      el("td", {}, reply),
      el("td", { class: "sound-actions" },
        button(t("Save"), () => act({ action: "command", name: c.name, reply: reply.value })),
        button(t("Remove"), () => { if (confirm(t("RemoveCommandConfirm", "!" + c.name))) act({ action: "deleteCommand", name: c.name }); })));
  });
  const newName = el("input", { type: "text", placeholder: t("NewName"), spellcheck: "false" });
  const newReply = el("input", { type: "text", placeholder: t("NewReply"), spellcheck: "false" });
  const create = button(t("Create"), () => act({ action: "command", name: newName.value, reply: newReply.value }), "primary");
  const commandTable = el("table", { class: "sound-table command-table" },
    el("thead", {}, el("tr", {}, el("th", { text: t("ColCommand") }), el("th", { text: t("ColReply") }), el("th", { text: t("ColActions") }))),
    el("tbody", {}, commandRows,
      el("tr", { class: "new-row" }, el("td", {}, newName), el("td", {}, newReply), el("td", { class: "sound-actions" }, create))));

  document.getElementById("form").replaceChildren(
    el("h2", { text: t("Tab") }),
    el("section", { class: "card" }, el("h3", { text: t("Sounds") }),
      row(t("Volume"), el("div", { class: "inline" }, volume, volumeOut)),
      el("p", { class: "hint", text: state.groups.some((g) => g.sounds.length) ? t("SoundsNote") : t("NoSounds") }),
      state.groups.map(groupPanel),
      el("div", { class: "inline" }, add)),
    importCard(),
    el("section", { class: "card" }, el("h3", { text: t("Commands") }), el("p", { class: "hint", text: t("CommandsNote") }),
      el("div", { class: "group-panel" }, commandTable)));
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
