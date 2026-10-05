// The goals editor, served by the plugin at /metas-editor and shown in the
// web panel. Titles, kinds, targets and the look are saved as a whole; the
// "+1", "-1" and "Set" buttons change only the count, so a save never undoes
// an event that just arrived.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
const KINDS = ["follows", "subs", "bits", "donations", "members", "gifts"];
const FONTS = ["Poppins", "Montserrat", "Nunito", "Fredoka", "Rubik", "Inter", "Roboto", "Lilita One",
  "Bebas Neue", "Oswald", "Press Start 2P", "Arial", "Verdana"];
let S = {};
let config = null;

// ---- helpers ----
function t(key, ...args) {
  let s = S["Goals." + key];
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

function setSave(state, text) {
  const node = document.getElementById("saveState");
  node.className = "save " + (state === "ok" ? "ok" : state === "bad" ? "bad" : "");
  node.textContent = text || (state === "ok" ? t("Saved") : state === "saving" ? t("Saving") : "");
}

function failed(e) {
  setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
}

// ---- saving ----
let saveTimer = null;
function changed({ redraw = false } = {}) {
  setSave("saving");
  clearTimeout(saveTimer);
  saveTimer = setTimeout(save, 500);
  if (redraw) renderForm();
  preview();
}

async function save() {
  try {
    const saved = await api("/api/goals", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(config) });
    takeCounts(saved);
    setSave("ok");
  } catch (e) {
    failed(e);
  }
}

// Only the counts come from the plugin: what is being typed stays.
function takeCounts(fresh) {
  const counts = new Map((fresh.goals || []).map((g) => [g.id, g.current]));
  for (const g of config.goals) if (counts.has(g.id)) g.current = counts.get(g.id);
  for (const node of document.querySelectorAll("[data-count]")) {
    const goal = config.goals.find((g) => g.id === node.dataset.count);
    if (goal) node.textContent = GoalsRender.number(goal.current, S.lang) + " / " + GoalsRender.number(goal.target, S.lang);
  }
  preview();
}

async function adjust(id, value, set) {
  try {
    takeCounts(await api("/api/goals-adjust", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ id, value, set }) }));
  } catch (e) {
    failed(e);
  }
}

// ---- preview ----
function preview() {
  const box = document.getElementById("pgoals");
  GoalsRender.apply(box, config);
  GoalsRender.draw(box, config, S.lang, "");
  if (!config.goals.length) box.replaceChildren(el("p", { class: "hint", text: t("Empty") }));
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
function goalCard(goal) {
  const kind = el("select", {}, KINDS.map((k) => el("option", { value: k, text: t("Kind." + k) })));
  kind.value = goal.kind;
  kind.addEventListener("change", () => {
    goal.kind = kind.value;
    if (goal.kind === "donations" && !goal.prefix) goal.prefix = t("DefaultMoney");
    changed({ redraw: true });
  });
  const target = el("input", { type: "number", min: 1, step: 1, value: goal.target, class: "num" });
  target.addEventListener("input", () => { if (Number(target.value) >= 1) { goal.target = Number(target.value); changed(); } });

  const setTo = el("input", { type: "number", min: 0, step: 1, value: goal.current, class: "num" });
  const count = el("span", { class: "value", "data-count": goal.id,
    text: GoalsRender.number(goal.current, S.lang) + " / " + GoalsRender.number(goal.target, S.lang) });
  const button = (text, onclick) => el("button", { type: "button", class: "btn small", text, onclick });
  const remove = button(t("Remove"), () => {
    if (!confirm(t("RemoveConfirm", goal.title || t("Kind." + goal.kind)))) return;
    config.goals = config.goals.filter((g) => g !== goal);
    changed({ redraw: true });
  });

  return el("section", { class: "card" },
    el("h3", { text: goal.title || t("Kind." + goal.kind) }),
    row(t("GoalTitle"), textCtl(goal.title, (v) => { goal.title = v; changed(); })),
    row(t("Kind"), kind, t("KindNote." + goal.kind)),
    row(t("Target"), target),
    row(t("Prefix"), textCtl(goal.prefix, (v) => { goal.prefix = v; changed(); }), t("PrefixNote")),
    row(t("Current"), el("div", { class: "inline" }, count, button("−1", () => adjust(goal.id, -1, false)),
      button("+1", () => adjust(goal.id, 1, false)), setTo,
      button(t("Set"), () => adjust(goal.id, Number(setTo.value) || 0, true)),
      button(t("Reset"), () => adjust(goal.id, 0, true)))),
    row(t("ResetOnLive"), switchCtl(goal.resetOnLive, (v) => { goal.resetOnLive = v; changed(); }), t("ResetOnLiveNote")),
    row(t("OneLink"), copyCtl(location.origin + "/metas?meta=" + goal.id)),
    row("", el("div", { class: "inline" }, remove)));
}

function renderForm() {
  const add = el("button", { type: "button", class: "btn small primary", text: t("Add") });
  add.addEventListener("click", () => {
    config.goals.push({ id: "meta-" + Date.now().toString(36), title: t("Default.Title"), kind: "follows",
      target: 100, current: 0, prefix: "", resetOnLive: false });
    changed({ redraw: true });
  });
  const listCard = el("section", { class: "card" }, el("h3", { text: t("Goals") }),
    el("p", { class: "hint", text: t("GoalsNote") }),
    row(t("AllLink"), copyCtl(location.origin + "/metas")),
    config.goals.length < 20 ? row("", el("div", { class: "inline" }, add)) : null);

  const font = el("input", { type: "text", value: config.font, list: "fonts", spellcheck: "false" });
  font.addEventListener("change", () => { config.font = font.value.trim() || "Poppins"; changed(); });
  const lookCard = el("section", { class: "card" }, el("h3", { text: t("Look") }),
    row(t("Font"), font),
    row(t("FontSize"), rangeCtl(12, 72, 1, config.fontSize, " px", (v) => { config.fontSize = v; changed(); })),
    row(t("TextColor"), colorCtl(config.textColor, (v) => { config.textColor = v; changed(); })),
    row(t("Bar"), colorCtl(config.accent, (v) => { config.accent = v; changed(); })),
    row(t("BarBack"), el("div", { class: "inline" },
      colorCtl(config.bubbleColor, (v) => { config.bubbleColor = v; changed(); }),
      rangeCtl(0, 100, 5, config.bubbleOpacity, "%", (v) => { config.bubbleOpacity = v; changed(); }))),
    row(t("Shadow"), switchCtl(config.shadow, (v) => { config.shadow = v; changed(); })));

  document.getElementById("form").replaceChildren(el("h2", { text: t("Title") }), listCard,
    ...config.goals.map(goalCard), lookCard);
}

// Counts arriving from the live while the editor is open.
function listen() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (event) => {
    let msg;
    try {
      msg = JSON.parse(event.data);
    } catch (e) {
      return;
    }
    if (msg.type === "goals") takeCounts(msg.config);
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
    config = await api("/api/goals");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : t("LoadFailed", e.message));
    return;
  }
  document.body.append(el("datalist", { id: "fonts" }, FONTS.map((f) => el("option", { value: f }))));
  document.getElementById("app").hidden = false;
  renderForm();
  preview();
  new ResizeObserver(fit).observe(document.getElementById("frame"));
  listen();
}

init();
