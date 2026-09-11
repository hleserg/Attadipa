# Introducing Attadipa

## Product and audience

Owner direction, 9 September 2026, recorded in
[issue #519](https://github.com/hleserg/Attadipa/issues/519):

**Attadipa is an open operating system for wearable devices and personal mesh
nodes. Watches are the first embodiment, not the boundary of the project.**
The ambition is a common open platform for applications, device makers and
users. Messaging and navigation are early applications, not the OS definition.

The website and README serve different readers:

- **Public site:** people curious about the project, including those unfamiliar
  with technology or uncomfortable with GitHub. Explain the purpose, show the
  experience, describe why openness matters, and offer a direct way to join the
  conversation without requiring GitHub.
- **README:** developers and technical contributors. Introduce the same OS,
  then explain architecture, capabilities, hardware, development and current
  implementation. Make the simulator and contribution paths easy to find.

The previous advice to remove “OS” from the repository description and topics
was wrong. It inferred the product identity from an overly narrow README.
That recommendation is withdrawn; do not apply the old topic-removal command.

## The message

The human benefit of the architecture is that a device can grow beyond the
functions its original maker imagined: different applications and different
hardware can share a common system.

Explain this through independent operation, personal applications and shared
development. Phone integration is optional, not forbidden. Keep the future
app ecosystem and device standard clearly framed as the project's ambition;
do not invent a released SDK, marketplace or universal hardware compatibility.

Use the canonical name **Attadipa**. Introduce the meaning briefly near the
start: *attadīpa*, relying on oneself. The [naming guide](../brand/naming.md)
holds the full brand rules.

## Public experience

The primary website action continues the project story on the page.
Telegram is an existing direct contact path. GitHub remains available for
readers who want source code or technical participation.

Do not use internal design-review dashboards as a consumer demo. A public demo
would need a separate visitor-facing experience; until one exists, the site
can show interface imagery directly without sending people into review tools.

Present the interface as part of the OS. Keep image captions short and clear:
“Interface design previews” is enough beside design imagery. Preserve detailed
provenance in [pics/README.md](../../pics/README.md), not in the main pitch.
Do not present mockups as photographs or claim an untested hardware result.

Describe active development once, positively and accurately. Safety guidance
and dated technical evidence remain accessible in the README and reports.
Missing features, validity enums, board revisions and acceptance procedures
are not the organising structure of the public page.

## README and developer entry

Lead with the OS and why its layers exist. Describe applications, core services,
hardware providers and the UI in concrete terms. Keep the native simulator
instructions visible; design-review tooling can live in a secondary section.

Current implementation and bench evidence must remain traceable, with
limitations attached to the specific result. A result from one board is not
universal platform support.

## Sharing the project

A useful short repository description would be:

> An open operating system for wearable devices and personal mesh nodes.
> Shared services, independent operation and applications beyond one device.

This is proposed copy, not a claim that repository settings were changed.
Do not alter topics or social settings as a side effect of a text edit.

The existing [social card](../assets/og-card.jpg) can provide a consistent
identity. Future photos or video should show real devices and observable
interaction: the watch on a wrist, moving between apps, or the same application
on different hardware. One navigation result illustrates one application,
not the entire product thesis.

Match public posts to their audience. General readers get the project and
its experience; contributors get a concrete technical contribution path.
Do not publish community posts or infer permission to send messages from
permission to improve the site.
