# nx-discord-rpc

An Atmosphère sysmodule for the Nintendo Switch that reports the running title to the
nx-discord-rpc relay, which shows it as Discord Rich Presence. The console holds only an API key
issued by the relay.

Work in progress. There is no release yet; nothing here counts as tested until
[docs/hardware-checklist.md](docs/hardware-checklist.md) has been run on a console.

The relay receives your Discord link, the current game name, elapsed time and icon. It keeps no play
history and never sees your Discord password.

## Parts

- The sysmodule reports the running title to the relay.
- The app (homebrew `.nro`) shows the status, edits the settings and links your Discord account.
- The overlay (Tesla) toggles the settings in game.

## Requirements

- A Switch running Atmosphère.
- A relay that implements [docs/protocol.md](docs/protocol.md).
- For the overlay: nx-ovlloader and Tesla Menu or Ultrahand.

## Build and install

Everything comes from the Nix flake.

    nix develop -c make test
    nix develop -c make dist

Copy the contents of `out/` to the root of the SD card:

- `atmosphere/contents/4200000000D15C0D/` (`exefs.nsp`, `toolbox.json`, `flags/boot2.flag`): the sysmodule
- `switch/nx-discord-rpc/nx-discord-rpc.nro`: the app
- `switch/.overlays/nx-discord-rpc.ovl`: the overlay

Reboot so the sysmodule starts. `make sysmodule`, `make app` and `make overlay` build one part each.
To preview the app on a PC: `nix develop .#desktop -c make app-desktop`, then
`build/app-desktop/nx-discord-rpc --fake <scenario>`.

## Linking

Open the app, go to the Account tab and start the link. Scan the QR code, or open the link and enter
the code on your phone. The key is kept in `config.ini` and the Discord username is shown. Unlinking
asks the relay to drop the link; if the relay cannot be reached, "Forget locally" only clears the key
on the console.

As a fallback, write an API key to `api_key.txt` (see below).

## Settings

The app (Settings tab) and the overlay edit the same settings; Debug log is only in the app. Changes
apply within about a second and are written to `config.ini` after about 2 seconds without further
changes. The relay URL is not editable from either: change it in `config.ini`.

## Configuration

`sdmc:/config/nx-discord-rpc/config.ini`, one `key = value` per line:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Report the presence at all |
| `private_mode` | `false` | Show "Playing a game" instead of the title |
| `show_elapsed` | `true` | Show the time spent on the current title |
| `show_firmware` | `false` | Append the firmware version |
| `show_home_menu` | `false` | Report the HOME Menu too |
| `debug_log` | `false` | Write debug lines to the log |
| `relay_url` | `https://nx-discord-rpc.0xsharkboy.dev` | The relay's base URL (https only). Empty uses the default; an invalid URL disables sending (logged), the API key never goes to another host |
| `api_key` | empty | The API key issued by the relay |
| `admin_key` | empty | Optional relay admin key, used only for icon uploads |
| `username` | empty | The Discord username shown by the UIs; informational only, kept up to 32 bytes |

The sysmodule reads the file at boot. To set the API key, write it to
`sdmc:/config/nx-discord-rpc/api_key.txt`: it is imported at boot, or within 30 seconds while the
sysmodule has no working key, saved to `config.ini`, then the file is deleted.
After a rejected key (401) or a broken Discord link (409), a new `api_key.txt` is needed.

Title icons are uploaded to the relay automatically, once per title and session. None are uploaded in
private mode or on the HOME Menu.

The log is `sdmc:/config/nx-discord-rpc/log.txt`. Keys are masked in it.

## License

SPDX-License-Identifier: GPL-3.0-or-later

See [LICENSE](LICENSE).
