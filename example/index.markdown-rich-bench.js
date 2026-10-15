import React from 'react';
import { AppRegistry } from 'react-native';
import { MarkdownPerfBench } from './src/bench/MarkdownPerfBench';
import { name } from './app.json';
AppRegistry.registerComponent(name, () => () => <MarkdownPerfBench rich />);
