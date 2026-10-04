// Draws the events overlay (/eventos) and the events editor's preview: the
// latest events as a list, or one label such as "Last sub: Someone". Uses
// ChatRender for the platform icons and fonts. Text goes in as text nodes.
"use strict";

const EventsRender = (() => {
  const MARKS = { follow: "♥", sub: "★", resub: "★", giftsub: "✚", bits: "◆", donation: "$", raid: "⚑",
    membership: "★", gift: "✚" };

  function rgba(hex, opacity) {
    const n = parseInt((hex || "#000000").slice(1), 16);
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${Math.max(0, Math.min(100, opacity)) / 100})`;
  }

  function apply(box, config) {
    ChatRender.loadFont(config.font);
    box.classList.add("ev-box");
    box.classList.toggle("ev-shadow", !!config.shadow);
    box.style.setProperty("--ev-font", `"${config.font}", "Poppins", sans-serif`);
    box.style.setProperty("--ev-size", config.fontSize + "px");
    box.style.setProperty("--ev-text", config.textColor);
    box.style.setProperty("--ev-accent", config.accent);
    box.style.setProperty("--ev-bubble", rgba(config.bubbleColor, config.bubbleOpacity));
  }

  function item(config, entry, fresh) {
    const node = document.createElement("div");
    node.className = "ev-item" + (fresh ? " ev-new" : "");
    if (config.showPlatform && entry.platform) node.append(ChatRender.icon(entry.platform));
    const mark = document.createElement("span");
    mark.className = "ev-mark";
    mark.textContent = MARKS[entry.type] || "★";
    node.append(mark);
    const text = document.createElement("span");
    text.className = "ev-line";
    text.textContent = entry.text || entry.name || "";
    node.append(text);
    return node;
  }

  // The latest events, newest on top, of the kinds turned on in the list.
  function list(box, config, recent, freshId) {
    const on = config.list.types || {};
    const shown = recent.filter((e) => on[e.type] !== false).slice(0, config.list.max);
    box.replaceChildren(...shown.map((e) => item(config, e, e.id && e.id === freshId)));
  }

  // "Último sub: {nome}" with the latest one, or the empty text.
  function fill(template, data, emptyText) {
    const values = data ? { nome: data.name, name: data.name, quantidade: data.amount, amount: data.amount }
      : { nome: emptyText, name: emptyText, quantidade: "", amount: "" };
    return (template || "").replace(/\{(nome|name|quantidade|amount)\}/g, (_, k) => values[k] || "").replace(/\(\s*\)/g, "").trim();
  }

  function label(box, config, kind, labels) {
    const data = labels[kind];
    const node = document.createElement("div");
    node.className = "ev-label";
    if (config.showPlatform && data && data.platform) node.append(ChatRender.icon(data.platform));
    const text = document.createElement("span");
    text.textContent = fill(config.labels[kind], data, config.emptyText);
    node.append(text);
    box.replaceChildren(node);
  }

  return { apply, list, label, fill };
})();
