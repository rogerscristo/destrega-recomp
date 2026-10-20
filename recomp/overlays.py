"""Overlay inventory: which disc file loads at which address (from REA analysis
of FUN_8004c1e8 / FUN_8003e36c / FUN_8006b35c: files are memcpy'd verbatim to
*(0x800244d0)=0x80010000 (P1 slot) or *(0x800244d4)=0x8001a268 (P2 slot))."""
import os

P1, P2 = 0x80010000, 0x8001a268
CHARS = ['MGRAD', 'MSEREA', 'MANZEAL', 'MMILENA', 'MDOIL', 'MREUS', 'MTEEM', 'MKUGA',
         'MROSEN', 'MLAON', 'MFALMA', 'MZAUBER', 'MSOLD', 'MGENE']


def overlay_list():
    out = []
    for c in CHARS:
        out.append((f'OVL/{c}.BIN', P1))
    for c in CHARS:
        out.append((f'OVL/{c}2.BIN', P2))
    out.append(('OVL/EVENT_SP.BIN', P1))
    out.append(('OVL/EVENTOVL.BIN', P2))
    out.append(('OVL/OPTOVL.BIN', P1))
    return out
