import React from "react";
import { AppRegistry, StatusBar } from "react-native";
import { SafeAreaProvider, SafeAreaView } from "react-native-safe-area-context";
import { MarkdownAnnouncement } from "./src/MarkdownVisualStress";
import { name } from "./app.json";
function Announcement() {
  return <SafeAreaProvider><StatusBar barStyle="light-content" />
    <SafeAreaView style={{ flex: 1, backgroundColor: "#0B1020" }}>
      <MarkdownAnnouncement onBack={() => {}} />
    </SafeAreaView></SafeAreaProvider>;
}
AppRegistry.registerComponent(name, () => Announcement);
