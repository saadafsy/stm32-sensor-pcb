# Hardware

Custom 4-layer PCB designed in KiCad 9 (stackup: F.Cu / inner GND / inner +3V3 / B.Cu).

- `sensor-telemetry-board.kicad_pro` - project
- `sensor-telemetry-board.kicad_sch` - schematic
- `sensor-telemetry-board.kicad_pcb` - 4-layer layout

## Key parts

| Ref | Part | Role |
|-----|------|------|
| U1 | STM32F411CEU6 | MCU (Cortex-M4F, 100 MHz) |
| U? | BME280 | temperature / humidity / pressure over I2C |
| U? | CP2102N | USB-C to UART bridge |
| U? | AMS1117-3.3 | 3.3 V LDO |
| U? | USBLC6-2 | USB data-line ESD protection |
| Y1 | 8 MHz crystal | HSE reference for the 100 MHz PLL |

## Status

Design complete and DFM-validated (POFV across all vias); Gerbers, BOM, and CPL
can be regenerated from this project for a JLCPCB PCBA order. The board is in shipping 
and will be brought up soon (see the top-level README for status).

To export fabrication files: open the project in KiCad and run the Fabrication
Toolkit plugin, or export Gerbers / drill / BOM / CPL manually from the PCB and
schematic editors.
