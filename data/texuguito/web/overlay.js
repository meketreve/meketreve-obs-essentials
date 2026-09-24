const SPEED_PX_PER_SEC = 60;
const SPEED_MIN_FACTOR = 0.75;
const SPEED_SPREAD_FACTOR = 0.5;
const PIXELS_PER_WALK_FRAME = 8;

// Wander behaviour: walk to a random spot, stand there a while, repeat.
const MIN_WALK_DISTANCE_PX = 48;
const IDLE_MIN_MS = 1500;
const IDLE_MAX_MS = 5000;
// While standing, a viewer occasionally glances around (front / left / right).
const LOOK_MIN_MS = 1000;
const LOOK_MAX_MS = 2500;
const FACE_FRONT_CHANCE = 0.5;

const canvas = document.getElementById("parade");
const ctx = canvas.getContext("2d");

const viewers = new Map();

let manifest = null;
// Fallback until manifest.json loads; matches the real sprite size (64x64),
// so layout math run before the fetch resolves is already correct.
let frameWidth = 64;
let frameHeight = 64;

const imageCache = new Map(); // filename -> HTMLImageElement
const shadingMaskCache = new Map(); // "row:column" -> HTMLCanvasElement (grayscale)
const tintCache = new Map(); // "row:column|color" -> HTMLCanvasElement (tinted)

function loadImage(filename) {
  let img = imageCache.get(filename);
  if (!img) {
    img = new Image();
    img.failed = false;
    img.onerror = () => {
      img.failed = true;
      console.error(`[texuguito] falha ao carregar asset: ${filename}`);
    };
    img.src = `/static/assets/lpc/${filename}`;
    imageCache.set(filename, img);
  }
  return img;
}

async function loadManifest() {
  try {
    const response = await fetch("/static/assets/lpc/manifest.json");
    manifest = await response.json();
  } catch (err) {
    console.error("[texuguito] falha ao carregar manifest.json, tentando de novo em 3s:", err);
    setTimeout(loadManifest, 3000);
    return;
  }

  frameWidth = manifest.frame_width;
  frameHeight = manifest.frame_height;

  for (const layer of manifest.base_layers) loadImage(layer);
  for (const file of Object.values(manifest.hats)) loadImage(file);
  for (const file of Object.values(manifest.accessories)) loadImage(file);
  for (const wing of Object.values(manifest.wings)) {
    loadImage(wing.bg);
    loadImage(wing.fg);
  }
}

// True once every asset the manifest lists has settled — either finished
// loading or failed. A failed image (404, corrupt, decode error) has
// complete=true and naturalWidth=0 forever, so treating "not yet settled"
// as the only reason to wait means one bad asset can't block the overlay
// forever; drawSpriteLayer/getShadingMask separately skip failed images so
// a single missing asset degrades instead of blanking everything.
function assetsReady() {
  if (!manifest) return false;
  for (const img of imageCache.values()) {
    if (!img.failed && (!img.complete || img.naturalWidth === 0)) return false;
  }
  return true;
}

function laneY() {
  return canvas.height - frameHeight - 10;
}

// The OBS Browser Source can be any size the streamer picks, so track the
// window instead of assuming a hardcoded size. laneY() and randomX() both
// derive from the canvas dimensions, so they adapt on their own; existing
// viewers keep a y captured at upsert time, so re-seat them onto the new lane.
//
// Setting canvas.width/height resets the 2D context to its default state
// (per spec), which silently flips imageSmoothingEnabled back to true — so
// it has to be re-applied here, every time, not just once at module load.
function resizeCanvas() {
  canvas.width = window.innerWidth;
  canvas.height = window.innerHeight;
  ctx.imageSmoothingEnabled = false; // pixel art: never interpolate between texels
  const y = laneY();
  const maxX = Math.max(0, canvas.width - frameWidth);
  for (const viewer of viewers.values()) {
    viewer.y = y;
    viewer.x = Math.min(viewer.x, maxX);
    viewer.targetX = Math.min(viewer.targetX, maxX);
  }
}

function randomX(width) {
  return Math.random() * Math.max(0, canvas.width - width);
}

// Deterministic per-username initial walking direction, same idea as the
// server's old seeded avatar shape: same username always starts the same way.
function hashUsername(username) {
  let hash = 0;
  for (let i = 0; i < username.length; i++) {
    hash = (hash * 31 + username.charCodeAt(i)) | 0;
  }
  return hash;
}

function initialDirection(username) {
  return hashUsername(username) % 2 === 0 ? 1 : -1;
}

// Random speed, re-rolled at the start of every walk.
function randomSpeed() {
  return SPEED_PX_PER_SEC * (SPEED_MIN_FACTOR + Math.random() * SPEED_SPREAD_FACTOR);
}

function randomBetween(min, max) {
  return min + Math.random() * (max - min);
}

// Picks a random destination on the lane at least MIN_WALK_DISTANCE_PX away
// (when the lane is wide enough), so walks never degrade into a tiny shuffle.
function pickTargetX(fromX) {
  const maxX = Math.max(0, canvas.width - frameWidth);
  const minDistance = Math.min(MIN_WALK_DISTANCE_PX, maxX / 2);
  for (let attempt = 0; attempt < 8; attempt++) {
    const target = Math.random() * maxX;
    if (Math.abs(target - fromX) >= minDistance) return target;
  }
  return fromX < maxX / 2 ? maxX : 0;
}

function startWalking(viewer) {
  viewer.state = "walking";
  viewer.targetX = pickTargetX(viewer.x);
  viewer.direction = viewer.targetX >= viewer.x ? 1 : -1;
  viewer.speed = randomSpeed();
  viewer.distanceWalked = 0; // restart the walk cycle from its first stride
  viewer.facingFront = false;
}

function startIdle(viewer, now, durationMs = randomBetween(IDLE_MIN_MS, IDLE_MAX_MS)) {
  viewer.state = "idle";
  viewer.idleUntil = now + durationMs;
  viewer.nextLookAt = now + randomBetween(LOOK_MIN_MS, LOOK_MAX_MS);
  viewer.facingFront = false;
}

// Standing still: every so often turn to face the camera or either side.
function lookAround(viewer, now) {
  if (Math.random() < FACE_FRONT_CHANCE) {
    viewer.facingFront = true;
  } else {
    viewer.facingFront = false;
    viewer.direction = Math.random() < 0.5 ? 1 : -1;
  }
  viewer.nextLookAt = now + randomBetween(LOOK_MIN_MS, LOOK_MAX_MS);
}

function updateMovement(viewer, now, deltaSeconds) {
  if (viewer.state === "idle") {
    if (now >= viewer.idleUntil) {
      startWalking(viewer);
    } else if (now >= viewer.nextLookAt) {
      lookAround(viewer, now);
    }
    return;
  }

  const remaining = viewer.targetX - viewer.x;
  const travelled = Math.min(viewer.speed * deltaSeconds, Math.abs(remaining));
  viewer.x += Math.sign(remaining) * travelled;
  viewer.distanceWalked += travelled;
  if (Math.abs(viewer.targetX - viewer.x) < 0.5) {
    viewer.x = viewer.targetX;
    startIdle(viewer, now);
  }
}

// Client-side wander state that must survive server updates for a viewer.
const MOVEMENT_KEYS = [
  "x", "targetX", "direction", "speed", "distanceWalked",
  "state", "idleUntil", "nextLookAt", "facingFront",
];

function upsertViewer(payload) {
  const existing = viewers.get(payload.username);
  // The server sends seconds remaining, not booleans: turn them into absolute
  // deadlines on the same clock tick() uses so drawViewer can re-check them
  // every frame instead of latching a stale boolean until the next broadcast.
  const now = performance.now();
  const viewer = {
    ...payload,
    y: laneY(),
    danceUntil: now + (payload.dance_remaining || 0) * 1000,
    cheerUntil: now + (payload.cheer_remaining || 0) * 1000,
  };

  if (existing) {
    for (const key of MOVEMENT_KEYS) viewer[key] = existing[key];
  } else {
    viewer.x = randomX(frameWidth);
    viewer.targetX = viewer.x;
    viewer.direction = initialDirection(payload.username);
    viewer.speed = randomSpeed();
    viewer.distanceWalked = 0;
    // Short random first pause so a batch of new arrivals doesn't set off in lockstep.
    startIdle(viewer, now, randomBetween(0, IDLE_MIN_MS));
  }
  viewers.set(payload.username, viewer);
}

function removeViewer(username) {
  viewers.delete(username);
}

function applySnapshot(snapshot) {
  viewers.clear();
  for (const viewer of snapshot.viewers) {
    upsertViewer(viewer);
  }
}

// --- Soundboard ---
// !p and !tts clips play here, in the Browser Source, so OBS captures them
// with the overlay. Clips queue up and play one at a time; !stop skips the
// one currently playing.

const audioQueue = [];
let currentAudio = null;

function playNextAudio() {
  const next = audioQueue.shift();
  if (!next) {
    currentAudio = null;
    return;
  }
  const audio = new Audio(next.url);
  audio.volume = Math.min(1, Math.max(0, next.volume ?? 1));
  // A failed load fires "error" AND rejects play(), and pausing a clip that is
  // still starting rejects play() too. Only advance while this clip is still
  // the current one, so no path can skip the next clip in the queue.
  const finish = () => {
    if (currentAudio === audio) playNextAudio();
  };
  audio.onended = finish;
  audio.onerror = () => {
    console.error(`[texuguito] falha ao tocar áudio: ${next.url}`);
    finish();
  };
  currentAudio = audio;
  audio.play().catch((err) => {
    console.error("[texuguito] não foi possível tocar o áudio:", err);
    finish();
  });
}

function enqueueAudio(url, volume) {
  audioQueue.push({ url, volume });
  if (!currentAudio) playNextAudio();
}

function stopCurrentAudio() {
  if (!currentAudio) return;
  // pause() doesn't fire "ended", so advance by hand; that also makes the
  // paused clip stale, which turns its handlers into no-ops.
  currentAudio.pause();
  playNextAudio();
}

function connect() {
  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(`${protocol}//${location.host}/ws`);

  ws.onmessage = (event) => {
    const message = JSON.parse(event.data);
    if (message.type === "snapshot") {
      applySnapshot(message);
    } else if (message.type === "audio") {
      enqueueAudio(message.url, message.volume);
    } else if (message.type === "audio_stop") {
      stopCurrentAudio();
    } else if (message.type === "left") {
      removeViewer(message.username);
    } else {
      upsertViewer(message.viewer);
    }
  };

  ws.onclose = () => {
    setTimeout(connect, 1000);
  };
}

// --- Sprite composition ---

function walkFrameColumn(distanceWalked) {
  const columns = manifest.walk_frame_columns;
  const step = Math.floor(distanceWalked / PIXELS_PER_WALK_FRAME) % columns.length;
  return columns[step];
}

// Which sheet cell to draw this frame. Walking cycles the side-view walk
// frames; standing uses the LPC idle frame (column 0, feet together), either
// in side view or turned towards the camera (the "down" row). Only the
// right-facing side row is used — left is drawn by mirroring it.
function spritePose(viewer) {
  if (viewer.state === "walking") {
    return {
      row: manifest.direction_row.right,
      column: walkFrameColumn(viewer.distanceWalked),
      mirrored: viewer.direction < 0,
    };
  }
  const column = manifest.idle_frame_column ?? 0;
  if (viewer.facingFront) {
    return { row: manifest.direction_row.down, column, mirrored: false };
  }
  return { row: manifest.direction_row.right, column, mirrored: viewer.direction < 0 };
}

// Grayscale (luminance) render of the composed body+head layer for one sheet
// cell. This is the reusable "shading" that getTintedBase multiplies an
// arbitrary color against — computed once per (row, column), not per viewer.
// Skips any base layer that failed to load rather than drawing a blank
// image into the mask (see assetsReady's comment for why a failure can't
// be allowed to block forever).
function getShadingMask(row, column) {
  const key = `${row}:${column}`;
  let mask = shadingMaskCache.get(key);
  if (mask) return mask;

  mask = document.createElement("canvas");
  mask.width = frameWidth;
  mask.height = frameHeight;
  const mctx = mask.getContext("2d");

  const sx = column * frameWidth;
  const sy = row * frameHeight;

  for (const layer of manifest.base_layers) {
    const img = loadImage(layer);
    if (!img.failed) {
      mctx.drawImage(img, sx, sy, frameWidth, frameHeight, 0, 0, frameWidth, frameHeight);
    }
  }

  const imageData = mctx.getImageData(0, 0, frameWidth, frameHeight);
  const data = imageData.data;
  for (let i = 0; i < data.length; i += 4) {
    const luminance = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
    data[i] = luminance;
    data[i + 1] = luminance;
    data[i + 2] = luminance;
    // data[i + 3] (alpha) is left untouched.
  }
  mctx.putImageData(imageData, 0, 0);

  shadingMaskCache.set(key, mask);
  return mask;
}

// Body+head tinted to `color`, cached per (row, column, color) so no combo is
// ever recomputed. "multiply" against the grayscale mask approximates the
// target color while keeping the sprite's shading; "destination-in" restores
// the transparency multiply would otherwise flatten to opaque everywhere.
function getTintedBase(row, column, color) {
  const key = `${row}:${column}|${color}`;
  let tinted = tintCache.get(key);
  if (tinted) return tinted;

  const mask = getShadingMask(row, column);

  tinted = document.createElement("canvas");
  tinted.width = frameWidth;
  tinted.height = frameHeight;
  const tctx = tinted.getContext("2d");

  tctx.drawImage(mask, 0, 0);
  tctx.globalCompositeOperation = "multiply";
  tctx.fillStyle = color;
  tctx.fillRect(0, 0, frameWidth, frameHeight);
  tctx.globalCompositeOperation = "destination-in";
  tctx.drawImage(mask, 0, 0);
  tctx.globalCompositeOperation = "source-over";

  tintCache.set(key, tinted);
  return tinted;
}

// Skips drawing if `image` failed to load — see assetsReady's comment.
function drawSpriteLayer(image, pose, x, y) {
  if (image.failed) return;
  const sx = pose.column * frameWidth;
  const sy = pose.row * frameHeight;
  ctx.drawImage(image, sx, sy, frameWidth, frameHeight, x, y, frameWidth, frameHeight);
}

// Draws one fully-composed viewer at (x, y) in the CURRENT ctx transform.
// Layer order: wings-behind, tinted body, hat, accessory, wings-in-front —
// wings must straddle the body or they render entirely on top of it.
// Mirrors horizontally when the pose says so (facing left), since only the
// right-facing side row of each sheet is ever used. Position is rounded to
// whole pixels so the browser never bilinear-samples the sprite across a
// fractional offset.
function drawCharacter(viewer, pose, x, y) {
  const mirrored = pose.mirrored;
  x = Math.round(x);
  y = Math.round(y);

  ctx.save();
  if (mirrored) {
    ctx.translate(x + frameWidth, y);
    ctx.scale(-1, 1);
  } else {
    ctx.translate(x, y);
  }

  const wing = Object.hasOwn(manifest.wings, viewer.acessorio)
    ? manifest.wings[viewer.acessorio]
    : null;

  if (wing) {
    drawSpriteLayer(loadImage(wing.bg), pose, 0, 0);
  }

  ctx.drawImage(getTintedBase(pose.row, pose.column, viewer.cor), 0, 0);

  if (viewer.chapeu && Object.hasOwn(manifest.hats, viewer.chapeu)) {
    drawSpriteLayer(loadImage(manifest.hats[viewer.chapeu]), pose, 0, 0);
  }

  if (
    viewer.acessorio &&
    !wing &&
    Object.hasOwn(manifest.accessories, viewer.acessorio)
  ) {
    drawSpriteLayer(loadImage(manifest.accessories[viewer.acessorio]), pose, 0, 0);
  }

  if (wing) {
    drawSpriteLayer(loadImage(wing.fg), pose, 0, 0);
  }

  ctx.restore();
}

// --- Per-frame drawing ---

// Sub border and cheer ring hug the character's actual drawn silhouette
// (manifest.content_box), not the full transparent 64x64 cell — the cell
// has ~20px of empty margin on most sides, so decorations centered on the
// full cell would float visibly away from the sprite they're decorating.
function drawBadges(viewer, yOffset) {
  let label = "";
  if (viewer.is_broadcaster) label = "♛";
  else if (viewer.is_mod) label = "MOD";
  if (label) {
    ctx.fillStyle = "#ffffff";
    ctx.font = "10px sans-serif";
    ctx.textAlign = "center";
    ctx.fillText(label, viewer.x + frameWidth / 2, yOffset - 14);
  }
  if (viewer.is_sub) {
    const box = manifest.content_box;
    ctx.strokeStyle = "#f6c90e";
    ctx.lineWidth = 1;
    ctx.strokeRect(viewer.x + box.x - 1, yOffset + box.y - 1, box.w + 2, box.h + 2);
  }
}

function drawViewer(viewer, timestamp) {
  const dancing = timestamp < viewer.danceUntil;
  const cheering = timestamp < viewer.cheerUntil;
  const bounce = dancing ? Math.abs(Math.sin(timestamp / 120)) * 6 : 0;
  const yOffset = viewer.y - bounce;

  if (cheering) {
    const box = manifest.content_box;
    ctx.beginPath();
    ctx.strokeStyle = "#ffd700";
    ctx.lineWidth = 2;
    ctx.arc(viewer.x + frameWidth / 2, yOffset + box.y + box.h / 2, box.h * 0.7, 0, Math.PI * 2);
    ctx.stroke();
  }

  drawCharacter(viewer, spritePose(viewer), viewer.x, yOffset);

  drawBadges(viewer, yOffset);

  ctx.fillStyle = "#ffffff";
  ctx.font = "10px monospace";
  ctx.textAlign = "center";
  ctx.fillText(viewer.nick, viewer.x + frameWidth / 2, yOffset - 4);
}

let lastTimestamp = performance.now();

function tick(timestamp) {
  const deltaSeconds = (timestamp - lastTimestamp) / 1000;
  lastTimestamp = timestamp;

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  if (assetsReady()) {
    for (const viewer of viewers.values()) {
      updateMovement(viewer, timestamp, deltaSeconds);
      drawViewer(viewer, timestamp);
    }
  }

  requestAnimationFrame(tick);
}

resizeCanvas();
window.addEventListener("resize", resizeCanvas);

connect();
loadManifest();
requestAnimationFrame(tick);
