import test from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { cases } from "./markdown-visual.mjs";

// Real socket regression: exercise arbitrary UTF-8 byte cuts and the iOS
// MIME-sniffing header, not just the fixture string generator.
test("visual server streams lossless UTF-8 with nosniff and serves the concept image", async () => {
  const port = 19882;
  const child = spawn(process.execPath, [new URL("../rich-markdown-server.mjs", import.meta.url).pathname], {
    env: { ...process.env, PORT: String(port) }, stdio: ["ignore", "pipe", "pipe"],
  });
  try {
    await Promise.race([once(child.stdout, "data"), once(child, "exit").then(() => { throw new Error("fixture server exited"); })]);
    const host = `127.0.0.1:${port}`;
    const fixtures = cases(host, "regression");
    assert.equal(new Set(fixtures.map(x => x.id)).size, 9);
    const response = await fetch(`http://${host}/visual?case=unicode&chunk=7&interval=10&run=regression`);
    assert.equal(response.headers.get("x-content-type-options"), "nosniff");
    const reader = response.body.getReader();
    const first = await reader.read();
    const chunks = [first.value];
    assert.equal(first.done, false);
    // Transport may coalesce writes; do not assert a fetch read boundary.
    for (;;) { const next = await reader.read(); if (next.done) break; chunks.push(next.value); }
    assert.equal(Buffer.concat(chunks).toString("utf8"), fixtures.find(x => x.id === "unicode").markdown);
    const all = await (await fetch(`http://${host}/visual?case=all&chunk=65536&run=regression`)).text();
    assert.equal(all, fixtures.filter(x => x.id !== "announce").map(x => x.markdown).join("\n\n"));
    assert.ok(all.includes("LAST TABLE SENTINEL: row 60"));
    const picture = await fetch(`http://${host}/daylight-concept.png`);
    assert.equal(picture.headers.get("content-type"), "image/png");
    assert.deepEqual([...new Uint8Array(await picture.arrayBuffer()).slice(0, 8)], [137,80,78,71,13,10,26,10]);
    assert.equal((await fetch(`http://${host}/visual?case=unknown`)).status, 404);
  } finally {
    child.kill("SIGTERM");
    if (child.exitCode === null && child.signalCode === null) await once(child, "exit");
  }
});
