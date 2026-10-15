const end = (id) => `\n\n---\n✅ **END visual case ${id}**\n`;

export function cases(host, run = 'visual') {
  const image = `http://${host}/image.png?key=${encodeURIComponent(run)}`;
  const bad = `http://${host}/broken.png?key=${encodeURIComponent(run)}`;
  return [
    { id: 'typography', title: 'Typography and entities', markdown: `# Heading One\n## Heading Two\n### Heading Three\n#### Heading Four\n##### Heading Five\n###### Heading Six\n\n**bold**, *italic*, ~~strike~~, \`inline code\`, [safe link](https://example.com/?a=1&amp;b=2).\n\n***bold italic***, **bold with \`code\`**, and soft\nline / hard  \nline breaks.\n\nEntities: &amp; &lt; &#x1F680; and escaped \\*literal stars\\*.${end(1)}` },
    { id: 'structure', title: 'Nested lists and quotes', markdown: `> **Quoted intro**\n>\n> - [x] done\n> - [ ] pending\n>   1. first\n>   2. second\n\n1. ordered\n   - nested bullet\n     - deep bullet\n2. final${end(2)}` },
    { id: 'tables', title: 'Tables', markdown: `| Left | Center | Right |\n| :--- | :----: | ---: |\n| **bold** | [link](https://example.com) | 123 |\n| a_very_long_unbroken_table_value_that_must_wrap_or_clip_safely | *middle* | |\n| missing | | trailing |\n\n| A | B | C | D | E | F |\n| --- | --- | --- | --- | --- | --- |\n| one | two | three | four | five | six |\n\n> Quoted table:\n>\n> | Item | Value |\n> | --- | --- |\n> | nested | **works** |${end(3)}` },
    { id: 'code', title: 'Code languages', markdown: `\`\`\`js\nconst greeting = "hello"; // comment\nconsole.log(greeting, 42);\n\`\`\`\n\n\`\`\`ts\ninterface Result { ok: boolean }\nconst value: number = 7;\n\`\`\`\n\n\`\`\`json\n{"name":"visual","count":42,"ok":true}\n\`\`\`\n\n\`\`\`python\ndef greet(name):\n    return f"Hi {name}" # comment\n\`\`\`\n\n\`\`\`unknown\nthis stays plain: 123 "text"\n${'x'.repeat(600)}\n\`\`\`${end(4)}` },
    { id: 'images', title: 'Images', markdown: `Before ![checkerboard](${image}) after.\n\n[![linked image](${image})](https://example.com)\n\n![missing](${bad}) ![invalid](http://${host}/invalid.png) ![oversized](http://${host}/oversize.png)\n\n![blocked](file:///tmp/not-loaded.png)\n\n![large image](http://${host}/large.png?key=${encodeURIComponent(run)})${end(5)}` },
    { id: 'unicode', title: 'Unicode', markdown: `CJK: 你好世界 日本語 한국어. RTL: مرحبا بالعالم שלום. Combining: cafe\u0301, A\u030A, 👩🏽‍💻.\n\nMixed **مرحبا 世界 🚀** and [עברית](https://example.com).${end(6)}` },
    { id: 'edges', title: 'Streaming edges', markdown: `A **delimiter that closes late** and [reference][late].\n\n[late]: https://example.com/path?a=1&amp;b=2\n\n![alt that is complete only at close](${image})\n\n\`\`\`js\nconst unfinished = "fence closes later";\n\`\`\`${end(7)}` },
    { id: 'bigtable', title: 'Large table', markdown: `| # | Name | Status |\n| ---: | :--- | :---: |\n${Array.from({ length: 60 }, (_, i) => `| ${i + 1} | row-${i + 1} | ${i % 2 ? 'pending' : '**ready**'} |`).join('\n')}\n\n**LAST TABLE SENTINEL: row 60**${end(8)}` },
    { id: 'announce', title: 'Daylight launch plan', markdown: `# Daylight: a calmer way to focus

Daylight should feel like a quiet desk: **one meaningful intention**, a gentle timer, and a closing ritual. *Avoid noisy streaks and leaderboards.* ~~More features~~ **more clarity** is the launch principle.

## UI direction

![Daylight focus-screen concept](http://${host}/daylight-concept.png?run=${encodeURIComponent(run)})

Use warm paper, forest text, and sunlit accents. Lead with \`Today’s focus\`, then a short reflection. See the [Daylight concept](https://example.com/daylight-concept).

## Launch plan

- [x] Define the promise: “Make room for one meaningful thing.”
- [ ] Recruit 20 people overwhelmed by task apps
  - Ask for a seven-day diary, not feature requests
  - Watch where they abandon the ritual
- [ ] Ship a small public beta
  1. Invite a focused cohort
  2. Review completion notes weekly
  3. Keep only habits people return to

| Moment | Daylight choice | Why it matters |
| :--- | :---: | ---: |
| Morning | One focus card | reduces decision fatigue |
| Midday | Gentle reset | makes drift recoverable |
| Evening | Two-line reflection | closes the loop |

> “A focus app earns trust when it leaves people with more attention than it takes.”

## Starter shape

\`\`\`ts
type Focus = { title: string; minutes: number; done: boolean };
const begin = (focus: Focus) => ({ ...focus, minutes: 25 });
\`\`\`

\`\`\`json
{ "theme": "daylight", "dailyFocusLimit": 1 }
\`\`\`

Start with the ritual, measure *returning calm*, and let the product grow from there.` }

  ];
}
