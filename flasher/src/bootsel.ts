// Puts an IOmcu that runs rcbench firmware into BOOTSEL, and finds the RP2350
// boot ROM's PICOBOOT device.
//
// The IOmcu firmware links pico-sdk's USB stdio (firmware/iomcu/CMakeLists.txt,
// pico_enable_stdio_usb) with the SDK's default reset interface: a host that
// sets the line coding of its CDC (communications device class) port to
// 1200 baud makes the firmware call the boot ROM's reset into BOOTSEL
// (pico_usb_reset, PICO_USB_RESET_MAGIC_BAUD_RATE 1200). Opening the port at
// 1200 baud sends that line coding.

import { PICO_STDIO_PID, RP2350_BOOT_PID, RPI_VENDOR_ID } from './picoboot.ts';

export const MAGIC_BAUD = 1200;

/** Filter for the serial port chooser: any Raspberry Pi USB ID. */
export const IOMCU_PORT_FILTER: SerialPortFilter[] = [{ usbVendorId: RPI_VENDOR_ID }];
/** Filter for the USB device chooser: the RP2350 boot ROM. */
export const BOOTROM_FILTER: USBDeviceFilter[] = [{ vendorId: RPI_VENDOR_ID, productId: RP2350_BOOT_PID }];

const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

/**
 * Opens the port at 1200 baud and closes it. The device leaves the bus while
 * the port is open, so a failing close is the expected end.
 */
export async function touch1200(port: SerialPort): Promise<void> {
  const info = port.getInfo();
  if (info.usbVendorId !== undefined && info.usbVendorId !== RPI_VENDOR_ID) {
    throw new Error(`port with USB vendor ID 0x${info.usbVendorId.toString(16)} is no IOmcu`);
  }
  if (info.usbProductId === RP2350_BOOT_PID) return;
  await port.open({ baudRate: MAGIC_BAUD });
  await sleep(200);
  await port.close().catch(() => undefined);
}

/** True for the USB ID pico-sdk gives an application with USB stdio. */
export const isPicoStdio = (info: SerialPortInfo): boolean =>
  info.usbVendorId === RPI_VENDOR_ID && info.usbProductId === PICO_STDIO_PID;

/**
 * A boot ROM the page holds permission for, else the browser's chooser.
 * Returns null when the visitor closes the chooser.
 */
export async function pickBootrom(usb: USB): Promise<USBDevice | null> {
  const known = (await usb.getDevices()).find((d) => d.vendorId === RPI_VENDOR_ID && d.productId === RP2350_BOOT_PID);
  if (known) return known;
  try {
    return await usb.requestDevice({ filters: BOOTROM_FILTER });
  } catch (e) {
    if (e instanceof DOMException && e.name === 'NotFoundError') return null;
    throw e;
  }
}
