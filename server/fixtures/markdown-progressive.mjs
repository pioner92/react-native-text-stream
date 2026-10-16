// Each prefix intentionally stops at a transport seam. The visual screen can
// hold it open long enough to inspect native layout before the next byte arrives.
export function progressiveStages(host, run) {
  const intro = '## A focused first release\n\nHere is the plan as it arrives.\n\n';
  const header = intro + '| Feature | Status |\n| :--- | ---: |\n';
  const table = header + '| Focus timer | Ready |\n| Daily recap | Next |\n\n';
  const image = table + '### Visual direction\n\n![';
  const url = `http://${host}/image.png?delay=2500&key=progressive-${encodeURIComponent(run)}`;
  return {
    first: intro + '| Fea',
    cells: intro + '| Feature | Sta',
    separator: intro + '| Feature | Status |\n| :--',
    rollback: intro + '| Feature | Status |\nThis is ordinary prose, not a table.',
    fence: intro + '```',
    language: intro + '```typescript',
    code: intro + '```typescript\nconst focus',
    header,
    row: header + '| Focus ti',
    image,
    label: image + 'Purple and teal concept',
    url: image + `Purple and teal concept](${url}`,
    loading: image + `Purple and teal concept](${url})\n\nThe answer continues while the image loads.`,
  };
}

export function progressiveCompletion(stages, stage) {
  if (stage === 'rollback') return stages.rollback + '\n\nThe original text is preserved.\n';
  if (['fence', 'language', 'code'].includes(stage)) {
    return stages.code + ': number = 25;\nconsole.log(focus);\n```\n\nThe code block is complete.\n';
  }
  return stages.loading;
}
