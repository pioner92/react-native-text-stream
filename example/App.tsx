import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  Animated,
  Easing,
  Pressable,
  ScrollView,
  StatusBar,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { fetch } from 'react-native-nitro-fetch';
import { StreamText } from 'react-native-text-stream';
import { createUtf8StreamDecoder } from './src/utf8';
import { BenchScreen } from './src/bench/BenchScreen';

// iOS simulator reaches the host's localhost directly.
const SERVER = 'http://localhost:9877';
// 7-byte chunks split emoji mid-character on purpose.
const STREAM_URL = `${SERVER}/?fmt=raw&chunk=7&tps=60&repeat=40`;
const FREEZE_MS = 5000;

const C = {
  bg: '#0A0A0F',
  card: '#14141B',
  border: '#24242E',
  text: '#ECECF1',
  muted: '#8B8B9A',
  native: '#8B5CF6',
  js: '#F59E0B',
  live: '#F472B6',
  done: '#34D399',
};

export default function App(): React.JSX.Element {
  const [bench, setBench] = useState(false);
  if (!bench) return <Demo onOpenBench={() => setBench(true)} />;
  return (
    <SafeAreaProvider>
      <StatusBar barStyle="light-content" />
      <SafeAreaView style={styles.screen} edges={['top', 'bottom']}>
        <BenchScreen onBack={() => setBench(false)} />
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

function Demo({ onOpenBench }: { onOpenBench: () => void }): React.JSX.Element {
  const [buffer, setBuffer] = useState<ArrayBuffer>();
  const [jsText, setJsText] = useState('');
  const [jsBytes, setJsBytes] = useState(0);
  const [jsCallbacks, setJsCallbacks] = useState(0);
  const [jsState, setJsState] = useState<'idle' | 'live' | 'done'>('idle');
  const [frozen, setFrozen] = useState(false);
  const runId = useRef(0);

  const reset = useCallback(() => {
    runId.current++;
    setBuffer(undefined);
    setJsText('');
    setJsBytes(0);
    setJsCallbacks(0);
    setJsState('idle');
  }, []);

  const start = useCallback(async () => {
    reset();
    const run = runId.current;

    // Native: URLSession -> TSB1 buffer -> StreamText. JS only hands the buffer over.
    fetch(STREAM_URL, { stream: true, nativeBuffer: true })
      .then((res) => run === runId.current && setBuffer(res.nativeBuffer))
      .catch((e) => console.warn('native stream failed', e));

    // Baseline: every chunk crosses into JS, gets decoded and re-rendered.
    try {
      const res = await fetch(STREAM_URL, { stream: true });
      const reader = (res as any).body.getReader();
      const decode = createUtf8StreamDecoder();
      setJsState('live');
      for (;;) {
        const { value, done } = await reader.read();
        if (run !== runId.current) return reader.cancel();
        if (done) break;
        const piece = decode(value);
        setJsText((t) => t + piece);
        setJsBytes((b) => b + value.byteLength);
        setJsCallbacks((n) => n + 1);
      }
      setJsState('done');
    } catch (e) {
      console.warn('JS stream failed', e);
    }
  }, [reset]);

  const freezeJs = useCallback(() => {
    setFrozen(true);
    // Let the "frozen" banner commit before the busy-wait blocks the JS thread.
    setTimeout(() => {
      const until = Date.now() + FREEZE_MS;
      while (Date.now() < until) {
        // busy-wait on purpose
      }
      setFrozen(false);
    }, 50);
  }, []);

  return (
    <SafeAreaProvider>
      <StatusBar barStyle="light-content" />
      <SafeAreaView style={styles.screen} edges={['top', 'bottom']}>
        <View style={styles.header}>
          <View style={styles.eyebrowRow}>
            <Text style={styles.eyebrow}>REACT NATIVE · NITRO</Text>
            <Pressable onPress={onOpenBench} hitSlop={12}>
              <Text style={styles.benchLink}>Benchmarks ›</Text>
            </Pressable>
          </View>
          <Text style={styles.title}>Native Text Streaming</Text>
          <Text style={styles.subtitle}>
            fetch → ArrayBuffer → native view.{'\n'}Zero JS on the hot path.
          </Text>
          <View style={styles.threads}>
            <ThreadPulse label="UI thread" color={C.done} driver="native" />
            <ThreadPulse label="JS thread" color={C.js} driver="js" />
          </View>
        </View>

        <Card
          badge="NATIVE"
          badgeColor={C.native}
          title="StreamText"
          caption="URLSession → TSB1 buffer → UITextView · 0 JS callbacks"
        >
          <StreamText
            source={buffer}
            fontSize={14}
            color={C.text}
            showStats
            style={StyleSheet.absoluteFill}
          />
        </Card>

        <Card
          badge="JS"
          badgeColor={C.js}
          title="setState + <Text>"
          caption="ReadableStream → decode → React re-render per chunk"
        >
          <JsStreamView text={jsText} />
          <StatusPill
            state={jsState}
            detail={`${formatBytes(jsBytes)} · ${jsCallbacks} callbacks`}
          />
          {frozen && (
            <View style={styles.frozen}>
              <View style={styles.frozenPill}>
                <Text style={styles.frozenText}>🧊 JS thread frozen for {FREEZE_MS / 1000}s</Text>
                <Text style={styles.frozenHint}>The native card above keeps streaming</Text>
              </View>
            </View>
          )}
        </Card>

        <View style={styles.controls}>
          <Button label="Start streaming" onPress={start} variant="primary" />
          <Button label={`Freeze JS · ${FREEZE_MS / 1000}s`} onPress={freezeJs} variant="warn" />
          <Button label="Reset" onPress={reset} variant="ghost" />
        </View>
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

function Card(props: {
  badge: string;
  badgeColor: string;
  title: string;
  caption: string;
  children: React.ReactNode;
}) {
  return (
    <View style={styles.card}>
      <View style={styles.cardHeader}>
        <View style={[styles.badge, { backgroundColor: props.badgeColor + '26' }]}>
          <Text style={[styles.badgeText, { color: props.badgeColor }]}>{props.badge}</Text>
        </View>
        <Text style={styles.cardTitle}>{props.title}</Text>
      </View>
      <Text style={styles.cardCaption} numberOfLines={1}>
        {props.caption}
      </Text>
      <View style={styles.cardBody}>{props.children}</View>
    </View>
  );
}

function JsStreamView({ text }: { text: string }) {
  const scroll = useRef<ScrollView>(null);
  return (
    <ScrollView
      ref={scroll}
      style={StyleSheet.absoluteFill}
      contentContainerStyle={styles.jsContent}
      indicatorStyle="white"
      onContentSizeChange={() => scroll.current?.scrollToEnd({ animated: false })}
    >
      <Text style={styles.jsText}>{text}</Text>
    </ScrollView>
  );
}

function StatusPill({ state, detail }: { state: 'idle' | 'live' | 'done'; detail: string }) {
  const dot = state === 'live' ? C.live : state === 'done' ? C.done : C.muted;
  return (
    <View style={styles.pill}>
      <Text style={[styles.pillText, { color: dot }]}>● </Text>
      <Text style={styles.pillText}>
        {state.toUpperCase()}  {detail}
      </Text>
    </View>
  );
}

// "native" pulses on the UI thread (native driver); "js" is driven by JS timers and stops when JS is blocked.
function ThreadPulse({ label, color, driver }: { label: string; color: string; driver: 'native' | 'js' }) {
  const native = useRef(new Animated.Value(0)).current;
  const [jsPhase, setJsPhase] = useState(0);

  useEffect(() => {
    if (driver === 'native') {
      const loop = Animated.loop(
        Animated.timing(native, {
          toValue: 1,
          duration: 900,
          easing: Easing.inOut(Easing.quad),
          useNativeDriver: true,
        })
      );
      loop.start();
      return () => loop.stop();
    }
    const started = Date.now();
    const id = setInterval(() => setJsPhase(((Date.now() - started) % 900) / 900), 33);
    return () => clearInterval(id);
  }, [driver, native]);

  const ring =
    driver === 'native'
      ? {
          opacity: native.interpolate({ inputRange: [0, 1], outputRange: [0.7, 0] }),
          transform: [{ scale: native.interpolate({ inputRange: [0, 1], outputRange: [1, 2.6] }) }],
        }
      : { opacity: 0.7 * (1 - jsPhase), transform: [{ scale: 1 + 1.6 * jsPhase }] };

  return (
    <View style={styles.thread}>
      <View style={styles.pulseBox}>
        <Animated.View style={[styles.pulseRing, { backgroundColor: color }, ring]} />
        <View style={[styles.pulseDot, { backgroundColor: color }]} />
      </View>
      <Text style={styles.threadText}>{label}</Text>
    </View>
  );
}

function Button(props: { label: string; onPress: () => void; variant: 'primary' | 'warn' | 'ghost' }) {
  const v = buttonVariants[props.variant];
  return (
    <Pressable
      onPress={props.onPress}
      style={({ pressed }) => [styles.button, v.container, pressed && styles.buttonPressed]}
    >
      <Text style={[styles.buttonText, v.text]}>{props.label}</Text>
    </Pressable>
  );
}

function formatBytes(bytes: number) {
  return bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KB`;
}

const buttonVariants = {
  primary: StyleSheet.create({
    container: { backgroundColor: C.native, flex: 1.4 },
    text: { color: '#fff' },
  }),
  warn: StyleSheet.create({
    container: { borderColor: C.js + '80', borderWidth: 1, flex: 1.2 },
    text: { color: C.js },
  }),
  ghost: StyleSheet.create({
    container: { backgroundColor: '#1E1E27', flex: 0.8 },
    text: { color: C.muted },
  }),
};

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: C.bg, paddingHorizontal: 16 },
  header: { paddingTop: 8, paddingBottom: 14 },
  eyebrowRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  eyebrow: { color: C.native, fontSize: 11, fontWeight: '700', letterSpacing: 1.6 },
  benchLink: { color: C.muted, fontSize: 13, fontWeight: '600' },
  title: { color: C.text, fontSize: 30, fontWeight: '800', letterSpacing: -0.6, marginTop: 4 },
  subtitle: { color: C.muted, fontSize: 14, lineHeight: 20, marginTop: 6 },
  threads: { flexDirection: 'row', gap: 10, marginTop: 14 },
  thread: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    backgroundColor: C.card,
    borderColor: C.border,
    borderWidth: 1,
    borderRadius: 999,
    paddingVertical: 6,
    paddingHorizontal: 12,
  },
  threadText: { color: C.text, fontSize: 12, fontWeight: '600' },
  pulseBox: { width: 10, height: 10, alignItems: 'center', justifyContent: 'center' },
  pulseRing: { position: 'absolute', width: 10, height: 10, borderRadius: 5 },
  pulseDot: { width: 8, height: 8, borderRadius: 4 },
  card: {
    flex: 1,
    backgroundColor: C.card,
    borderColor: C.border,
    borderWidth: 1,
    borderRadius: 20,
    padding: 14,
    marginBottom: 12,
  },
  cardHeader: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  badge: { borderRadius: 6, paddingHorizontal: 7, paddingVertical: 3 },
  badgeText: { fontSize: 10, fontWeight: '800', letterSpacing: 1 },
  cardTitle: { color: C.text, fontSize: 16, fontWeight: '700' },
  cardCaption: { color: C.muted, fontSize: 12, marginTop: 6 },
  cardBody: {
    flex: 1,
    marginTop: 10,
    borderRadius: 14,
    backgroundColor: '#0E0E14',
    overflow: 'hidden',
  },
  jsContent: { paddingHorizontal: 12, paddingTop: 14, paddingBottom: 40 },
  jsText: { color: C.text, fontSize: 14, lineHeight: 21 },
  pill: {
    position: 'absolute',
    right: 10,
    bottom: 10,
    height: 22,
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: 10,
    borderRadius: 11,
    backgroundColor: 'rgba(255,255,255,0.08)',
    borderColor: 'rgba(255,255,255,0.12)',
    borderWidth: 1,
  },
  pillText: { color: 'rgba(255,255,255,0.85)', fontSize: 11, fontWeight: '600', fontVariant: ['tabular-nums'] },
  frozen: {
    position: 'absolute',
    top: 0,
    right: 0,
    bottom: 0,
    left: 0,
    backgroundColor: 'rgba(10,10,15,0.78)',
    borderRadius: 14,
    alignItems: 'center',
    justifyContent: 'center',
  },
  frozenPill: {
    alignItems: 'center',
    backgroundColor: '#1C170C',
    borderColor: C.js + '66',
    borderWidth: 1,
    borderRadius: 14,
    paddingVertical: 12,
    paddingHorizontal: 18,
  },
  frozenText: { color: C.js, fontSize: 15, fontWeight: '700' },
  frozenHint: { color: C.muted, fontSize: 12, marginTop: 4 },
  controls: { flexDirection: 'row', gap: 10, paddingTop: 2, paddingBottom: 6 },
  button: { height: 48, borderRadius: 14, alignItems: 'center', justifyContent: 'center' },
  buttonPressed: { opacity: 0.75, transform: [{ scale: 0.98 }] },
  buttonText: { fontSize: 15, fontWeight: '700' },
});
