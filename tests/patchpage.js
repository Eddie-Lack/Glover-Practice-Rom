// Tests the patch page in Chromium: it patches a ROM in .v64 byte order, and the saved ROM must equal the
// ROM that make built. The practice ROM has the size of the original, 8 MiB, and the page says so.
//
//     node tests/patchpage.js
//
// Needs `make patch` first, and Playwright (`npm install playwright` in any folder on NODE_PATH).
const { chromium } = require("playwright");
const fs = require("fs");
const os = require("os");
const path = require("path");

const root = path.resolve(__dirname, "..");
(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "patchpage-"));
  const z64 = fs.readFileSync(path.join(root, "baserom.us.z64"));
  const v64 = Buffer.alloc(z64.length);
  for (let i = 0; i < z64.length; i += 2) { v64[i] = z64[i + 1]; v64[i + 1] = z64[i]; }
  fs.writeFileSync(path.join(tmp, "glover.v64"), v64);

  const browser = await chromium.launch();
  const page = await browser.newPage({ acceptDownloads: true });
  const errors = [];
  page.on("pageerror", (e) => errors.push(String(e)));
  await page.goto("file://" + path.join(root, "build", "glover-practice.html"));
  const subtitle = await page.evaluate(() => {
    const el = document.getElementById("version");
    return el.hidden ? null : { text: el.textContent, version: VERSION };
  });
  await page.setInputFiles("#file", path.join(tmp, "glover.v64"));
  await page.waitForFunction(() => !document.getElementById("save-rom").disabled || document.querySelector("[data-state=fail]"),
    null, { timeout: 60000 });
  const applied = await page.textContent("#s-apply .detail");
  const [download] = await Promise.all([page.waitForEvent("download"), page.click("#save-rom")]);
  const out = path.join(tmp, "out.z64");
  await download.saveAs(out);
  await browser.close();
  fs.rmSync(path.join(tmp, "glover.v64"));

  const saved = fs.readFileSync(out);
  const same = saved.equals(fs.readFileSync(path.join(root, "build", "glover-practice.z64")));
  fs.rmSync(tmp, { recursive: true });
  const problem = errors.length ? errors.join("; ")
    : saved.length !== 0x800000 ? "the saved ROM is " + saved.length.toString(16) + " bytes, not 800000"
    : !applied.startsWith("8 MiB") ? "the page reports '" + applied + "', not 8 MiB"
    : !subtitle || !subtitle.version || subtitle.text !== "Version " + subtitle.version
      ? "the page does not show 'Version " + (subtitle && subtitle.version) + "' under its title"
    : !same ? "the saved ROM differs from build/glover-practice.z64"
    : "";
  if (problem) {
    console.log("FAIL patchpage: " + problem);
    process.exit(1);
  }
  console.log("PASS patchpage: the page made the same 8 MiB ROM from a .v64 copy, and shows " + subtitle.text);
})();
