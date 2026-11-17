# Hardware checklist

Run on the console before trusting a build. Tick each box only when the expected result happened.

1. [ ] Back up `sdmc:/config/nx-discord-rpc/` to the PC. Expected: a copy of `config.ini` (if any) exists off the console.
2. [ ] Delete `api_key` and `username` from `config.ini`, and `api_key.txt`. Copy `out/` to the SD card and reboot. Expected: the console boots normally.
3. [ ] Open the app from the Album or a title takeover. Expected: tabs Status, Settings, Account, About; the Status footer says "Not linked. Open Account".
4. [ ] Check the special characters in the app (Switch shared font): the ellipsis in "Requesting a code…" (Account, while linking) and in "Unlinking…", the middle dot in "Linked as <name> · relay OK" (Status, once linked). Expected: no boxes or question marks.
5. [ ] Account tab: start a link, scan the QR code. Expected: the relay page opens, the code matches the one on the console.
6. [ ] Enter the code on the phone. Expected: the app shows the linked Discord username within a few seconds and Discord shows the presence.
7. [ ] Wait at least a minute after the link, then read the `Heap:` lines in `sdmc:/config/nx-discord-rpc/log.txt` (one per minute). Expected: "KB in use" stays well under the 1024 KB heap, with no steady growth from one line to the next.
8. [ ] Start a second link or press the link button repeatedly. Expected: no second request while one is pending; if the relay rate limit (10 per hour) is hit, a rate limit message with a wait time appears.
9. [ ] Start a link, then put the console to sleep and wake it. Expected: the link screen says "Link interrupted", not stuck on the code.
10. [ ] Unlink from the Account tab, then link again. Expected: Discord presence disappears after unlink; the second link works and the username returns.
11. [ ] Turn Wi-Fi off and unlink. Expected: within a few seconds, a dialog offering "Forget locally"; choosing it clears the key and username on the console.
12. [ ] With Wi-Fi still off, start a link. Expected: within a few seconds, "Link failed" with "Could not reach the relay. Check the console's connection." and a Retry button. Then turn Wi-Fi on, press Retry and finish the link.
13. [ ] In a game, open the app. Expected: the Status card shows the real game icon and name (matching the NACP icon).
14. [ ] At the HOME Menu, open the app. Expected: the Status card shows the neutral glyph, not a game icon.
15. [ ] Settings tab: toggle each setting (Enabled, Private mode, Show firmware version, Show HOME Menu, Debug log). Expected: the Status tab reflects it within about 1 s; Discord within about 5 s.
16. [ ] Open the overlay in game (Tesla Menu or Ultrahand). Expected: it opens and shows the toggles Enabled, Private mode, Show firmware and Show HOME Menu (no Debug log). The presence line shows the middle dot between the game and its details, and ends with an ellipsis when it is too long for the overlay; both render correctly (Tesla font).
17. [ ] Change a toggle in the app, then look at the overlay, and the reverse. Expected: the other side reflects the change within about 1 s.
18. [ ] Change settings from the overlay, wait 3 s, reboot. Expected: `config.ini` holds the changes and they are still set after reboot.
19. [ ] Remove `atmosphere/contents/4200000000D15C0D/flags/boot2.flag`, reboot, open the app, then the overlay. Expected: the app shows at once a blocking screen saying "NX Discord RPC is not running.", whose only action is Start now; the overlay says it too, without hanging or crashing.
20. [ ] Restore `boot2.flag`, reboot. Expected: the app and overlay work again.
21. [ ] Presence still behaves as before: a game shows its title with elapsed time; HOME shows nothing unless "Show HOME Menu" is on; private mode shows "Playing a game"; no upload or request happens in private mode or at HOME.
22. [ ] Sleep on the dock for several minutes, with background wakes. Expected: nothing is sent while asleep; presence resumes after a real wake.
23. [ ] Restart the relay while playing. Expected: the sysmodule recovers on its own and the presence returns.
24. [ ] Check `log.txt`. Expected: no key or admin key appears in it.
25. [ ] Link a Discord account that has a custom avatar. Expected: the Account tab shows the picture within a few seconds of the "Linked as" line.
26. [ ] On a console that was already linked before this version, reboot and open the Account tab. Expected: the picture appears once the sysmodule has read the profile at boot.
27. [ ] Break the link (revoke NX RPC in Discord, User Settings > Authorized Apps) or turn Wi-Fi off, then reopen the Account tab. Expected: the picture stays.
28. [ ] Unlink from the Account tab. Expected: the picture disappears, and `avatar.png` and `avatar.key` are gone from `sdmc:/config/nx-discord-rpc/`.
29. [ ] Link an account without a custom avatar. Expected: the Account tab shows Discord's default avatar.
30. [ ] Change the avatar on Discord. Expected: the new picture shows within about a week (the next token refresh), or right away after unlinking and linking again.
31. [ ] Settings tab: turn Start at boot off, then reboot and open the app. Expected: `boot2.flag` is gone from `atmosphere/contents/4200000000D15C0D/flags/` and the app shows the not-running screen with a Start now button.
32. [ ] Press Start now. Expected: within a few seconds the app opens normally on the Status tab; Start at boot is still off in Settings.
33. [ ] Turn Start at boot on, then reboot. Expected: `boot2.flag` is back, the sysmodule runs and the app opens normally.
34. [ ] With Show paused games on, play a game, press HOME, and wait for Discord. Expected: the details read "Paused · HOME Menu". Repeat from the eShop and the Album (details "Paused · Nintendo eShop", "Paused · Album"), and the User page ("Paused · User page"). Note whether Discord hides or keeps its timer while paused (the console sends no elapsed time then): ______.
35. [ ] Return to the game. Expected: the details return to normal and the elapsed time resumes. Open the on-screen keyboard in a game. Expected: it still shows as playing.
36. [ ] Turn Show paused games off, then press HOME in a game. Expected: the game keeps showing as playing.
37. [ ] Turn Show docked or handheld on, then dock and undock the console. Expected: the details end with "Docked" or "Handheld" within a few seconds.
38. [ ] Turn Show total playtime on. Expected: the details end with a total (for example "45 min total") that matches the playtime on the console's profile page for the current profile.
39. [ ] Start a game through a forwarder. Expected: its icon shows on Discord at once.
40. [ ] Play the same game on a second console. Expected: the image is reused, and the relay log shows no second upload.
41. [ ] Run a v0.3 console against the new relay. Expected: it still shows icons.
