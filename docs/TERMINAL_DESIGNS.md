# Aether Terminal designs

Studio → Terminal contains ten stock designs plus Follow desktop. Each includes a coordinated palette and static native background. These are original Aether interpretations, not official ports or downloaded theme packages. Geometry is drawn with Direct2D; Woodland Zen uses the project's existing generated artwork. No background animation timer, network feed or mouse parallax is added.

| Aether design | Visual direction | Research reference |
| --- | --- | --- |
| Aubergine | Warm plum, orange and quiet concentric forms | [GNOME Terminal profiles and colors](https://teams.pages.gitlab.gnome.org/Websites/help.gnome.org/gnome-terminal/app-colors.html) |
| Midnight Violet | Deep charcoal with lavender accents | [Dracula specification](https://draculatheme.com/spec) |
| Arctic | Cool slate with icy cyan ribbons | [Nord](https://www.nordtheme.com/docs/) |
| Pastel Mocha | Soft purple, warm text and spacious contrast | [Catppuccin](https://github.com/catppuccin/catppuccin) |
| Tokyo Dusk | Deep blue and restrained city-night ribbons | [Tokyo Night](https://github.com/folke/tokyonight.nvim) |
| Warm Earth | Muted brown, sand and contour lines | [Gruvbox](https://github.com/morhetz/gruvbox) |
| Solar Paper | Cream paper with dark teal text and contours | [Solarized](https://ethanschoonover.com/solarized/) |
| Carbon | Neutral dark surface and diagonal engineering lines | [One Dark](https://github.com/joshdick/onedark.vim) |
| Woodland Zen | Green pond artwork, quiet tint and cream text | Existing Aether Woodland Zen artwork |
| Neon Horizon | Purple sky, a low sun and perspective grid | [SynthWave](https://github.com/robb0wen/synthwave-vscode) |

The broader interaction research uses [GNOME profiles](https://teams.pages.gitlab.gnome.org/Websites/help.gnome.org/gnome-terminal/pref.html), [kitty background configuration](https://sw.kovidgoyal.net/kitty/conf/), [Ghostty themes](https://ghostty.org/docs/features/theme), and [iTerm2 profile colors](https://iterm2.com/documentation-preferences-profiles-colors.html). Aether keeps profile defaults, palette choice, typography, cursor and background controls together.

## Customize

Choose PowerShell, Command Prompt or an installed WSL distribution as the default shell. Select a stock palette, browse installed monospace fonts, set size, choose outline/underline/bar cursor, and select a local PNG or JPEG. Background tint adjusts readability; Use stock background clears the image override. Artwork can be disabled independently. Design changes retain running tabs and shell processes. The Appearance button in Terminal opens this Studio section directly.

Pin Aether Terminal adds a built-in launch target to the dock. It inherits the selected icon pack's terminal asset. Existing app pins remain available.

## Shell behavior and limits

Commands are executed by real ConPTY-backed shells, not an emulated command interpreter. Pipes, redirection, scripts and foreground cancellation belong to those shells. Aether supports ANSI/truecolor text, cursor addressing, alternate screen, scroll regions, keyboard navigation, application cursor mode, Alt text, bracketed paste, bounded scrollback and up to four tabs. Grid limits are 512 columns by 200 rows. Clipboard shortcuts use Ctrl+Shift+C/V; Ctrl+C reaches the shell.

This remains an initial terminal emulator. Complex grapheme/double-width rendering, IME, terminal mouse reporting, complete xterm protocol compatibility and full terminal screen-reader integration are not certified. WSL is used only if already installed; Aether does not install a distribution. Background images are static and local. Shell privilege is inherited from the process that starts Aether.
