// Streaming UTF-8 decoder for the JS baseline: keeps a split multi-byte
// sequence until its remaining bytes arrive in the next chunk.
export function createUtf8StreamDecoder() {
  let pending: number[] = [];

  return (chunk: Uint8Array): string => {
    const bytes = pending.length ? [...pending, ...chunk] : Array.from(chunk);
    pending = [];
    let out = '';
    let i = 0;
    while (i < bytes.length) {
      const b = bytes[i];
      const need = b < 0x80 ? 1 : b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 1;
      if (i + need > bytes.length) {
        pending = bytes.slice(i);
        break;
      }
      let cp =
        need === 1 ? b : need === 2 ? b & 0x1f : need === 3 ? b & 0x0f : b & 0x07;
      for (let k = 1; k < need; k++) cp = (cp << 6) | (bytes[i + k] & 0x3f);
      out += String.fromCodePoint(cp);
      i += need;
    }
    return out;
  };
}
