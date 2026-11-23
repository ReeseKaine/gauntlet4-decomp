import struct, sys
rom = open(sys.argv[1], 'rb').read()
entry = struct.unpack('>I', rom[4:8])[0]
OFF = 0x1000
# ELF header (32-bit, big-endian, EM_68K=4, ET_EXEC), 2 program headers
eh = b'\x7fELF' + bytes([1, 2, 1, 0]) + b'\0'*8
eh += struct.pack('>HHIIIIIHHHHHH', 2, 4, 1, entry, 52, 0, 0, 52, 32, 2, 0, 0, 0)
ph  = struct.pack('>8I', 1, OFF, 0, 0, len(rom), len(rom), 5, 0x1000)       # ROM: R-X at 0x000000
ph += struct.pack('>8I', 1, 0, 0xFF0000, 0xFF0000, 0, 0x10000, 6, 0x1000)  # Work RAM: RW- at 0xFF0000
out = (eh + ph).ljust(OFF, b'\0') + rom
open(sys.argv[2], 'wb').write(out)
print(hex(entry), len(out))
