#!/usr/bin/env python3
"""Read CRC-framed BME280 telemetry, validate it, print/plot, and log to CSV.

Frame (21 bytes, little-endian):
  0xAA | LEN(=16) | seq:u32 | temp:i32 (0.01 C) | press:u32 (Pa) | hum:u32 (0.01 %) |
  CRC16-CCITT (over LEN+payload, i.e. bytes 1..17) | 0x55
"""
import argparse, struct, csv, collections
import serial   # pip install pyserial

START, END, PAYLOAD_LEN, TOTAL = 0xAA, 0x55, 16, 21

def crc16_ccitt(data: bytes) -> int:
    """Must match crc.c exactly: poly 0x1021, init 0xFFFF, no final XOR."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc

def read_frames(ser):
    """Resync on START, validate markers + length + CRC, yield engineering units.
    Corrupt or misaligned data is silently skipped — that is the CRC doing its job."""
    while True:
        b = ser.read(1)
        if not b or b[0] != START:
            continue                       # scan until we land on a start marker
        rest = ser.read(TOTAL - 1)
        if len(rest) < TOTAL - 1:
            continue                       # timed out mid-frame; resync
        frame = b + rest
        if frame[1] != PAYLOAD_LEN or frame[-1] != END:
            continue                       # markers wrong -> misaligned, resync on next START
        rx_crc = frame[18] | (frame[19] << 8)
        if crc16_ccitt(frame[1:18]) != rx_crc:
            continue                       # CRC mismatch -> drop the frame
        seq, temp_c100, press_pa, hum_c100 = struct.unpack_from("<IiII", frame, 2)
        yield seq, temp_c100 / 100.0, press_pa / 100.0, hum_c100 / 100.0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="e.g. COM5 (Windows) or /dev/ttyUSB0")
    ap.add_argument("--plot", action="store_true", help="live matplotlib plot")
    ap.add_argument("--csv", default="telemetry.csv", help="CSV log path")
    a = ap.parse_args()

    ser = serial.Serial(a.port, 921600, timeout=1)   # 8-N-1 are pyserial defaults
    log = csv.writer(open(a.csv, "w", newline=""))
    log.writerow(["seq", "temp_C", "press_hPa", "hum_pct"])

    if a.plot:
        import matplotlib.pyplot as plt              # pip install matplotlib
        N = 300
        T, H, P = (collections.deque(maxlen=N) for _ in range(3))
        _, ax = plt.subplots(3, 1, sharex=True)
        for seq, t, p, h in read_frames(ser):
            log.writerow([seq, f"{t:.2f}", f"{p:.2f}", f"{h:.2f}"])
            T.append(t); H.append(h); P.append(p)
            for axis, data, label in zip(ax, (T, H, P),
                                         ("Temp (C)", "Humidity (%)", "Press (hPa)")):
                axis.clear(); axis.plot(data); axis.set_ylabel(label)
            plt.pause(0.05)
    else:
        for seq, t, p, h in read_frames(ser):
            log.writerow([seq, f"{t:.2f}", f"{p:.2f}", f"{h:.2f}"])
            print(f"seq {seq:>6}  {t:6.2f} C  {h:5.1f} %RH  {p:8.2f} hPa")

if __name__ == "__main__":
    main()
