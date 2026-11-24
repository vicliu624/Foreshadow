# ADS-B offline map data notice

English · [简体中文](map-license.zh-CN.md)

Made with Natural Earth. The bundled land contours derive from Natural Earth's
1:50m land data; country boundaries, first-level administrative regions and
populated places derive from its 1:10m cultural data. Natural Earth identifies its raster and vector map data as public
domain and permits modification and redistribution, including commercial use,
without permission or mandatory attribution. See the
[upstream terms](https://www.naturalearthdata.com/about/terms-of-use/).

[map-source.json](map-source.json) records the original data URL, original SHA-256,
conversion and delivered data hashes. The cultural layers use a pinned upstream
Git revision, preserve polygon/hole grouping and apply 0.01-degree RDP
generalization before rounding coordinates to five decimals. Seven unnamed
admin-1 features are omitted; country context remains available where covered.
`tools/maps/build_adsb_geography.py` reproduces the resource from checksum-pinned
inputs. Geographic context uses generalized region containment; nearby city
distance is a separate reference, not a municipal/district jurisdiction claim.
These layers are not an authoritative navigation chart or a source of aircraft positions.

The imported map data retains that public-domain status. The project's application
and conversion code retain their applicable GPL-3.0-or-later terms. This notice
does not relicense third-party names or artwork, or require attribution beyond the
upstream terms. The complete production source archive contains the delivered
`map.json`, `geography.json`, the converter and their provenance.
