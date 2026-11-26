# Power-management physical validation

Use the experimental power executable, not the stable release. Exit any other LCD program. Check which sleep states Windows supports with powercfg /a. Test ordinary startup and a normal display update. If using an explicit backlight value, choose the value you want to restore; no startup brightness change occurs.

1. No arguments: suspend, verify the framebuffer is blank and brightness unchanged, wake, verify full display refresh and fresh metrics. Repeat with hibernation if supported.
2. With --backlight 180 (or your actual preferred value): suspend, verify light off; wake, verify light returns and display updates. Repeat with hibernation. If USB power is removed by the PC, the display cannot be inspected while asleep.
3. Repeat multiple times, including resuspend quickly after wake; exit during the two-second wake delay.
4. If practical, disconnect while asleep, wake, reconnect; verify recovery and backlight restoration. A disconnected or stalled device may prevent blanking, but Windows must still suspend.
5. Verify no persistent setting changed by physically power cycling after closing the app. Test invalid argument (256) separately: error and no USB access.

The automated tests cover the pure transition/generation model, exact command bytes and numeric validation; they cannot validate Windows notification delivery or hardware timing. The stable monitor is unchanged.
