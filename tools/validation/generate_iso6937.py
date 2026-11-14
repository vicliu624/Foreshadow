"""Regenerate fixed DVB ISO6937 character facts with glibc iconv (offline)."""
import ctypes
from pathlib import Path


def generate():
    library = ctypes.CDLL(None, use_errno=True)
    library.iconv_open.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    library.iconv_open.restype = ctypes.c_void_p
    library.iconv.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t),
                             ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t)]
    library.iconv.restype = ctypes.c_size_t
    library.iconv_close.argtypes = [ctypes.c_void_p]

    def decode(data):
        codec = library.iconv_open(b"UTF-32LE", b"ISO_6937")
        if codec == ctypes.c_void_p(-1).value:
            raise RuntimeError("The reference system must provide glibc ISO_6937")
        source = ctypes.create_string_buffer(data); output = ctypes.create_string_buffer(32)
        before = ctypes.c_char_p(ctypes.addressof(source)); after = ctypes.c_char_p(ctypes.addressof(output))
        remaining = ctypes.c_size_t(len(data)); available = ctypes.c_size_t(32)
        result = library.iconv(codec, ctypes.byref(before), ctypes.byref(remaining), ctypes.byref(after), ctypes.byref(available))
        library.iconv_close(codec)
        if result == ctypes.c_size_t(-1).value or remaining.value or available.value != 28:
            return None
        return int.from_bytes(output.raw[:4], "little")

    single = [decode(bytes([value])) for value in range(256)]
    single[0xa4] = 0x20ac  # DVB's EURO assignment overrides the ISO6937 reserved position.
    pairs = [(first << 8 | second, value) for first in range(0xc1, 0xd0) for second in range(256)
             if (value := decode(bytes([first, second]))) is not None]
    assert all(value <= 0xffff for value in single if value is not None) and all(value <= 0xffff for _, value in pairs)
    lines = ["// Fixed character mapping, not localized UI text. Generated offline with glibc iconv.",
             "// Reproduce with tools/validation/generate_iso6937.py; DVB 0xA4 maps to EURO.",
             "#pragma once", "#include <array>", "#include <cstdint>", "namespace signalscope::transport::iso6937 {",
             "inline constexpr char32_t invalid=0x110000;", "inline constexpr std::array<char32_t,256> single{"]
    for offset in range(0, 256, 8):
        lines.append("    " + ",".join("invalid" if value is None else f"0x{value:04x}" for value in single[offset:offset+8]) + ",")
    lines += ["};", "struct Combined {std::uint16_t pair;char32_t unicode;};", f"inline constexpr std::array<Combined,{len(pairs)}> combined{{{{"]
    lines += [f"    {{0x{pair:04x},0x{value:04x}}}," for pair, value in pairs]
    lines += ["}};", "}"]
    target = Path(__file__).resolve().parents[2] / "src/backend/transport/iso6937.hpp"
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated 256 direct positions and {len(pairs)} combining pairs")


if __name__ == "__main__":
    generate()
