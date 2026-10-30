# Dynamic panel — parked design note

**Status: parked 2026-10-30.** Written down so it is not re-derived.

## The idea

Let the person commissioning a panel add pages, tabs and buttons on the panel
itself, and bind each button to `target device → command → action → params` —
the same envelope Hyperwisor already uses — sent over the HSC.

No firmware build to commission a site. An installer configures a cabinet or a
home standing in front of it.

## Why this shape, and not a cloud-authored config

An earlier proposal was to push Hyperwisor's dashboard config down and render it
on the panel. That was rejected, and on reflection the rejection is right: the
person commissioning is physically at the panel, not at a desk, and a panel that
only works when someone else has configured it in a browser is a worse product.

The binding model is still borrowed from Hyperwisor, because it is proven and it
is what the HSC already carries.

## The thing that decides whether it is usable

**Pick, don't type.** Nobody is entering a device UUID on a 7-inch panel with a
finger. The editor must offer:

- **target** — a list of devices this panel may actually reach
- **command** — a list the product declares, not free text
- **params** — typed fields, not raw JSON

Get this wrong and the idea fails for reasons that look like the idea being
wrong.

### Prerequisite: the panel cannot currently discover anything

The firmware has no `getConnectedDevices`. The HSC already answers that query
for the web and the apps, and the panel holds grants — it simply never asks.
This is small, and everything else depends on it.

## Two hard parts

**A bad config must not brick a wall panel.** On a web page a broken layout is a
blank div. On a panel screwed to a wall it is a device with no way back.
Validate at load, keep the last-good config, and provide a recovery gesture that
restores a minimal layout. **Build this before the editor, not after.**

**Who may edit.** This is already solved once in the vehicle product, in
`service_pin.h`: a customer wandered into Bus Setup and HMI Role, reprogrammed
the baud rate of every RS-485 slave, and recovering the bus needed a scanner and
a laptop. A tile that fires `relay_control` is an installer tool, not a customer
setting. Reuse that precedent.

## Build order

1. **Device discovery** — `getConnectedDevices` in the firmware.
2. **A static bound tile** — `examples/02_bound_tile`. Done; proves the envelope.
3. **Make that tile editable** — long-press behind the service PIN, pickers,
   persistence to the 2 MB filesystem as versioned JSON, last-good retained.
4. **Then pages and tabs.**

Tabs first is the tempting order and the wrong one: it builds navigation around
bindings that do not exist yet. A grid of editable tiles is most of the value
for a fraction of the work.

## Open question

Should the panel's config sync up to Hyperwisor as device metadata? It would
give backup and restore when a panel dies — a replacement would come up already
commissioned — without moving authoring to the cloud.
