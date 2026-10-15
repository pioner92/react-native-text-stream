package com.textstream

import java.util.concurrent.Executor
import org.junit.Assert.*
import org.junit.Test

class StreamTextMarkdownWorkerTest {
  private class Queue : Executor {
    val work = ArrayDeque<Runnable>()
    override fun execute(command: Runnable) { work.addLast(command) }
    fun runNext() { work.removeFirst().run() }
  }
  private data class Delivery(val generation: Long, val bytes: Long, val text: String, val changed: Int, val acknowledge: () -> Unit)
  private fun worker(queue: Queue, deliveries: MutableList<Delivery>, fail: Boolean = false) = StreamTextMarkdownWorker(
      executor = queue,
      parse = { text, _ -> if (fail) null else StreamTextMarkdown.plain(text) },
      style = { document, _ -> document.text },
      deliver = { generation, bytes, document, _, changed, acknowledge ->
        deliveries.add(Delivery(generation, bytes, document.text, changed, acknowledge))
      })

  @Test fun deliversOnlyOneSnapshotUntilTheUiAcknowledgesIt() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered)

    worker.submit("A", 10, 1, false, 0)
    queue.runNext()
    worker.submit("AB", 10, 2, false, 0)
    worker.submit("ABC", 10, 3, false, 0)

    // The worker must not queue a second UI result while the first is awaiting application.
    assertEquals(1, delivered.size)
    assertTrue(queue.work.isEmpty())
    delivered.single().acknowledge()
    assertEquals(1, queue.work.size)
    queue.runNext()
    assertEquals(listOf("A", "ABC"), delivered.map { it.text })
  }

  @Test fun coalescesWhileWaitingForUIAndDiffsAgainstAcknowledgedOutput() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered)
    worker.submit("abc", 1, 3, false, 0)
    queue.runNext()
    worker.submit("abc1", 1, 4, false, 0)
    worker.submit("abc23", 1, 5, false, 0)
    assertTrue(queue.work.isEmpty())
    assertEquals(1, delivered.size)
    delivered[0].acknowledge()
    queue.runNext()
    assertEquals("abc23", delivered[1].text)
    assertEquals(5L, delivered[1].bytes)
    assertEquals(3, delivered[1].changed)
  }

  @Test fun lateAcknowledgementCannotRestoreOldSourceAfterClear() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered)
    worker.submit("same prefix", 1, 11, false, 0)
    queue.runNext()
    worker.clear()
    worker.submit("same prefix new source", 2, 22, false, 0)
    delivered[0].acknowledge()
    queue.runNext()
    assertEquals(2L, delivered[1].generation)
    assertEquals(0, delivered[1].changed)
  }

  @Test fun resetDropsAnOldQueuedDeliveryAndRestartsFromANewBase() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered)
    worker.submit("old", 1, 3, false, 0)
    queue.runNext()

    worker.clear()
    worker.submit("new", 7, 3, false, 0)
    // This completion belongs to the discarded epoch. It may wake the drain, but cannot become
    // the semantic base for the replacement source.
    delivered.single().acknowledge()
    queue.runNext()

    assertEquals("new", delivered[1].text)
    assertEquals(7, delivered[1].generation)
    assertEquals(0, delivered[1].changed)
  }

  @Test fun failedParserStillDeliversSourceAndAcknowledgesNextSnapshot() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered, fail = true)
    worker.submit("**raw**", 4, 7, false, 0)
    queue.runNext()
    assertEquals("**raw**", delivered[0].text)
    worker.submit("**raw** tail", 4, 12, false, 0)
    delivered[0].acknowledge()
    queue.runNext()
    assertEquals("**raw** tail", delivered[1].text)
  }

  @Test fun styleEpochResetForcesAFullPatchEvenWhenTextIsUnchanged() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered)
    worker.submit("same", 1, 4, false, 0xff000000.toInt())
    queue.runNext()
    delivered.single().acknowledge()
    queue.runNext() // settles the worker into its idle state

    worker.clear()
    worker.submit("same", 2, 4, false, 0xffffffff.toInt())
    queue.runNext()
    assertEquals(0, delivered[1].changed)
  }

  @Test fun rapidStyleResetsCoalesceToTheLatestEpochBeforeParsing() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered)

    worker.submit("same", 1, 4, false, 0xff000000.toInt())
    queue.runNext()
    delivered.single().acknowledge()
    queue.runNext()

    worker.clear()
    worker.submit("same", 2, 4, false, 0xff202020.toInt())
    worker.clear()
    worker.submit("same", 3, 4, false, 0xffffffff.toInt())
    queue.runNext()

    assertEquals(2, delivered.size)
    assertEquals(3, delivered.last().generation)
    assertEquals(0, delivered.last().changed)
  }

  @Test fun idleWorkerSchedulesASecondIndependentStream() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val worker = worker(queue, delivered)
    worker.submit("first", 1, 5, false, 0)
    queue.runNext()
    delivered.single().acknowledge()
    queue.runNext()
    assertTrue(queue.work.isEmpty())

    worker.submit("second", 2, 6, false, 0)
    assertEquals(1, queue.work.size)
    queue.runNext()
    assertEquals("second", delivered[1].text)
    assertEquals(0, delivered[1].changed)
  }
}
