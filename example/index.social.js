import React, { useState } from 'react';
import { AppRegistry, StatusBar } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { BenchScreen } from './src/bench/BenchScreen';
import { SocialMountBench } from './src/bench/SocialMountBench';
import { name as appName } from './app.json';

function SocialBenchmarkApp() {
  const [screen, setScreen] = useState('streaming');
  return <SafeAreaProvider>
    <StatusBar hidden />
    <SafeAreaView style={{ flex: 1, backgroundColor: '#0D1017' }}>
      {screen === 'mount' ? <SocialMountBench onBack={() => setScreen('streaming')} /> : (
        <BenchScreen onBack={() => {}} onOpenHistory={() => {}} onOpenMount={() => setScreen('mount')} />
      )}
    </SafeAreaView>
  </SafeAreaProvider>;
}

AppRegistry.registerComponent(appName, () => SocialBenchmarkApp);
