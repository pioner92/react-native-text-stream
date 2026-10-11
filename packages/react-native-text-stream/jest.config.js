module.exports = {
  preset: '@react-native/jest-preset',
  roots: ['<rootDir>/src'],
  testMatch: ['**/__tests__/**/*.test.{ts,tsx}'],
  setupFiles: ['<rootDir>/jest.setup.js'],
}
