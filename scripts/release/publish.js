import { writeFileSync, readFileSync } from "node:fs";

const releaseNotesPath = "release-notes.md";

function extractChangelogContent(filePath) {
  const changelog = readFileSync(filePath, "utf-8");
  const lines = changelog.split(/\r?\n/);
  let content = "";
  let capture = false;

  for (const line of lines) {
    let firstCapture = false;
    if (line.startsWith("## ")) {
      if (capture) break;
      capture = true;
      firstCapture = true;
    }

    if (!firstCapture && capture) {
      content += line + "\n";
    }
  }

  return content.trim().replaceAll("###", "##");
}

const [, , godotVersions] = process.argv;
if (!godotVersions) {
  console.error("Cannot find Godot versions");
  process.exit(1);
}

const content = extractChangelogContent("CHANGELOG.md");
writeFileSync(releaseNotesPath, `Godot version: ${godotVersions}\n\n` + content);
console.log(`Wrote ${releaseNotesPath} (${content.length} chars)`);
