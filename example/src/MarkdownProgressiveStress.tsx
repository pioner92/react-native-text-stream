import React, { useEffect, useState } from 'react';
import { Platform, Pressable, ScrollView, Text, View } from 'react-native';
import { StreamText, streamFetch, useStreamTextRef } from 'react-native-text-stream';

const stages = ['first', 'cells', 'separator', 'header', 'row', 'rollback', 'fence', 'language', 'code', 'image', 'label', 'url', 'loading'] as const;
const host = Platform.OS === 'android' ? '10.0.2.2' : '127.0.0.1';

/** Holds a real HTTP stream at each rich-block seam for visual inspection. */
type MarkdownProgressiveStressProps = {
  onBack: () => void;
  initialStage?: typeof stages[number];
  initialLight?: boolean;
  initialNarrow?: boolean;
};

export function MarkdownProgressiveStress({
  onBack,
  initialStage = 'first',
  initialLight = false,
  initialNarrow = false,
}: MarkdownProgressiveStressProps) {
  const ref = useStreamTextRef();
  const [stage, setStage] = useState<typeof stages[number]>(initialStage);
  const [light, setLight] = useState(initialLight);
  const [narrow, setNarrow] = useState(initialNarrow);
  const [run, setRun] = useState(0);
  const [status, setStatus] = useState('streaming');
  useEffect(() => {
    ref.clear(); setStatus('streaming');
    const stream = streamFetch(`http://${host}:9882/progressive?stage=${stage}&run=${Date.now()}`);
    ref.attach(stream);
    return () => { stream.abort(); };
  }, [stage, run, ref]);
  const foreground = light ? '#172033' : '#EDF1FA';
  const button = (label: string, action: () => void) => <Pressable key={label} accessibilityRole="button"
    accessibilityLabel={label} onPress={action} style={{ padding: 9, borderRadius: 6, backgroundColor: light ? '#E9EDF7' : '#242E44' }}>
    <Text style={{ fontSize: 13, color: foreground }}>{label}</Text>
  </Pressable>;
  return <View style={{ flex: 1, backgroundColor: light ? '#FFFFFF' : '#10141E', padding: 12 }}>
    <View style={{ flexDirection: 'row', gap: 6, flexWrap: 'wrap' }}>
      {button('Back', onBack)}
      {button(light ? 'Light' : 'Dark', () => setLight(!light))}
      {button(narrow ? '220 dp' : 'Full width', () => setNarrow(!narrow))}
      {button('Replay seam', () => setRun(run + 1))}
      {stages.map(item => button(item, () => { setStage(item); setRun(run + 1); }))}
    </View>
    <Text style={{ color: foreground, fontSize: 12, marginVertical: 12 }}>Early blocks · {stage} · {status} · seam held for 30 s</Text>
    <ScrollView><View style={{ width: narrow ? 220 : '100%' }}>
      <StreamText ref={ref} nativeID="markdown-progressive-stress" recordDisplayLog format="markdown" markdownFlavor="gfm"
        fontSize={16} lineHeight={24} color={foreground} onStatusChange={state => setStatus(state)} />
    </View></ScrollView>
  </View>;
}
