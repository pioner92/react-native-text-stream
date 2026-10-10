import React, { useEffect, useState } from 'react';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { fetch } from 'react-native-nitro-fetch';
import { StreamTextView } from './StreamTextView';

const STREAM_URL = 'http://localhost:9877/?fmt=raw&chunk=7&tps=60&repeat=40';

const HISTORY = [
  { mine: true, text: 'Hey! Can you explain how native text streaming works?' },
  { mine: false, text: 'Sure. The response is written into a native buffer, and the view reads it every frame.' },
  { mine: true, text: 'Show me a longer answer, streamed.' },
];

export function ChatSpike({ onBack }: { onBack: () => void }) {
  const [buffer, setBuffer] = useState<ArrayBuffer>();
  const [jsTicks, setJsTicks] = useState(0);

  useEffect(() => {
    const id = setInterval(() => setJsTicks((n) => n + 1), 100);
    return () => clearInterval(id);
  }, []);

  const stream = async () => {
    setBuffer(undefined);
    const res = await fetch(STREAM_URL, { stream: true, nativeBuffer: true });
    setBuffer(res.nativeBuffer);
  };

  const freeze = () =>
    setTimeout(() => {
      const until = Date.now() + 5000;
      while (Date.now() < until) {
        // busy-wait on purpose
      }
    }, 50);

  return (
    <View style={styles.screen}>
      <View style={styles.header}>
        <Pressable onPress={onBack} hitSlop={12}>
          <Text style={styles.link}>‹ Demo</Text>
        </Pressable>
        <Text style={styles.ticks}>JS {jsTicks}</Text>
        <Pressable onPress={stream} style={styles.button}>
          <Text style={styles.buttonText}>Stream</Text>
        </Pressable>
        <Pressable onPress={freeze} style={[styles.button, styles.freeze]}>
          <Text style={styles.buttonText}>Freeze 5s</Text>
        </Pressable>
      </View>
      <ScrollView contentContainerStyle={styles.list}>
        {HISTORY.map((m, i) => (
          <View key={i} style={[styles.bubble, m.mine ? styles.mine : styles.theirs]}>
            <Text style={styles.text}>{m.text}</Text>
          </View>
        ))}
        <View style={[styles.bubble, styles.theirs, styles.streamBubble]}>
          <StreamTextView source={buffer} fontSize={15} color="#ECECF1" />
        </View>
        <Text style={styles.footer}>— end of list —</Text>
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1 },
  header: { flexDirection: 'row', alignItems: 'center', gap: 10, paddingVertical: 10 },
  link: { color: '#8B5CF6', fontSize: 15, fontWeight: '600' },
  ticks: { color: '#ECECF1', flex: 1, fontVariant: ['tabular-nums'] },
  button: { backgroundColor: '#8B5CF6', borderRadius: 10, paddingHorizontal: 10, paddingVertical: 7 },
  freeze: { backgroundColor: '#B45309' },
  buttonText: { color: 'white', fontWeight: '700' },
  list: { paddingVertical: 12, gap: 10 },
  bubble: { maxWidth: '85%', borderRadius: 18, paddingHorizontal: 14, paddingVertical: 10 },
  mine: { alignSelf: 'flex-end', backgroundColor: '#8B5CF6' },
  theirs: { alignSelf: 'flex-start', backgroundColor: '#24242E' },
  streamBubble: { width: '85%' },
  text: { color: '#ECECF1', fontSize: 15, lineHeight: 21 },
  footer: { color: '#8B8B9A', textAlign: 'center', marginTop: 8 },
});
