# ADS-B 离线地图数据声明

[English](map-license.md) · 简体中文

Made with Natural Earth。内置陆地轮廓来自 Natural Earth 的 1:50m 陆地数据；国家边界、一级行政区和居民地来自其 1:10m 人文地理数据。
Natural Earth 将其栅格及矢量地图数据标明为公有领域，允许修改和再分发，包括商业使用，
无需事先许可或强制署名。见[上游条款](https://www.naturalearthdata.com/about/terms-of-use/)。

[map-source.json](map-source.json)记录原始数据地址、原始 SHA-256、转换过程及交付数据哈希。
人文图层固定上游 Git 版本，保留多边形及孔洞分组，以 0.01 度 RDP 简化后将坐标舍入到小数点后五位。省略七个未命名的一级行政区要素；有覆盖时仍提供国家信息。`tools/maps/build_adsb_geography.py` 可从固定校验值的原始输入重建资源。地理位置采用概化行政区的包含关系，附近城市距离是单独的位置参考，不声称市／县区级行政归属。图层不是权威导航图，也不提供航空器位置数据。

导入的地图数据保留公有领域状态；项目应用及转换代码保留其适用的 GPL-3.0-or-later 条款。
本声明不重新授权第三方名称或美术作品，也不增加上游条款以外的署名要求。
完整生产源码归档包含交付的 `map.json`、`geography.json`、转换工具及其来源记录。
