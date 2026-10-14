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

async function post(body) {
  return api("/api/tab/desfile", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body) });
}

async function save() {
  try {
    const { scale, speed, names, nameSize } = state;
    const next = await post({ scale, speed, names, nameSize });
    // The quests table keeps what is being typed in it.
    state = { ...next, quests: state.quests };
    setSave("ok");
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
  }
}

// --- Quests: the settings and the list go together, a moment after the
// last change. New rows get their id from the answer, so the next save
// keeps them (a quest that changes id starts over for everyone).
let questTimer = null;
function questsChanged() {
  setSave("saving");
  clearTimeout(questTimer);
  questTimer = setTimeout(saveQuests, 400);
}

async function saveQuests() {
  const q = state.quests;
  try {
    const next = await post({ quests: { enabled: q.enabled, perViewer: q.perViewer, whisper: q.whisper,
      showLevel: q.showLevel, announce: q.announce, list: q.list } });
    if (next.error) {
      setSave("bad", next.error);
      return;
    }
    next.quests.list.forEach((saved, i) => { if (q.list[i]) q.list[i].id = saved.id; });
    q.whisperError = next.quests.whisperError;
    setSave("ok");
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
  }
}

function questTitle(quest) {
  return t((quest.goal === 1 ? "QuestOne." : "QuestMany.") + quest.kind, quest.goal);
}

function numberCtl(value, min, max, onChange) {
  const input = el("input", { type: "number", class: "num", min, max, step: 1, value });
  input.addEventListener("input", () => {
    const v = Math.round(Number(input.value));
    if (input.value === "" || !Number.isFinite(v)) return;
    onChange(Math.min(max, Math.max(min, v)));
  });
  return input;
}

function questRow(quest, index) {
  const q = state.quests;
  const kind = el("select", {}, q.kinds.map((k) => {
    const option = el("option", { value: k.id, text: t("QuestKindName." + k.id) });
    option.selected = k.id === quest.kind;
    return option;
  }));
  const title = el("input", { type: "text", value: quest.title || "", placeholder: questTitle(quest), maxlength: 60, spellcheck: "false" });
  const refreshPlaceholder = () => { title.placeholder = questTitle(quest); };
  kind.addEventListener("change", () => { quest.kind = kind.value; refreshPlaceholder(); questsChanged(); });
  title.addEventListener("input", () => { quest.title = title.value; questsChanged(); });
  const goal = numberCtl(quest.goal, 1, 100000, (v) => { quest.goal = v; refreshPlaceholder(); questsChanged(); });
  const points = numberCtl(quest.points, 0, 100000, (v) => { quest.points = v; questsChanged(); });
  const xp = numberCtl(quest.xp, 0, 10000, (v) => { quest.xp = v; questsChanged(); });
  const remove = el("button", { type: "button", class: "btn small", text: t("QuestRemove") });
  remove.addEventListener("click", () => { q.list.splice(index, 1); questsChanged(); renderForm(); });
  // One quest per row; the fields wrap on a narrow panel.
  const field = (label, control, cls) => el("label", { class: "quest-field " + (cls || "") }, el("span", { text: label }), control);
  return el("div", { class: "quest-row" + (quest.enabled ? "" : " off") },
    el("div", { class: "quest-on", title: t("QuestColOn") },
      switchCtl(quest.enabled, (v) => { quest.enabled = v; questsChanged(); renderForm(); })),
    field(t("QuestColKind"), kind, "kind"), field(t("QuestColGoal"), goal), field(t("QuestColPoints"), points),
    field(t("QuestColXp"), xp), field(t("QuestColTitle"), title, "title"),
    el("div", { class: "quest-actions" }, remove));
}

function questsCard() {
  const q = state.quests;
  const announce = el("select", {}, ["none", "level", "all"].map((id) => {
    const option = el("option", { value: id, text: t("QuestsAnnounce" + id[0].toUpperCase() + id.slice(1)) });
    option.selected = id === q.announce;
    return option;
  }));
  announce.addEventListener("change", () => { q.announce = announce.value; questsChanged(); });

  const add = el("button", { type: "button", class: "btn small primary", text: t("QuestAdd") });
  add.disabled = q.list.length >= q.maxQuests;
  add.addEventListener("click", () => {
    q.list.push({ id: "", kind: "messages", goal: 10, points: 20, xp: 10, enabled: true, title: "" });
    questsChanged();
    renderForm();
  });
  const defaults = el("button", { type: "button", class: "btn small", text: t("QuestDefaults") });
  defaults.addEventListener("click", async () => {
    if (!confirm(t("QuestDefaultsConfirm"))) return;
    clearTimeout(questTimer);
    try {
      const next = await post({ questDefaults: true });
      state.quests = next.quests;
      setSave("ok");
      renderForm();
    } catch (e) {
      setSave("bad", t("SaveFailed", e.message));
    }
  });


  return el("section", { class: "card" }, el("h3", { text: t("Quests") }),
    el("p", { class: "hint", text: t("QuestsNote") }),
    row(t("QuestsOn"), switchCtl(q.enabled, (v) => { q.enabled = v; questsChanged(); })),
    row(t("QuestsPerViewer"), rangeCtl(1, q.maxPerViewer, 1, q.perViewer, "", (v) => { q.perViewer = v; questsChanged(); })),
    row(t("QuestsShowLevel"), switchCtl(q.showLevel, (v) => { q.showLevel = v; questsChanged(); }), t("QuestsShowLevelNote")),
    row(t("QuestsAnnounce"), announce),
    row(t("QuestsWhisper"), switchCtl(q.whisper, (v) => { q.whisper = v; questsChanged(); }), t("QuestsWhisperNote")),
    q.whisperError ? el("p", { class: "hint bad", text: t("QuestsWhisperError", q.whisperError) }) : null,
    el("div", { class: "group-panel quest-panel" }, q.list.map(questRow),
      el("div", { class: "group-head" }, add, defaults)));
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
      row(t("BrowserSource"), el("div", { class: "inline" }, el("code", { text: state.url }), copy), t("LinkNote"))),
    state.quests ? questsCard() : null);
}

async function init() {
  try {
    S = await api("/api/i18n" + location.search);
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
