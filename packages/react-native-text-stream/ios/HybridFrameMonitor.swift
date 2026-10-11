//
//  HybridFrameMonitor.swift
//  Pods
//

import Foundation
import NitroModules
import QuartzCore

/// Counts UI-thread frames and hitches with a CADisplayLink. Benchmark-only.
class HybridFrameMonitor: HybridFrameMonitorSpec {
  private let lock = NSLock()
  private var startedAt: CFTimeInterval = 0
  private var lastTimestamp: CFTimeInterval = 0
  private var frames = 0
  private var hitches = 0
  private var droppedFrames = 0
  private var maxFrame: CFTimeInterval = 0

  // Main thread only.
  private var link: CADisplayLink?
  private var waiters: [(Double) -> Void] = []
  private var idleWaiters: [(frames: Int, resolve: (Double) -> Void)] = []
  private var onTimeStreak = 0
  private var streakStartedAt: Double = 0

  func waitIdle(frames: Double) throws -> Promise<Double> {
    let promise = Promise<Double>()
    DispatchQueue.main.async { [self] in
      if link == nil {
        promise.resolve(withResult: nowMs())
      } else {
        // Count only frames after this call.
        onTimeStreak = 0
        idleWaiters.append((Int(frames), { promise.resolve(withResult: $0) }))
      }
    }
    return promise
  }

  func start() throws {
    lock.withLock {
      frames = 0
      hitches = 0
      droppedFrames = 0
      maxFrame = 0
      lastTimestamp = 0
      startedAt = CACurrentMediaTime()
    }
    DispatchQueue.main.async { [self] in
      guard link == nil else { return }
      let link = CADisplayLink(target: FrameMonitorProxy(self), selector: #selector(FrameMonitorProxy.tick(_:)))
      link.add(to: .main, forMode: .common)
      self.link = link
    }
  }

  func stop() throws -> Promise<FrameStats> {
    let promise = Promise<FrameStats>()
    onNextFrame { [self] _ in
      link?.invalidate()
      link = nil
      promise.resolve(withResult: snapshot())
    }
    return promise
  }

  func streamTextDisplayLog(key: String) throws -> [Double] {
    StreamTextDisplayLogs.shared.log(for: key)
  }

  func streamTextRenderStats(key: String) throws -> [Double] {
    StreamTextRenderStats.shared.stats(for: key)
  }

  func streamTextTotals(reset: Bool) throws -> [Double] {
    StreamTextRenderStats.enabled = true
    return StreamTextRenderStats.shared.totals(reset: reset)
  }

  func memoryFootprintMb() throws -> Double {
    var info = task_vm_info_data_t()
    var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
    let result = withUnsafeMutablePointer(to: &info) {
      $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
        task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
      }
    }
    return result == KERN_SUCCESS ? Double(info.phys_footprint) / 1_048_576 : -1
  }

  func markPainted() throws -> Promise<Double> {
    let promise = Promise<Double>()
    onNextFrame { promise.resolve(withResult: $0) }
    return promise
  }

  /// Runs `body` on the first frame after the main queue drains the work queued before this call.
  private func onNextFrame(_ body: @escaping (Double) -> Void) {
    DispatchQueue.main.async { [self] in
      if link == nil {
        body(nowMs())
      } else {
        waiters.append(body)
      }
    }
  }

  private func snapshot() -> FrameStats {
    lock.withLock {
      FrameStats(
        frames: Double(frames),
        hitches: Double(hitches),
        droppedFrames: Double(droppedFrames),
        maxFrameMs: maxFrame * 1000,
        durationMs: (CACurrentMediaTime() - startedAt) * 1000
      )
    }
  }

  fileprivate func tick(_ link: CADisplayLink) {
    let budget = link.targetTimestamp - link.timestamp
    let onTime: Bool = lock.withLock {
      var onTime = true
      if lastTimestamp > 0, budget > 0 {
        let interval = link.timestamp - lastTimestamp
        maxFrame = max(maxFrame, interval)
        if interval > budget * 1.5 {
          onTime = false
          hitches += 1
          droppedFrames += max(0, Int((interval / budget).rounded()) - 1)
        }
      }
      lastTimestamp = link.timestamp
      frames += 1
      return onTime
    }
    let now = nowMs()

    if onTime {
      if onTimeStreak == 0 { streakStartedAt = now }
      onTimeStreak += 1
    } else {
      onTimeStreak = 0
    }
    if !idleWaiters.isEmpty {
      let streak = onTimeStreak
      let idleSince = streakStartedAt
      idleWaiters.removeAll { waiter in
        guard streak >= waiter.frames else { return false }
        waiter.resolve(idleSince)
        return true
      }
    }

    guard !waiters.isEmpty else { return }
    let ready = waiters
    waiters.removeAll()
    ready.forEach { $0(now) }
  }
}

private func nowMs() -> Double {
  Date().timeIntervalSince1970 * 1000
}

/// Breaks the CADisplayLink -> target retain cycle.
private final class FrameMonitorProxy: NSObject {
  private weak var target: HybridFrameMonitor?
  init(_ target: HybridFrameMonitor) { self.target = target }
  @objc func tick(_ link: CADisplayLink) { target?.tick(link) }
}
