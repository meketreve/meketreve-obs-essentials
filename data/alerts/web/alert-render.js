// Shared by the overlay (/alertas) and the editor's preview (/editor): what an
// alert looks like and sounds like. The default kit is drawn and synthesized
// here, so it needs no image or sound files.
"use strict";

const AlertRender = (() => {
  const SYSTEM_FONTS = new Set([
    "arial", "arial black", "impact", "verdana", "tahoma", "trebuchet ms",
    "georgia", "times new roman", "courier new", "comic sans ms", "sans-serif",
    "serif", "monospace", "system-ui",
  ]);

  const PLATFORM_NAMES = { twitch: "Twitch", youtube: "YouTube", kick: "Kick" };

  // ---- Default drawings (viewBox 0 0 100 100, "A" = accent colour) ----
  const DRAWINGS = {
    star: `<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff59d"/><stop offset="1" stop-color="A"/></linearGradient></defs>
      <g class="k-spin-slow"><path d="M50 6l12.6 27.4 29.9 3.3-22.3 20.2 6.2 29.4L50 71.4 23.6 86.3l6.2-29.4L7.5 36.7l29.9-3.3z" fill="url(#g)" stroke="#fff" stroke-width="3" stroke-linejoin="round"/></g>
      <circle class="k-twinkle" cx="88" cy="14" r="4" fill="#fff"/><circle class="k-twinkle k-d2" cx="12" cy="80" r="3" fill="#fff"/><circle class="k-twinkle k-d3" cx="90" cy="78" r="2.5" fill="#fff"/>`,
    crown: `<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff8e1"/><stop offset="1" stop-color="A"/></linearGradient></defs>
      <g class="k-bob"><path d="M12 72L6 28l24 18L50 14l20 32 24-18-6 44z" fill="url(#g)" stroke="#fff" stroke-width="3" stroke-linejoin="round"/>
      <rect x="12" y="74" width="76" height="12" rx="4" fill="A" stroke="#fff" stroke-width="3"/>
      <circle cx="50" cy="52" r="7" fill="#e91e63" stroke="#fff" stroke-width="2"/><circle cx="28" cy="58" r="5" fill="#29b6f6" stroke="#fff" stroke-width="2"/><circle cx="72" cy="58" r="5" fill="#66bb6a" stroke="#fff" stroke-width="2"/></g>
      <circle class="k-twinkle" cx="50" cy="6" r="3" fill="#fff"/>`,
    gift: `<g class="k-bob"><rect x="14" y="44" width="72" height="46" rx="5" fill="A" stroke="#fff" stroke-width="3"/>
      <rect x="44" y="44" width="12" height="46" fill="#fff" opacity=".85"/>
      <g class="k-lid"><rect x="8" y="30" width="84" height="16" rx="4" fill="A" stroke="#fff" stroke-width="3"/><rect x="44" y="30" width="12" height="16" fill="#fff" opacity=".85"/>
      <path d="M50 30c-10-18-30-16-24-4 3 5 14 5 24 4zm0 0c10-18 30-16 24-4-3 5-14 5-24 4z" fill="#fff"/></g></g>`,
    gifts: `<g class="k-bob"><rect x="46" y="50" width="46" height="36" rx="4" fill="#fff" opacity=".25" stroke="#fff" stroke-width="2"/>
      <rect x="6" y="46" width="52" height="42" rx="5" fill="A" stroke="#fff" stroke-width="3"/><rect x="27" y="46" width="10" height="42" fill="#fff" opacity=".85"/>
      <g class="k-lid"><rect x="2" y="34" width="60" height="14" rx="4" fill="A" stroke="#fff" stroke-width="3"/><rect x="27" y="34" width="10" height="14" fill="#fff" opacity=".85"/>
      <path d="M32 34c-8-14-24-12-19-3 3 4 11 4 19 3zm0 0c8-14 24-12 19-3-3 4-11 4-19 3z" fill="#fff"/></g>
      <text x="76" y="40" font-size="26" font-weight="900" fill="#fff" text-anchor="middle" font-family="Arial Black,sans-serif">×</text></g>`,
    gem: `<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff"/><stop offset=".45" stop-color="A"/><stop offset="1" stop-color="#311b92"/></linearGradient></defs>
      <g class="k-bob"><path d="M28 14h44l20 24-42 50L8 38z" fill="url(#g)" stroke="#fff" stroke-width="3" stroke-linejoin="round"/>
      <path d="M8 38h84M28 14l10 24 12 50M72 14l-10 24-12 50M38 38l12-24 12 24" fill="none" stroke="#fff" stroke-width="2" opacity=".7"/></g>
      <circle class="k-twinkle" cx="78" cy="22" r="4" fill="#fff"/><circle class="k-twinkle k-d2" cx="20" cy="70" r="3" fill="#fff"/>`,
    coins: `<g class="k-bob"><ellipse cx="40" cy="82" rx="28" ry="9" fill="#f9a825" stroke="#fff" stroke-width="3"/><rect x="12" y="66" width="56" height="16" fill="#fbc02d"/><ellipse cx="40" cy="66" rx="28" ry="9" fill="#ffeb3b" stroke="#fff" stroke-width="3"/>
      <rect x="12" y="52" width="56" height="14" fill="#fbc02d"/><ellipse cx="40" cy="52" rx="28" ry="9" fill="#ffee58" stroke="#fff" stroke-width="3"/></g>
      <g class="k-flip"><circle cx="74" cy="30" r="18" fill="#ffd54f" stroke="#fff" stroke-width="3"/><text x="74" y="38" font-size="22" font-weight="900" fill="A" text-anchor="middle" font-family="Arial Black,sans-serif">$</text></g>`,
    rocket: `<g class="k-shake"><path d="M50 6c18 14 22 38 14 60H36C28 44 32 20 50 6z" fill="#eceff1" stroke="#fff" stroke-width="3"/>
      <circle cx="50" cy="34" r="8" fill="A" stroke="#fff" stroke-width="3"/>
      <path d="M36 50L20 68l16 2zM64 50l16 18-16 2z" fill="A" stroke="#fff" stroke-width="3" stroke-linejoin="round"/>
      <path class="k-flame" d="M40 68h20l-10 26z" fill="#ffab00"/><path class="k-flame k-d2" d="M44 68h12l-6 16z" fill="#fff59d"/></g>`,
    medal: `<g class="k-bob"><path d="M30 6h14l10 34H40zM70 6H56L46 40h14z" fill="A" stroke="#fff" stroke-width="3" stroke-linejoin="round"/>
      <circle cx="50" cy="64" r="28" fill="#ffca28" stroke="#fff" stroke-width="3"/><circle cx="50" cy="64" r="19" fill="none" stroke="#fff" stroke-width="2" opacity=".8"/>
      <path d="M50 50l4.4 9 9.9 1.4-7.2 7 1.7 9.8L50 72.6l-8.8 4.6 1.7-9.8-7.2-7 9.9-1.4z" fill="#fff"/></g>`,
    heart: `<g class="k-beat"><path d="M50 88S8 62 8 34c0-14 10-24 22-24 9 0 16 5 20 12 4-7 11-12 20-12 12 0 22 10 22 24 0 28-42 54-42 54z" fill="A" stroke="#fff" stroke-width="3" stroke-linejoin="round"/>
      <ellipse cx="30" cy="30" rx="8" ry="5" fill="#fff" opacity=".5" transform="rotate(-30 30 30)"/></g>`,
  };

  function drawing(id, accent) {
    const body = DRAWINGS[id];
    if (!body) return null;
    const uid = "g" + Math.random().toString(36).slice(2, 8);
    const svg = body.replace(/\bA\b/g, accent).replace(/id="g"/g, `id="${uid}"`).replace(/url\(#g\)/g, `url(#${uid})`);
    const wrap = document.createElement("div");
    wrap.className = "ar-drawing";
    wrap.innerHTML = `<svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">${svg}</svg>`;
    return wrap;
  }

  // ---- Default sounds (Web Audio) ----
  let audioCtx = null;
  function ctx() {
    if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    if (audioCtx.state === "suspended") audioCtx.resume();
    return audioCtx;
  }

  function tone(ac, out, { freq, type = "sine", start = 0, length = 0.3, gain = 0.4, to = null, attack = 0.005 }) {
    const osc = ac.createOscillator();
    const g = ac.createGain();
    const t0 = ac.currentTime + start;
    osc.type = type;
    osc.frequency.setValueAtTime(freq, t0);
    if (to) osc.frequency.exponentialRampToValueAtTime(to, t0 + length);
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.exponentialRampToValueAtTime(gain, t0 + attack);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + length);
    osc.connect(g).connect(out);
    osc.start(t0);
    osc.stop(t0 + length + 0.05);
  }

  function noise(ac, out, { start = 0, length = 0.6, gain = 0.3, from = 400, to = 4000 }) {
    const buffer = ac.createBuffer(1, Math.floor(ac.sampleRate * length), ac.sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
    const src = ac.createBufferSource();
    src.buffer = buffer;
    const filter = ac.createBiquadFilter();
    filter.type = "bandpass";
    filter.Q.value = 1.2;
    const t0 = ac.currentTime + start;
    filter.frequency.setValueAtTime(from, t0);
    filter.frequency.exponentialRampToValueAtTime(to, t0 + length);
    const g = ac.createGain();
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.exponentialRampToValueAtTime(gain, t0 + length * 0.4);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + length);
    src.connect(filter).connect(g).connect(out);
    src.start(t0);
  }

  const SOUNDS = {
    pop: (ac, out) => {
      tone(ac, out, { freq: 900, to: 260, length: 0.14, gain: 0.6 });
      tone(ac, out, { freq: 1400, to: 500, start: 0.09, length: 0.12, gain: 0.35 });
      return 0.3;
    },
    chime: (ac, out) => {
      [[880, 0], [1318.5, 0.12], [1760, 0.24]].forEach(([f, s]) => {
        tone(ac, out, { freq: f, start: s, length: 1.2, gain: 0.3 });
        tone(ac, out, { freq: f * 2.01, start: s, length: 0.6, gain: 0.08 });
      });
      return 1.5;
    },
    fanfare: (ac, out) => {
      const notes = [[523.3, 0, 0.14], [659.3, 0.14, 0.14], [784, 0.28, 0.14], [1046.5, 0.42, 0.7]];
      notes.forEach(([f, s, l]) => {
        tone(ac, out, { freq: f, type: "sawtooth", start: s, length: l, gain: 0.12, attack: 0.02 });
        tone(ac, out, { freq: f, type: "triangle", start: s, length: l, gain: 0.3, attack: 0.02 });
      });
      tone(ac, out, { freq: 1318.5, type: "triangle", start: 0.42, length: 0.7, gain: 0.18, attack: 0.02 });
      return 1.2;
    },
    coins: (ac, out) => {
      for (let i = 0; i < 6; i++) {
        tone(ac, out, { freq: 1567.98, type: "square", start: i * 0.09, length: 0.07, gain: 0.08 });
        tone(ac, out, { freq: 2093, type: "square", start: i * 0.09 + 0.045, length: 0.2, gain: 0.08 });
      }
      return 0.8;
    },
    levelup: (ac, out) => {
      [523.3, 587.3, 659.3, 784, 880, 1046.5, 1318.5].forEach((f, i) =>
        tone(ac, out, { freq: f, type: "square", start: i * 0.07, length: i === 6 ? 0.6 : 0.09, gain: 0.1 }));
      return 1.1;
    },
    whoosh: (ac, out) => {
      noise(ac, out, { length: 0.8, gain: 0.5, from: 300, to: 5000 });
      tone(ac, out, { freq: 110, to: 45, start: 0.55, length: 0.6, gain: 0.7 });
      tone(ac, out, { freq: 784, type: "triangle", start: 0.7, length: 0.5, gain: 0.2 });
      return 1.4;
    },
    sparkle: (ac, out) => {
      for (let i = 0; i < 10; i++)
        tone(ac, out, { freq: 1800 + Math.random() * 2400, start: i * 0.06, length: 0.25, gain: 0.12 });
      return 0.9;
    },
  };

  // Plays a sound reference; resolves when it is done (or fails).
  function playSound(ref, volume) {
    if (!ref || volume <= 0) return Promise.resolve();
    if (ref.startsWith("builtin:")) {
      const make = SOUNDS[ref.slice(8)];
      if (!make) return Promise.resolve();
      try {
        const ac = ctx();
        const out = ac.createGain();
        out.gain.value = volume;
        out.connect(ac.destination);
        const seconds = make(ac, out);
        return new Promise((r) => setTimeout(r, seconds * 1000));
      } catch (e) {
        return Promise.resolve();
      }
    }
    return playUrl(resolve(ref), volume);
  }

  function playUrl(url, volume) {
    return new Promise((done) => {
      const audio = new Audio(url);
      audio.volume = Math.max(0, Math.min(1, volume));
      const finish = () => done();
      audio.onended = finish;
      audio.onerror = finish;
      audio.play().catch(finish);
      current.audios.push(audio);
    });
  }

  function resolve(ref) {
    if (!ref) return "";
    if (ref.startsWith("media:")) return "/media/" + encodeURIComponent(ref.slice(6));
    return ref;
  }

  function isVideo(url) {
    return /\.(webm|mp4)(\?|$)/i.test(url) || /\/video\//.test(url);
  }

  // ---- Text ----
  function escapeHtml(s) {
    return String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
  }

  function renderText(template, alert) {
    const values = {
      nome: alert.name, name: alert.name,
      quantidade: alert.amount, amount: alert.amount,
      mensagem: alert.message, message: alert.message,
      plataforma: PLATFORM_NAMES[alert.platform] || alert.platform, platform: PLATFORM_NAMES[alert.platform] || alert.platform,
      detalhe: alert.detail, detail: alert.detail,
    };
    return escapeHtml(template).replace(/\{(\w+)\}/g, (all, key) =>
      key in values ? `<span class="ar-hl">${escapeHtml(values[key])}</span>` : all);
  }

  const loadedFonts = new Set();
  function loadFont(family) {
    const name = (family || "").trim();
    if (!name || SYSTEM_FONTS.has(name.toLowerCase()) || loadedFonts.has(name)) return;
    loadedFonts.add(name);
    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = `https://fonts.googleapis.com/css2?family=${encodeURIComponent(name).replace(/%20/g, "+")}:wght@400;700;900&display=swap`;
    document.head.appendChild(link);
  }

  // ---- Showing one alert ----
  const current = { box: null, audios: [], skip: null };

  function sleep(ms, skipSignal) {
    return new Promise((r) => {
      const t = setTimeout(r, ms);
      if (skipSignal) skipSignal.push(() => { clearTimeout(t); r(); });
    });
  }

  function buildBox(config, type, alert) {
    const box = document.createElement("div");
    box.className = `ar-box ar-layout-${type.layout}`;
    box.style.setProperty("--ar-accent", type.accent);
    box.style.setProperty("--ar-text", type.textColor);
    box.style.setProperty("--ar-size", `${type.fontSize}px`);
    box.style.setProperty("--ar-image", `${type.imageSize}px`);
    box.style.fontFamily = `"${type.font}", "Poppins", "Arial Black", system-ui, sans-serif`;
    loadFont(type.font);

    if (type.layout !== "text" && type.image && type.imageSize > 0) {
      const media = document.createElement("div");
      media.className = "ar-media";
      let el = null;
      if (type.image.startsWith("builtin:")) {
        el = drawing(type.image.slice(8), type.accent);
        const burst = document.createElement("div");
        burst.className = "ar-burst";
        for (let i = 0; i < 12; i++) {
          const dot = document.createElement("i");
          dot.style.setProperty("--a", `${i * 30}deg`);
          burst.appendChild(dot);
        }
        media.appendChild(burst);
      } else {
        const url = resolve(type.image);
        if (isVideo(url)) {
          el = document.createElement("video");
          el.src = url;
          el.autoplay = true;
          el.playsInline = true;
          el.volume = Math.max(0, Math.min(1, (config.volume / 100) * (type.volume / 100)));
        } else {
          el = document.createElement("img");
          el.src = url;
          el.alt = "";
        }
      }
      if (el) media.appendChild(el);
      box.appendChild(media);
    }

    const texts = document.createElement("div");
    texts.className = "ar-texts";
    const title = document.createElement("div");
    title.className = "ar-title";
    title.innerHTML = renderText(type.text, alert);
    texts.appendChild(title);
    if (type.showMessage && alert.message && !/\{(mensagem|message)\}/.test(type.text)) {
      const message = document.createElement("div");
      message.className = "ar-message";
      message.textContent = alert.message;
      texts.appendChild(message);
    }
    box.appendChild(texts);
    return box;
  }

  // Shows the alert inside `stage` (a 1920x1080-ish element) and resolves
  // when it is gone. `ttsUrl` is read after the sound.
  async function show(stage, config, alert, ttsUrl) {
    const type = config.types && config.types[alert.type];
    if (!type) return;
    const skipSignal = [];
    const audios = [];
    current.skip = () => skipSignal.splice(0).forEach((f) => f());
    current.audios = audios;

    stage.dataset.position = config.position;
    stage.style.setProperty("--ar-margin", `${config.margin}px`);
    const box = buildBox(config, type, alert);
    current.box = box;
    box.classList.add(`ar-in-${type.animIn}`);
    stage.appendChild(box);

    const volume = (config.volume / 100) * (type.volume / 100);
    const sound = (async () => {
      await playSound(type.sound, volume);
      if (ttsUrl) await playUrl(ttsUrl, Math.max(volume, 0.6));
    })();
    const minimum = sleep(type.duration * 1000, skipSignal);
    // Long speech keeps the alert up, but never forever.
    await Promise.race([Promise.all([minimum, sound]), sleep((type.duration + 30) * 1000, skipSignal)]);

    box.classList.remove(`ar-in-${type.animIn}`);
    box.classList.add(`ar-out-${type.animOut}`);
    await sleep(type.animOut === "none" ? 0 : 600);
    audios.forEach((a) => { try { a.pause(); } catch (e) { /* gone */ } });
    box.remove();
    // A newer alert (the editor's preview) may have taken over already.
    if (current.box === box) {
      current.box = null;
      current.skip = null;
    }
  }

  function skip() {
    if (current.skip) current.skip();
    current.audios.forEach((a) => { try { a.pause(); } catch (e) { /* gone */ } });
  }

  return { show, skip, playSound, drawing, renderText, resolve, loadFont, DRAWINGS, SOUNDS, isVideo };
})();
