## Bootstrap(header)

magic header: "PXR-USDC" : 8 bytes
version number: uint8 x 8(0 = major, 1 = minor, 2 = patch, rest unused) :8 bytes
int64_t tocOffset
int64_t _reserved[8]

=> total 88 bytes

## TOC(table of contents)

List of Sections.
