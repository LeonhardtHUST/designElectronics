# 本分支 GitHub Pages 部署指南

本指南针对 `LeonhardtHUST/designElectronics`。中文首页为默认入口，英文完整文档保留于 `en/`。上游是 [rORrEtboLt/designElectronics](https://github.com/rORrEtboLt/designElectronics)，本分支不代表上游官方网站。

**这是部署准备说明，不是已上线声明。** 首次部署必须以 Actions 的 build/deploy 成功和实际访问验证为准。仓库 Pages 配置、workflow 是否启用及本地代码是否已推送是三件独立的事情。

## 1. 设置发布来源

1. 进入本分支仓库的 **Settings → Pages → Build and deployment**。
2. 将 **Source** 从 **Deploy from a branch** 改为 **GitHub Actions**。若目前显示分支 `None`，不要选择 `main /docs` 作为替代：仓库不保存编译后的 WASM，仅发布静态 docs 会导致编辑器无法加载。
3. 保存设置。需要仓库管理权限；组织策略可能限制 Pages 或 Actions。允许本工作流使用的 Actions，并检查 **Settings → Actions → General**。
4. 在 **Settings → Environments → github-pages** 检查部署分支允许 `main`；若有 required reviewers，部署时需人工批准。

## 2. 启用并运行工作流

1. 进入 **Actions**。如果 fork 的工作流整体停用，先确认启用仓库工作流。
2. 打开 **Pages** 工作流。如果显示 disabled，点击 **Enable workflow**。
3. 确保待发布的 workflow、页面、CMake 和 WASM 源码修复已按维护者审核流程提交并推送到本分支 `main`。**更改 Pages 设置不会上传本地未提交代码**；手动运行也只构建远端所选分支的代码。
4. 点击 **Run workflow → Branch: main → Run workflow**。以后推送到 `main` 也会触发；不会因 pull request 自动发布。
5. 等待 `build` 和 `deploy` 成功，打开 `github-pages` environment 或 deploy 步骤给出的 URL，再检查下列页面。不要仅凭配置保存成功宣布上线。

工作流使用 `contents: read`、`pages: write`、`id-token: write`，部署目标为 `github-pages`。构建通过 Emscripten 编译应用，确认 `.js`、`.wasm`、`.data` 非空后复制进 `docs/app/`；将根目录 `CREDITS.md`、`THIRD_PARTY_NOTICES.md`、`LICENSE` 一并复制到站点制品，然后使用官方 `upload-pages-artifact` 和 `deploy-pages` 发布。不要提交生成的二进制来绕过构建。

## 3. 预期地址与验证

以下为默认项目域名的**预期地址**，不是已验证的在线链接：

| 入口 | 预期地址 |
| --- | --- |
| 中文首页 | `https://LeonhardtHUST.github.io/designElectronics/` |
| 中文兼容入口 | `https://LeonhardtHUST.github.io/designElectronics/zh/` |
| 完整英文文档 | `https://LeonhardtHUST.github.io/designElectronics/en/` |
| 网页编辑器 | `https://LeonhardtHUST.github.io/designElectronics/app/` |
| 嵌入预览 | `https://LeonhardtHUST.github.io/designElectronics/embed/#example=4&view=breadboard&sim=1` |
| 嵌入演示 | `https://LeonhardtHUST.github.io/designElectronics/embed/demo.html` |

项目 basePath 是 `/designElectronics/`，不是域名根目录。页面图片、编辑器和 loader 使用相对地址；embed 通过 `../app/` 加载同一套 WASM。完整英文文档移至 `en/` 后仍可使用原有标签页、示例选择、主题、图片放大与分享示例。

部署后刷新页面并在浏览器开发者工具 Network 中确认 `app/designElectronics.js`、`app/designElectronics.wasm`、`app/designElectronics.data` 返回 200，编辑器可打开示例并仿真，embed 可打开同一电路并跳转编辑器，中文和英文切换正常。确认站点 `CREDITS.md`、`THIRD_PARTY_NOTICES.md`、`LICENSE` 可访问。网页版不等同桌面版：真实开发板上传、桌面 CLI/AI 工具和部分 GitHub 功能需桌面环境。

## 4. 可选 GitHub CLI 配置

以下命令仅供管理员主动执行；需要已安装并登录 `gh`，且当前身份具有相应权限。先读后写，不要对上游仓库执行。

```sh
gh api repos/LeonhardtHUST/designElectronics/pages
gh workflow view pages.yml --repo LeonhardtHUST/designElectronics

# 仅当 Pages 尚未创建（GET 返回 404）时创建 Actions 模式
gh api --method POST repos/LeonhardtHUST/designElectronics/pages -f build_type=workflow

# 已存在但来源不正确时更新，而不是再次 POST
gh api --method PUT repos/LeonhardtHUST/designElectronics/pages -f build_type=workflow

gh workflow enable pages.yml --repo LeonhardtHUST/designElectronics
gh workflow run pages.yml --repo LeonhardtHUST/designElectronics --ref main
gh run list --repo LeonhardtHUST/designElectronics --workflow pages.yml
```

`404` 也可能表示当前身份无权限；请先确认身份和仓库访问权限。运行命令不会提交或推送本地文件。仓库 About 的 Website 如仍指向上游站点，应在本分支设置中改为本分支预期地址，但不能据此推断已部署成功。

## 5. 排障

- **Pages 尚未配置 / configure-pages 失败**：回到 Settings → Pages 确认 Source 为 GitHub Actions，而非 Deploy from a branch / None；检查管理员权限和 Pages 可用性。
- **无法 Run workflow / workflow disabled**：启用 fork Actions 和 Pages 工作流，确认远端 `main` 已包含 `workflow_dispatch`。
- **构建失败**：先看第一个出错步骤。检查 SDL/SDL_ttf 下载、Emscripten 安装与版本、CMake 和链接日志。Web 构建必须排除不支持的原生后端调用；若出现 SPICE CLI/selftest undefined symbols，应修复 WASM 源码护栏后重新构建，不能跳过编译只发布静态页面。
- **署名文件检查失败**：确认三个根目录文档均已提交到运行工作流的分支且非空，不要删除检查或修改版权归属以绕过失败。
- **deploy 权限失败 / 等待审核**：检查 `pages: write`、`id-token: write`，以及 `github-pages` 环境分支规则、required reviewers 和组织策略。
- **首页或子页面 404**：确认 deploy 成功、使用本分支域名和 `/designElectronics/` 项目路径，并等待 GitHub Pages 发布传播；不要访问原作者域名来替代本分支验证。
- **首页可打开但编辑器不加载**：检查三个 WASM 制品是否上传；不要使用 branch/docs 模式，也不要把 `/app/` 当作域名根路径。根据 Network 和 Console 的第一个错误排查。
- **图片、英文页、embed 路径错误**：保留目录相对地址和大小写。`en/`、`zh/` 到资源为 `../assets/`，embed 到应用为 `../app/`；变更仓库名或自定义域名时同步检查 canonical、示例链接和 basePath。
- **下载 404**：本分支 Releases 可能尚未发布对应资产，Pages 部署不会创建桌面发行包；查看本分支 Releases 或按构建指南自行编译。
