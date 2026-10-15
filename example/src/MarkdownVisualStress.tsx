import React, { useEffect, useRef, useState } from 'react';
import { Platform, Pressable, ScrollView, Text, View } from 'react-native';
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
  const [status, setStatus] = useState('Loading');

  useEffect(() => {
    if (film) return;
    scroll.current?.scrollTo({ y: 0, animated: false });
    const timer = setTimeout(() => {
      active.current?.abort(); ref.clear();
      const stream = streamFetch(`http://${host}:9882/visual?case=${cases[index]}&chunk=${slow ? 7 : 1024}&interval=${slow ? 35 : 4}&run=${Date.now()}`);
      active.current = stream; ref.attach(stream);
    }, 80);
    return () => { clearTimeout(timer); active.current?.abort(); };
  }, [index, run, slow, film, ref]);
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

/** English announcement: the response is streamed by native networking, without JS chunk delivery. */
export function MarkdownAnnouncement({ onBack }: { onBack: () => void }) {
  const ref = useStreamTextRef();
  const scroll = useRef<ScrollView>(null);
  const active = useRef<TextStream | null>(null);
  const [state, setState] = useState('ready');
  useEffect(() => () => active.current?.abort(), []);
  const start = () => {
    active.current?.abort(); ref.clear(); scroll.current?.scrollTo({ y: 0, animated: false }); setState('streaming');
    const stream = streamFetch(`http://${host}:9882/visual?case=announce&chunk=8&interval=150&run=${Date.now()}`);
    active.current = stream; ref.attach(stream);
  };
  return <View style={{ flex: 1, backgroundColor: '#0B1020', paddingHorizontal: 22, paddingTop: 20 }}>
    <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
      <Text style={{ color: '#A997FF', fontWeight: '800', fontSize: 15, letterSpacing: 1.5 }}>STREAMTEXT</Text>
      <Text style={{ color: '#7C8DAA', fontSize: 11 }}>REACT NATIVE</Text>
    </View>
    <Text style={{ color: '#F2F5FF', fontSize: 32, fontWeight: '800', letterSpacing: -1, marginTop: 20 }}>Rich answers.{"\n"}Native streaming.</Text>
    <Text style={{ color: '#A6B5CE', fontSize: 14, marginTop: 8, marginBottom: 20 }}>Introducing rich Markdown for StreamText.</Text>
    <View style={{ backgroundColor: '#1C2540', padding: 14, borderRadius: 12, marginBottom: 14 }}>
      <Text style={{ color: '#D2DCF0', fontSize: 13, lineHeight: 18 }}>Turn my focus app idea into a launch plan, with a UI direction and starter code.</Text>
    </View>
    <View style={{ flex: 1, backgroundColor: '#131D30', borderRadius: 18, borderWidth: 1, borderColor: '#26364F', overflow: 'hidden' }}>
      <View style={{ padding: 16, borderBottomWidth: 1, borderColor: '#26364F', flexDirection: 'row', justifyContent: 'space-between' }}>
        <Text style={{ color: '#E8EEFA', fontSize: 13, fontWeight: '600' }}>Daylight · Product agent</Text>
        <Text style={{ color: '#79E2C6', fontSize: 11 }}>{state === 'done' ? 'COMPLETE ✓' : state === 'streaming' ? '● STREAMING' : 'READY'}</Text>
      </View>
      <ScrollView ref={scroll} contentContainerStyle={{ padding: 16, paddingBottom: 24 }}
        onContentSizeChange={() => { if (state !== 'ready') scroll.current?.scrollToEnd({ animated: true }); }}>
        <StreamText ref={ref} format="markdown" markdownFlavor="gfm" fontSize={16} lineHeight={24} color="#EDF2FC"
          onStatusChange={status => { if (status === 'done' || status === 'error') setState(status); }} />
      </ScrollView>
    </View>
    <Text style={{ color: '#A6B5CE', textAlign: 'center', fontSize: 12, paddingVertical: 16 }}>react-native-text-stream · iOS + Android</Text>
    {state === 'ready' && <Pressable accessibilityLabel="Record demo" onPress={start} style={{ backgroundColor: '#A997FF', padding: 14, borderRadius: 12, marginBottom: 10 }}>
      <Text style={{ color: '#101426', textAlign: 'center', fontWeight: '700' }}>Generate launch plan</Text>
    </Pressable>}
    {state === 'done' && <Pressable accessibilityLabel="Back to visual tests" onPress={onBack} style={{ position: 'absolute', top: 0, right: 0, width: 40, height: 35 }} />}
  </View>;
}
