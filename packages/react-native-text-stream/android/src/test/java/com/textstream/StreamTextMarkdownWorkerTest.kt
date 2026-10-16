package com.textstream

import java.util.concurrent.Executor
import org.junit.Assert.*
import org.junit.Test

class StreamTextMarkdownWorkerTest {
  @Test fun richWidthChangeReplacesAttachmentsEvenWhenParsedTextIsIdentical() {
    val queue = Queue()
    val deliveries = mutableListOf<Delivery>()
    val widths = mutableListOf<Int>()
    val document = StreamTextMarkdown("\uFFFC", intArrayOf(0), intArrayOf(1), intArrayOf(0),
        intArrayOf(0), intArrayOf(0), arrayOf(null),
        arrayOf(StreamTextMarkdown.RichBlock(2, 0, 0, 0, null, "https://example.test/a", "alt")))
    val worker = StreamTextMarkdownWorker(executor = queue, parse = { _, _, _ -> document },
        style = { value, _, width, _ -> widths.add(width); value.text },
        deliver = { generation, bytes, final, value, _, changed, acknowledge ->
          deliveries.add(Delivery(generation, bytes, final, value.text, changed, acknowledge))
        })
    fun submit(width: Int) {
      worker.submit("source", 1, 6, true, 0, width)
      queue.runNext()
      deliveries.last().acknowledge()
      queue.runNext()
    }
    submit(300)
    submit(300)
    submit(190)
    assertEquals(listOf(0, 1, 0), deliveries.map { it.changed })
    // The unchanged 300px snapshot reuses its worker-built attachments; only a width change
    // needs a new measured table/image span.
    assertEquals(listOf(300, 190), widths)
  }

  @Test fun pendingImageUrlBytesReuseTheAcknowledgedVisualSnapshot() {
    val queue = Queue()
    val deliveries = mutableListOf<Delivery>()
    val pending = StreamTextMarkdown("\uFFFC", intArrayOf(0), intArrayOf(1), intArrayOf(0),
        intArrayOf(0), intArrayOf(0), arrayOf(null),
        arrayOf(StreamTextMarkdown.RichBlock(StreamTextMarkdown.BLOCK_IMAGE_PENDING, 0, 0, 0, null, null, "diagram")))
    var styleCalls = 0
    val worker = StreamTextMarkdownWorker(executor = queue, parse = { _, _, _ -> pending },
        style = { document, _, _, _ -> styleCalls++; document.text },
        deliver = { generation, bytes, final, value, _, changed, acknowledge ->
          deliveries.add(Delivery(generation, bytes, final, value.text, changed, acknowledge))
        })
    worker.submit("![diagram](https://exa", 1, 22, true, 0, 300)
    queue.runNext()
    deliveries.single().acknowledge()
    queue.runNext()
    worker.submit("![diagram](https://example.test/longer", 1, 37, true, 0, 300)
    queue.runNext()
    assertEquals(1, styleCalls)
    assertEquals(1, deliveries.last().changed)
  }

  @Test fun visualSnapshotReuseNeverCrossesColorStyleOrGenerationBoundaries() {
    val queue = Queue()
    val deliveries = mutableListOf<Delivery>()
    val pending = StreamTextMarkdown("\uFFFC", intArrayOf(0), intArrayOf(1), intArrayOf(0),
        intArrayOf(0), intArrayOf(0), arrayOf(null),
        arrayOf(StreamTextMarkdown.RichBlock(StreamTextMarkdown.BLOCK_IMAGE_PENDING, 0, 0, 0, null, null, "diagram")))
    var styleCalls = 0
    val worker = StreamTextMarkdownWorker(executor = queue, parse = { _, _, _ -> pending },
        style = { document, _, _, _ -> styleCalls++; document.text },
        deliver = { generation, bytes, final, value, _, changed, acknowledge ->
          deliveries.add(Delivery(generation, bytes, final, value.text, changed, acknowledge))
        })
    fun submit(generation: Long, color: Int, style: StreamTextStyle = StreamTextStyle()) {
      worker.submit("![diagram](partial", generation, 19, true, color, 300, style)
      queue.runNext()
      deliveries.last().acknowledge()
      queue.runNext()
    }
    submit(1, 0xff112233.toInt())
    submit(1, 0xff223344.toInt())
    submit(1, 0xff223344.toInt(), StreamTextStyle(fontSize = 18f))
    worker.clear()
    submit(2, 0xff223344.toInt(), StreamTextStyle(fontSize = 18f))
    assertEquals(4, styleCalls)
  }
  private class Queue : Executor {
    val work = ArrayDeque<Runnable>()
    override fun execute(command: Runnable) { work.addLast(command) }
    fun runNext() { work.removeFirst().run() }
  }
  private data class Delivery(val generation: Long, val bytes: Long, val final: Boolean, val text: String, val changed: Int, val acknowledge: () -> Unit)
  private fun worker(queue: Queue, deliveries: MutableList<Delivery>, fail: Boolean = false) = StreamTextMarkdownWorker(
      executor = queue,
      parse = { text, _, _ -> if (fail) null else StreamTextMarkdown.plain(text) },
      style = { document, _, _, _ -> document.text },
      deliver = { generation, bytes, final, document, _, changed, acknowledge ->
        deliveries.add(Delivery(generation, bytes, final, document.text, changed, acknowledge))
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

  @Test fun terminalParseIsDeliveredAsFinalEvenAtTheSameByteOffset() {
    val queue = Queue()
    val delivered = mutableListOf<Delivery>()
    val parseFinal = mutableListOf<Boolean>()
    val worker = StreamTextMarkdownWorker(
        executor = queue,
        parse = { text, _, final -> parseFinal.add(final); StreamTextMarkdown.plain(if (final) "$text final" else text) },
        style = { document, _, _, _ -> document.text },
        deliver = { generation, bytes, final, document, _, changed, acknowledge ->
          delivered.add(Delivery(generation, bytes, final, document.text, changed, acknowledge))
        })

    worker.submit("[unfinished", 9, 11, true, 0, final = false)
    queue.runNext()
    delivered.single().acknowledge()
    queue.runNext()
    worker.submit("[unfinished", 9, 11, true, 0, final = true)
    queue.runNext()

    assertEquals(listOf(false, true), parseFinal)
    assertEquals(11L, delivered[1].bytes)
    assertTrue(delivered[1].final)
    assertEquals("[unfinished final", delivered[1].text)
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
