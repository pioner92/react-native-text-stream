const reviewProofsEnabled = process.env.HARNESS_REVIEW_PROOFS === '1';
const allHarnessTests = '<rootDir>/__tests__/**/*.harness.{ts,tsx}';
const standardHarnessTests = '<rootDir>/__tests__/**/!(review-repro|registry-retention).harness.{ts,tsx}';

export default {
  preset: 'react-native-harness',
  // The harness CLI overwrites testPathIgnorePatterns with its platform filter.
  // Use a negative testMatch instead, so destructive device proofs require opt-in.
  testMatch: reviewProofsEnabled ? [allHarnessTests] : [standardHarnessTests],
};
