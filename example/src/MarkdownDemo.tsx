import React, { useEffect, useRef, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { StreamText, streamFetch, useStreamTextRef, type TextStream } from 'react-native-text-stream';

export function MarkdownDemo({ onBack }: { onBack: () => void }) {
  const ref = useStreamTextRef();
  const active = useRef<TextStream | null>(null);
  const [status, setStatus] = useState('Ready');
  const [markdown, setMarkdown] = useState(true);
  useEffect(() => () => active.current?.abort(), []);
  const start = (large: boolean) => {
    active.current?.abort();
    ref.clear();
    const params = large ? 'bytes=102400&chunk=4096&tps=60' : 'repeat=1&chunk=17&tps=60';
    try {
      const stream = streamFetch(`http://localhost:9879/?fmt=raw&content=markdown&${params}`);
      active.current = stream;
      ref.attach(stream);
    } catch (error) {
      setStatus(String(error));
    }
  };
  const freeze = () => setTimeout(() => {
    const until = Date.now() + 5000;
    while (Date.now() < until) { /* Demonstrate native progress while JS is blocked. */ }
  }, 50);
  return <View style={styles.screen}>
    <View style={styles.controls}>
      <Pressable onPress={onBack}><Text style={styles.link}>‹ Demo</Text></Pressable>
      <Pressable onPress={() => start(false)}><Text style={styles.link}>Stream</Text></Pressable>
      <Pressable onPress={() => start(true)}><Text style={styles.link}>100 KB</Text></Pressable>
      <Pressable onPress={freeze}><Text style={styles.link}>Freeze JS</Text></Pressable>
      <Pressable onPress={() => setMarkdown(v => !v)}><Text style={styles.link}>{markdown ? 'Raw' : 'Markdown'}</Text></Pressable>
    </View>
    <Text style={styles.status}>{status}</Text>
    <ScrollView contentContainerStyle={styles.content}>
      <StreamText ref={ref} format={markdown ? 'markdown' : 'plain'} markdownFlavor="gfm"
        nativeID="markdown-demo" recordDisplayLog fontSize={16} color="#ECECF1"
        onStatusChange={(state, bytes) => setStatus(`${state} · ${bytes.toLocaleString()} source bytes`)} />
    </ScrollView>
  </View>;
}
const styles = StyleSheet.create({
  screen: { flex: 1 },
  controls: { flexDirection: 'row', flexWrap: 'wrap', gap: 18, padding: 16 },
  link: { color: '#A78BFA', fontSize: 15, fontWeight: '600' },
  status: { color: '#8B8B9A', marginHorizontal: 16, marginBottom: 8 },
  content: { padding: 16 },
});
