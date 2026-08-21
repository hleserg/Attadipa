# Independent by design

Attadipa treats a phone, cloud service, Internet connection, and Attadipa Node
as capability enhancements, not as a mandatory brain for the wearable.

Core functionality is functionality that is both essential to the product's
purpose and reasonably executable within the target device's resource and UX
constraints. It should remain local: timekeeping, local interaction, already
available navigation data, and communication with locally available hardware.

Remote providers are appropriate when they add a capability unavailable on the
device, improve quality, or exchange data beyond its local scope. Their state,
freshness, and quality must be visible to the capability model and UI. Losing a
provider must degrade only the dependent capability, honestly and gracefully;
it must not turn the wearable into a brick.

This is an engineering constraint, not a ban on connected features. Prefer
offline-first behaviour where it is reasonable, and make every external
dependency explicit, optional where feasible, and recoverable when absent.
