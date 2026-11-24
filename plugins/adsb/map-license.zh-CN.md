# ADS-B 离线地图数据声明

[English](map-license.md) · 简体中文

Made with Natural Earth。内置陆地轮廓来自 Natural Earth 的 1:50m 陆地数据。
Natural Earth 将其栅格及矢量地图数据标明为公有领域，允许修改和再分发，包括商业使用，
无需事先许可或强制署名。见[上游条款](https://www.naturalearthdata.com/about/terms-of-use/)。

[map-source.json](map-source.json)记录原始数据地址、原始 SHA-256、转换过程及交付数据哈希。
转换将多边形坐标舍入到小数点后五位。轮廓用于粗略的离线地理概览，不是权威导航图，
也不提供航空器位置数据。

导入的地图数据保留公有领域状态；项目应用及转换代码保留其适用的 GPL-3.0-or-later 条款。
本声明不重新授权第三方名称或美术作品，也不增加上游条款以外的署名要求。
完整生产源码归档包含交付的 `map.json` 及其来源记录。
