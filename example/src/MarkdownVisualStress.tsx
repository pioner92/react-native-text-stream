import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Platform, Pressable, ScrollView, Text, View, type NativeScrollEvent, type NativeSyntheticEvent } from 'react-native';
import { StreamScrollFollow } from './StreamScrollFollow';
import { MarkdownProgressiveStress } from './MarkdownProgressiveStress';
import { StreamText, streamFetch, useStreamTextRef, type TextStream } from 'react-native-text-stream';

const host = Platform.OS === 'android' ? '10.0.2.2' : '127.0.0.1';
const cases = ['typography', 'structure', 'tables', 'code', 'images', 'unicode', 'edges', 'bigtable', 'all'];

/** Manual visual regression fixture. All renders one mixed message; individual cases isolate seams. */
export function MarkdownVisualStress({ onBack }: { onBack: () => void }) {
  const ref = useStreamTextRef();
  const scroll = useRef<ScrollView>(null);
  const active = useRef<TextStream | null>(null);
  const [index, setIndex] = useState(0);
  const [run, setRun] = useState(0);
  const [light, setLight] = useState(false);
  const [narrow, setNarrow] = useState(false);
  const [large, setLarge] = useState(false);
  const [pitch, setPitch] = useState(false);
  const [slow, setSlow] = useState(false);
  const [film, setFilm] = useState(false);
  const [progressive, setProgressive] = useState(false);
  const [status, setStatus] = useState('Loading');

  useEffect(() => {
    if (film || progressive) return;
    scroll.current?.scrollTo({ y: 0, animated: false });
    const timer = setTimeout(() => {
      active.current?.abort(); ref.clear();
      const stream = streamFetch(`http://${host}:9882/visual?case=${cases[index]}&chunk=${slow ? 7 : 1024}&interval=${slow ? 35 : 4}&run=${Date.now()}`);
      active.current = stream; ref.attach(stream);
    }, 80);
    return () => { clearTimeout(timer); active.current?.abort(); };
  }, [index, run, slow, film, progressive, ref]);
  if (progressive) return <MarkdownProgressiveStress onBack={() => setProgressive(false)} />;
  if (film) return <MarkdownAnnouncement onBack={() => setFilm(false)} />;
  const foreground = light ? '#172033' : '#EDF1FA';
  const background = light ? '#FFFFFF' : '#10141E';
  const button = (label: string, action: () => void) => <Pressable key={label} accessibilityRole="button" accessibilityLabel={label}
    onPress={action} style={{ paddingHorizontal: 9, paddingVertical: 8, backgroundColor: light ? '#E9EDF7' : '#242E44', borderRadius: 6 }}>
    <Text style={{ fontSize: 12, color: foreground }}>{label}</Text></Pressable>;
  return <View style={{ flex: 1, backgroundColor: background }}>
    <View style={{ padding: 8, gap: 6 }}>
      <View style={{ flexDirection: 'row', gap: 6, flexWrap: 'wrap' }}>
        {button('Back', onBack)}{button('Prev', () => setIndex((index + cases.length - 1) % cases.length))}
        {button('Next', () => setIndex((index + 1) % cases.length))}{button('All', () => setIndex(8))}
        {button('Replay', () => setRun(run + 1))}{button('Film', () => setFilm(true))}
        {button('Early blocks', () => setProgressive(true))}
      </View>
      <View style={{ flexDirection: 'row', gap: 6, flexWrap: 'wrap' }}>
        {button(light ? 'Light' : 'Dark', () => setLight(!light))}{button(narrow ? '220 dp' : 'Full width', () => setNarrow(!narrow))}
        {button(large ? '22 pt' : '16 pt', () => setLarge(!large))}{button(pitch ? 'Pitch 28' : 'Auto pitch', () => setPitch(!pitch))}
        {button(slow ? 'Slow' : 'Fast', () => setSlow(!slow))}
        {button('Top', () => scroll.current?.scrollTo({ y: 0, animated: false }))}
        {button('End', () => scroll.current?.scrollToEnd({ animated: false }))}
      </View>
      <Text style={{ color: foreground, fontSize: 12 }}>{index + 1}/9 · {cases[index]} · {status}</Text>
    </View>
    <ScrollView ref={scroll} contentContainerStyle={{ padding: 16 }}>
      <View style={{ width: narrow ? 220 : '100%', borderColor: light ? '#CAD3E3' : '#3B4962', borderWidth: 1 }}>
        <StreamText ref={ref} nativeID="markdown-visual-stress" recordDisplayLog format="markdown" markdownFlavor="gfm"
          fontSize={large ? 22 : 16} lineHeight={pitch ? 28 : undefined} color={foreground}
          onStatusChange={(state, bytes) => setStatus(`${state} · ${bytes} B`)} />
      </View>
      <Text style={{ color: foreground, paddingTop: 14, fontSize: 11 }}>END OF NATIVE VIEW</Text>
    </ScrollView>
  </View>;
}

/** English announcement: native networking, parsing and rendering; JS only controls the demo viewport. */
export function MarkdownAnnouncement({ onBack }: { onBack: () => void }) {
  const ref = useStreamTextRef();
  const scroll = useRef<ScrollView>(null);
  const active = useRef<TextStream | null>(null);
  const follow = useRef(new StreamScrollFollow());
  const frame = useRef<number | null>(null);
  const [state, setState] = useState('ready');
  const [showLatest, setShowLatest] = useState(false);
  const cancelFollow = useCallback(() => {
    if (frame.current !== null) cancelAnimationFrame(frame.current);
    frame.current = null;
  }, []);
  const followLatest = useCallback(() => {
    if (!follow.current.shouldFollow || frame.current !== null) return;
    // Coalesce layout callbacks. Never restart a scroll animation for each token.
    frame.current = requestAnimationFrame(() => {
      frame.current = null;
      if (follow.current.shouldFollow) scroll.current?.scrollToEnd({ animated: false });
    });
  }, []);
  useEffect(() => () => { active.current?.abort(); cancelFollow(); }, [cancelFollow]);
  const beginGesture = () => { follow.current.beginGesture(); cancelFollow(); };
  const endGesture = () => { follow.current.endGesture(); setShowLatest(!follow.current.following); followLatest(); };
  const observeScroll = (event: NativeSyntheticEvent<NativeScrollEvent>) => {
    const { contentOffset, layoutMeasurement, contentSize } = event.nativeEvent;
    const previous = follow.current.following;
    follow.current.observe(contentOffset.y, layoutMeasurement.height, contentSize.height);
    if (previous !== follow.current.following) setShowLatest(!follow.current.following);
  };
  const start = () => {
    active.current?.abort(); cancelFollow(); follow.current.reset(); setShowLatest(false);
    ref.clear(); scroll.current?.scrollTo({ y: 0, animated: false }); setState('streaming');
    const stream = streamFetch(`http://${host}:9882/visual?case=announce&chunk=8&interval=100&run=${Date.now()}`);
    active.current = stream; ref.attach(stream);
  };
  const resume = () => { follow.current.resume(); setShowLatest(false); followLatest(); };
  return <View style={{ flex: 1, backgroundColor: '#0B1020', paddingHorizontal: 18, paddingTop: 10 }}>
    <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
      <Text style={{ color: '#B7A8FF', fontWeight: '800', fontSize: 13, letterSpacing: 1.5 }}>STREAMTEXT</Text>
      <Text style={{ color: '#A5B3CB', fontSize: 11 }}>REACT NATIVE · iOS + Android</Text>
    </View>
    <Text style={{ color: '#F2F5FF', fontSize: 26, fontWeight: '800', letterSpacing: -0.7, marginTop: 10 }}>Markdown. In motion.</Text>
    <Text style={{ color: '#B2BED3', fontSize: 12, marginTop: 4, marginBottom: 12 }}>Rich answers, streamed and rendered natively.</Text>
    <View style={{ backgroundColor: '#1C2540', padding: 12, borderRadius: 12, marginBottom: 12 }}>
      <Text style={{ color: '#DCE4F3', fontSize: 13, lineHeight: 18 }}>Turn my focus app idea into a launch plan, with a UI direction and starter code.</Text>
    </View>
    <View style={{ flex: 1, backgroundColor: '#131D30', borderRadius: 16, borderWidth: 1, borderColor: '#33435E', overflow: 'hidden' }}>
      <View style={{ padding: 12, borderBottomWidth: 1, borderColor: '#33435E', flexDirection: 'row', justifyContent: 'space-between' }}>
        <Text style={{ color: '#E8EEFA', fontSize: 13, fontWeight: '600' }}>Daylight · Product agent</Text>
        <Text accessibilityLiveRegion="polite" style={{ color: state === 'error' ? '#FFB4AB' : '#8BE6CE', fontSize: 10 }}>{state === 'done' ? 'COMPLETE ✓' : state === 'streaming' ? '● STREAMING' : state === 'error' ? 'RETRY' : 'READY'}</Text>
      </View>
      <ScrollView ref={scroll} contentContainerStyle={{ padding: 14, paddingBottom: 24 }} scrollEventThrottle={32}
        onContentSizeChange={followLatest} onScroll={observeScroll}
        onScrollBeginDrag={beginGesture} onScrollEndDrag={endGesture}
        onMomentumScrollBegin={beginGesture} onMomentumScrollEnd={endGesture}>
        <StreamText ref={ref} format="markdown" markdownFlavor="gfm" fontSize={16} lineHeight={24} color="#EDF2FC"
          onStatusChange={status => {
            if (status === 'done') setState('done');
            else if (status === 'error' || status === 'cancelled' || status === 'truncated') setState('error');
          }} />
      </ScrollView>
      {showLatest && <Pressable accessibilityRole="button" accessibilityLabel="Jump to latest" onPress={resume}
        style={{ position: 'absolute', bottom: 12, alignSelf: 'center', backgroundColor: '#E4DDFF', paddingHorizontal: 14, paddingVertical: 9, borderRadius: 20 }}>
        <Text style={{ color: '#17112E', fontSize: 12, fontWeight: '600' }}>↓ Latest</Text>
      </Pressable>}
      {(state === 'ready' || state === 'error') && <Pressable accessibilityRole="button" accessibilityLabel="Generate launch plan" onPress={start}
        style={{ position: 'absolute', bottom: 18, left: 18, right: 18, backgroundColor: '#B7A8FF', padding: 12, borderRadius: 12 }}>
        <Text style={{ color: '#101426', textAlign: 'center', fontWeight: '700' }}>Generate launch plan</Text>
      </Pressable>}
    </View>
    <View style={{ minHeight: 38, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
      <Text style={{ color: '#B2BED3', fontSize: 11 }}>react-native-text-stream</Text>
      {state === 'done' && <Pressable accessibilityRole="button" accessibilityLabel="Replay response" onPress={start} hitSlop={8}>
        <Text style={{ color: '#C0B2FF', fontSize: 11 }}>Replay ↻</Text>
      </Pressable>}
      <Pressable accessibilityRole="button" accessibilityLabel="Back to visual tests" onPress={onBack} hitSlop={8}>
        <Text style={{ color: '#B2BED3', fontSize: 11 }}>Native Markdown</Text>
      </Pressable>
    </View>
  </View>;
}
