# Wake your PC and sign in from Xbox

Lunalight can wake a paired PC, wait for Vibeshine, and show a Windows PIN prompt
directly on Xbox. It sends the PIN through Vibeshine's **Remote Input** connection
without opening the desktop stream. Your Windows sign-in stays enabled.

The prompt accepts numeric PINs of 4 to 127 digits, including leading zeroes.
Lunalight does not save or log the PIN, clears the field on submission or cancel,
and overwrites its temporary delivery buffer when the connection ends. It sends
one submission and never automatically retries after an uncertain result.

## Prepare Vibeshine

1. Install Vibeshine as a Windows service and ensure that it starts automatically.
   It must be available while Windows is locked and before you sign in.
2. Enable **Remote Input** in Vibeshine if needed and check that its built-in
   **Remote Input** entry appears in the Xbox library. A configured game renamed
   Remote Input is not a substitute. Update Vibeshine if this entry is unavailable.
   Keep an application named **Desktop**, with its command empty, for the optional
   **View PC sign-in** fallback.
3. While the PC is awake, pair it with Lunalight and connect once. This records
   the host address and MAC address needed to wake it later.
4. Before testing wake, lock Windows with **Win + L**. On Xbox, focus your PC,
   open its options with **Y**, and choose **Sign in to PC**. Enter the PIN in the
   Xbox prompt and choose **Send PIN**. Verify locally that Windows unlocks.
   Windows must have your intended account and its PIN sign-in option selected.

Vibeshine documents Remote Input and Windows login-screen support in its
[project README](https://github.com/Nonary/vibeshine). Its input-only connection
may send synthetic black frames for protocol compatibility. Lunalight discards
these without starting a video decoder or audio player. It does not request a
desktop, change stream display settings, or resume/quit a running game.

The Xbox prompt cannot see whether Windows is locked, which account is selected,
or whether the PIN was accepted. **PIN sent** reports delivery, not a verified
Windows sign-in. Use **View PC sign-in** if Windows needs a different account,
password, another sign-in option, or a secure-attention prompt.

## Enable Wake-on-LAN on ASUS ROG Strix B550-F Gaming

Use the motherboard's wired Ethernet connection to your router or switch. Keep
the PC connected to mains power with its PSU switch on. Test with the Xbox and
PC on the same home network, rather than a guest network.

### BIOS

1. Restart the PC and press **Delete** or **F2** to enter the ASUS UEFI.
2. Press **F7** for Advanced Mode.
3. Open **Advanced → APM Configuration**.
4. Set **Power On By PCI-E → Enabled**.
5. Set **ErP Ready → Disabled** so standby power is available for wake.
6. Press **F10**, confirm, and let Windows start.

The menu wording can vary with BIOS revisions. See ASUS's
[Wake-on-LAN BIOS instructions](https://www.asus.com/support/faq/1045950/) and
[ErP guidance](https://www.asus.com/global/support/faq/1048459/).

### Windows Ethernet adapter

1. Right-click **Start → Device Manager → Network adapters**.
2. Open the wired Ethernet adapter's properties. The onboard adapter on this
   board is normally listed as **Intel Ethernet Controller I225-V**.
3. Under **Advanced**, enable **Wake on Magic Packet**, and **Enable PME** if
   the driver exposes it. If offered, enable **Wake on Magic Packet from power
   off state** as well.
4. Under **Power Management**, check **Allow this device to wake the computer**
   and **Only allow a magic packet to wake the computer**. Leave **Allow the
   computer to turn off this device to save power** enabled if the driver
   requires it for wake settings.
5. If those options are missing or disabled, install the LAN driver from the
   [ASUS B550-F Gaming support page](https://www.asus.com/supportonly/rog%20strix%20b550-f%20gaming/helpdesk_download/)
   and restart before checking again.

ASUS includes the adapter wake and PME checks in its
[troubleshooting steps](https://www.asus.com/support/faq/1045950/).
Intel also documents [I225-V wake settings](https://www.intel.com/content/www/us/en/support/articles/000060275/ethernet-products/gigabit-ethernet-controllers-up-to-2-5gbe.html).

For shutdown wake, disable Windows Fast Startup through **Control Panel →
Power Options → Choose what the power buttons do → Change settings that are
currently unavailable**, then clear **Turn on fast startup** and save. This is
Windows Fast Startup, not the BIOS Fast Boot setting. Start by testing sleep;
wake from full shutdown depends on the adapter, firmware, and Windows setup.

## Wake and sign in

1. Put the PC to **Sleep** from Windows.
2. On the Xbox Hosts screen, focus the PC and press **X**, or choose **Wake and
   sign in** from its options.
3. Lunalight waits up to about a minute for the service to respond consistently,
   then opens the local PIN prompt. Slow network probes can delay the final
   timeout message. Leaving Hosts cancels the automatic opening of the prompt.
4. Enter your Windows PIN on Xbox and choose **Send PIN**. Lunalight connects to
   Remote Input, sends a space press to dismiss the lock-screen wallpaper, waits
   1.5 seconds, clears partial input with **Ctrl+A**, **Backspace**, then sends
   the PIN and **Enter** once. It disconnects after allowing queued input to drain.
5. After **PIN sent**, choose **Open library** and pick a game. If Windows is
   still locked, use **View PC sign-in** to inspect it before another attempt.

**Cancel** stops a pending operation and waits for its connection to close.
Partially delivered input is not submitted or retried automatically. Windows can
accept some PINs on the final digit, so cancellation cannot undo a completed sign-in.

### Optional streamed sign-in

Choose **View PC sign-in** for a password, an alphanumeric PIN, selecting an
account or sign-in option, or checking a delivery result. Use **Enter** to dismiss
the lock screen, **Tab** to change fields, or the **right stick** and **LT** to
point and click. The D-pad moves through the keyboard and **A** presses a key.
**X** deletes a character and **Menu** sends Enter. After unlocking Windows,
choose **Open library**.

**B** hides the keyboard. Reopen it with **View + Menu → Keyboard**. The sign-in
stream always uses Lunalight's controller keyboard, even if the normal keyboard
setting is off. A physical keyboard also works.

If the PC already has a running host session, this fallback resumes it for sign-in
without closing the game. **Open library** disconnects the stream and leaves
that session running. If no session is running and Desktop is missing, the app
explains how to add it rather than starting an arbitrary game. A configured
autostart game is skipped for the sign-in connection.

Keep your Windows sign-in settings enabled. This flow handles the Windows lock
screen; it cannot enter a pre-boot disk-encryption PIN or a BIOS password.

Lunalight derives the directed broadcast from a matching Xbox IPv4 interface's
reported prefix. For example, host `10.0.0.3` with a matching `/24` interface
uses `10.0.0.255`, rather than `10.255.255.255`. Wake packets go to ports 7 and 9
on both the local directed broadcast and `255.255.255.255`. Saved real MAC
addresses survive empty or zero MACs returned by HTTP serverinfo and discovery.
