#!/usr/bin/env node
// Give Codex the command-output compaction Claude Code already has.
//
// Register it as a PreToolUse hook in ~/.codex/hooks.json:
//
//   {"matcher": "shell|local_shell|unified_exec|Bash",
//    "hooks": [{"type": "command",
//               "command": "node \"<repo>/tools/agents/rtk-codex-hook.cjs\"",
//               "timeout": 5000}]}
//
// `rtk hook claude` is the processor both agents share, so the exclusions in
// ~/.config/rtk/config.toml — the ones that keep esptool, idf.py and the flash
// tools unfiltered — apply to Codex through this file without being written
// twice. What rtk cannot do is read Codex's payload: Claude Code sends a shell
// call as one string, Codex sends argv (["bash","-lc","git status"]), and rtk
// answers a non-string command with silence. This unwraps the shapes we know
// and leaves anything else alone, because a guess here rewrites somebody's
// command.
//
// It never blocks a command: any failure exits 0 with no output, which Codex
// reads as "no opinion".
//
//   node tools/agents/rtk-codex-hook.cjs --self-test

const { execFileSync } = require('child_process');
const fs = require('fs');

const SHELLS = /^(?:.*\/)?(?:ba|z|)sh$/;   // sh, bash, zsh, with or without a path
const FLAGS = /^-[a-z]*c$/;                 // -c, -lc, -ec … the flag that takes a command

// The command string inside a tool input, and how to put a rewritten one back.
// null when this is not a shape we understand.
function unwrap(command) {
    if (typeof command === 'string') return { text: command, rewrap: (s) => s };
    if (Array.isArray(command) && command.length === 3 &&
        SHELLS.test(String(command[0])) && FLAGS.test(String(command[1])) &&
        typeof command[2] === 'string') {
        return { text: command[2], rewrap: (s) => [command[0], command[1], s] };
    }
    return null;
}

function rewrite(text) {
    const out = execFileSync('rtk', ['hook', 'claude'], {
        input: JSON.stringify({ tool_name: 'Bash', tool_input: { command: text } }),
        encoding: 'utf8',
    });
    if (!out.trim()) return null;
    const decision = JSON.parse(out).hookSpecificOutput || {};
    const updated = (decision.updatedInput || {}).command;
    return typeof updated === 'string' && updated !== text ? updated : null;
}

function selfTest() {
    const assert = require('assert');
    assert.strictEqual(unwrap('git status').text, 'git status');
    assert.deepStrictEqual(unwrap(['bash', '-lc', 'git status']).rewrap('rtk git status'),
                           ['bash', '-lc', 'rtk git status']);
    assert.strictEqual(unwrap(['/bin/sh', '-c', 'ls']).text, 'ls');
    // Shapes we deliberately do not touch: a bare argv, and an empty one. Joining
    // argv into a shell line would change quoting, which is a way to break a
    // command while claiming to save tokens.
    assert.strictEqual(unwrap(['ls', '-la']), null);
    assert.strictEqual(unwrap([]), null);
    assert.strictEqual(unwrap(undefined), null);
    console.log('self-test ok');
}

function main() {
    if (process.argv[2] === '--self-test') return selfTest();

    const payload = JSON.parse(fs.readFileSync(0, 'utf8'));
    const input = payload.tool_input || payload.toolInput || {};
    const shape = unwrap(input.command);
    if (!shape) return;

    const rewritten = rewrite(shape.text);
    if (rewritten === null) return;

    // Every other key of the input is carried through: Codex sends `workdir`
    // and `timeout_ms` beside the command, and an updatedInput that dropped
    // them would run the command somewhere else.
    process.stdout.write(JSON.stringify({
        hookSpecificOutput: {
            hookEventName: 'PreToolUse',
            permissionDecisionReason: 'RTK auto-rewrite',
            updatedInput: { ...input, command: shape.rewrap(rewritten) },
        },
    }));
}

try { main(); } catch { /* never block a command over output compaction */ }
