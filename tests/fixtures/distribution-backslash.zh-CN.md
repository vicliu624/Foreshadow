# 反斜杠归档路径拒绝夹具

[English](distribution-backslash.md) · 简体中文

此处的 `distribution-backslash.tar` 是一个平台无关的 USTAR 恶意归档负例，仅含名称为 `data\escape`、内容为五字节 `owned` 的普通文件，不含可执行内容。

文件使用 Python 标准库 `tarfile` 的 `USTAR_FORMAT` 预先生成，文件时间为 0。保留原始反斜杠是测试的必要条件：libarchive 的 Windows PAX 写入器会把反斜杠转为斜杠，因此在 Windows 上动态生成这个负例会改变所测试的输入。该归档必须在路径校验阶段拒绝，不能因为缺少组件而在后续阶段拒绝。
