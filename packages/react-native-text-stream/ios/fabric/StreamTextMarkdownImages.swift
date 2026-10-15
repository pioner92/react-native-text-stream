import ImageIO
import UIKit

enum StreamTextMarkdownImageError: Error {
  case blocked
  case oversize
  case invalid

  var status: String {
    switch self {
    case .blocked: return "blocked"
    case .oversize: return "oversize"
    case .invalid: return "error"
    }
  }
}

/// A network-only, deduplicating image loader for Markdown attachments.
///
/// Its small scheduler is intentional: URLSession's delegate queue limits callback work, not the
/// number of live HTTP requests. At most two requests are resumed at any point. Every callback is
/// delivered on the main queue, so an attachment can safely invalidate TextKit when it receives a
/// result. The returned closure removes only the corresponding subscriber; when the final
/// subscriber disappears, the queued or active request is cancelled.
final class StreamTextMarkdownImageLoader: NSObject, URLSessionDataDelegate, URLSessionTaskDelegate {
  static let shared = StreamTextMarkdownImageLoader()

  private final class Request {
    let id = UUID()
    let url: URL
    var data = Data()
    var subscribers: [UUID: Subscriber] = [:]
    var task: URLSessionDataTask?

    init(url: URL) {
      self.url = url
    }
  }

  private final class Subscriber {
    let id = UUID()
    private let lock = NSLock()
    private var cancelled = false
    private let completion: (Result<UIImage, Error>) -> Void

    init(completion: @escaping (Result<UIImage, Error>) -> Void) {
      self.completion = completion
    }

    func cancel() {
      lock.withLock { cancelled = true }
    }

    func deliver(_ result: Result<UIImage, Error>) {
      let shouldDeliver = lock.withLock { !cancelled }
      if shouldDeliver { completion(result) }
    }
  }

  private let lock = NSLock()
  private let maxBytes = 8 * 1024 * 1024
  private let maxDecodedPixels = 4_000_000
  private let maxThumbnailEdge = 2_000
  private let maxActiveRequests = 2
  private let cacheBudget = 32 * 1024 * 1024

  // All collections below are protected by lock. taskIdentities prevents a delayed completion of
  // an old cancelled task from removing a newer request for the same URL.
  private var requests: [String: Request] = [:]
  private var pendingKeys: [String] = []
  private var taskIdentities: [Int: (key: String, requestID: UUID)] = [:]
  private var activeRequests = 0
  private var cachedImages: [String: (image: UIImage, cost: Int)] = [:]
  private var cacheOrder: [String] = [] // Least-recently used first.
  private var cachedBytes = 0
  private var session: URLSession!

  private override init() {
    super.init()

    let configuration = URLSessionConfiguration.ephemeral
    configuration.httpCookieStorage = nil
    configuration.httpShouldSetCookies = false
    configuration.urlCredentialStorage = nil
    configuration.urlCache = nil
    configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
    configuration.timeoutIntervalForRequest = 15
    configuration.timeoutIntervalForResource = 30

    let delegateQueue = OperationQueue()
    delegateQueue.name = "StreamText.markdown.image.net"
    delegateQueue.maxConcurrentOperationCount = 1
    session = URLSession(configuration: configuration, delegate: self, delegateQueue: delegateQueue)
  }

  /// Begins (or joins) an image request. Calling the returned closure is idempotent and prevents
  /// this subscriber from observing future results.
  @discardableResult
  func load(url raw: String, completion: @escaping (Result<UIImage, Error>) -> Void) -> (() -> Void)? {
    let subscriber = Subscriber(completion: completion)
    guard let url = allowedURL(raw) else {
      dispatch(.failure(StreamTextMarkdownImageError.blocked), to: subscriber)
      return { subscriber.cancel() }
    }

    if let image = lock.withLock({ cachedImageLocked(for: raw) }) {
      dispatch(.success(image), to: subscriber)
      return { subscriber.cancel() }
    }

    lock.withLock {
      if let request = requests[raw] {
        request.subscribers[subscriber.id] = subscriber
      } else {
        let request = Request(url: url)
        request.subscribers[subscriber.id] = subscriber
        requests[raw] = request
        pendingKeys.append(raw)
      }
      scheduleLocked()
    }

    return { [weak self] in
      subscriber.cancel()
      self?.cancel(url: raw, subscriberID: subscriber.id)
    }
  }

  private func allowedURL(_ raw: String) -> URL? {
    guard let url = URL(string: raw), let scheme = url.scheme?.lowercased(),
          scheme == "http" || scheme == "https" else {
      return nil
    }
    return url
  }

  private func cancel(url key: String, subscriberID: UUID) {
    let task: URLSessionDataTask? = lock.withLock {
      guard let request = requests[key] else { return nil }
      request.subscribers.removeValue(forKey: subscriberID)
      guard request.subscribers.isEmpty else { return nil }

      requests.removeValue(forKey: key)
      pendingKeys.removeAll { $0 == key }
      guard let task = request.task else { return nil }

      taskIdentities.removeValue(forKey: task.taskIdentifier)
      activeRequests -= 1
      scheduleLocked()
      return task
    }
    task?.cancel()
  }

  /// Must run with lock held.
  private func cachedImageLocked(for key: String) -> UIImage? {
    guard let cached = cachedImages[key] else { return nil }
    cacheOrder.removeAll { $0 == key }
    cacheOrder.append(key)
    return cached.image
  }

  /// Must run with lock held. UIImage's decoded bitmap is the accounted payload; source bytes are
  /// released before this point.
  private func cacheImageLocked(_ image: UIImage, for key: String) {
    let cost = image.cgImage.map { $0.bytesPerRow * $0.height } ?? 0
    if let previous = cachedImages.removeValue(forKey: key) {
      cachedBytes -= previous.cost
      cacheOrder.removeAll { $0 == key }
    }

    // A single decoded image can be larger than the complete budget only if ImageIO violates the
    // thumbnail bound. Do not retain it in that case.
    guard cost <= cacheBudget else { return }
    while cachedBytes > cacheBudget - cost, let victim = cacheOrder.first {
      cacheOrder.removeFirst()
      if let removed = cachedImages.removeValue(forKey: victim) {
        cachedBytes -= removed.cost
      }
    }
    cachedImages[key] = (image, cost)
    cacheOrder.append(key)
    cachedBytes += cost
  }

  /// Must run with lock held.
  private func scheduleLocked() {
    while activeRequests < maxActiveRequests, !pendingKeys.isEmpty {
      let key = pendingKeys.removeFirst()
      guard let request = requests[key], request.task == nil, !request.subscribers.isEmpty else {
        continue
      }

      let task = session.dataTask(with: request.url)
      request.task = task
      taskIdentities[task.taskIdentifier] = (key, request.id)
      activeRequests += 1
      task.resume()
    }
  }

  /// Must run with lock held.
  private func currentRequestLocked(for task: URLSessionTask) -> (key: String, request: Request)? {
    guard let identity = taskIdentities[task.taskIdentifier],
          let request = requests[identity.key],
          request.id == identity.requestID,
          request.task === task else {
      return nil
    }
    return (identity.key, request)
  }

  func urlSession(
    _ session: URLSession,
    task: URLSessionTask,
    willPerformHTTPRedirection response: HTTPURLResponse,
    newRequest request: URLRequest,
    completionHandler: @escaping (URLRequest?) -> Void
  ) {
    // Do not follow redirects: a safe initial URL must not turn into a credentialed, file, or
    // custom-scheme request later in the chain.
    completionHandler(nil)
  }

  func urlSession(
    _ session: URLSession,
    dataTask: URLSessionDataTask,
    didReceive response: URLResponse,
    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void
  ) {
    let allowed = lock.withLock { currentRequestLocked(for: dataTask) != nil }
    guard allowed,
          let http = response as? HTTPURLResponse,
          (200 ..< 300).contains(http.statusCode) else {
      completionHandler(.cancel)
      finish(dataTask, result: .failure(StreamTextMarkdownImageError.invalid))
      return
    }

    guard response.expectedContentLength < 0 || response.expectedContentLength <= Int64(maxBytes) else {
      completionHandler(.cancel)
      finish(dataTask, result: .failure(StreamTextMarkdownImageError.oversize))
      return
    }
    completionHandler(.allow)
  }

  func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
    let exceeded = lock.withLock { () -> Bool in
      guard let (_, request) = currentRequestLocked(for: dataTask) else { return false }
      guard data.count <= maxBytes - request.data.count else { return true }
      request.data.append(data)
      return false
    }
    if exceeded {
      dataTask.cancel()
      finish(dataTask, result: .failure(StreamTextMarkdownImageError.oversize))
    }
  }

  func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
    guard let dataTask = task as? URLSessionDataTask else { return }
    let data = lock.withLock { currentRequestLocked(for: dataTask)?.request.data }
    guard let data else { return } // Cancelled/superseded task; its subscribers were already removed.

    guard error == nil else {
      finish(dataTask, result: .failure(StreamTextMarkdownImageError.invalid))
      return
    }

    guard let image = decode(data) else {
      finish(dataTask, result: .failure(StreamTextMarkdownImageError.invalid))
      return
    }
    finish(dataTask, result: .success(image))
  }

  private func decode(_ data: Data) -> UIImage? {
    guard let source = CGImageSourceCreateWithData(data as CFData, nil) else { return nil }

    let edge = min(maxThumbnailEdge, Int(Double(maxDecodedPixels).squareRoot()))
    let options: [CFString: Any] = [
      kCGImageSourceCreateThumbnailFromImageAlways: true,
      kCGImageSourceThumbnailMaxPixelSize: edge,
      kCGImageSourceCreateThumbnailWithTransform: true,
      kCGImageSourceShouldCacheImmediately: true,
    ]
    guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else {
      return nil
    }
    // maxThumbnailEdge is 2,000, so the decoded bitmap is bounded at 4 MP even for very large
    // source assets; oversized source dimensions are downsampled rather than rejected.
    return UIImage(cgImage: image)
  }

  private func finish(_ task: URLSessionDataTask, result: Result<UIImage, Error>) {
    let completed: (key: String, subscribers: [Subscriber])? = lock.withLock {
      guard let (key, request) = currentRequestLocked(for: task) else { return nil }

      if case .success(let image) = result {
        cacheImageLocked(image, for: key)
      }
      requests.removeValue(forKey: key)
      taskIdentities.removeValue(forKey: task.taskIdentifier)
      activeRequests -= 1
      let subscribers = Array(request.subscribers.values)
      scheduleLocked()
      return (key, subscribers)
    }

    guard let completed, !completed.subscribers.isEmpty else { return }
    DispatchQueue.main.async {
      completed.subscribers.forEach { $0.deliver(result) }
    }
  }

  private func dispatch(_ result: Result<UIImage, Error>, to subscriber: Subscriber) {
    DispatchQueue.main.async {
      subscriber.deliver(result)
    }
  }
}
