Pod::Spec.new do |s|
  s.name         = "TextStreamTesting"
  s.version      = "0.0.0"
  s.summary      = "Benchmark and test helpers for react-native-text-stream (example app only)"
  s.homepage     = "https://github.com/alexshumihin/react-native-text-stream"
  s.license      = "MIT"
  s.authors      = "Alex Shumihin"
  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => "https://github.com/alexshumihin/react-native-text-stream.git" }

  s.source_files = ["ios/**/*.swift"]

  load 'nitrogen/generated/ios/TextStreamTesting+autolinking.rb'
  add_nitrogen_files(s)

  # Reads the library's internals through `@_spi(Testing) import TextStream`.
  s.dependency 'TextStream'
  install_modules_dependencies(s)
end
