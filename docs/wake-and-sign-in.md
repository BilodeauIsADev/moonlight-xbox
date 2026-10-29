# Wake your PC and sign in from Xbox

Lunalight can wake a paired PC, wait for its streaming service, and open a stream
with a controller keyboard for the Windows sign-in screen. Your Windows PIN or
password stays enabled. The app sends key presses to Windows and does not save
credentials or decide whether Windows accepted them.

## Prepare Vibeshine

1. Install Vibeshine as a Windows service and ensure that it starts automatically.
   It must be available while Windows is locked and before you sign in.
2. In Vibeshine's application list, keep an application named **Desktop** with its
   command left empty. Case and surrounding spaces do not matter.
3. While the PC is awake, pair it with Lunalight and connect once. This records
   the host address and MAC address needed to wake it later.
4. Before testing wake, lock Windows with **Win + L**. On Xbox, focus your PC,
   open its options with **Y**, and choose **Sign in to PC**. Verify that you can
   see and interact with the Windows sign-in screen.

Vibeshine documents automatic capture switching for the Windows login screen
and UAC prompts in its [project README](https://github.com/Nonary/vibeshine).
If the stream is black, check its service and capture/display configuration on
an already-awake PC first. A virtual display setup must capture the screen where
Windows shows sign-in.

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
   then opens the stream and keyboard. Slow network probes can delay the final
   timeout message. Leaving Hosts cancels the automatic opening of the stream.
4. Use **Enter** to dismiss the Windows lock screen, then enter your PIN or
   password. Use **Tab** to change fields or the **right stick** to move the
   pointer and **LT** to click. The D-pad moves through the keyboard and **A**
   presses a key. **X** deletes a character and **Menu** sends Enter.
5. When you see your unlocked desktop, choose **Open library** and pick a game.
   Windows validates your credentials; the button does not automatically detect
   or complete sign-in.

**B** hides the keyboard. Reopen it with **View + Menu → Keyboard**. The sign-in
stream always uses Lunalight's controller keyboard, even if the normal keyboard
setting is off. A physical keyboard also works.

If the PC already has a running host session, Lunalight resumes it for sign-in
without closing the game. **Open library** disconnects the stream and leaves
that session running. If no session is running and Desktop is missing, the app
explains how to add it rather than starting an arbitrary game. A configured
autostart game is skipped for the sign-in connection.

Keep your Windows sign-in settings enabled. This flow handles the Windows lock
screen; it cannot enter a pre-boot disk-encryption PIN or a BIOS password.
