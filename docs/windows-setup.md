# Connecting the LCD to Windows

## How we made it communicate

The original LCDInfo adapter exposes an FTDI FT245BM USB FIFO (USB VID:PID **0403:E300**, product **USB Display**). The legacy software used the FTDI D2XX driver. Our working Windows 10 configuration instead binds the Microsoft generic **WinUSB** driver to this specific device. We communicate directly via the Windows WinUSB API, without the original D2XX DLL or a custom kernel driver.

This is a Windows driver association change, not a firmware update. It does not change the USB VID/PID or write the display EEPROM. The original test PC used a locally installed OEM INF and a dedicated device interface GUID. The exact installer used in the historical setup was not recorded in our notes.

## Recommended setup using Zadig

[Zadig](https://zadig.akeo.ie/) is a third-party utility that can install a WinUSB binding for an existing USB device. The following is a recommended reproduction method, not a claim that this exact utility was used in our original tests.

1. Connect the LCD and close any legacy LCDInfo software.
2. In Device Manager, identify the display and check Properties > Details > Hardware Ids. Confirm **USB\\VID_0403&PID_E300**. Do not rely on the name alone.
3. Obtain Zadig from its official website. Use a version compatible with your Windows release. Run it with administrator privileges.
4. If necessary, select Options > List All Devices. Select **USB Display** and confirm USB ID **0403 E300** in Zadig. Do not select a different FTDI device, keyboard, mouse or USB hub.
5. Select **WinUSB** as the target driver and choose Install Driver or Replace Driver, as appropriate. Do not choose libusbK or libusb-win32 for this application.
6. Confirm in Device Manager that the driver is WinUSB (winusb.sys). Reconnect the LCD if Windows requests it.
7. Launch the monitor. Normal operation does not need administrative privileges, provided the installed device permissions allow access.

On Windows 7, the driver package and its signature must be compatible with the installed Windows updates; use an appropriate signed package and a compatible installer. Do not disable driver signature enforcement. Windows 7/8/8.1 installation has not yet been validated by this project.

## What the application checks

The application looks for the original VID/PID and obtains the registered device interface path, opens it via WinUSB, verifies the bulk endpoints (OUT 02, IN 81, 64-byte packets), and expects the model and geometry response before drawing. See [protocol.md](protocol.md). If the driver is still bound to D2XX, the WinUSB application cannot open it. Do not run both software stacks against the device simultaneously.

## Reverting to the original driver

Close the monitor. In Device Manager, use Roll Back Driver if available, or Update Driver to reinstall the correct original FTDI driver package for this device. Keep a copy of your original driver package before replacing the binding. Never install an unrelated driver or modify the FTDI EEPROM to make the device match.

References: [Microsoft WinUSB installation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-installation), [Zadig](https://zadig.akeo.ie/).
