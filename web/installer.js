// SPDX-License-Identifier: GPL-3.0-only
//
// What the installer page does with the FM-1 it finds. JamesOS needs the boot
// configuration of its combined "from V15" package, which only the official
// firmware's updater writes, so the route is:
//   official V15  ->  the base package (boot configuration + JamesOS)  ->  this build (app update)
// An FM-1 under another community firmware (SLOOP, Felucca) first returns to
// the official V15, which that firmware's own update path supports.

// "official" (V15, the only official version the base package is made for), "official-other" (any other
// official version, such as V16), "jamesos" (this fork FM-1_926NNNN, upstream 1.0a FM-1_9262108,
// upstream 1.0b and later FM-1_967NNNN), "felucca" (SLOOP, Felucca and their 9XX identities) or "other"
export function firmwareKind(text) {
  if (text === "FM-1_015") return "official";
  if (/^FM-1_0\d\d$/.test(text)) return "official-other";
  if (/^FM-1_9(26|67)\d{4}$/.test(text)) return "jamesos";
  if (/^FM-1_9\d\d$/.test(text)) return "felucca";
  return "other";
}

// The package to install on a running FM-1: "app", "base", or null when it must return to V15 first.
export function packageFor(text) {
  const kind = firmwareKind(text);
  return kind === "jamesos" ? "app" : kind === "official" ? "base" : null;
}

// A JamesOS 1.0a build (upstream 2108, this fork up to 2150): its projects are in the old format, which
// 1.0b-based builds list as OLD FORMAT and do not load until converted with upstream's browser Editor.
export function oldProjectFormat(text) {
  const m = /^FM-1_926(\d{4})$/.exec(text);
  return !!m && Number(m[1]) <= 2150;
}

// The package that finishes an update left in a loader: this build's own loader takes the app package,
// the official V15 loader takes the base package (its own layout), any other loader is left alone.
export function packageForLoader(text, appLoader) {
  if (text.toLowerCase() === appLoader.toLowerCase()) return "app";
  if (/^ota-FM-1_015$/i.test(text)) return "base";
  return null;
}

// Read requests of a whole install, for the progress bar: step 1 and the image in 512-byte reads.
export function totalReads(imageLength) {
  return 74 + Math.ceil(imageLength / 512);
}
