private final class DrawToken: @unchecked Sendable {
  private let lock = NSLock()
  private var isCancelled = false
  var cancelled: Bool { lock.withLock { isCancelled } }
  func cancel() { lock.withLock { isCancelled = true } }
  weak var canvas: StreamTextCanvas?
  init(_ canvas: StreamTextCanvas?) { self.canvas = canvas }
}
