// Draws the goals overlay (/metas) and the goals editor's preview: a title,
// "current / target" and a bar per goal. Uses ChatRender for the fonts.
// Text goes in as text nodes.
"use strict";

const GoalsRender = (() => {
  function rgba(hex, opacity) {
    const n = parseInt((hex || "#000000").slice(1), 16);
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${Math.max(0, Math.min(100, opacity)) / 100})`;
  }

  function apply(box, config) {
    ChatRender.loadFont(config.font);
    box.classList.add("gl-box");
    box.classList.toggle("gl-shadow", !!config.shadow);
    box.style.setProperty("--gl-font", `"${config.font}", "Poppins", sans-serif`);
    box.style.setProperty("--gl-size", config.fontSize + "px");
    box.style.setProperty("--gl-text", config.textColor);
    box.style.setProperty("--gl-accent", config.accent);
    box.style.setProperty("--gl-bubble", rgba(config.bubbleColor, config.bubbleOpacity));
  }

  function number(value, lang) {
    return new Intl.NumberFormat(lang === "pt" ? "pt-BR" : "en-US", { maximumFractionDigits: 2 }).format(value);
  }

  // Keeps the bars that are already there, so the width slides to the new value.
  function goal(node, data, lang) {
    if (!node) {
      node = document.createElement("div");
      node.innerHTML = '<div class="gl-head"><span class="gl-title"></span><span class="gl-count"></span></div>' +
        '<div class="gl-bar"><div class="gl-fill" style="width:0"></div></div>';
    }
    const done = data.current >= data.target;
    node.className = "gl-goal" + (done ? " gl-done" : "");
    node.dataset.id = data.id;
    node.querySelector(".gl-title").textContent = data.title;
    node.querySelector(".gl-count").textContent =
      `${data.prefix}${number(data.current, lang)} / ${data.prefix}${number(data.target, lang)}`;
    const percent = Math.max(0, Math.min(100, (data.current / data.target) * 100));
    requestAnimationFrame(() => { node.querySelector(".gl-fill").style.width = percent + "%"; });
    return node;
  }

  // Every goal, or only the one with that id.
  function draw(box, config, lang, only) {
    const goals = (config.goals || []).filter((g) => !only || g.id === only);
    const old = new Map([...box.children].map((n) => [n.dataset.id, n]));
    box.replaceChildren(...goals.map((g) => goal(old.get(g.id), g, lang)));
  }

  return { apply, draw, number };
})();
