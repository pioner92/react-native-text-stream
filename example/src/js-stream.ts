/**
 * Incremental text transport for examples that intentionally render in JavaScript.
 *
 * React Native's built-in `fetch` is XHR-backed and does not expose a ReadableStream.
 * XHR's `onprogress` is the supported JS-facing incremental boundary: these callbacks are
 * deliberately what the JS benchmark counts. It is not a measure of TCP packet boundaries.
 */
export interface JsTextStreamInit {
  method?: string;
  headers?: Record<string, string> | ReadonlyArray<readonly [string, string]>;
  body?: string;
  signal?: AbortSignal;
}

export interface JsTextStreamResult {
  status: number;
  /** UTF-8 bytes represented by the text delivered to JavaScript. */
  bytes: number;
  text: string;
}

export interface JsTextStream {
  /** Resolves after a successful 2xx response; rejects for abort, transport, and HTTP errors. */
  done: Promise<JsTextStreamResult>;
  abort(): void;
}

/** UTF-8 byte length without relying on a global TextEncoder type in the example TS config. */
export function utf8ByteLength(text: string): number {
  let bytes = 0;
  for (let i = 0; i < text.length; i++) {
    const unit = text.charCodeAt(i);
    if (unit < 0x80) bytes += 1;
    else if (unit < 0x800) bytes += 2;
    else if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < text.length) {
      const next = text.charCodeAt(i + 1);
      if (next >= 0xdc00 && next <= 0xdfff) {
        bytes += 4;
        i++;
      } else bytes += 3;
    } else bytes += 3;
  }
  return bytes;
}

export function streamTextOverXhr(
  url: string,
  onChunk: (text: string, bytes: number) => void | Promise<void>,
  init: JsTextStreamInit = {},
): JsTextStream {
  let xhr: XMLHttpRequest | undefined;
  let settled = false;
  let seenChars = 0;
  let seenBytes = 0;
  let pendingCallbacks = 0;
  let successPending = false;
  let rejectDone!: (error: Error) => void;
  let resolveDone!: (result: JsTextStreamResult) => void;

  const done = new Promise<JsTextStreamResult>((resolve, reject) => {
    resolveDone = resolve;
    rejectDone = reject;
  });
  // Consumers can intentionally use just `abort()`; do not turn that into an unhandled rejection.
  done.catch(() => {});

  const finishError = (error: Error) => {
    if (settled) return;
    settled = true;
    init.signal?.removeEventListener('abort', onSignalAbort);
    rejectDone(error);
  };
  const finishSuccess = () => {
    if (settled || !xhr) return;
    if (pendingCallbacks) {
      successPending = true;
      return;
    }
    settled = true;
    init.signal?.removeEventListener('abort', onSignalAbort);
    resolveDone({ status: xhr.status, bytes: seenBytes, text: xhr.responseText });
  };
  const abortError = () => Object.assign(new Error('The request was aborted'), { name: 'AbortError' });
  const onSignalAbort = () => request.abort();
  const request: JsTextStream = {
    done,
    abort() {
      if (settled) return;
      // RN does not dispatch `abort` when this happens before `send()`, so settle here too.
      finishError(abortError());
      xhr?.abort();
    },
  };
  const emit = (text: string, bytes: number) => {
    if (!text.length && !bytes) return;
    try {
      const callback = onChunk(text, bytes);
      if (callback && typeof (callback as Promise<void>).then === 'function') {
        pendingCallbacks++;
        Promise.resolve(callback).then(
          () => {
            pendingCallbacks--;
            if (successPending) finishSuccess();
          },
          (error) => {
            pendingCallbacks--;
            finishError(error instanceof Error ? error : new Error(String(error)));
            xhr?.abort();
          },
        );
      }
    } catch (error) {
      finishError(error instanceof Error ? error : new Error(String(error)));
      xhr?.abort();
    }
  };

  if (init.signal?.aborted) {
    finishError(abortError());
    return request;
  }

  try {
    xhr = new XMLHttpRequest();
    xhr.onprogress = () => {
      if (settled || !xhr) return;
      const text = xhr.responseText;
      const appended = text.slice(seenChars);
      seenChars = text.length;
      // Android's XHR can report `loaded: 0` while still appending responseText. Counting the
      // actual delivered text is portable and matches what JS variants can render.
      const bytes = utf8ByteLength(appended);
      seenBytes += bytes;
      emit(appended, bytes);
    };
    xhr.onload = () => {
      if (!xhr || settled) return;
      // Some servers (and tiny responses) never dispatch a progress callback before load.
      const text = xhr.responseText;
      // React Native dispatches a plain Event for `load`; the tail still needs exact UTF-8
      // accounting for benchmarks and non-ASCII demo text.
      const appended = text.slice(seenChars);
      seenChars = text.length;
      const bytes = utf8ByteLength(appended);
      seenBytes += bytes;
      emit(appended, bytes);
      if (settled) return;
      if (xhr.status < 200 || xhr.status >= 300) {
        finishError(new Error(`HTTP ${xhr.status} for ${url}`));
        return;
      }
      finishSuccess();
    };
    xhr.onerror = () => finishError(new Error(`Network request failed for ${url}`));
    xhr.ontimeout = () => finishError(new Error(`Request timed out for ${url}`));
    xhr.onabort = () => finishError(abortError());
    xhr.open(init.method ?? (init.body == null ? 'GET' : 'POST'), url);
    if (init.headers) {
      const headers = Array.isArray(init.headers) ? init.headers : Object.entries(init.headers);
      for (const [name, value] of headers) xhr.setRequestHeader(name, value);
    }
    init.signal?.addEventListener('abort', onSignalAbort, { once: true });
    xhr.send(init.body ?? null);
  } catch (error) {
    finishError(error instanceof Error ? error : new Error(String(error)));
  }

  return request;
}
