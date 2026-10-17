# Sleek theme and widget packs

Four optional native theme/widget presets are included: Graphite (charcoal/blue), Linen (warm light), Aurora (indigo/lavender), and Ember (aubergine/peach). Each directory contains a theme.aethertheme and widgets.aetherplugin manifest.

Import themes and widget manifests through the corresponding native Aether tools. Importing does not automatically apply a pack. The widgets use existing native implementations for clock, focused application and actions that open Activities, Quick Settings or Desktop Studio. These are data-only presets, not executable plugins.

The current Studio icon/preset browser is separate from these legacy theme/widget import files. See the main README and docs/TESTING.md for current capabilities and validation limitations.

Optional asset checks can be configured from packs/Sleek/validation into a separate build directory with CMake. They validate data and do not change native taskbars or wallpapers.
