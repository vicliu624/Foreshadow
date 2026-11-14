# Low-edge fine-synchronization recording

English · [简体中文](说明.md)

Actual Pager control near 478.005003 MHz, BW500/SF7, nominal −9 dBm. The receiver center was 478.5 MHz at 1.75 MS/s. `capture.cu8` and its JSON provenance preserve an original three-second recording's interval [229376,688128): 458752 complex samples, 917504 bytes, approximately 0.262 seconds. Crop coordinates, hashes and original probe phase are preserved in `provenance.json`.

The old v2 automatic 11 dB path missed this segment. Half/whole-symbol consistency followed by fractional carrier and chip-timing refinement (approximately two samples) allowed the full structure to pass. A fit using known settings is diagnostic evidence and must not replace blind regression. This does not verify payload, CRC or device identity.
