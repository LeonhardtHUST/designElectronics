# 致谢与来源 / Credits

## 项目与分支

本仓库 [LeonhardtHUST/designElectronics](https://github.com/LeonhardtHUST/designElectronics) 是 [rORrEtboLt/designElectronics](https://github.com/rORrEtboLt/designElectronics) 的 fork。感谢原项目作者及贡献者提供电子设计应用的基础实现。本分支进行课程适配、中文说明与界面等修改；这些修改不表示本分支维护者创作了整个原项目，也不表示上游认可或担保本分支。

原项目 MIT 版权声明保持不变：**Copyright (c) 2026 Nesara Hippla**。完整授权见 [LICENSE](LICENSE)，离线合并文本见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本分支不以新的署名替换原始版权。

## 随源码使用的第三方组件

下表依据仓库实际许可文件及源码头部整理，不是完整系统 SBOM；各平台、工具链与发布包的传递依赖还需按实际构建核对。

| 组件与用途 | 作者 / 来源 | 实际许可及原文位置 |
| --- | --- | --- |
| SDL3：窗口、输入与渲染 | Sam Lantinga 与 SDL 贡献者；https://github.com/libsdl-org/SDL | zlib；当前本地预编译 SDL 3.4.20 的 `external/prebuilt/SDL3-3.4.20/LICENSE.txt`，全文已收入 NOTICE，不要求构建时存在该本地目录 |
| SDL3_ttf：字体渲染 | Sam Lantinga 与 SDL_ttf 贡献者；https://github.com/libsdl-org/SDL_ttf | zlib；当前本地预编译 SDL_ttf 3.2.2 的 `external/prebuilt/SDL3_ttf-3.2.2/LICENSE.txt`，全文已收入 NOTICE |
| libwebp：WebP 图像解码 | Google Inc. 与贡献者；https://github.com/webmproject/libwebp | BSD-3-Clause；`third_party/libwebp/COPYING` |
| stb：图像读取、写入与缩放 | Sean Barrett 与贡献者；https://github.com/nothings/stb | 本项目选择 MIT；原文件同时提供 Public Domain / Unlicense 备选，完整双许可保留于 `third_party/stb/LICENSE` |
| NanoSVG：SVG 解析与栅格化 | Mikko Mononen；https://github.com/memononen/nanosvg | zlib；`third_party/nanosvg/LICENSE.txt`；`nanosvg.h` 另保留基于 Anti-Grain Geometry 2.4 SVG 示例的来源与 Copyright (C) 2002-2004 Maxim Shemanarev (McSeem)，以及 canvg 弧线计算与 Bézier 包围盒参考来源 |
| minih264：H.264 视频编码 | https://github.com/lieff/minih264 的作者与贡献者 | CC0 1.0 Universal；`third_party/minih264/LICENSE` 及 `minih264e.h` 文件头的公有领域奉献声明 |
| minimp4：MP4 封装 | https://github.com/lieff/minimp4；文件头亦注明 https://github.com/aspt/mp4 | CC0 1.0 Universal；`third_party/minimp4/LICENSE` 及 `minimp4.h` 文件头 |

视频组件的 CC0 文本不授予专利或商标权；软件许可说明不构成对各司法辖区视频专利问题的保证。

## 字体与衍生链

- **Sarasa Gothic SC / 更纱黑体**：Copyright (c) 2015-2025 Renzhi Li（Belleve Invis）。保留其衍生链署名：2016 The Inter Project Authors；2014-2021 Adobe Systems Incorporated（Reserved Font Name “Source”）；2012 Google Inc.。采用 SIL Open Font License 1.1，原文：`assets/fonts/OFL-SarasaGothic.txt`。来源：https://github.com/be5invis/Sarasa-Gothic 。
- **JetBrains Maple Mono**：Copyright 2025 Space Time，融合 JetBrains Mono（Copyright 2020 The JetBrains Mono Project Authors）和 Maple Mono（Copyright 2022 The Maple Mono Project Authors）。三层署名均保留，采用 SIL Open Font License 1.1，原文：`assets/fonts/OFL-JetBrainsMapleMono.txt`。来源：https://github.com/SpaceTimee/Fusion-JetBrainsMapleMono 、https://github.com/JetBrains/JetBrainsMono 、https://github.com/subframe7536/maple-font 。

字体不适用本项目 MIT 许可；分发或修改字体须遵守 OFL 的版权、保留名称及禁止单独售卖等条件。

## 系统组件、可选功能与发布责任

libcurl 和 ngspice 是系统提供或按构建选项启用的外部依赖，不能因代码支持它们就认定默认发布包捆绑了它们。若某个构建实际链接、加载或分发它们，发布者须核对该版本与其传递依赖的实际许可，并保留相应声明。

SDL_ttf 的 FreeType、HarfBuzz、PlutoSVG 等可选传递依赖是否存在，取决于 SDL_ttf 实际构建配置；仅保留 SDL_ttf 的 zlib 许可不能代替这些依赖各自的许可。发布者须对实际携带的二进制、字体及其依赖另行核对和补充随包声明。

应用“关于 → 开放源代码许可证与致谢”通过构建内嵌的 LICENSE、THIRD_PARTY_NOTICES.md、CREDITS.md 显示离线全文；网络链接只是来源入口，不是阅读许可的前提。

## 维护约定

更新依赖、字体或发布包配置时，应重新核对实际版本的许可原文，保留作者、版权和衍生链声明，并同步更新本清单及 THIRD_PARTY_NOTICES.md。CMake 会将这三份文档重新嵌入应用，桌面打包与 Pages 工作流也会携带对应文件；不能仅更新在线链接而省略离线声明。
