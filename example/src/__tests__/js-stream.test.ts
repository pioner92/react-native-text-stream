import { streamTextOverXhr, utf8ByteLength } from '../js-stream';

class FakeXhr {
  static latest: FakeXhr | undefined;
  status = 200;
  responseText = '';
  onprogress: ((event: ProgressEvent) => void) | null = null;
  onload: ((event: Event) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  ontimeout: ((event: Event) => void) | null = null;
  onabort: ((event: Event) => void) | null = null;
  aborted = false;

  constructor() {
    FakeXhr.latest = this;
  }

  open() {}
  setRequestHeader() {}
  send() {}
  abort() {
    this.aborted = true;
    this.onabort?.({} as Event);
  }
  progress(loaded: number) {
    this.onprogress?.({ loaded } as ProgressEvent);
  }
  load() {
    // This matches React Native: XMLHttpRequest dispatches a plain Event for load.
    this.onload?.({} as Event);
  }
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

describe('streamTextOverXhr', () => {
  const originalXhr = globalThis.XMLHttpRequest;

  beforeEach(() => {
    FakeXhr.latest = undefined;
    globalThis.XMLHttpRequest = FakeXhr as unknown as typeof XMLHttpRequest;
  });
  afterAll(() => {
    globalThis.XMLHttpRequest = originalXhr;
  });

  it('emits progress deltas and losslessly counts a Unicode tail when RN load has no loaded field', async () => {
    const chunks: Array<[string, number]> = [];
    const request = streamTextOverXhr('http://example.test/stream', (text, bytes) => {
      chunks.push([text, bytes]);
    });
    const xhr = FakeXhr.latest!;
    xhr.responseText = 'Пр';
    // Android can report zero here even when `responseText` advanced.
    xhr.progress(0);
    xhr.responseText = 'Привет 🚀';
    xhr.load();

    await expect(request.done).resolves.toEqual({
      status: 200,
      text: 'Привет 🚀',
      bytes: utf8ByteLength('Привет 🚀'),
    });
    expect(chunks).toEqual([
      ['Пр', 4],
      ['ивет 🚀', utf8ByteLength('ивет 🚀')],
    ]);
  });

  it('rejects HTTP failures and aborts a request already in flight', async () => {
    const failed = streamTextOverXhr('http://example.test/status', () => {});
    const failedXhr = FakeXhr.latest!;
    failedXhr.status = 503;
    failedXhr.responseText = 'unavailable';
    failedXhr.load();
    await expect(failed.done).rejects.toThrow('HTTP 503');

    const request = streamTextOverXhr('http://example.test/slow', () => {});
    const xhr = FakeXhr.latest!;
    xhr.responseText = 'first';
    xhr.progress(5);
    request.abort();
    expect(xhr.aborted).toBe(true);
    await expect(request.done).rejects.toMatchObject({ name: 'AbortError' });
  });

  it('rejects without creating or sending an XHR when its signal was already aborted', async () => {
    const controller = new AbortController();
    controller.abort();
    const request = streamTextOverXhr('http://example.test/pre-abort', () => {}, { signal: controller.signal });
    expect(FakeXhr.latest).toBeUndefined();
    await expect(request.done).rejects.toMatchObject({ name: 'AbortError' });
  });

  it('waits for asynchronous callbacks and rejects if either a progress or final tail callback rejects', async () => {
    const progressFailure = deferred<void>();
    const progress = streamTextOverXhr('http://example.test/progress', () => progressFailure.promise);
    const progressXhr = FakeXhr.latest!;
    progressXhr.responseText = 'first';
    progressXhr.progress(5);
    progressXhr.load();
    progressFailure.reject(new Error('progress callback failed'));
    await expect(progress.done).rejects.toThrow('progress callback failed');
    expect(progressXhr.aborted).toBe(true);

    const tailFailure = deferred<void>();
    const tail = streamTextOverXhr('http://example.test/tail', () => tailFailure.promise);
    const tailXhr = FakeXhr.latest!;
    tailXhr.responseText = 'final';
    tailXhr.load();
    tailFailure.reject(new Error('tail callback failed'));
    await expect(tail.done).rejects.toThrow('tail callback failed');
    expect(tailXhr.aborted).toBe(true);
  });

  it('does not resolve a successful response before its final asynchronous callback settles', async () => {
    const callback = deferred<void>();
    const request = streamTextOverXhr('http://example.test/final', () => callback.promise);
    const xhr = FakeXhr.latest!;
    xhr.responseText = 'final';
    xhr.load();
    let settled = false;
    request.done.then(() => (settled = true));
    await Promise.resolve();
    expect(settled).toBe(false);
    callback.resolve();
    await expect(request.done).resolves.toMatchObject({ status: 200, text: 'final' });
  });
});
