# Backslash archive-path rejection fixture

English · [简体中文](distribution-backslash.zh-CN.md)

`distribution-backslash.tar` is a pre-generated malicious-path test archive. Its sole USTAR entry is named `data\escape`, containing the five bytes `owned`. It contains no executable code. Timestamp fields are zero so the archive is reproducible.

The archive is checked in because creating a PAX archive through libarchive on Windows can normalize backslashes before the application sees the entry. Regenerating it through that path would remove the condition the test must exercise. Keep the original archive bytes.

The installer must reject this archive at entry-path validation. A later failure because a declared component is missing does not establish protection against backslash path traversal.
