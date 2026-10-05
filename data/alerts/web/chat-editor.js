// The chat-on-screen editor, served by the plugin at /chat-editor. The dock
// opens it with a token after "#"; every API call carries it.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
const PLATFORMS = [["twitch", "Twitch"], ["youtube", "YouTube"], ["kick", "Kick"]];
const FONTS = ["Poppins", "Montserrat", "Nunito", "Fredoka", "Rubik", "Inter", "Roboto", "Lilita One",
  "Bebas Neue", "Oswald", "Press Start 2P", "Arial", "Verdana"];
let S = {};
let config = null;
let samples = [];
let eventSamples = [];
const EVENT_TYPES = ["follow", "sub", "resub", "giftsub", "donation", "raid", "membership", "gift"];

// ---- helpers ----
function t(key, ...args) {
  let s = S["ChatOverlay." + key];
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
  restyle();
}

async function save() {
  try {
    await api("/api/chat-config", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(config) });
    setSave("ok");
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
  }
}

// ---- preview: the sample lines keep coming, in the order of the platforms on ----
const pbox = document.getElementById("pbox");
let nextSample = 0;

// Messages, with the events that are turned on mixed in after every other one.
function previewItems() {
  const msgs = samples.filter((m) => config.platforms[m.platform]).map((m) => ({ kind: "msg", data: m }));
  const evs = eventSamples.filter((e) => config.platforms[e.platform] && config.events[e.type]).map((e) => ({ kind: "event", data: e }));
  const out = [];
  for (let i = 0; i < Math.max(msgs.length, evs.length * 2); i++) {
    if (msgs.length) out.push(msgs[i % msgs.length]);
    if (i % 2 === 1 && evs.length) out.push(evs[((i - 1) / 2) % evs.length]);
  }
  return out;
}

function show(item, cfg) {
  if (item.kind === "event") ChatRender.addEvent(pbox, cfg, item.data);
  else ChatRender.add(pbox, cfg, item.data);
}

function restyle() {
  ChatRender.apply(pbox, config);
  pbox.replaceChildren();
  for (const item of previewItems().slice(-config.maxMessages)) show(item, { ...config, animation: "none", fadeAfter: 0 });
}

function tick() {
  const items = previewItems();
  if (items.length) {
    const item = items[nextSample++ % items.length];
    show({ ...item, data: { ...item.data, id: "p" + Date.now() } }, config);
  }
}

async function testLive(kind) {
  const hint = document.getElementById("liveHint");
  try {
    const r = await api("/api/chat-test" + (kind ? "?kind=" + kind : ""), { method: "POST" });
    hint.textContent = r.overlays > 0 ? t("SentLive", r.overlays) : t("NoOverlay");
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

function rangeCtl(min, max, step, value, unit, onChange, zeroText) {
  const show = (v) => (Number(v) === 0 && zeroText ? zeroText : v + unit);
  const out = el("span", { class: "value", text: show(value) });
  const input = el("input", { type: "range", min, max, step, value });
  input.addEventListener("input", () => {
    out.textContent = show(input.value);
    onChange(Number(input.value));
  });
  return el("div", { class: "inline" }, input, out);
}

function segCtl(options, value, onChange) {
  const seg = el("div", { class: "seg" });
  for (const [v, label] of options) {
    const b = el("button", { type: "button", class: v === value ? "active" : "", text: label });
    b.addEventListener("click", () => {
      seg.querySelectorAll("button").forEach((x) => x.classList.remove("active"));
      b.classList.add("active");
      onChange(v);
    });
    seg.append(b);
  }
  return seg;
}

function colorCtl(value, onChange) {
  const input = el("input", { type: "color", value });
  input.addEventListener("input", () => onChange(input.value.toUpperCase()));
  return input;
}

function copyCtl(getUrl) {
  const code = el("code", { text: getUrl() });
  const copy = el("button", { type: "button", class: "btn small", text: t("Copy") });
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(getUrl());
      copy.textContent = t("Copied");
    } catch (e) {
      copy.textContent = getUrl();
    }
  });
  return { node: el("div", { class: "inline" }, code, copy), code, copy };
}

// ---- form ----
function eventsCard() {
  const card = el("section", { class: "card" }, el("h3", { text: t("Events") }),
    el("p", { class: "hint", text: t("EventsNote") }));
  for (const type of EVENT_TYPES) {
    card.append(row(S["Alerts.Type." + type] || type, switchCtl(config.events[type], (v) => { config.events[type] = v; changed(); })));
  }
  card.append(row(t("EventColor"), colorCtl(config.eventColor, (v) => { config.eventColor = v; changed(); })));
  const test = el("button", { type: "button", class: "btn small", text: t("TestEvent") });
  test.addEventListener("click", () => testLive("event"));
  card.append(row("", test));
  return card;
}

function onlyUrl() {
  const on = PLATFORMS.filter(([p]) => config.platforms[p]).map(([p]) => p);
  return location.origin + "/chat?p=" + on.join(",");
}

function renderForm() {
  const form = document.getElementById("form");
  const main = location.origin + "/chat";
  const only = copyCtl(onlyUrl);

  const platformCard = el("section", { class: "card" }, el("h3", { text: t("Platforms") }));
  for (const [p, label] of PLATFORMS) {
    platformCard.append(row(label, el("div", { class: "inline" },
      ChatRender.icon(p),
      switchCtl(config.platforms[p], (v) => {
        config.platforms[p] = v;
        only.code.textContent = onlyUrl();
        only.copy.textContent = t("Copy");
        changed();
      }, label))));
  }
  platformCard.append(el("p", { class: "hint", text: t("PlatformsNote") }));
  platformCard.append(row(t("OnlyLink"), only.node, t("OnlyLinkNote")));
  platformCard.append(el("p", { class: "hint", text: t("TwitchRule") }));

  const hidden = el("input", { type: "text", value: config.hideUsers, spellcheck: "false" });
  hidden.addEventListener("input", () => { config.hideUsers = hidden.value; changed(); });

  const fontInput = el("input", { type: "text", value: config.font, list: "fonts", spellcheck: "false" });
  fontInput.addEventListener("change", () => { config.font = fontInput.value.trim() || "Poppins"; changed(); });

  form.replaceChildren(
    el("h2", { text: t("Title") }),
    platformCard,
    el("section", { class: "card" }, el("h3", { text: t("Filters") }),
      row(t("HideCommands"), switchCtl(config.hideCommands, (v) => { config.hideCommands = v; changed(); }), t("HideCommandsNote")),
      row(t("HideUsers"), hidden, t("HideUsersNote"))),
    el("section", { class: "card" }, el("h3", { text: t("Look") }),
      row(t("Font"), fontInput),
      row(t("FontSize"), rangeCtl(12, 72, 1, config.fontSize, " px", (v) => { config.fontSize = v; changed(); })),
      row(t("TextColor"), colorCtl(config.textColor, (v) => { config.textColor = v; changed(); })),
      row(t("Bubble"), el("div", { class: "inline" },
        colorCtl(config.bubbleColor, (v) => { config.bubbleColor = v; changed(); }),
        rangeCtl(0, 100, 5, config.bubbleOpacity, "%", (v) => { config.bubbleOpacity = v; changed(); })), t("BubbleNote")),
      row(t("Shadow"), switchCtl(config.shadow, (v) => { config.shadow = v; changed(); })),
      row(t("ShowPlatform"), switchCtl(config.showPlatform, (v) => { config.showPlatform = v; changed(); })),
      row(t("ShowBadges"), switchCtl(config.showBadges, (v) => { config.showBadges = v; changed(); }))),
    eventsCard(),
    el("section", { class: "card" }, el("h3", { text: t("Behavior") }),
      row(t("Newest"), segCtl([["bottom", t("NewestBottom")], ["top", t("NewestTop")]], config.newest, (v) => { config.newest = v; changed(); })),
      row(t("Animation"), segCtl([["slide", t("AnimSlide")], ["fade", t("AnimFade")], ["none", t("AnimNone")]], config.animation, (v) => { config.animation = v; changed(); })),
      row(t("MaxMessages"), rangeCtl(1, 50, 1, config.maxMessages, "", (v) => { config.maxMessages = v; changed(); })),
      row(t("FadeAfter"), rangeCtl(0, 120, 5, config.fadeAfter, " s", (v) => { config.fadeAfter = v; changed(); }, t("Never")))),
    el("section", { class: "card" }, el("h3", { text: t("Link") }),
      row(t("BrowserSource"), copyCtl(() => main).node, t("LinkNote"))));
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
    config = await api("/api/chat-config");
    const sample = await api("/api/chat-sample");
    samples = sample.messages || [];
    eventSamples = sample.events || [];
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : t("LoadFailed", e.message));
    return;
  }
  document.body.append(el("datalist", { id: "fonts" }, FONTS.map((f) => el("option", { value: f }))));
  document.getElementById("app").hidden = false;
  document.getElementById("liveBtn").addEventListener("click", () => testLive());
  // The preview is the 480×720 source "Add to scene" creates, scaled down.
  const frame = document.getElementById("frame");
  new ResizeObserver(() => { pbox.style.transform = `scale(${frame.clientWidth / 480})`; }).observe(frame);
  renderForm();
  restyle();
  setInterval(tick, 1800);
}

init();
