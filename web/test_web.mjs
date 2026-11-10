// SPDX-License-Identifier: GPL-3.0-only
// After SLOOP's web/test_web.mjs, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the installer's JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - fm1pkg.js: productOf and logicalImage on the released packages
// - fm1ota.js: installs, resumes and unplugs against a simulated FM-1
// - installer.js: which package each firmware and each update loader gets, over the whole route
//   SLOOP -> official V15 -> JamesOS base -> this build

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { logicalImage, productOf, validateStockPackage, STOCK_V15_SIZE } from "./fm1pkg.js";
import { Updater, pack7, unpack7, FM1_PORT, OUR_LOADER } from "./fm1ota.js";
import { firmwareKind, packageFor, packageForLoader, totalReads } from "./installer.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const HERE = fileURLToPath(new URL(".", import.meta.url));

/* ------------------------------------------------------------- packages (fm1pkg.js) --- */
for (const name of ["JamesOS-1.0a-build-2108.fwsc", "JamesOS-1.0a-build-2108-from-v15.fwsc"]) {
  const raw = new Uint8Array(readFileSync(join(HERE, "../releases", name)));
  const image = logicalImage(raw);
  ok(productOf(raw) === "FM-1_9262108" && image.length === raw.length - 20 && image[47] === raw[48],
    `fm1pkg.js: identity and logical image of ${name}`);
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, finalIdentity = "FM-1_9262150", name = "FM-1", identity = "FM-1_9262108", loader = "ota-FM-1_904" } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.finalIdentity = finalIdentity; this.loader = loader;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, name);
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loader, "FM-1"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "FM-1"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}


const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
const APP = "FM-1_9262150", BASE = "FM-1_9262108", APP_LOADER = "ota-FM-1_904";

{
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, APP, (k) => steps.push(k));
  ok(got === APP && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: update: JamesOS -> its loader -> this build (${dev.served} reads)`);

  const failedBoot = new FakeFM1(image, { finalIdentity: "FM-1_000" });
  const e0 = await new Updater(failedBoot.access).install(image, APP).then(() => null, (e) => e);
  ok(e0?.code === "mismatch", "fm1ota.js: another identity after the write is not reported as installed");

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot(APP_LOADER, "FM-1");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, APP).then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, APP).then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");

  const other = new FakeFM1(image, { name: "Arturia MiniFreak" });
  const e3 = await new Updater(other.access).install(image, APP).then(() => null, (x) => x);
  ok(e3 && e3.code === "notfound" && other.served === 0 && /Arturia MiniFreak/.test(e3.detail),
    "fm1ota.js: a port of other gear is left alone; 'not found' lists it");
  ok(["FM-1", "JamesOS", "Felucca", "SLOOP", "MIDIIN2 (FM-1)", "USB-MIDI"].every((n) => FM1_PORT.test(n)) &&
     !["Arturia MiniFreak", "P-6", "Element", "IAC Driver Bus 1"].some((n) => FM1_PORT.test(n)),
    "fm1ota.js: FM1_PORT takes the FM-1's names, not other gear");

  const stockLoader = new FakeFM1(image);
  stockLoader.boot("ota-FM-1_015", "FM-1 Update");
  const e4 = await new Updater(stockLoader.access).resume(image).then(() => null, (x) => x);
  ok(e4 && e4.code === "foreign" && stockLoader.served === 0, "fm1ota.js: the official loader is not resumed without a product to check");
  const e5 = await validateStockPackage(new Uint8Array(STOCK_V15_SIZE)).then(() => null, (x) => x);
  ok(e5 && /official FM-1 V15/.test(e5.message), "fm1pkg.js: only the exact official V15 is accepted (SHA-256)");
}

/* ------------------------------------------------------------- the route (installer.js) --- */
ok(firmwareKind("FM-1_015") === "official" && firmwareKind("FM-1_9262108") === "jamesos" && firmwareKind(APP) === "jamesos" &&
   firmwareKind("FM-1_927") === "felucca" && firmwareKind("FM-1_900") === "felucca" && firmwareKind("FM-2_015") === "other" &&
   firmwareKind("FM-1_92621080") === "other", "installer.js: official, JamesOS, SLOOP / Felucca and unknown identities");
ok(packageFor("FM-1_015") === "base" && packageFor(BASE) === "app" && packageFor(APP) === "app" &&
   packageFor("FM-1_927") === null && packageFor("FM-1_1234") === null,
  "installer.js: official gets the base, JamesOS the update, anything else nothing");
ok(packageForLoader(APP_LOADER, APP_LOADER) === "app" && packageForLoader("OTA-FM-1_904", APP_LOADER) === "app" &&
   packageForLoader("ota-FM-1_015", APP_LOADER) === "base" && packageForLoader("ota-FM-1_900", APP_LOADER) === null,
  "installer.js: each loader is finished with its own package; SLOOP's loader is left alone");
ok(OUR_LOADER({ text: APP_LOADER }) && !OUR_LOADER({ text: "ota-FM-1_015" }) && totalReads(1024) === 76 && totalReads(1025) === 77,
  "installer.js: loader family and progress total");

{ /* SLOOP -> official V15 -> JamesOS base -> this build, each pass chosen as the page chooses it */
  const dev = new FakeFM1(image, { identity: "FM-1_927", loader: "ota-FM-1_900", finalIdentity: "FM-1_015" });
  const up = () => new Updater(dev.access);
  const found = async () => { const d = await up().find((id) => !/^ota-/i.test(id.model)); d.link.close(); return d.id.text; };
  const trail = [];
  trail.push(packageFor(await found()));                              /* SLOOP: no package, return to V15 */
  ok((await up().install(image, "FM-1_015")) === "FM-1_015", "route: SLOOP returns to the official V15");
  dev.loader = "ota-FM-1_015"; dev.finalIdentity = BASE;
  trail.push(packageFor(await found()));
  ok((await up().install(image, BASE)) === BASE, "route: the official V15 takes the JamesOS base");
  dev.loader = APP_LOADER; dev.finalIdentity = APP;
  trail.push(packageFor(await found()));
  ok((await up().install(image, APP)) === APP && dev.bad === 0, "route: the base is updated to this build");
  ok(eq(trail, [null, "base", "app"]), "route: the page offers nothing, then the base, then the update");

  /* a base install cut off in the official loader is finished with the base, and checked when back */
  const cut = new FakeFM1(image, { finalIdentity: BASE });
  cut.boot("ota-FM-1_015", "FM-1 Update");
  const ota = await new Updater(cut.access).find((id) => /^ota-/i.test(id.model));
  ota.link.close();
  const which = packageForLoader(ota.id.text, APP_LOADER);
  const st = [];
  const r = await new Updater(cut.access).resume(image, (k) => st.push(k), { product: BASE });
  ok(which === "base" && r === true && cut.bad === 0 && st.at(-1) === "done", "route: a cut base install resumes in the official loader");
}

console.log(failed ? `${failed} FAILED` : "all passed");
process.exit(failed ? 1 : 0);
