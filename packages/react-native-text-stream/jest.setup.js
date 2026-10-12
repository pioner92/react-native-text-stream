// The native boundary, mocked for every unit test: the host component (its view config is kept
// for the contract tests), the view commands and the StreamTextBuffers TurboModule. StreamText
// calls these while it is imported, so the mocks create their own objects.
jest.mock('react-native/Libraries/NativeComponent/NativeComponentRegistry', () => {
  const configs = {}
  return {
    configs,
    get: (name, config) => {
      configs[name] = config
      return name
    },
  }
})

jest.mock('react-native/Libraries/Utilities/codegenNativeCommands', () => {
  const commands = { attach: jest.fn(), clear: jest.fn(), options: null }
  return {
    __esModule: true,
    default: (options) => {
      if (options) commands.options = options
      return commands
    },
  }
})

jest.mock('react-native/Libraries/TurboModule/TurboModuleRegistry', () => {
  const actual = jest.requireActual('react-native/Libraries/TurboModule/TurboModuleRegistry')
  const buffers = { put: jest.fn(), fetch: jest.fn(), cancel: jest.fn() }
  const requested = []
  return {
    ...actual,
    buffers,
    requested,
    get: (name) => {
      requested.push(name)
      return name === 'StreamTextBuffers' ? buffers : actual.get(name)
    },
  }
})
