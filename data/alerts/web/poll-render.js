// Draws the poll overlay (/enquete) and the poll editor's preview: the
// question, a bar per option and a line with how to vote and the time left
// (or the result). Uses ChatRender for the fonts. Text goes in as text nodes.
"use strict";

const PollRender = (() => {
  function rgba(hex, opacity) {
    const n = parseInt((hex || "#000000").slice(1), 16);
    return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${Math.max(0, Math.min(100, opacity)) / 100})`;
  }

  function apply(box, config) {
    ChatRender.loadFont(config.font);
    box.classList.add("pl-box");
    box.classList.toggle("pl-shadow", !!config.shadow);
    box.style.setProperty("--pl-font", `"${config.font}", "Poppins", sans-serif`);
    box.style.setProperty("--pl-size", config.fontSize + "px");
    box.style.setProperty("--pl-text", config.textColor);
    box.style.setProperty("--pl-accent", config.accent);
    box.style.setProperty("--pl-bubble", rgba(config.bubbleColor, config.bubbleOpacity));
  }

  function clock(ms) {
    const s = Math.ceil(ms / 1000);
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
  }

  function fill(template, values) {
    return (template || "").replace(/%(\d)/g, (_, n) => (values[n - 1] !== undefined ? values[n - 1] : ""));
  }

  function div(className, text) {
    const node = document.createElement("div");
    node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }

  // poll as the plugin sends it; remainingMs counted from now by the caller.
  // texts: {vote, result, noVotes}. Empty box when there is nothing to show.
  function draw(box, poll, texts, remainingMs) {
    if (!poll || !poll.options) {
      box.replaceChildren();
      return;
    }
    let card = box.querySelector(".pl-card");
    if (!card || card.dataset.question !== poll.question || card.children.length !== poll.options.length + 2) {
      card = div("pl-card");
      card.dataset.question = poll.question;
      card.append(div("pl-question", poll.question));
      poll.options.forEach((o, i) => {
        const option = div("pl-option");
        const head = div("pl-head");
        const num = document.createElement("span");
        num.className = "pl-num";
        num.textContent = i + 1;
        const text = document.createElement("span");
        text.className = "pl-text";
        text.textContent = o.text;
        const pct = document.createElement("span");
        pct.className = "pl-pct";
        head.append(num, text, pct);
        const bar = div("pl-bar");
        const barFill = div("pl-fill");
        barFill.style.width = "0";
        bar.append(barFill);
        option.append(head, bar);
        card.append(option);
      });
      card.append(div("pl-foot"));
      box.replaceChildren(card);
    }
    card.classList.toggle("pl-closed", !poll.open);
    const options = card.querySelectorAll(".pl-option");
    poll.options.forEach((o, i) => {
      const share = poll.total ? Math.round((o.votes / poll.total) * 100) : 0;
      options[i].classList.toggle("pl-win", !poll.open && poll.winner === i + 1);
      options[i].querySelector(".pl-pct").textContent = `${share}% (${o.votes})`;
      const barFill = options[i].querySelector(".pl-fill");
      requestAnimationFrame(() => { barFill.style.width = share + "%"; });
    });
    const foot = card.querySelector(".pl-foot");
    if (poll.open) {
      const how = fill(texts.vote, [poll.options.length]);
      foot.textContent = poll.timed ? `${how} · ${clock(remainingMs)}` : how;
    } else if (poll.winner > 0) {
      foot.textContent = fill(texts.result, [poll.options[poll.winner - 1].text, poll.total]);
    } else {
      foot.textContent = texts.noVotes || "";
    }
  }

  return { apply, draw, clock };
})();
