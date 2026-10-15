// Seam (contract) tests: each boundary between the layers has two sides written in different
// languages, kept in sync only by hand. These read both sides and fail when one changes alone:
// a renamed prop, event, command or module, a status the JS types don't know, or a TSB1 header
// that no longer matches what streamFetch writes.
import fs from 'node:fs'
import path from 'node:path'
import { headerStatus, TSB_HEADER_SIZE } from '../buffer'
import '../StreamText'
import { tsbBuffer } from './tsb'

const root = path.resolve(__dirname, '../..')
const read = (file: string) => fs.readFileSync(path.join(root, file), 'utf8')
const all = (source: string, pattern: RegExp) => [...source.matchAll(pattern)].map((m) => m[1]!)

const shadowNode = read('cpp/StreamTextViewShadowNode.hpp')
// The module's constructor and its streamFetch() methods are in StreamTextFetch.cpp.
const buffersModule = read('cpp/StreamTextBuffers.hpp') + read('cpp/StreamTextBuffers.cpp') + read('cpp/StreamTextFetch.cpp')
const componentView = read('ios/fabric/StreamTextComponentView.mm')
const renderer = read('ios/fabric/StreamTextRenderer.swift')
const provider = read('ios/fabric/StreamTextBuffersProvider.mm')
const tsbHeader = read('ios/TextStreamBuffer.h')
const types = read('src/types.ts')
const androidFetcher = read('android/src/main/jni/StreamTextFetchAndroid.cpp')
const androidFetcherKotlin = read('android/src/main/java/com/textstream/StreamTextFetch.kt')

const { configs } = jest.requireMock('react-native/Libraries/NativeComponent/NativeComponentRegistry')
const commands = jest.requireMock('react-native/Libraries/Utilities/codegenNativeCommands').default()
const { requested } = jest.requireMock('react-native/Libraries/TurboModule/TurboModuleRegistry')

const viewConfig = () => configs.StreamTextView() as {
  uiViewClassName: string
  validAttributes: Record<string, unknown>
  directEventTypes: Record<string, { registrationName: string }>
}

describe('JS view config ↔ C++ component', () => {
  it('names the same component', () => {
    expect(Object.keys(configs)).toEqual(['StreamTextView'])
    expect(viewConfig().uiViewClassName).toBe('StreamTextView')
    expect(shadowNode).toContain('StreamTextViewComponentName[] = "StreamTextView"')
  })

  it('sends exactly the props the C++ props parser reads', () => {
    const parsed = [
      ...all(shadowNode, /convertRawProp\(context, rawProps, "(\w+)"/g),
      ...all(shadowNode, /rawProps\.at\("(\w+)"/g),
    ]
    expect(Object.keys(viewConfig().validAttributes).sort()).toEqual([...new Set(parsed)].sort())
  })

  it('compares the source prop by identity (two ArrayBuffers deep-compare as equal)', () => {
    const source = viewConfig().validAttributes.source as { diff: (a: unknown, b: unknown) => boolean }
    const buffer = new ArrayBuffer(8)
    expect(source.diff(buffer, buffer)).toBe(false)
    expect(source.diff(buffer, new ArrayBuffer(8))).toBe(true)
  })
})

describe('C++ event ↔ JS handler', () => {
  it('registers the event C++ dispatches', () => {
    const [cppName] = all(shadowNode, /dispatchEvent\(\s*"(\w+)"/g)
    expect(cppName).toBe('statusChange')
    const topName = `top${cppName![0]!.toUpperCase()}${cppName!.slice(1)}`
    expect(viewConfig().directEventTypes[topName]).toEqual({ registrationName: 'onStatusChange' })
  })

  it('carries the payload keys the JS handler reads', () => {
    expect(all(shadowNode, /setProperty\(runtime, "(\w+)"/g).sort()).toEqual(['bytes', 'generation', 'serial', 'status'])
  })

  it('every status the native view reports is a StreamTextStatus', () => {
    const declared = all(types, /\|\s*'(\w+)'/g)
    const reported = new Set([
      ...all(renderer, /reportStatus\("(\w+)"/g),
      ...all(renderer, /return "(\w+)"/g),
      ...all(componentView, /isEqualToString:@"(\w+)"\]/g).filter((s) => s !== 'attach' && s !== 'clear'),
    ])
    expect(reported.size).toBeGreaterThanOrEqual(6)
    for (const status of reported) expect(declared).toContain(status)
  })
})

describe('JS commands ↔ native component view', () => {
  it('sends only commands the view handles', () => {
    const handled = all(componentView.slice(componentView.indexOf('handleCommand')), /isEqualToString:@"(\w+)"/g)
    expect(commands.options.supportedCommands.sort()).toEqual(['attach', 'clear'])
    for (const command of commands.options.supportedCommands) expect(handled).toContain(command)
  })

  it('attach takes the numeric id the TurboModule returns', () => {
    expect(componentView).toMatch(/int bufferId = \[args\.firstObject intValue\]/)
    expect(componentView).toContain('StreamTextBufferRegistry::shared().take(bufferId)')
  })

  it('reads the generation where JS sends it: after the id for attach, alone for clear', () => {
    expect(componentView).toMatch(/int generation = args\.count > 1 \? \[args\[1\] intValue\] : 0;/)
    expect(componentView).toMatch(/generation:args\.count > 0 \? \[args\.firstObject intValue\] : 0\]/)
    expect(componentView).toContain('_propGeneration = reformatRetainedImperative ? _generation : next.sourceGeneration;')
  })
})

describe('JS ↔ StreamTextBuffers TurboModule', () => {
  it('asks for the module C++ registers, under the name the provider maps', () => {
    expect(requested).toContain('StreamTextBuffers')
    expect(buffersModule).toContain('static constexpr auto kModuleName = "StreamTextBuffers";')
    expect(buffersModule).toContain('TurboModule(kModuleName,')
    const pkg = JSON.parse(read('package.json'))
    expect(pkg.codegenConfig.ios.modulesProvider).toEqual({ StreamTextBuffers: 'StreamTextBuffersProvider' })
    expect(provider).toContain('@interface StreamTextBuffersProvider : NSObject <RCTModuleProvider>')
    expect(provider).toContain('std::make_shared<facebook::react::StreamTextBuffersModule>')
  })

  it('exposes fetch(url, init, onEvent) and cancel(id), which streamFetch() calls', () => {
    expect(buffersModule).toMatch(/methodMap_\["fetch"\] = MethodMetadata\{3, fetch\}/)
    expect(buffersModule).toMatch(/methodMap_\["cancel"\] = MethodMetadata\{1, cancel\}/)
    // The platforms' requests, built with the module on both.
    expect(read('ios/fabric/StreamTextFetch.mm')).toContain('streamTextStartFetch(')
    expect(read('android/src/main/jni/CMakeLists.txt')).toContain('StreamTextFetch.cpp')
    expect(read('android/src/main/jni/CMakeLists.txt')).toContain('StreamTextFetchAndroid.cpp')
  })

  it('is what Android autolinking includes and registers by name', () => {
    const android = require(path.join(root, 'react-native.config.js')).dependency.platforms.android
    // Autolinking emits `#include <X.h>` and `std::make_shared<X>(jsInvoker)` for cxxModuleHeaderName X.
    expect(buffersModule).toContain(`class ${android.cxxModuleHeaderName} : public TurboModule`)
    expect(read(`android/src/main/jni/${android.cxxModuleHeaderName}.h`)).toContain('#include "StreamTextBuffers.hpp"')
    // ...and `#include <react/renderer/components/<libraryName>/ComponentDescriptors.h>`.
    const descriptors = read(`android/src/main/jni/react/renderer/components/${android.libraryName}/ComponentDescriptors.h`)
    expect(descriptors).toContain('#include "StreamTextViewShadowNode.hpp"')
    expect(android.componentDescriptors).toEqual(['StreamTextViewComponentDescriptor'])
    expect(shadowNode).toContain('using StreamTextViewComponentDescriptor =')
    expect(read('android/src/main/jni/CMakeLists.txt')).toContain(`react_codegen_${android.libraryName}`)
    expect(read('android/src/main/jni/textstream.h')).toContain(`${android.libraryName}_ModuleProvider`)
  })

  it('exposes put with one argument, returning 0 for a buffer it cannot keep', () => {
    expect(buffersModule).toMatch(/methodMap_\["put"\] = MethodMetadata\{1, put\}/)
    expect(buffersModule).toMatch(/return buffer \? StreamTextBufferRegistry::shared\(\)\.put\(std::move\(buffer\)\) : 0;/)
  })
})

/** Field offsets of TSBHeader, laid out with C alignment rules. */
function headerLayout(source: string) {
  const struct = source.slice(source.indexOf('typedef struct {'), source.indexOf('} TSBHeader;'))
  const sizes: Record<string, number> = { uint8_t: 1, uint16_t: 2, uint32_t: 4, uint64_t: 8 }
  const offsets: Record<string, number> = {}
  let offset = 0
  for (const [, type, name, count] of struct.matchAll(/(uint\d+_t)\s+(\w+)(?:\[(\d+)\])?;/g)) {
    const size = sizes[type!]!
    offset = Math.ceil(offset / size) * size
    offsets[name!] = offset
    offset += size * (count ? Number(count) : 1)
  }
  return { offsets, size: offset }
}

describe('TSB1 buffer: streamFetch ↔ renderer (Swift) ↔ JS', () => {
  it('every native streamFetch implementation uses the renderer’s canonical header', () => {
    expect(read('cpp/StreamTextFetch.cpp')).toContain('#include "../ios/TextStreamBuffer.h"')
    expect(read('ios/fabric/StreamTextFetch.mm')).toContain('#include "../TextStreamBuffer.h"')
    expect(androidFetcher).toContain('#include "../../../../ios/TextStreamBuffer.h"')
    expect(read('ios/fabric/StreamTextFetch.mm')).toContain('tsb_append(')
    // Android writes through the ByteBuffer in Kotlin, then C++ release-publishes committed bytes.
    expect(androidFetcherKotlin).toContain('data.put(scratch, 0, read)')
    expect(androidFetcherKotlin).toContain('events.publish(handle, committed.toLong())')
    expect(androidFetcher).toContain('__atomic_store_n(&static_cast<TSBHeader *>(base)->committed, committed, __ATOMIC_RELEASE)')
  })

  it('the header is 64 bytes, with committed and status where JS reads them', () => {
    const { offsets, size } = headerLayout(tsbHeader)
    expect(size).toBe(TSB_HEADER_SIZE)
    expect(tsbHeader).toContain(`#define TSB_HEADER_SIZE ${TSB_HEADER_SIZE}`)
    expect(offsets.committed).toBe(8)
    expect(offsets.capacity).toBe(16)
    expect(offsets.status).toBe(24)
  })

  it('JS and the renderer map every status code the same way', () => {
    const codes = Object.fromEntries(
      [...tsbHeader.matchAll(/TSB_STATUS_(\w+) = (\d+)/g)].map((m) => [m[1]!, Number(m[2])])
    )
    expect(Object.keys(codes).sort()).toEqual(['CANCELLED', 'DONE', 'ERROR', 'OPEN', 'OVERFLOW'])
    // The renderer: final statuses by code; anything else is "error".
    const swift: Record<string, string> = Object.fromEntries(
      [...renderer.matchAll(/case UInt32\(TSB_STATUS_(\w+)\): return "(\w+)"/g)].map((m) => [m[1]!, m[2]!])
    )
    for (const [name, code] of Object.entries(codes)) {
      if (name === 'OPEN') {
        expect(headerStatus(tsbBuffer('', { status: code }))).toBe('streaming')
        continue
      }
      expect(headerStatus(tsbBuffer('', { status: code }))).toBe(swift[name] ?? 'error')
    }
  })
})
