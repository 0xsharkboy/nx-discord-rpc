# Hardware checklist

Run on the console before trusting a build. Tick each box only when the expected result happened.

1. [ ] Back up `sdmc:/config/nx-discord-rpc/` to the PC. Expected: a copy of `config.ini` (if any) exists off the console.
2. [ ] Delete `api_key` and `username` from `config.ini`, and `api_key.txt`. Copy `out/` to the SD card and reboot. Expected: the console boots normally.
3. [ ] Open the app from the Album or a title takeover. Expected: tabs Status, Settings, Account, About; Status says not linked.
4. [ ] The em dash, middle dot and ellipsis show correctly in the app (Switch shared font). Expected: no boxes or question marks.
5. [ ] Account tab: start a link, scan the QR code. Expected: the relay page opens, the code matches the one on the console.
6. [ ] Enter the code on the phone. Expected: the app shows the linked Discord username within a few seconds and Discord shows the presence.
7. [ ] Start a second link or press the link button repeatedly. Expected: no second request while one is pending; if the relay rate limit (10 per hour) is hit, a rate limit message with a wait time appears.
8. [ ] Start a link, then put the console to sleep and wake it. Expected: the link screen says "interrupted", not stuck on pending.
9. [ ] Unlink from the Account tab, then link again. Expected: Discord presence disappears after unlink; the second link works and the username returns.
10. [ ] Turn Wi-Fi off and unlink. Expected: an error offering "Forget locally"; choosing it clears the key and username on the console.
11. [ ] In a game, open the app. Expected: the Status card shows the real game icon and name (matching the NACP icon).
12. [ ] At the HOME Menu, open the app. Expected: the Status card shows the neutral glyph, not a game icon.
13. [ ] Settings tab: toggle each setting (enabled, private mode, elapsed time, firmware, HOME Menu, debug log). Expected: each applies within about 1 s and Discord reflects it.
14. [ ] Open the overlay in game (Tesla Menu or Ultrahand). Expected: it opens, shows the same toggles, the em dash, middle dot and ellipsis render correctly (Tesla font).
15. [ ] Change a toggle in the app, then look at the overlay, and the reverse. Expected: the other side reflects the change within about 1 s.
16. [ ] Change settings from the overlay, wait 3 s, reboot. Expected: `config.ini` holds the changes and they are still set after reboot.
17. [ ] Remove `atmosphere/contents/4200000000D15C0D/flags/boot2.flag`, reboot, open the app, then the overlay. Expected: the app shows a blocking screen saying the sysmodule is not running, offering no other action; the overlay shows it too and does not crash.
18. [ ] Restore `boot2.flag`, reboot. Expected: the app and overlay work again.
19. [ ] Presence still behaves as before: a game shows its title with elapsed time; HOME shows nothing unless "HOME Menu" is on; private mode shows "Playing a game"; no upload or request happens in private mode or at HOME.
20. [ ] Sleep on the dock for several minutes, with background wakes. Expected: nothing is sent while asleep; presence resumes after a real wake.
21. [ ] Restart the relay while playing. Expected: the sysmodule recovers on its own and the presence returns.
22. [ ] Check `log.txt`. Expected: no key or admin key appears in it.
