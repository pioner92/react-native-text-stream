import test from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { cases } from "./markdown-visual.mjs";
import { progressiveStages, progressiveCompletion } from "./markdown-progressive.mjs";

const timedFetch = (url, options = {}) => fetch(url, {
  ...options,
  signal: options.signal
    ? AbortSignal.any([options.signal, AbortSignal.timeout(10_000)])
    : AbortSignal.timeout(10_000),
});

// Real socket regression: exercise arbitrary UTF-8 byte cuts and the iOS
// MIME-sniffing header, not just the fixture string generator.
test("visual server streams lossless UTF-8 with nosniff and serves the concept image", async () => {
  const port = 19882;
  const child = spawn(process.execPath, [new URL("../rich-markdown-server.mjs", import.meta.url).pathname], {
    env: { ...process.env, PORT: String(port) }, stdio: ["ignore", "pipe", "pipe"],
  });
  try {
    await Promise.race([once(child.stdout, "data", { signal: AbortSignal.timeout(10_000) }), once(child, "exit", { signal: AbortSignal.timeout(10_000) }).then(() => { throw new Error("fixture server exited"); })]);
    const host = `127.0.0.1:${port}`;
    const fixtures = cases(host, "regression");
    assert.equal(new Set(fixtures.map(x => x.id)).size, 9);
    const response = await timedFetch(`http://${host}/visual?case=unicode&chunk=7&interval=10&run=regression`);
    assert.equal(response.headers.get("x-content-type-options"), "nosniff");
    const reader = response.body.getReader();
    const first = await reader.read();
    const chunks = [first.value];
    assert.equal(first.done, false);
    // Transport may coalesce writes; do not assert a fetch read boundary.
    for (;;) { const next = await reader.read(); if (next.done) break; chunks.push(next.value); }
    assert.equal(Buffer.concat(chunks).toString("utf8"), fixtures.find(x => x.id === "unicode").markdown);
    const all = await (await timedFetch(`http://${host}/visual?case=all&chunk=65536&run=regression`)).text();
    assert.equal(all, fixtures.filter(x => x.id !== "announce").map(x => x.markdown).join("\n\n"));
    assert.ok(all.includes("LAST TABLE SENTINEL: row 60"));
    const picture = await timedFetch(`http://${host}/daylight-concept.png`);
    assert.equal(picture.headers.get("content-type"), "image/png");
    assert.deepEqual([...new Uint8Array(await picture.arrayBuffer()).slice(0, 8)], [137,80,78,71,13,10,26,10]);
    const credentialKey = "host-credentials";
    const credentialed = await timedFetch(`http://${host}/credential-markdown?key=${credentialKey}`, {
      headers: { authorization: "Bearer fixture-token", cookie: "fixture=session" },
    });
    assert.equal(credentialed.status, 200);
    assert.match(credentialed.headers.get("set-cookie") ?? "", /^image-isolation=/);
    const markdown = await credentialed.text();
    assert.ok(markdown.includes(`/image.png?key=${credentialKey}`));
    assert.ok(!markdown.includes("fixture-token"));
    assert.ok(!markdown.includes("fixture=session"));
    const imageResponse = await timedFetch(`http://${host}/image.png?key=${credentialKey}`);
    assert.equal(imageResponse.status, 200);
    assert.deepEqual(await (await timedFetch(`http://${host}/counts?key=${credentialKey}`)).json(),
      { count: 1, authorization: false, cookie: false });
    assert.deepEqual(await (await timedFetch(`http://${host}/counts?key=${credentialKey}-stream`)).json(),
      { count: 1, authorization: true, cookie: true });
    assert.equal((await timedFetch(`http://${host}/visual?case=unknown`)).status, 404);
    const seams = progressiveStages(host, 'regression');
    for (const [stage, prefix] of Object.entries(seams)) {
      assert.ok(progressiveCompletion(seams, stage).startsWith(prefix), `${stage} must continue to the same final source`);
      const controller = new AbortController();
      try {
        const held = await timedFetch(`http://${host}/progressive?stage=${stage}&run=regression`, { signal: controller.signal });
        assert.equal(held.headers.get('x-content-type-options'), 'nosniff');
        const reader = held.body.getReader();
        const chunks = [];
        let count = 0;
        const expected = Buffer.from(prefix);
        while (count < expected.length) {
          const next = await reader.read();
          assert.equal(next.done, false);
          chunks.push(next.value); count += next.value.length;
        }
        assert.deepEqual(Buffer.concat(chunks), expected);
      } finally { controller.abort(); }
    }
    assert.equal((await timedFetch(`http://${host}/progressive?stage=unknown`)).status, 404);
  } finally {
    child.kill("SIGTERM");
    if (child.exitCode === null && child.signalCode === null) await once(child, "exit", { signal: AbortSignal.timeout(5_000) });
  }
});
