// Draws chat lines for the chat-on-screen Browser Source and the editor's
// preview. Text always goes in as text nodes, never as HTML.
"use strict";

const ChatRender = (() => {
  const SYSTEM_FONTS = new Set([
    "arial", "arial black", "impact", "verdana", "tahoma", "trebuchet ms",
    "georgia", "times new roman", "courier new", "comic sans ms", "sans-serif",
  ]);
  const loadedFonts = new Set();
  // Names without a color get one picked from the name, so they stay the same.
  const PALETTE = ["#FF6B6B", "#FFB347", "#FFD93D", "#6BCB77", "#4D96FF", "#9B5DE5", "#F15BB5", "#00BBF9",
    "#00F5D4", "#FF7F50"];
  const SVG = "http://www.w3.org/2000/svg";

  // [viewBox, [[fill, path], ...]] drawn at the text size.
  const ICONS = {
    twitch: ["0 0 16 16", [
      ["#9146FF", "M2.5 1 1.5 4v9.5h3.25V15h1.9l1.85-1.5h2.6L15 9.6V1z"],
      ["#FFFFFF", "M3.4 2.4h10.2v6.6l-2.3 2.3H8.6l-1.9 1.6v-1.6H3.4z"],
      ["#9146FF", "M7.2 4.6h1.3v3.5H7.2zm3.4 0h1.3v3.5h-1.3z"]]],
    youtube: ["0 0 16 16", [
      ["#FF0033", "M15.6 4.3a2 2 0 0 0-1.4-1.4C13 2.6 8 2.6 8 2.6s-5 0-6.2.3A2 2 0 0 0 .4 4.3 21 21 0 0 0 .1 8a21 21 0 0 0 .3 3.7 2 2 0 0 0 1.4 1.4c1.2.3 6.2.3 6.2.3s5 0 6.2-.3a2 2 0 0 0 1.4-1.4c.3-1.2.3-3.7.3-3.7s0-2.5-.3-3.7"],
      ["#FFFFFF", "M6.4 10.3 10.6 8 6.4 5.7z"]]],
    kick: ["0 0 16 16", [
      ["#53FC18", "M0 3a3 3 0 0 1 3-3h10a3 3 0 0 1 3 3v10a3 3 0 0 1-3 3H3a3 3 0 0 1-3-3z"],
      ["#000000", "M3.5 3h3v3h1.2V4.5h1.6V3h3v3.5h-1.6V8h1.6v4.5h-3V11H7.7V9.5H6.5V13h-3z"]]],
    broadcaster: ["0 0 16 16", [
      ["#FFC83D", "M1 4.5 4.6 7.6 8 2.5l3.4 5.1L15 4.5 13.6 13H2.4z"]]],
    mod: ["0 0 16 16", [
      ["#00AD03", "M2 1h12v14H2z"],
      ["#FFFFFF", "M10.8 3.2 12.8 3.2 12.8 5.2 7.6 10.4 8.6 11.4 7.6 12.4 6.6 11.4 4.8 13.2 3.8 12.2 5.6 10.4 4.6 9.4 5.6 8.4 6.6 9.4z"]]],
    sub: ["0 0 16 16", [
      ["#B98BFF", "m8 1 2.1 4.6 5 .5-3.8 3.4 1.1 5L8 12l-4.4 2.5 1.1-5L.9 6.1l5-.5z"]]],
  };

  function loadFont(family) {
    const name = (family || "").trim();
    if (!name || SYSTEM_FONTS.has(name.toLowerCase()) || loadedFonts.has(name)) return;
    loadedFonts.add(name);
    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = `https://fonts.googleapis.com/css2?family=${encodeURIComponent(name).replace(/%20/g, "+")}:wght@400;700&display=swap`;
    document.head.appendChild(link);
  }

  function icon(name, title) {
    const [viewBox, parts] = ICONS[name];
    const svg = document.createElementNS(SVG, "svg");
    svg.setAttribute("viewBox", viewBox);
    svg.setAttribute("class", "cr-icon");
    svg.setAttribute("aria-label", title || name);
    for (const [fill, d] of parts) {
      const path = document.createElementNS(SVG, "path");
      path.setAttribute("fill", fill);
      path.setAttribute("d", d);
      svg.append(path);
    }
    return svg;
  }

  function rgba(hex, opacity) {
    const n = parseInt((hex || "#000000").slice(1), 16);
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${Math.max(0, Math.min(100, opacity)) / 100})`;
  }

  function nameColor(msg) {
    if (/^#[0-9a-fA-F]{6}$/.test(msg.color || "")) return msg.color;
    let h = 0;
    for (const c of msg.author || "") h = (h * 31 + c.codePointAt(0)) >>> 0;
    return PALETTE[h % PALETTE.length];
  }

  // Style that applies to every line: set on the container.
  function apply(box, config) {
    loadFont(config.font);
    box.classList.add("cr-box");
    box.classList.toggle("cr-top", config.newest === "top");
    box.classList.toggle("cr-shadow", !!config.shadow);
    box.style.setProperty("--cr-font", `"${config.font}", "Poppins", sans-serif`);
    box.style.setProperty("--cr-size", config.fontSize + "px");
    box.style.setProperty("--cr-text", config.textColor);
    box.style.setProperty("--cr-bubble", rgba(config.bubbleColor, config.bubbleOpacity));
  }

  // The message text with emote pictures in place of their names.
  function body(msg) {
    const out = document.createElement("span");
    out.className = "cr-text";
    const text = msg.text || "";
    let at = 0;
    const emotes = (msg.emotes || []).filter((e) => /^https?:\/\//.test(e.url)).sort((a, b) => a.start - b.start);
    for (const e of emotes) {
      if (e.start < at || e.start + e.length > text.length) continue;
      if (e.start > at) out.append(text.slice(at, e.start));
      const img = document.createElement("img");
      img.className = "cr-emote";
      img.src = e.url;
      img.alt = text.substr(e.start, e.length);
      out.append(img);
      at = e.start + e.length;
    }
    if (at < text.length) out.append(text.slice(at));
    return out;
  }

  function line(config, msg) {
    const node = document.createElement("div");
    node.className = "cr-line" + (config.animation !== "none" ? " cr-in-" + config.animation : "");
    node.dataset.platform = msg.platform;
    node.dataset.id = msg.id || "";
    node.dataset.user = msg.user || "";
    const head = document.createElement("span");
    head.className = "cr-head";
    if (config.showPlatform && ICONS[msg.platform]) head.append(icon(msg.platform));
    if (config.showBadges) {
      if (msg.broadcaster) head.append(icon("broadcaster"));
      if (msg.mod) head.append(icon("mod"));
      if (msg.sub) head.append(icon("sub"));
    }
    const name = document.createElement("span");
    name.className = "cr-name";
    name.style.color = nameColor(msg);
    name.textContent = msg.author || "";
    head.append(name);
    node.append(head);
    if (msg.highlight) {
      const tag = document.createElement("span");
      tag.className = "cr-highlight";
      tag.textContent = msg.highlight;
      node.append(tag);
    }
    node.append(body(msg));
    return node;
  }

  function leave(node) {
    if (node.classList.contains("cr-out")) return;
    node.classList.add("cr-out");
    setTimeout(() => node.remove(), 400);
  }

  // A sub, raid, Super Chat... as a marked line among the messages.
  const EVENT_MARKS = { follow: "♥", sub: "★", resub: "★", giftsub: "✚", donation: "$", raid: "⚑", membership: "★",
    gift: "✚" };

  function eventLine(config, ev) {
    const node = document.createElement("div");
    node.className = "cr-line cr-event" + (config.animation !== "none" ? " cr-in-" + config.animation : "");
    node.style.setProperty("--cr-accent", config.eventColor || "#FFB300");
    node.dataset.platform = ev.platform;
    node.dataset.id = ev.id || "";
    node.dataset.user = "";
    const head = document.createElement("span");
    head.className = "cr-head";
    if (config.showPlatform && ICONS[ev.platform]) head.append(icon(ev.platform));
    const mark = document.createElement("span");
    mark.className = "cr-mark";
    mark.textContent = EVENT_MARKS[ev.type] || "★";
    head.append(mark);
    node.append(head);
    const text = document.createElement("span");
    text.className = "cr-event-text";
    text.textContent = ev.text || "";
    node.append(text);
    if (ev.message) {
      const said = document.createElement("div");
      said.className = "cr-event-message";
      said.textContent = ev.message;
      node.append(said);
    }
    return node;
  }

  function addEvent(box, config, ev) {
    return place(box, config, eventLine(config, ev));
  }

  function add(box, config, msg) {
    return place(box, config, line(config, msg));
  }

  function place(box, config, node) {
    if (config.newest === "top") box.prepend(node);
    else box.append(node);
    const lines = [...box.querySelectorAll(".cr-line:not(.cr-out)")];
    const extra = lines.length - config.maxMessages;
    if (extra > 0) {
      const oldest = config.newest === "top" ? lines.slice(-extra) : lines.slice(0, extra);
      // Over the limit they go at once: an animation would push the box around.
      oldest.forEach((n) => n.remove());
    }
    if (config.fadeAfter > 0) setTimeout(() => leave(node), config.fadeAfter * 1000);
    return node;
  }

  // A deleted message (id), everything from a timed out or banned user, or
  // the whole chat of a platform when its moderators clear it (all).
  function remove(box, { platform, id, user, all }) {
    for (const node of box.querySelectorAll(".cr-line")) {
      if (node.dataset.platform !== platform) continue;
      if (all || (id && node.dataset.id === id) || (user && node.dataset.user === user)) node.remove();
    }
  }

  return { apply, add, addEvent, remove, icon, loadFont };
})();
