// The web panel's Theme tab, served at /tema-editor: one font and one set of
// colors, written into the overlays picked when "Apply" is pressed. A copy,
// not a link: each overlay can still be changed in its own tab afterwards.
"use strict";

// Inside the web panel (/painel) the panel draws the title and the tabs.
if (new URLSearchParams(location.search).has("embed")) document.documentElement.classList.add("embed");

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
const TARGETS = [["alertas", "Alerts.Title"], ["chat", "ChatOverlay.Title"], ["eventos", "EventsOverlay.Title"],
  ["metas", "Goals.Title"], ["enquete", "Poll.Title"], ["subathon", "Subathon.Title"]];
const FONTS = ["Poppins", "Montserrat", "Nunito", "Fredoka", "Rubik", "Inter", "Roboto", "Lilita One",
  "Bebas Neue", "Oswald", "Press Start 2P", "Arial", "Verdana"];
let S = {};
let theme = null;

// ---- helpers ----
function t(key, ...args) {
  let s = S["Theme." + key];
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
  node.textContent = text || (kind === "saving" ? t("Saving") : "");
}

// ---- preview: a goal bar and a poll with the theme ----
function preview() {
  const look = { ...theme, fontSize: 24 };
  const goal = document.getElementById("pgoal");
  GoalsRender.apply(goal, look);
  GoalsRender.draw(goal, { goals: [{ id: "x", title: t("SampleGoal"), current: 64, target: 100, prefix: "" }] }, S.lang, "");
  const poll = document.getElementById("ppoll");
  PollRender.apply(poll, look);
  PollRender.draw(poll, { question: S["Poll.SampleQuestion"] || "?", open: true, timed: false, total: 10, winner: 0,
    options: [{ text: S["Poll.SampleA"] || "A", votes: 6 }, { text: S["Poll.SampleB"] || "B", votes: 4 }] },
  { vote: S["Poll.Overlay.Vote"] || "" }, 0);
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

// ---- form ----
function renderForm() {
  const font = el("input", { type: "text", value: theme.font, list: "fonts", spellcheck: "false" });
  font.addEventListener("change", () => { theme.font = font.value.trim() || "Poppins"; preview(); });
  const lookCard = el("section", { class: "card" }, el("h3", { text: t("Look") }),
    el("p", { class: "hint", text: t("Note") }),
    row(t("Font"), font),
    row(t("TextColor"), colorCtl(theme.textColor, (v) => { theme.textColor = v; preview(); })),
    row(t("Accent"), colorCtl(theme.accent, (v) => { theme.accent = v; preview(); }), t("AccentNote")),
    row(t("Bubble"), el("div", { class: "inline" },
      colorCtl(theme.bubbleColor, (v) => { theme.bubbleColor = v; preview(); }),
      rangeCtl(0, 100, 5, theme.bubbleOpacity, "%", (v) => { theme.bubbleOpacity = v; preview(); }))),
    row(t("Shadow"), switchCtl(theme.shadow, (v) => { theme.shadow = v; preview(); })));

  const targetsCard = el("section", { class: "card" }, el("h3", { text: t("Targets") }),
    el("p", { class: "hint", text: t("TargetsNote") }),
    TARGETS.map(([k, label]) => row(S[label] || k, switchCtl(theme.targets[k], (v) => { theme.targets[k] = v; }))));
  const apply = el("button", { type: "button", class: "btn small primary", text: t("Apply") });
  apply.addEventListener("click", async () => {
    const names = TARGETS.filter(([k]) => theme.targets[k]).map(([, label]) => S[label] || label);
    if (!names.length || !confirm(t("ApplyConfirm", names.join(", ")))) return;
    setSave("saving");
    try {
      theme = await api("/api/theme-apply", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(theme) });
      setSave("ok", t("Applied", names.length));
    } catch (e) {
      setSave("bad", e.message === "forbidden" ? t("BadToken") : t("SaveFailed", e.message));
    }
  });
  targetsCard.append(row("", el("div", { class: "inline" }, apply)));

  document.getElementById("form").replaceChildren(el("h2", { text: t("Title") }), lookCard, targetsCard);
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
    theme = await api("/api/theme");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("BadToken") : t("LoadFailed", e.message));
    return;
  }
  document.body.append(el("datalist", { id: "fonts" }, FONTS.map((f) => el("option", { value: f }))));
  document.getElementById("app").hidden = false;
  renderForm();
  preview();
  new ResizeObserver(fit).observe(document.getElementById("frame"));
}

init();
