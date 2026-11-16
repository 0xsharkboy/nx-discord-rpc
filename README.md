# nx-discord-rpc

An Atmosphère sysmodule for the Nintendo Switch that reports the running title to the
nx-discord-rpc relay, which shows it as Discord Rich Presence. The console holds only an API key
issued by the relay.

Work in progress. There is no release yet and the linking flow has no tool on the console side.

## Requirements

- A Switch running Atmosphère.
- A relay that implements [docs/protocol.md](docs/protocol.md), and an API key for it (obtained by
  linking a Discord account through the relay's link flow).

## Build

Everything comes from the Nix flake.

    nix develop -c make test
    nix develop -c make sysmodule

The sysmodule lands in `out/atmosphere/contents/4200000000D15C0D/`; copy the contents of `out/` to
the root of the SD card.

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
| `username` | empty | The Discord username shown by the UIs; informational only, kept up to 32 bytes

Settings are read at boot. To set the API key, write it to
`sdmc:/config/nx-discord-rpc/api_key.txt`: it is imported at boot, or within 30 seconds while the
sysmodule has no working key, saved to `config.ini`, then the file is deleted.
After a rejected key (401) or a broken Discord link (409), a new `api_key.txt` is needed.

Title icons are uploaded to the relay automatically, once per title and session. None are uploaded in
private mode or on the HOME Menu.

The log is `sdmc:/config/nx-discord-rpc/log.txt`. Keys are masked in it.

## License

SPDX-License-Identifier: GPL-3.0-or-later

See [LICENSE](LICENSE).
