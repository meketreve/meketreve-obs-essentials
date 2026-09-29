// The alerts editor, served by the plugin at /editor. The dock opens it with
// a token after "#"; every API call carries it.
"use strict";

const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
let S = {};
let config = null;
let samples = {};
let media = [];
let current = "general";

const TYPES = ["follow", "sub", "resub", "giftsub", "bits", "donation", "raid", "membership", "gift", "share", "like"];
const MIN_TYPES = ["resub", "giftsub", "bits", "donation", "raid", "gift", "like"];
const MESSAGE_TYPES = ["sub", "resub", "bits", "donation", "membership"];
const FONTS = ["Poppins", "Montserrat", "Nunito", "Fredoka", "Rubik", "Bangers", "Luckiest Guy", "Lilita One",
  "Bebas Neue", "Oswald", "Permanent Marker", "Press Start 2P", "Arial Black", "Impact", "Verdana"];
const ANIM_IN = ["bounce", "fade", "zoom", "slide-down", "slide-up", "slide-left", "slide-right", "none"];
const ANIM_OUT = ["fade", "zoom", "slide-up", "slide-down", "none"];
const POSITIONS = ["top-left", "top-center", "top-right", "middle-left", "middle-center", "middle-right",
  "bottom-left", "bottom-center", "bottom-right"];

// ---- helpers ----
function t(key, ...args) {
  let s = S["Alerts." + key];
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
  node.textContent = text || (state === "ok" ? t("Editor.Saved") : state === "saving" ? t("Editor.Saving") : "");
}

// ---- saving ----
let saveTimer = null;
function scheduleSave() {
  setSave("saving");
  clearTimeout(saveTimer);
  saveTimer = setTimeout(save, 500);
}

async function save() {
  try {
    await api("/api/config", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(config) });
    setSave("ok");
  } catch (e) {
    setSave("bad", e.message === "forbidden" ? t("Editor.BadToken") : t("Editor.SaveFailed", e.message));
  }
}

async function replaceConfig(next) {
  clearTimeout(saveTimer);
  setSave("saving");
  try {
    config = await api("/api/config", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(next) });
    setSave("ok");
  } catch (e) {
    setSave("bad", t("Editor.SaveFailed", e.message));
  }
  renderNav();
  renderForm();
}

function typeCfg() {
  return config.types[current];
}

function setType(key, value, { nav = false, preview = true } = {}) {
  typeCfg()[key] = value;
  scheduleSave();
  if (nav) renderNav();
  if (preview) schedulePreview();
}

// ---- preview ----
const pstage = document.getElementById("pstage");
let previewTimer = null;

function fitPreview() {
  const frame = document.getElementById("frame");
  document.getElementById("scaler").style.transform = `scale(${frame.clientWidth / 1920})`;
}

function previewType() {
  return current === "general" ? "follow" : current;
}

function preview(withSound) {
  const type = previewType();
  AlertRender.skip();
  pstage.replaceChildren();
  const cfg = withSound ? config : { ...config, volume: 0 };
  AlertRender.show(pstage, cfg, samples[type] || { type, name: "Texuguito", amount: "1" }, "");
}

function schedulePreview() {
  clearTimeout(previewTimer);
  previewTimer = setTimeout(() => preview(false), 450);
}

async function testLive() {
  const hint = document.getElementById("liveHint");
  try {
    const r = await api("/api/test?type=" + previewType(), { method: "POST" });
    hint.textContent = r.overlays > 0 ? t("Editor.SentLive", r.overlays) : t("Editor.NoOverlay");
  } catch (e) {
    hint.textContent = e.message === "forbidden" ? t("Editor.BadToken") : t("Editor.SaveFailed", e.message);
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

function selectCtl(options, value, onChange) {
  const select = el("select", {}, options.map(([v, label]) => el("option", { value: v, text: label })));
  select.value = value;
  select.addEventListener("change", () => onChange(select.value));
  return select;
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

function refName(ref, kind) {
  if (!ref) return t("Editor.None");
  if (ref.startsWith("builtin:")) return t((kind === "image" ? "Editor.Img." : "Editor.Snd.") + ref.slice(8));
  if (ref.startsWith("media:")) return ref.slice(6);
  try {
    const u = new URL(ref);
    return u.hostname + (u.pathname.length > 30 ? u.pathname.slice(0, 30) + "…" : u.pathname);
  } catch (e) {
    return ref;
  }
}

function thumbFor(ref, accent) {
  const box = el("div", { class: "thumb" });
  if (!ref) return box;
  if (ref.startsWith("builtin:")) {
    const d = AlertRender.drawing(ref.slice(8), accent);
    if (d) box.append(d);
    return box;
  }
  const url = AlertRender.resolve(ref);
  if (AlertRender.isVideo(url)) box.append(el("video", { src: url, autoplay: true, muted: true, loop: true, playsinline: true }));
  else box.append(el("img", { src: url, alt: "", loading: "lazy" }));
  return box;
}

// ---- navigation ----
function renderNav() {
  const nav = document.getElementById("nav");
  nav.replaceChildren();
  const item = (key, label, on, color) => {
    const b = el("button", { type: "button", class: `nav-item${key === current ? " active" : ""}${on ? " on" : ""}` },
      el("span", { class: "dot" }), el("span", { class: "label", text: label }));
    if (color) b.style.setProperty("--dot", color);
    b.addEventListener("click", () => {
      current = key;
      renderNav();
      renderForm();
      schedulePreview();
    });
    return b;
  };
  nav.append(item("general", t("Editor.General"), true, "var(--accent)"), el("div", { class: "nav-sep" }));
  for (const type of TYPES) nav.append(item(type, t("Type." + type), config.types[type].enabled, config.types[type].accent));
}

// ---- forms ----
function renderForm() {
  const form = document.getElementById("form");
  form.replaceChildren(current === "general" ? generalForm() : typeForm());
}

function generalForm() {
  const wrap = el("div", { class: "form" });
  wrap.append(el("h2", { text: t("Editor.General") }));

  const grid = el("div", { class: "posgrid" });
  for (const p of POSITIONS) {
    const b = el("button", { type: "button", title: t("Editor.Pos." + p), class: p === config.position ? "active" : "" });
    b.addEventListener("click", () => {
      config.position = p;
      grid.querySelectorAll("button").forEach((x) => x.classList.remove("active"));
      b.classList.add("active");
      scheduleSave();
      schedulePreview();
    });
    grid.append(b);
  }
  wrap.append(el("section", { class: "card" }, el("h3", { text: t("Editor.Screen") }),
    row(t("Editor.Position"), grid),
    row(t("Editor.Margin"), rangeCtl(0, 400, 10, config.margin, " px", (v) => { config.margin = v; scheduleSave(); schedulePreview(); })),
    row(t("Editor.Gap"), rangeCtl(0, 10, 0.5, config.gap, " s", (v) => { config.gap = v; scheduleSave(); }), t("Editor.GapNote")),
    row(t("Editor.Volume"), rangeCtl(0, 100, 1, config.volume, "%", (v) => { config.volume = v; scheduleSave(); }))));

  const integrations = config.integrations;
  const keyCard = el("section", { class: "card" }, el("h3", { text: t("Editor.GifSearch") }));
  const renderKeys = () => {
    keyCard.replaceChildren(el("h3", { text: t("Editor.GifSearch") }));
    keyCard.append(row(t("Editor.Provider"), segCtl([["giphy", "GIPHY"], ["tenor", "Tenor"]], integrations.provider, (v) => {
      integrations.provider = v;
      scheduleSave();
      renderKeys();
    })));
    const field = integrations.provider === "giphy" ? "giphyKey" : "tenorKey";
    const input = el("input", { type: "text", value: integrations[field], placeholder: t("Editor.KeyPlaceholder"), spellcheck: "false" });
    input.addEventListener("input", () => { integrations[field] = input.value.trim(); scheduleSave(); });
    keyCard.append(row(t("Editor.ApiKey"), input, t(integrations.provider === "giphy" ? "Editor.KeyHelpGiphy" : "Editor.KeyHelpTenor")));
    const link = integrations.provider === "giphy" ? "https://developers.giphy.com/dashboard/" : "https://developers.google.com/tenor/guides/quickstart";
    keyCard.append(row("", el("a", { href: link, target: "_blank", rel: "noopener", text: t("Editor.GetKey"), style: "color:var(--accent-2)" })));
  };
  renderKeys();
  wrap.append(keyCard);

  const url = location.origin + "/alertas";
  const copy = el("button", { type: "button", class: "btn small", text: t("Editor.Copy") });
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(url);
      copy.textContent = t("Editor.Copied");
    } catch (e) {
      copy.textContent = url;
    }
  });
  wrap.append(el("section", { class: "card" }, el("h3", { text: t("Editor.OverlayLink") }),
    row(t("Editor.BrowserSource"), el("div", { class: "inline" }, el("code", { text: url }), copy), t("Editor.OverlayNote"))));

  const reset = el("button", { type: "button", class: "btn", text: t("Editor.ResetAll") });
  reset.addEventListener("click", () => {
    if (confirm(t("Editor.ResetAllConfirm"))) replaceConfig({ ...config, types: {} });
  });
  wrap.append(el("section", { class: "card" }, el("h3", { text: t("Editor.Kit") }), el("p", { class: "hint", text: t("Editor.KitNote") }),
    el("div", { class: "actions" }, reset)));
  return wrap;
}

function variableChips(input) {
  const pt = S.lang === "pt";
  const names = pt ? ["nome", "quantidade", "mensagem", "detalhe", "plataforma"] : ["name", "amount", "message", "detail", "platform"];
  return el("div", { class: "chips" }, names.map((n) => {
    const chip = el("button", { type: "button", class: "chip", text: `{${n}}` });
    chip.addEventListener("click", () => {
      const at = input.selectionStart ?? input.value.length;
      input.value = input.value.slice(0, at) + `{${n}}` + input.value.slice(input.selectionEnd ?? at);
      input.dispatchEvent(new Event("input"));
      input.focus();
    });
    return chip;
  }));
}

function mediaPicker(kind) {
  const cfg = typeCfg();
  const key = kind === "image" ? "image" : "sound";
  const wrap = el("div", { class: "media-pick" });
  const draw = () => {
    wrap.replaceChildren();
    if (kind === "image") wrap.append(thumbFor(cfg[key], cfg.accent));
    wrap.append(el("span", { class: "media-name", text: refName(cfg[key], kind), title: cfg[key] }));
    if (kind === "sound" && cfg.sound) {
      const play = el("button", { type: "button", class: "btn small", text: "▶" });
      play.addEventListener("click", () => AlertRender.playSound(cfg.sound, (config.volume / 100) * (cfg.volume / 100) || 0.8));
      wrap.append(play);
    }
    const change = el("button", { type: "button", class: "btn small", text: t("Editor.Change") });
    change.addEventListener("click", () => openPicker(kind, cfg[key], (ref) => {
      setType(key, ref);
      draw();
    }));
    wrap.append(change);
  };
  draw();
  return wrap;
}

function typeForm() {
  const cfg = typeCfg();
  const type = current;
  const wrap = el("div", { class: "form" });
  wrap.append(el("h2", {}, switchCtl(cfg.enabled, (v) => setType("enabled", v, { nav: true }), t("Editor.Enabled")),
    el("span", { text: t("Type." + type) })));
  wrap.append(el("p", { class: "hint", text: t("Editor.About." + type) }));

  const when = el("section", { class: "card" }, el("h3", { text: t("Editor.When") }));
  if (MIN_TYPES.includes(type)) {
    const min = el("input", { type: "number", min: 0, step: type === "donation" ? 0.5 : 1, value: cfg.min });
    min.addEventListener("input", () => setType("min", Math.max(0, Number(min.value) || 0), { preview: false }));
    when.append(row(t("Editor.Min"), min, t("Editor.MinUnit." + type)));
  }
  when.append(row(t("Editor.Duration"), rangeCtl(1, 30, 0.5, cfg.duration, " s", (v) => setType("duration", v, { preview: false })),
    t("Editor.DurationNote")));
  wrap.append(when);

  const text = el("input", { type: "text", value: cfg.text, maxlength: 300 });
  text.addEventListener("input", () => setType("text", text.value));
  const textCard = el("section", { class: "card" }, el("h3", { text: t("Editor.Text") }),
    row(t("Editor.Message"), el("div", {}, text, variableChips(text))));
  if (MESSAGE_TYPES.includes(type)) {
    textCard.append(row(t("Editor.ShowMessage"), switchCtl(cfg.showMessage, (v) => setType("showMessage", v))));
    textCard.append(row(t("Editor.Tts"), switchCtl(cfg.tts, (v) => setType("tts", v, { preview: false })), t("Editor.TtsNote")));
  }
  const font = el("input", { type: "text", value: cfg.font, list: "fonts", maxlength: 60 });
  font.addEventListener("change", () => setType("font", font.value.trim() || "Poppins"));
  textCard.append(row(t("Editor.Font"), font, t("Editor.FontNote")));
  textCard.append(row(t("Editor.FontSize"), rangeCtl(12, 160, 1, cfg.fontSize, " px", (v) => setType("fontSize", v))));
  textCard.append(row(t("Editor.Colors"), el("div", { class: "inline" },
    el("span", { class: "hint", text: t("Editor.TextColor") }), colorCtl(cfg.textColor, (v) => setType("textColor", v)),
    el("span", { class: "hint", text: t("Editor.Accent") }), colorCtl(cfg.accent, (v) => setType("accent", v, { nav: true })))));
  wrap.append(textCard);

  wrap.append(el("section", { class: "card" }, el("h3", { text: t("Editor.Image") }),
    row(t("Editor.Image"), mediaPicker("image")),
    row(t("Editor.ImageSize"), rangeCtl(0, 600, 10, cfg.imageSize, " px", (v) => setType("imageSize", v))),
    row(t("Editor.Layout"), segCtl([["above", t("Editor.Layout.above")], ["side", t("Editor.Layout.side")], ["text", t("Editor.Layout.text")]],
      cfg.layout, (v) => setType("layout", v)))));

  wrap.append(el("section", { class: "card" }, el("h3", { text: t("Editor.Sound") }),
    row(t("Editor.Sound"), mediaPicker("sound")),
    row(t("Editor.Volume"), rangeCtl(0, 100, 1, cfg.volume, "%", (v) => setType("volume", v, { preview: false })))));

  wrap.append(el("section", { class: "card" }, el("h3", { text: t("Editor.Animation") }),
    row(t("Editor.AnimIn"), selectCtl(ANIM_IN.map((a) => [a, t("Editor.Anim." + a)]), cfg.animIn, (v) => setType("animIn", v))),
    row(t("Editor.AnimOut"), selectCtl(ANIM_OUT.map((a) => [a, t("Editor.Anim." + a)]), cfg.animOut, (v) => setType("animOut", v)))));

  const copyStyle = el("button", { type: "button", class: "btn", text: t("Editor.CopyStyle") });
  copyStyle.addEventListener("click", () => {
    if (!confirm(t("Editor.CopyStyleConfirm"))) return;
    for (const other of TYPES) {
      if (other === type) continue;
      for (const k of ["layout", "animIn", "animOut", "font", "fontSize", "textColor", "imageSize", "duration"])
        config.types[other][k] = cfg[k];
    }
    scheduleSave();
  });
  const reset = el("button", { type: "button", class: "btn", text: t("Editor.ResetType") });
  reset.addEventListener("click", () => {
    if (!confirm(t("Editor.ResetTypeConfirm"))) return;
    const types = { ...config.types };
    delete types[type];
    replaceConfig({ ...config, types });
  });
  wrap.append(el("div", { class: "actions" }, copyStyle, reset));
  return wrap;
}

// ---- picker ----
const dialog = document.getElementById("picker");
let picker = null;

function openPicker(kind, value, onPick) {
  picker = { kind, value, onPick, tab: value && value.startsWith("media:") ? "files" : value && /^https?:/.test(value) ? "link" : "builtin" };
  document.getElementById("pickerTitle").textContent = t(kind === "image" ? "Editor.PickImage" : "Editor.PickSound");
  const none = document.getElementById("pickerNone");
  none.textContent = t(kind === "image" ? "Editor.NoImage" : "Editor.NoSound");
  none.onclick = () => choose("");
  renderPicker();
  dialog.showModal();
}

function choose(ref) {
  const done = picker.onPick;
  dialog.close();
  done(ref);
}

function renderPicker() {
  const tabs = document.getElementById("pickerTabs");
  const names = picker.kind === "image" ? ["builtin", "files", "search", "link"] : ["builtin", "files", "link"];
  tabs.replaceChildren(...names.map((n) => {
    const b = el("button", { type: "button", class: n === picker.tab ? "active" : "", text: t("Editor.Tab." + n) });
    b.addEventListener("click", () => { picker.tab = n; renderPicker(); });
    return b;
  }));
  const body = document.getElementById("pickerBody");
  body.replaceChildren();
  if (picker.tab === "builtin") body.append(picker.kind === "image" ? builtinImages() : builtinSounds());
  else if (picker.tab === "files") filesTab(body);
  else if (picker.tab === "link") body.append(linkTab());
  else searchTab(body);
}

function builtinImages() {
  const accent = typeCfg().accent;
  return el("div", { class: "grid" }, Object.keys(AlertRender.DRAWINGS).map((id) => {
    const ref = "builtin:" + id;
    const tile = el("button", { type: "button", class: "tile" + (picker.value === ref ? " active" : "") },
      thumbFor(ref, accent), el("span", { class: "name", text: t("Editor.Img." + id) }));
    tile.addEventListener("click", () => choose(ref));
    return tile;
  }));
}

function soundRow(ref, label, extra) {
  const play = el("button", { type: "button", class: "btn small", text: "▶" });
  play.addEventListener("click", () => AlertRender.playSound(ref, 0.8));
  const use = el("button", { type: "button", class: "btn small primary", text: t("Editor.Use") });
  use.addEventListener("click", () => choose(ref));
  return el("div", { class: "sound-item" + (picker.value === ref ? " active" : "") }, play, el("span", { class: "name", text: label }), extra, use);
}

function builtinSounds() {
  return el("div", { class: "sound-list" }, Object.keys(AlertRender.SOUNDS).map((id) => soundRow("builtin:" + id, t("Editor.Snd." + id))));
}

async function loadMedia() {
  try {
    media = (await api("/api/media")).files || [];
  } catch (e) {
    media = [];
  }
}

async function upload(files) {
  for (const file of files) {
    try {
      await api("/api/media?name=" + encodeURIComponent(file.name), { method: "POST", body: file });
    } catch (e) {
      alert(t("Editor.UploadFailed", file.name, e.message));
    }
  }
  await loadMedia();
  renderPicker();
}

function filesTab(body) {
  const accept = picker.kind === "image" ? "image/gif,image/png,image/jpeg,image/webp,video/webm,video/mp4" : "audio/mpeg,audio/wav,audio/ogg,.mp3,.wav,.ogg";
  const input = el("input", { type: "file", multiple: true, accept, hidden: true });
  input.addEventListener("change", () => upload([...input.files]));
  const pick = el("button", { type: "button", class: "btn primary", text: t("Editor.Upload") });
  pick.addEventListener("click", () => input.click());
  const drop = el("div", { class: "drop" }, el("p", { text: t(picker.kind === "image" ? "Editor.DropImage" : "Editor.DropSound") }), pick, input);
  drop.addEventListener("dragover", (e) => { e.preventDefault(); drop.classList.add("over"); });
  drop.addEventListener("dragleave", () => drop.classList.remove("over"));
  drop.addEventListener("drop", (e) => {
    e.preventDefault();
    drop.classList.remove("over");
    upload([...e.dataTransfer.files]);
  });
  body.append(drop);

  const kinds = picker.kind === "image" ? ["image", "video"] : ["audio"];
  const files = media.filter((f) => kinds.includes(f.kind));
  if (!files.length) {
    body.append(el("p", { class: "hint", text: t("Editor.NoFiles") }));
    return;
  }
  const remove = (name) => async (e) => {
    e.stopPropagation();
    if (!confirm(t("Editor.DeleteConfirm", name))) return;
    try {
      await api("/api/media/delete?name=" + encodeURIComponent(name), { method: "POST" });
    } catch (err) { /* already gone */ }
    await loadMedia();
    renderPicker();
  };
  if (picker.kind === "sound") {
    body.append(el("div", { class: "sound-list" }, files.map((f) => {
      const del = el("button", { type: "button", class: "icon", title: t("Editor.Delete"), text: "🗑" });
      del.addEventListener("click", remove(f.name));
      return soundRow("media:" + f.name, f.name, del);
    })));
    return;
  }
  body.append(el("div", { class: "grid" }, files.map((f) => {
    const ref = "media:" + f.name;
    const del = el("span", { class: "icon del", role: "button", title: t("Editor.Delete"), text: "✕" });
    del.addEventListener("click", remove(f.name));
    const tile = el("button", { type: "button", class: "tile" + (picker.value === ref ? " active" : "") },
      thumbFor(ref), el("span", { class: "name", text: f.name }), del);
    tile.addEventListener("click", () => choose(ref));
    return tile;
  })));
}

function linkTab() {
  const input = el("input", { type: "url", placeholder: "https://…", value: /^https?:/.test(picker.value) ? picker.value : "" });
  const holder = el("div", { style: "margin-top:12px" });
  const show = () => {
    holder.replaceChildren();
    if (/^https?:\/\/\S+$/.test(input.value.trim()) && picker.kind === "image") holder.append(thumbFor(input.value.trim()));
  };
  input.addEventListener("input", show);
  show();
  const use = el("button", { type: "button", class: "btn primary", text: t("Editor.Use") });
  use.addEventListener("click", () => {
    const v = input.value.trim();
    if (/^https?:\/\/\S+$/.test(v)) choose(v);
  });
  return el("div", {}, el("p", { class: "hint", text: t(picker.kind === "image" ? "Editor.LinkImageNote" : "Editor.LinkSoundNote") }),
    el("div", { class: "searchbar" }, input, use), holder);
}

function searchTab(body) {
  const integrations = config.integrations;
  const provider = integrations.provider;
  const keyField = provider === "giphy" ? "giphyKey" : "tenorKey";
  const providerSeg = segCtl([["giphy", "GIPHY"], ["tenor", "Tenor"]], provider, (v) => {
    integrations.provider = v;
    scheduleSave();
    renderPicker();
  });
  if (!integrations[keyField]) {
    const input = el("input", { type: "text", placeholder: t("Editor.KeyPlaceholder"), spellcheck: "false" });
    const saveKey = el("button", { type: "button", class: "btn primary", text: t("Editor.SaveKey") });
    saveKey.addEventListener("click", () => {
      if (!input.value.trim()) return;
      integrations[keyField] = input.value.trim();
      scheduleSave();
      renderPicker();
    });
    const link = provider === "giphy" ? "https://developers.giphy.com/dashboard/" : "https://developers.google.com/tenor/guides/quickstart";
    body.append(el("div", { class: "keybox" }, providerSeg,
      el("p", { text: t(provider === "giphy" ? "Editor.KeyHelpGiphy" : "Editor.KeyHelpTenor") }),
      el("a", { href: link, target: "_blank", rel: "noopener", text: t("Editor.GetKey") }),
      el("div", { class: "searchbar" }, input, saveKey)));
    return;
  }

  let stickers = picker.stickers ?? true;
  const input = el("input", { type: "search", placeholder: t("Editor.SearchPlaceholder"), value: picker.query || "" });
  const results = el("div", { class: "grid" });
  const status = el("p", { class: "hint" });
  const kindSeg = segCtl([["1", t("Editor.Stickers")], ["0", "GIFs"]], stickers ? "1" : "0", (v) => {
    stickers = v === "1";
    picker.stickers = stickers;
    run();
  });
  async function run() {
    picker.query = input.value.trim();
    status.textContent = t("Editor.Searching");
    results.replaceChildren();
    try {
      const items = await searchGifs(provider, integrations[keyField], picker.query, stickers);
      status.textContent = items.length ? "" : t("Editor.NoResults");
      results.replaceChildren(...items.map((g) => {
        const tile = el("button", { type: "button", class: "tile" }, el("div", { class: "thumb" }, el("img", { src: g.preview, alt: g.title || "", loading: "lazy" })));
        tile.addEventListener("click", () => choose(g.url));
        return tile;
      }));
    } catch (e) {
      status.textContent = t("Editor.SearchFailed", e.message);
    }
  }
  let timer = null;
  input.addEventListener("input", () => { clearTimeout(timer); timer = setTimeout(run, 400); });
  body.append(el("div", { class: "searchbar" }, input, kindSeg, providerSeg), status, results,
    el("p", { class: "attrib", text: provider === "giphy" ? "Powered by GIPHY" : "Via Tenor" }));
  run();
}

async function searchGifs(provider, key, query, stickers) {
  const lang = S.lang === "pt" ? "pt" : "en";
  if (provider === "giphy") {
    const kind = stickers ? "stickers" : "gifs";
    const url = query
      ? `https://api.giphy.com/v1/${kind}/search?api_key=${encodeURIComponent(key)}&q=${encodeURIComponent(query)}&limit=30&rating=pg-13&lang=${lang}`
      : `https://api.giphy.com/v1/${kind}/trending?api_key=${encodeURIComponent(key)}&limit=30&rating=pg-13`;
    const res = await fetch(url);
    if (!res.ok) throw new Error(res.status === 401 || res.status === 403 ? t("Editor.BadKey") : String(res.status));
    const data = await res.json();
    return (data.data || []).map((g) => ({
      title: g.title,
      preview: (g.images.fixed_width_small || g.images.fixed_height_small || g.images.original).url,
      url: g.images.original.url.split("?")[0],
    }));
  }
  const filter = stickers ? "gif_transparent,tinygif_transparent" : "gif,tinygif";
  const base = query ? `https://tenor.googleapis.com/v2/search?q=${encodeURIComponent(query)}&` : "https://tenor.googleapis.com/v2/featured?";
  const url = `${base}key=${encodeURIComponent(key)}&client_key=meketreve_obs&limit=30&media_filter=${filter}` +
    `${stickers ? "&searchfilter=sticker" : ""}&locale=${lang === "pt" ? "pt_BR" : "en_US"}&contentfilter=medium`;
  const res = await fetch(url);
  if (!res.ok) throw new Error(res.status === 400 || res.status === 403 ? t("Editor.BadKey") : String(res.status));
  const data = await res.json();
  return (data.results || []).map((r) => {
    const f = r.media_formats || {};
    const small = f.tinygif || f.tinygif_transparent || f.gif || f.gif_transparent;
    const big = f.gif || f.gif_transparent || small;
    return { title: r.content_description, preview: small && small.url, url: big && big.url };
  }).filter((g) => g.url);
}

// ---- start ----
async function init() {
  try {
    S = await api("/api/i18n");
  } catch (e) {
    S = {};
  }
  document.querySelectorAll("[data-i18n]").forEach((n) => { n.textContent = t(n.dataset.i18n); });
  document.title = "Meketreve · " + t("Editor.Title");
  document.documentElement.lang = S.lang === "pt" ? "pt-BR" : "en";
  if (!token) {
    fatal(t("Editor.NoToken"));
    return;
  }
  try {
    config = await api("/api/config");
    samples = await api("/api/sample");
  } catch (e) {
    fatal(e.message === "forbidden" ? t("Editor.BadToken") : t("Editor.LoadFailed", e.message));
    return;
  }
  const fonts = el("datalist", { id: "fonts" }, FONTS.map((f) => el("option", { value: f })));
  document.body.append(fonts);
  document.getElementById("app").hidden = false;
  document.getElementById("previewBtn").addEventListener("click", () => preview(true));
  document.getElementById("liveBtn").addEventListener("click", testLive);
  new ResizeObserver(fitPreview).observe(document.getElementById("frame"));
  fitPreview();
  renderNav();
  renderForm();
  loadMedia();
  schedulePreview();
}

init();
