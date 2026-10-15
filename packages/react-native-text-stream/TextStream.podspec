require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "TextStream"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported, :visionos => 1.0 }
  s.source       = { :git => "https://github.com/pioner92/react-native-text-stream.git", :tag => "v#{s.version}" }

  s.source_files = [
    # Implementation (Swift)
    "ios/**/*.{swift}",
    # Autolinking/Registration (Objective-C++)
    "ios/**/*.{m,mm}",
    # Implementation (C++ objects)
    "cpp/**/*.{h,hpp,c,cpp}",
    # TSB1 buffer format, read from Swift
    "ios/TextStreamBuffer.h",
    "ios/StreamMarkdown.h",
  ]

  # Swift imports the Markdown C ABI directly; keeping this public also makes the same parser
  # available to the Android/iOS C++ targets without a platform wrapper.
  s.public_header_files = ["ios/TextStreamBuffer.h", "ios/StreamMarkdown.h"]

  s.pod_target_xcconfig = {
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    # Swift is exposed to Objective-C++ (the component view) with C++ interop.
    "SWIFT_OBJC_INTEROP_MODE" => "objcxx",
    # A Swift module other pods can import (the example's testing pod, via @_spi(Testing)).
    "DEFINES_MODULE" => "YES",
    # The generated TextStream-Swift.h is only for this pod's own .mm files (static linkage on
    # Xcode 26.4 breaks when it is installed).
    "SWIFT_INSTALL_OBJC_HEADER" => "NO",
  }

  s.dependency 'React-jsi'
  s.dependency 'React-callinvoker'
  install_modules_dependencies(s)
end
